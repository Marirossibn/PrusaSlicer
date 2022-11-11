#include "DefaultSupportTree.hpp"

#include <libslic3r/Optimize/NLoptOptimizer.hpp>
#include <libslic3r/SLA/Clustering.hpp>
#include <libslic3r/MeshNormals.hpp>
#include <libslic3r/Execution/ExecutionTBB.hpp>

namespace Slic3r { namespace sla {

using Slic3r::opt::initvals;
using Slic3r::opt::bounds;
using Slic3r::opt::StopCriteria;
using Slic3r::opt::Optimizer;
using Slic3r::opt::AlgNLoptSubplex;
using Slic3r::opt::AlgNLoptGenetic;

DefaultSupportTree::DefaultSupportTree(SupportTreeBuilder &   builder,
                                     const SupportableMesh &sm)
    : m_sm(sm)
    , m_support_nmls(sm.pts.size(), 3)
    , m_builder(builder)
    , m_points(sm.pts.size(), 3)
    , m_thr(builder.ctl().cancelfn)
{
    // Prepare the support points in Eigen/IGL format as well, we will use
    // it mostly in this form.

    long i = 0;
    for (const SupportPoint &sp : m_sm.pts) {
        m_points.row(i).x() = double(sp.pos.x());
        m_points.row(i).y() = double(sp.pos.y());
        m_points.row(i).z() = double(sp.pos.z());
        ++i;
    }
}

bool DefaultSupportTree::execute(SupportTreeBuilder    &builder,
                                const SupportableMesh &sm)
{
    if(sm.pts.empty()) return false;

    DefaultSupportTree alg(builder, sm);

       // Let's define the individual steps of the processing. We can experiment
       // later with the ordering and the dependencies between them.
    enum Steps {
        BEGIN,
        PINHEADS,
        CLASSIFY,
        ROUTING_GROUND,
        ROUTING_NONGROUND,
        CASCADE_PILLARS,
        MERGE_RESULT,
        DONE,
        ABORT,
        NUM_STEPS
        //...
    };

       // Collect the algorithm steps into a nice sequence
    std::array<std::function<void()>, NUM_STEPS> program = {
        [] () {
            // Begin...
            // Potentially clear up the shared data (not needed for now)
        },

        std::bind(&DefaultSupportTree::add_pinheads, &alg),

        std::bind(&DefaultSupportTree::classify, &alg),

        std::bind(&DefaultSupportTree::routing_to_ground, &alg),

        std::bind(&DefaultSupportTree::routing_to_model, &alg),

        std::bind(&DefaultSupportTree::interconnect_pillars, &alg),

        std::bind(&DefaultSupportTree::merge_result, &alg),

        [] () {
            // Done
        },

        [] () {
            // Abort
        }
    };

    Steps pc = BEGIN;

    if(sm.cfg.ground_facing_only) {
        program[ROUTING_NONGROUND] = []() {
            BOOST_LOG_TRIVIAL(info)
                << "Skipping model-facing supports as requested.";
        };
    }

       // Let's define a simple automaton that will run our program.
    auto progress = [&builder, &pc] () {
        static const std::array<std::string, NUM_STEPS> stepstr {
            "Starting",
            "Generate pinheads",
            "Classification",
            "Routing to ground",
            "Routing supports to model surface",
            "Interconnecting pillars",
            "Merging support mesh",
            "Done",
            "Abort"
        };

        static const std::array<unsigned, NUM_STEPS> stepstate {
            0,
            30,
            50,
            60,
            70,
            80,
            99,
            100,
            0
        };

        if(builder.ctl().stopcondition()) pc = ABORT;

        switch(pc) {
        case BEGIN: pc = PINHEADS; break;
        case PINHEADS: pc = CLASSIFY; break;
        case CLASSIFY: pc = ROUTING_GROUND; break;
        case ROUTING_GROUND: pc = ROUTING_NONGROUND; break;
        case ROUTING_NONGROUND: pc = CASCADE_PILLARS; break;
        case CASCADE_PILLARS: pc = MERGE_RESULT; break;
        case MERGE_RESULT: pc = DONE; break;
        case DONE:
        case ABORT: break;
        default: ;
        }

        builder.ctl().statuscb(stepstate[pc], stepstr[pc]);
    };

       // Just here we run the computation...
    while(pc < DONE) {
        progress();
        program[pc]();
    }

    return pc == ABORT;
}

AABBMesh::hit_result DefaultSupportTree::pinhead_mesh_intersect(
    const Vec3d &s,
    const Vec3d &dir,
    double       r_pin,
    double       r_back,
    double       width,
    double       sd)
{
    return sla::pinhead_mesh_hit(suptree_ex_policy, m_sm.emesh, s, dir, r_pin, r_back, width, sd);
}

AABBMesh::hit_result DefaultSupportTree::bridge_mesh_intersect(
    const Vec3d &src, const Vec3d &dir, double r, double sd)
{
    return sla::beam_mesh_hit(suptree_ex_policy, m_sm.emesh, {src, dir, r}, sd);
}

bool DefaultSupportTree::interconnect(const Pillar &pillar,
                                     const Pillar &nextpillar)
{
    // We need to get the starting point of the zig-zag pattern. We have to
    // be aware that the two head junctions are at different heights. We
    // may start from the lowest junction and call it a day but this
    // strategy would leave unconnected a lot of pillar duos where the
    // shorter pillar is too short to start a new bridge but the taller
    // pillar could still be bridged with the shorter one.
    bool was_connected = false;

    Vec3d supper = pillar.startpoint();
    Vec3d slower = nextpillar.startpoint();
    Vec3d eupper = pillar.endpoint();
    Vec3d elower = nextpillar.endpoint();

    double zmin = ground_level(m_sm) + m_sm.cfg.base_height_mm;
    eupper.z() = std::max(eupper.z(), zmin);
    elower.z() = std::max(elower.z(), zmin);

       // The usable length of both pillars should be positive
    if(slower.z() - elower.z() < 0) return false;
    if(supper.z() - eupper.z() < 0) return false;

    double pillar_dist = distance(Vec2d{slower.x(), slower.y()},
                                      Vec2d{supper.x(), supper.y()});
    double bridge_distance = pillar_dist / std::cos(-m_sm.cfg.bridge_slope);
    double zstep = pillar_dist * std::tan(-m_sm.cfg.bridge_slope);

    if(pillar_dist < 2 * m_sm.cfg.head_back_radius_mm ||
        pillar_dist > m_sm.cfg.max_pillar_link_distance_mm) return false;

    if(supper.z() < slower.z()) supper.swap(slower);
    if(eupper.z() < elower.z()) eupper.swap(elower);

    double startz = 0, endz = 0;

    startz = slower.z() - zstep < supper.z() ? slower.z() - zstep : slower.z();
    endz = eupper.z() + zstep > elower.z() ? eupper.z() + zstep : eupper.z();

    if(slower.z() - eupper.z() < std::abs(zstep)) {
        // no space for even one cross

           // Get max available space
        startz = std::min(supper.z(), slower.z() - zstep);
        endz = std::max(eupper.z() + zstep, elower.z());

           // Align to center
        double available_dist = (startz - endz);
        double rounds = std::floor(available_dist / std::abs(zstep));
        startz -= 0.5 * (available_dist - rounds * std::abs(zstep));
    }

    auto pcm = m_sm.cfg.pillar_connection_mode;
    bool docrosses =
        pcm == PillarConnectionMode::cross ||
        (pcm == PillarConnectionMode::dynamic &&
         pillar_dist > 2*m_sm.cfg.base_radius_mm);

       // 'sj' means starting junction, 'ej' is the end junction of a bridge.
       // They will be swapped in every iteration thus the zig-zag pattern.
       // According to a config parameter, a second bridge may be added which
       // results in a cross connection between the pillars.
    Vec3d sj = supper, ej = slower; sj.z() = startz; ej.z() = sj.z() + zstep;

       // TODO: This is a workaround to not have a faulty last bridge
    while(ej.z() >= eupper.z() /*endz*/) {
        if(bridge_mesh_distance(sj, dirv(sj, ej), pillar.r_start) >= bridge_distance)
        {
            m_builder.add_crossbridge(sj, ej, pillar.r_start);
            was_connected = true;
        }

           // double bridging: (crosses)
        if(docrosses) {
            Vec3d sjback(ej.x(), ej.y(), sj.z());
            Vec3d ejback(sj.x(), sj.y(), ej.z());
            if (sjback.z() <= slower.z() && ejback.z() >= eupper.z() &&
                bridge_mesh_distance(sjback, dirv(sjback, ejback),
                                     pillar.r_start) >= bridge_distance) {
                // need to check collision for the cross stick
                m_builder.add_crossbridge(sjback, ejback, pillar.r_start);
                was_connected = true;
            }
        }

        sj.swap(ej);
        ej.z() = sj.z() + zstep;
    }

    return was_connected;
}

bool DefaultSupportTree::connect_to_nearpillar(const Head &head,
                                                  long        nearpillar_id)
{
    auto nearpillar = [this, nearpillar_id]() -> const Pillar& {
        return m_builder.pillar(nearpillar_id);
    };

    if (m_builder.bridgecount(nearpillar()) > m_sm.cfg.max_bridges_on_pillar)
        return false;

    Vec3d headjp = head.junction_point();
    Vec3d nearjp_u = nearpillar().startpoint();
    Vec3d nearjp_l = nearpillar().endpoint();

    double r = head.r_back_mm;
    double d2d = distance(to_2d(headjp), to_2d(nearjp_u));
    double d3d = distance(headjp, nearjp_u);

    double hdiff = nearjp_u.z() - headjp.z();
    double slope = std::atan2(hdiff, d2d);

    Vec3d bridgestart = headjp;
    Vec3d bridgeend = nearjp_u;
    double max_len = r * m_sm.cfg.max_bridge_length_mm / m_sm.cfg.head_back_radius_mm;
    double max_slope = m_sm.cfg.bridge_slope;
    double zdiff = 0.0;

       // check the default situation if feasible for a bridge
    if(d3d > max_len || slope > -max_slope) {
        // not feasible to connect the two head junctions. We have to search
        // for a suitable touch point.

        double Zdown = headjp.z() + d2d * std::tan(-max_slope);
        Vec3d touchjp = bridgeend; touchjp.z() = Zdown;
        double D = distance(headjp, touchjp);
        zdiff = Zdown - nearjp_u.z();

        if(zdiff > 0) {
            Zdown -= zdiff;
            bridgestart.z() -= zdiff;
            touchjp.z() = Zdown;

            double t = bridge_mesh_distance(headjp, DOWN, r);

               // We can't insert a pillar under the source head to connect
               // with the nearby pillar's starting junction
            if(t < zdiff) return false;
        }

        if(Zdown <= nearjp_u.z() && Zdown >= nearjp_l.z() && D < max_len)
            bridgeend.z() = Zdown;
        else
            return false;
    }

       // There will be a minimum distance from the ground where the
       // bridge is allowed to connect. This is an empiric value.
    double minz = ground_level(m_sm) + 4 * head.r_back_mm;
    if(bridgeend.z() < minz) return false;

    double t = bridge_mesh_distance(bridgestart, dirv(bridgestart, bridgeend), r);

       // Cannot insert the bridge. (further search might not worth the hassle)
    if(t < distance(bridgestart, bridgeend)) return false;

    std::lock_guard lk(m_bridge_mutex);

    if (m_builder.bridgecount(nearpillar()) < m_sm.cfg.max_bridges_on_pillar) {
        // A partial pillar is needed under the starting head.
        if(zdiff > 0) {
            m_builder.add_pillar(head.id, headjp.z() - bridgestart.z());
            m_builder.add_junction(bridgestart, r);
            m_builder.add_bridge(bridgestart, bridgeend, r);
        } else {
            m_builder.add_bridge(head.id, bridgeend);
        }

        m_builder.increment_bridges(nearpillar());
    } else return false;

    return true;
}

bool DefaultSupportTree::create_ground_pillar(const Junction &hjp,
                                             const Vec3d &sourcedir,
                                             long         head_id)
{
    auto [ret, pillar_id] = sla::create_ground_pillar(suptree_ex_policy,
                                                      m_builder, m_sm, hjp,
                                                      sourcedir, hjp.r, head_id);

    if (pillar_id >= 0) // Save the pillar endpoint in the spatial index
        m_pillar_index.guarded_insert(m_builder.pillar(pillar_id).endpt,
                                      unsigned(pillar_id));

    return ret;
}

void DefaultSupportTree::add_pinheads()
{
    // The minimum distance for two support points to remain valid.
    const double /*constexpr*/ D_SP = 0.1;

       // Get the points that are too close to each other and keep only the
       // first one
    auto aliases = cluster(m_points, D_SP, 2);

    PtIndices filtered_indices;
    filtered_indices.reserve(aliases.size());
    m_iheads.reserve(aliases.size());
    m_iheadless.reserve(aliases.size());
    for(auto& a : aliases) {
        // Here we keep only the front point of the cluster.
        filtered_indices.emplace_back(a.front());
    }

       // calculate the normals to the triangles for filtered points
    auto nmls = normals(suptree_ex_policy, m_points, m_sm.emesh,
                        m_sm.cfg.head_front_radius_mm, m_thr,
                        filtered_indices);

    // Not all of the support points have to be a valid position for
    // support creation. The angle may be inappropriate or there may
    // not be enough space for the pinhead. Filtering is applied for
    // these reasons.

    auto heads = reserve_vector<Head>(m_sm.pts.size());
    for (const SupportPoint &sp : m_sm.pts) {
        m_thr();
        heads.emplace_back(
            NaNd,
            sp.head_front_radius,
            0.,
            m_sm.cfg.head_penetration_mm,
            Vec3d::Zero(),         // dir
            sp.pos.cast<double>()  // displacement
            );
    }

    std::function<void(unsigned, size_t, double)> filterfn;
    filterfn = [this, &nmls, &heads, &filterfn](unsigned fidx, size_t i, double back_r) {
        m_thr();

        Vec3d n = nmls.row(Eigen::Index(i));

           // for all normals we generate the spherical coordinates and
           // saturate the polar angle to 45 degrees from the bottom then
           // convert back to standard coordinates to get the new normal.
           // Then we just create a quaternion from the two normals
           // (Quaternion::FromTwoVectors) and apply the rotation to the
           // arrow head.

        auto [polar, azimuth] = dir_to_spheric(n);

           // skip if the tilt is not sane
        if (polar < PI - m_sm.cfg.normal_cutoff_angle) return;

           // We saturate the polar angle to 3pi/4
        polar = std::max(polar, PI - m_sm.cfg.bridge_slope);

           // save the head (pinpoint) position
        Vec3d hp = m_points.row(fidx);

        double lmin = m_sm.cfg.head_width_mm, lmax = lmin;

        if (back_r < m_sm.cfg.head_back_radius_mm) {
            lmin = 0., lmax = m_sm.cfg.head_penetration_mm;
        }

           // The distance needed for a pinhead to not collide with model.
        double w = lmin + 2 * back_r + 2 * m_sm.cfg.head_front_radius_mm -
                   m_sm.cfg.head_penetration_mm;

        double pin_r = double(m_sm.pts[fidx].head_front_radius);

           // Reassemble the now corrected normal
        auto nn = spheric_to_dir(polar, azimuth).normalized();

           // check available distance
        AABBMesh::hit_result t = pinhead_mesh_intersect(hp, nn, pin_r,
                                                           back_r, w);

        if (t.distance() < w) {
            // Let's try to optimize this angle, there might be a
            // viable normal that doesn't collide with the model
            // geometry and its very close to the default.

            Optimizer<AlgNLoptGenetic> solver(get_criteria(m_sm.cfg));
            solver.seed(0); // we want deterministic behavior

            auto oresult = solver.to_max().optimize(
                [this, pin_r, back_r, hp](const opt::Input<3> &input)
                {
                    auto &[plr, azm, l] = input;

                    auto dir = spheric_to_dir(plr, azm).normalized();

                    return pinhead_mesh_intersect(
                               hp, dir, pin_r, back_r, l).distance();
                },
                initvals({polar, azimuth, (lmin + lmax) / 2.}), // start with what we have
                bounds({
                    {PI - m_sm.cfg.bridge_slope, PI},    // Must not exceed the slope limit
                    {-PI, PI}, // azimuth can be a full search
                    {lmin, lmax}
                }));

            if(oresult.score > w) {
                polar = std::get<0>(oresult.optimum);
                azimuth = std::get<1>(oresult.optimum);
                nn = spheric_to_dir(polar, azimuth).normalized();
                lmin = std::get<2>(oresult.optimum);
                t = AABBMesh::hit_result(oresult.score);
            }
        }

        if (t.distance() > w && hp.z() + w * nn.z() >= ground_level(m_sm)) {
            Head &h = heads[fidx];
            h.id        = fidx;
            h.dir       = nn;
            h.width_mm  = lmin;
            h.r_back_mm = back_r;
        } else if (back_r > m_sm.cfg.head_fallback_radius_mm) {
            filterfn(fidx, i, m_sm.cfg.head_fallback_radius_mm);
        }
    };

    execution::for_each(
        suptree_ex_policy, size_t(0), filtered_indices.size(),
        [this, &filterfn, &filtered_indices](size_t i) {
            filterfn(filtered_indices[i], i, m_sm.cfg.head_back_radius_mm);
        },
        execution::max_concurrency(suptree_ex_policy));

    for (size_t i = 0; i < heads.size(); ++i)
        if (heads[i].is_valid()) {
            m_builder.add_head(i, heads[i]);
            m_iheads.emplace_back(i);
        }

    m_thr();
}

void DefaultSupportTree::classify()
{
    // We should first get the heads that reach the ground directly
    PtIndices ground_head_indices;
    ground_head_indices.reserve(m_iheads.size());
    m_iheads_onmodel.reserve(m_iheads.size());

       // First we decide which heads reach the ground and can be full
       // pillars and which shall be connected to the model surface (or
       // search a suitable path around the surface that leads to the
       // ground -- TODO)
    for(unsigned i : m_iheads) {
        m_thr();

        Head &head = m_builder.head(i);
        double r = head.r_back_mm;
        Vec3d headjp = head.junction_point();

           // collision check
        auto hit = bridge_mesh_intersect(headjp, DOWN, r);

        if(std::isinf(hit.distance())) ground_head_indices.emplace_back(i);
        else if(m_sm.cfg.ground_facing_only)  head.invalidate();
        else m_iheads_onmodel.emplace_back(i);

        m_head_to_ground_scans[i] = hit;
    }

       // We want to search for clusters of points that are far enough
       // from each other in the XY plane to not cross their pillar bases
       // These clusters of support points will join in one pillar,
       // possibly in their centroid support point.

    auto pointfn = [this](unsigned i) {
        return m_builder.head(i).junction_point();
    };

    auto predicate = [this](const PointIndexEl &e1,
                            const PointIndexEl &e2) {
        double d2d = distance(to_2d(e1.first), to_2d(e2.first));
        double d3d = distance(e1.first, e2.first);
        return d2d < 2 * m_sm.cfg.base_radius_mm
               && d3d < m_sm.cfg.max_bridge_length_mm;
    };

    m_pillar_clusters = cluster(ground_head_indices, pointfn, predicate,
                                m_sm.cfg.max_bridges_on_pillar);
}

void DefaultSupportTree::routing_to_ground()
{
    ClusterEl cl_centroids;
    cl_centroids.reserve(m_pillar_clusters.size());

    for (auto &cl : m_pillar_clusters) {
        m_thr();

           // place all the centroid head positions into the index. We
           // will query for alternative pillar positions. If a sidehead
           // cannot connect to the cluster centroid, we have to search
           // for another head with a full pillar. Also when there are two
           // elements in the cluster, the centroid is arbitrary and the
           // sidehead is allowed to connect to a nearby pillar to
           // increase structural stability.

        if (cl.empty()) continue;

           // get the current cluster centroid
        auto &      thr    = m_thr;
        const auto &points = m_points;

        long lcid = cluster_centroid(
            cl, [&points](size_t idx) { return points.row(long(idx)); },
            [thr](const Vec3d &p1, const Vec3d &p2) {
                thr();
                return distance(Vec2d(p1.x(), p1.y()), Vec2d(p2.x(), p2.y()));
            });

        assert(lcid >= 0);
        unsigned hid = cl[size_t(lcid)]; // Head ID

        cl_centroids.emplace_back(hid);

        Head &h = m_builder.head(hid);

        if (!create_ground_pillar(h.junction(), h.dir, h.id)) {
            BOOST_LOG_TRIVIAL(warning)
                << "Pillar cannot be created for support point id: " << hid;
            m_iheads_onmodel.emplace_back(h.id);
            continue;
        }
    }

       // now we will go through the clusters ones again and connect the
       // sidepoints with the cluster centroid (which is a ground pillar)
       // or a nearby pillar if the centroid is unreachable.
    size_t ci = 0;
    for (auto cl : m_pillar_clusters) {
        m_thr();

        auto cidx = cl_centroids[ci++];

        auto q = m_pillar_index.query(m_builder.head(cidx).junction_point(), 1);
        if (!q.empty()) {
            long centerpillarID = q.front().second;
            for (auto c : cl) {
                m_thr();
                if (c == cidx) continue;

                auto &sidehead = m_builder.head(c);

                if (!connect_to_nearpillar(sidehead, centerpillarID) &&
                    !search_pillar_and_connect(sidehead)) {
                    // Vec3d pend = Vec3d{pstart.x(), pstart.y(), gndlvl};
                    // Could not find a pillar, create one
                    create_ground_pillar(sidehead.junction(), sidehead.dir, sidehead.id);
                }
            }
        }
    }
}

bool DefaultSupportTree::connect_to_ground(Head &head)
{
    auto [ret, pillar_id] = sla::search_ground_route(suptree_ex_policy,
                                                     m_builder, m_sm,
                                                     {head.junction_point(),
                                                      head.r_back_mm},
                                                     head.r_back_mm,
                                                     head.dir);

    if (pillar_id >= 0) {
        // Save the pillar endpoint in the spatial index
        m_pillar_index.guarded_insert(m_builder.pillar(pillar_id).endpt,
                                      unsigned(pillar_id));

        head.pillar_id = pillar_id;
    }

    return ret;
}

bool DefaultSupportTree::connect_to_model_body(Head &head)
{
    if (head.id <= SupportTreeNode::ID_UNSET) return false;

    auto it = m_head_to_ground_scans.find(unsigned(head.id));
    if (it == m_head_to_ground_scans.end()) return false;

    auto &hit = it->second;

    if (!hit.is_hit()) {
        // TODO scan for potential anchor points on model surface
        return false;
    }

    Vec3d hjp = head.junction_point();
    double zangle = std::asin(hit.direction().z());
    zangle = std::max(zangle, PI/4);
    double h = std::sin(zangle) * head.fullwidth();

       // The width of the tail head that we would like to have...
    h = std::min(hit.distance() - head.r_back_mm, h);

       // If this is a mini pillar dont bother with the tail width, can be 0.
    if (head.r_back_mm < m_sm.cfg.head_back_radius_mm) h = std::max(h, 0.);
    else if (h <= 0.) return false;

    Vec3d endp{hjp.x(), hjp.y(), hjp.z() - hit.distance() + h};
    auto center_hit = m_sm.emesh.query_ray_hit(hjp, DOWN);

    double hitdiff = center_hit.distance() - hit.distance();
    Vec3d hitp = std::abs(hitdiff) < 2*head.r_back_mm?
                     center_hit.position() : hit.position();

    long pillar_id = m_builder.add_pillar(head.id, hjp.z() - endp.z());
    Pillar &pill = m_builder.pillar(pillar_id);

    Vec3d taildir = endp - hitp;
    double dist = (hitp - endp).norm() + m_sm.cfg.head_penetration_mm;
    double w = dist - 2 * head.r_pin_mm - head.r_back_mm;

    if (w < 0.) {
        BOOST_LOG_TRIVIAL(warning) << "Pinhead width is negative!";
        w = 0.;
    }

    m_builder.add_anchor(head.r_back_mm, head.r_pin_mm, w,
                         m_sm.cfg.head_penetration_mm, taildir, hitp);

    m_pillar_index.guarded_insert(pill.endpoint(), pill.id);

    return true;
}

bool DefaultSupportTree::search_pillar_and_connect(const Head &source)
{
    // Hope that a local copy takes less time than the whole search loop.
    // We also need to remove elements progressively from the copied index.
    PointIndex spindex = m_pillar_index.guarded_clone();

    long nearest_id = SupportTreeNode::ID_UNSET;

    Vec3d querypt = source.junction_point();

    while(nearest_id < 0 && !spindex.empty()) { m_thr();
        // loop until a suitable head is not found
        // if there is a pillar closer than the cluster center
        // (this may happen as the clustering is not perfect)
        // than we will bridge to this closer pillar

        Vec3d qp(querypt.x(), querypt.y(), ground_level(m_sm));
        auto qres = spindex.nearest(qp, 1);
        if(qres.empty()) break;

        auto ne = qres.front();
        nearest_id = ne.second;

        if(nearest_id >= 0) {
            if (size_t(nearest_id) < m_builder.pillarcount()) {
                if(!connect_to_nearpillar(source, nearest_id) ||
                    m_builder.pillar(nearest_id).r_start < source.r_back_mm) {
                    nearest_id = SupportTreeNode::ID_UNSET;    // continue searching
                    spindex.remove(ne);       // without the current pillar
                }
            }
        }
    }

    return nearest_id >= 0;
}

void DefaultSupportTree::routing_to_model()
{
    // We need to check if there is an easy way out to the bed surface.
    // If it can be routed there with a bridge shorter than
    // min_bridge_distance.

    execution::for_each(
        suptree_ex_policy, m_iheads_onmodel.begin(), m_iheads_onmodel.end(),
        [this](const unsigned idx) {
            m_thr();

            auto &head = m_builder.head(idx);

            // Search nearby pillar
            if (search_pillar_and_connect(head)) { return; }

            // Cannot connect to nearby pillar. We will try to search for
            // a route to the ground.
            if (connect_to_ground(head)) { return; }

            // No route to the ground, so connect to the model body as a last resort
            if (connect_to_model_body(head)) { return; }

            // We have failed to route this head.
            BOOST_LOG_TRIVIAL(warning)
                << "Failed to route model facing support point. ID: " << idx;

            head.invalidate();
        },
        execution::max_concurrency(suptree_ex_policy));
}

void DefaultSupportTree::interconnect_pillars()
{
    // Now comes the algorithm that connects pillars with each other.
    // Ideally every pillar should be connected with at least one of its
    // neighbors if that neighbor is within max_pillar_link_distance

       // Pillars with height exceeding H1 will require at least one neighbor
       // to connect with. Height exceeding H2 require two neighbors.
    double H1 = m_sm.cfg.max_solo_pillar_height_mm;
    double H2 = m_sm.cfg.max_dual_pillar_height_mm;
    double d = m_sm.cfg.max_pillar_link_distance_mm;

       //A connection between two pillars only counts if the height ratio is
       // bigger than 50%
    double min_height_ratio = 0.5;

    std::set<unsigned long> pairs;

       // A function to connect one pillar with its neighbors. THe number of
       // neighbors is given in the configuration. This function if called
       // for every pillar in the pillar index. A pair of pillar will not
       // be connected multiple times this is ensured by the 'pairs' set which
       // remembers the processed pillar pairs
    auto cascadefn =
        [this, d, &pairs, min_height_ratio, H1] (const PointIndexEl& el)
    {
        Vec3d qp = el.first;    // endpoint of the pillar

        const Pillar& pillar = m_builder.pillar(el.second); // actual pillar

           // Get the max number of neighbors a pillar should connect to
        unsigned neighbors = m_sm.cfg.pillar_cascade_neighbors;

           // connections are already enough for the pillar
        if(pillar.links >= neighbors) return;

        double max_d = d * pillar.r_start / m_sm.cfg.head_back_radius_mm;
        // Query all remaining points within reach
        auto qres = m_pillar_index.query([qp, max_d](const PointIndexEl& e){
            return distance(e.first, qp) < max_d;
        });

           // sort the result by distance (have to check if this is needed)
        std::sort(qres.begin(), qres.end(),
                  [qp](const PointIndexEl& e1, const PointIndexEl& e2){
                      return distance(e1.first, qp) < distance(e2.first, qp);
                  });

        for(auto& re : qres) { // process the queried neighbors

            if(re.second == el.second) continue; // Skip self

            auto a = el.second, b = re.second;

               // Get unique hash for the given pair (order doesn't matter)
            auto hashval = pairhash(a, b);

               // Search for the pair amongst the remembered pairs
            if(pairs.find(hashval) != pairs.end()) continue;

            const Pillar& neighborpillar = m_builder.pillar(re.second);

               // this neighbor is occupied, skip
            if (neighborpillar.links >= neighbors) continue;
            if (neighborpillar.r_start < pillar.r_start) continue;

            if(interconnect(pillar, neighborpillar)) {
                pairs.insert(hashval);

                   // If the interconnection length between the two pillars is
                   // less than 50% of the longer pillar's height, don't count
                if(pillar.height < H1 ||
                    neighborpillar.height / pillar.height > min_height_ratio)
                    m_builder.increment_links(pillar);

                if(neighborpillar.height < H1 ||
                    pillar.height / neighborpillar.height > min_height_ratio)
                    m_builder.increment_links(neighborpillar);

            }

               // connections are enough for one pillar
            if(pillar.links >= neighbors) break;
        }
    };

       // Run the cascade for the pillars in the index
    m_pillar_index.foreach(cascadefn);

       // We would be done here if we could allow some pillars to not be
       // connected with any neighbors. But this might leave the support tree
       // unprintable.
       //
       // The current solution is to insert additional pillars next to these
       // lonely pillars. One or even two additional pillar might get inserted
       // depending on the length of the lonely pillar.

    size_t pillarcount = m_builder.pillarcount();

       // Again, go through all pillars, this time in the whole support tree
       // not just the index.
    for(size_t pid = 0; pid < pillarcount; pid++) {
        auto pillar = [this, pid]() { return m_builder.pillar(pid); };

           // Decide how many additional pillars will be needed:

        unsigned needpillars = 0;
        if (pillar().bridges > m_sm.cfg.max_bridges_on_pillar)
            needpillars = 3;
        else if (pillar().links < 2 && pillar().height > H2) {
            // Not enough neighbors to support this pillar
            needpillars = 2;
        } else if (pillar().links < 1 && pillar().height > H1) {
            // No neighbors could be found and the pillar is too long.
            needpillars = 1;
        }

        needpillars = std::max(pillar().links, needpillars) - pillar().links;
        if (needpillars == 0) continue;

           // Search for new pillar locations:

        bool   found    = false;
        double alpha    = 0; // goes to 2Pi
        double r        = 2 * m_sm.cfg.base_radius_mm;
        Vec3d  pillarsp = pillar().startpoint();

           // temp value for starting point detection
        Vec3d sp(pillarsp.x(), pillarsp.y(), pillarsp.z() - r);

           // A vector of bool for placement feasbility
        std::vector<bool>  canplace(needpillars, false);
        std::vector<Vec3d> spts(needpillars); // vector of starting points

        double gnd      = ground_level(m_sm);
        double min_dist = m_sm.cfg.pillar_base_safety_distance_mm +
                          m_sm.cfg.base_radius_mm + EPSILON;

        while(!found && alpha < 2*PI) {
            for (unsigned n = 0;
                 n < needpillars && (!n || canplace[n - 1]);
                 n++)
            {
                double a = alpha + n * PI / 3;
                Vec3d  s = sp;
                s.x() += std::cos(a) * r;
                s.y() += std::sin(a) * r;
                spts[n] = s;

                   // Check the path vertically down
                Vec3d check_from = s + Vec3d{0., 0., pillar().r_start};
                auto hr = bridge_mesh_intersect(check_from, DOWN, pillar().r_start);
                Vec3d gndsp{s.x(), s.y(), gnd};

                   // If the path is clear, check for pillar base collisions
                canplace[n] = std::isinf(hr.distance()) &&
                              std::sqrt(m_sm.emesh.squared_distance(gndsp)) >
                                  min_dist;
            }

            found = std::all_of(canplace.begin(), canplace.end(),
                                [](bool v) { return v; });

               // 20 angles will be tried...
            alpha += 0.1 * PI;
        }

        std::vector<long> newpills;
        newpills.reserve(needpillars);

        if (found)
            for (unsigned n = 0; n < needpillars; n++) {
                Vec3d s = spts[n];
                Pillar p(Vec3d{s.x(), s.y(), gnd}, s.z() - gnd, pillar().r_start);

                if (interconnect(pillar(), p)) {
                    Pillar &pp = m_builder.pillar(m_builder.add_pillar(p));

                    add_pillar_base(pp.id);

                    m_pillar_index.insert(pp.endpoint(), unsigned(pp.id));

                    m_builder.add_junction(s, pillar().r_start);
                    double t = bridge_mesh_distance(pillarsp, dirv(pillarsp, s),
                                                    pillar().r_start);
                    if (distance(pillarsp, s) < t)
                        m_builder.add_bridge(pillarsp, s, pillar().r_start);

                    if (pillar().endpoint().z() > ground_level(m_sm) + pillar().r_start)
                        m_builder.add_junction(pillar().endpoint(), pillar().r_start);

                    newpills.emplace_back(pp.id);
                    m_builder.increment_links(pillar());
                    m_builder.increment_links(pp);
                }
            }

        if(!newpills.empty()) {
            for(auto it = newpills.begin(), nx = std::next(it);
                 nx != newpills.end(); ++it, ++nx) {
                const Pillar& itpll = m_builder.pillar(*it);
                const Pillar& nxpll = m_builder.pillar(*nx);
                if(interconnect(itpll, nxpll)) {
                    m_builder.increment_links(itpll);
                    m_builder.increment_links(nxpll);
                }
            }

            m_pillar_index.foreach(cascadefn);
        }
    }
}

}} // namespace Slic3r::sla
