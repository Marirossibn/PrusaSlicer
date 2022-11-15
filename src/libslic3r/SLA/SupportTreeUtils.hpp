#ifndef SLASUPPORTTREEUTILS_H
#define SLASUPPORTTREEUTILS_H

#include <cstdint>
#include <optional>

#include <libslic3r/Execution/Execution.hpp>
#include <libslic3r/Optimize/NLoptOptimizer.hpp>
#include <libslic3r/Optimize/BruteforceOptimizer.hpp>
#include <libslic3r/MeshNormals.hpp>
#include <libslic3r/Geometry.hpp>
#include <libslic3r/SLA/SupportTreeBuilder.hpp>

#include <boost/variant.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/log/trivial.hpp>

namespace Slic3r { namespace sla {

using Slic3r::opt::initvals;
using Slic3r::opt::bounds;
using Slic3r::opt::StopCriteria;
using Slic3r::opt::Optimizer;
using Slic3r::opt::AlgNLoptSubplex;
using Slic3r::opt::AlgNLoptGenetic;
using Slic3r::Geometry::dir_to_spheric;
using Slic3r::Geometry::spheric_to_dir;

// Helper function for pillar interconnection where pairs of already connected
// pillars should be checked for not to be processed again. This can be done
// in constant time with a set of hash values uniquely representing a pair of
// integers. The order of numbers within the pair should not matter, it has
// the same unique hash. The hash value has to have twice as many bits as the
// arguments need. If the same integral type is used for args and return val,
// make sure the arguments use only the half of the type's bit depth.
template<class I, class DoubleI = IntegerOnly<I>>
IntegerOnly<DoubleI> pairhash(I a, I b)
{
    using std::ceil; using std::log2; using std::max; using std::min;
    static const auto constexpr Ibits = int(sizeof(I) * CHAR_BIT);
    static const auto constexpr DoubleIbits = int(sizeof(DoubleI) * CHAR_BIT);
    static const auto constexpr shift = DoubleIbits / 2 < Ibits ? Ibits / 2 : Ibits;

    I g = min(a, b), l = max(a, b);

       // Assume the hash will fit into the output variable
    assert((g ? (ceil(log2(g))) : 0) <= shift);
    assert((l ? (ceil(log2(l))) : 0) <= shift);

    return (DoubleI(g) << shift) + l;
}

// Give points on a 3D ring with given center, radius and orientation
// method based on:
// https://math.stackexchange.com/questions/73237/parametric-equation-of-a-circle-in-3d-space
template<size_t N>
class PointRing {
    std::array<double, N> m_phis;

    // Two vectors that will be perpendicular to each other and to the
    // axis. Values for a(X) and a(Y) are now arbitrary, a(Z) is just a
    // placeholder.
    // a and b vectors are perpendicular to the ring direction and to each other.
    // Together they define the plane where we have to iterate with the
    // given angles in the 'm_phis' vector
    Vec3d a = {0, 1, 0}, b;
    double m_radius = 0.;

    static inline bool constexpr is_one(double val)
    {
        constexpr double eps = 1e-20;

        return std::abs(std::abs(val) - 1) < eps;
    }

public:

    PointRing(const Vec3d &n) : m_phis{linspace_array<N>(0., 2 * PI)}
    {
        // We have to address the case when the direction vector v (same as
        // dir) is coincident with one of the world axes. In this case two of
        // its components will be completely zero and one is 1.0. Our method
        // becomes dangerous here due to division with zero. Instead, vector
        // 'a' can be an element-wise rotated version of 'v'
        if(is_one(n(X)) || is_one(n(Y)) || is_one(n(Z))) {
            a = {n(Z), n(X), n(Y)};
            b = {n(Y), n(Z), n(X)};
        }
        else {
            a(Z) = -(n(Y)*a(Y)) / n(Z); a.normalize();
            b = a.cross(n);
        }
    }

    Vec3d get(size_t idx, const Vec3d &src, double r) const
    {
        double phi = m_phis[idx];
        double sinphi = std::sin(phi);
        double cosphi = std::cos(phi);

        double rpscos = r * cosphi;
        double rpssin = r * sinphi;

        // Point on the sphere
        return {src(X) + rpscos * a(X) + rpssin * b(X),
                src(Y) + rpscos * a(Y) + rpssin * b(Y),
                src(Z) + rpscos * a(Z) + rpssin * b(Z)};
    }
};

template<class T, int N>
Vec<N, T> dirv(const Vec<N, T>& startp, const Vec<N, T>& endp) {
    return (endp - startp).normalized();
}

using Hit = AABBMesh::hit_result;

template<class It> Hit min_hit(It from, It to)
{
    auto mit = std::min_element(from, to, [](const Hit &h1, const Hit &h2) {
        return h1.distance() < h2.distance();
    });

    return *mit;
}

inline StopCriteria get_criteria(const SupportTreeConfig &cfg)
{
    return StopCriteria{}
        .rel_score_diff(cfg.optimizer_rel_score_diff)
        .max_iterations(cfg.optimizer_max_iterations);
}

// A simple sphere with a center and a radius
struct Ball { Vec3d p; double R; };

struct Beam { // Defines a set of rays displaced along a cone's surface
    static constexpr size_t SAMPLES = 8;

    Vec3d  src;
    Vec3d  dir;
    double r1;
    double r2; // radius of the beam 1 unit further from src in dir direction

    Beam(const Vec3d &s, const Vec3d &d, double R1, double R2):
        src{s}, dir{d}, r1{R1}, r2{R2} {};

    Beam(const Ball &src_ball, const Ball &dst_ball):
        src{src_ball.p}, dir{dirv(src_ball.p, dst_ball.p)}, r1{src_ball.R}
    {
        r2 = src_ball.R +
             (dst_ball.R - src_ball.R) / distance(src_ball.p, dst_ball.p);
    }

    Beam(const Vec3d &s, const Vec3d &d, double R)
        : src{s}, dir{d}, r1{R}, r2{R}
    {}
};

template<class Ex>
Hit beam_mesh_hit(Ex ex, const AABBMesh &mesh, const Beam &beam, double sd)
{
    Vec3d src = beam.src;
    Vec3d dst = src + beam.dir;
    double r_src = beam.r1;
    double r_dst = beam.r2;

    Vec3d D = (dst - src);
    Vec3d dir = D.normalized();
    PointRing<Beam::SAMPLES>  ring{dir};

    using Hit = AABBMesh::hit_result;

    // Hit results
    std::array<Hit, Beam::SAMPLES> hits;

    execution::for_each(
        ex, size_t(0), hits.size(),
        [&mesh, r_src, r_dst, src, dst, &ring, dir, sd, &hits](size_t i) {
            Hit &hit = hits[i];

            // Point on the circle on the pin sphere
            Vec3d p_src = ring.get(i, src, r_src + sd);
            Vec3d p_dst = ring.get(i, dst, r_dst + sd);
            Vec3d raydir = (p_dst - p_src).normalized();

            auto hr = mesh.query_ray_hit(p_src + r_src * raydir, raydir);

            if (hr.is_inside()) {
                if (hr.distance() > 2 * r_src + sd)
                    hit = Hit(0.0);
                else {
                    // re-cast the ray from the outside of the object
                    auto q = p_src + (hr.distance() + EPSILON) * raydir;
                    hit = mesh.query_ray_hit(q, raydir);
                }
            } else
                hit = hr;
        }, std::min(execution::max_concurrency(ex), Beam::SAMPLES));

    return min_hit(hits.begin(), hits.end());
}

template<class Ex>
Hit pinhead_mesh_hit(Ex              ex,
                     const AABBMesh &mesh,
                     const Vec3d    &s,
                     const Vec3d    &dir,
                     double          r_pin,
                     double          r_back,
                     double          width,
                     double          sd)
{
    static const size_t SAMPLES = 8;

    // Move away slightly from the touching point to avoid raycasting on the
    // inner surface of the mesh.

    auto &m         = mesh;
    using HitResult = AABBMesh::hit_result;

       // Hit results
    std::array<HitResult, SAMPLES> hits;

    struct Rings
    {
        double             rpin;
        double             rback;
        Vec3d              spin;
        Vec3d              sback;
        PointRing<SAMPLES> ring;

        Vec3d backring(size_t idx) { return ring.get(idx, sback, rback); }
        Vec3d pinring(size_t idx) { return ring.get(idx, spin, rpin); }
    } rings{r_pin + sd, r_back + sd, s, s + (r_pin + width + r_back) * dir, dir};

    // We will shoot multiple rays from the head pinpoint in the direction
    // of the pinhead robe (side) surface. The result will be the smallest
    // hit distance.

    execution::for_each(
        ex, size_t(0), hits.size(), [&m, &rings, sd, &hits](size_t i) {
            // Point on the circle on the pin sphere
            Vec3d ps = rings.pinring(i);
            // This is the point on the circle on the back sphere
            Vec3d p = rings.backring(i);

            auto &hit = hits[i];

               // Point ps is not on mesh but can be inside or
               // outside as well. This would cause many problems
               // with ray-casting. To detect the position we will
               // use the ray-casting result (which has an is_inside
               // predicate).

            Vec3d n = (p - ps).normalized();
            auto  q = m.query_ray_hit(ps + sd * n, n);

            if (q.is_inside()) { // the hit is inside the model
                if (q.distance() > rings.rpin) {
                    // If we are inside the model and the hit
                    // distance is bigger than our pin circle
                    // diameter, it probably indicates that the
                    // support point was already inside the
                    // model, or there is really no space
                    // around the point. We will assign a zero
                    // hit distance to these cases which will
                    // enforce the function return value to be
                    // an invalid ray with zero hit distance.
                    // (see min_element at the end)
                    hit = HitResult(0.0);
                } else {
                    // re-cast the ray from the outside of the
                    // object. The starting point has an offset
                    // of 2*safety_distance because the
                    // original ray has also had an offset
                    auto q2 = m.query_ray_hit(ps + (q.distance() + 2 * sd) * n, n);
                    hit     = q2;
                }
            } else
                hit = q;
        }, std::min(execution::max_concurrency(ex), SAMPLES));

    return min_hit(hits.begin(), hits.end());
}

template<class Ex>
Hit pinhead_mesh_hit(Ex              ex,
                     const AABBMesh &mesh,
                     const Head     &head,
                     double          safety_d)
{
    return pinhead_mesh_hit(ex, mesh, head.pos, head.dir, head.r_pin_mm,
                            head.r_back_mm, head.width_mm, safety_d);
}

template<class Ex>
std::optional<DiffBridge> search_widening_path(Ex                     policy,
                                               const SupportableMesh &sm,
                                               const Vec3d           &jp,
                                               const Vec3d           &dir,
                                               double                 radius,
                                               double new_radius)
{
    double w = radius + 2 * sm.cfg.head_back_radius_mm;
    double stopval = w + jp.z() - ground_level(sm);
    Optimizer<AlgNLoptSubplex> solver(get_criteria(sm.cfg).stop_score(stopval));

    auto [polar, azimuth] = dir_to_spheric(dir);

    double fallback_ratio = radius / sm.cfg.head_back_radius_mm;

    auto oresult = solver.to_max().optimize(
        [&policy, &sm, jp, radius, new_radius](const opt::Input<3> &input) {
            auto &[plr, azm, t] = input;

            auto d = spheric_to_dir(plr, azm).normalized();

            auto sd = sm.cfg.safety_distance(new_radius);

            double ret = pinhead_mesh_hit(policy, sm.emesh, jp, d, radius,
                                          new_radius, t, sd)
                             .distance();

            Beam beam{jp + t * d, d, new_radius};
            double down = beam_mesh_hit(policy, sm.emesh, beam, sd).distance();

            if (ret > t && std::isinf(down))
                ret += jp.z() - ground_level(sm);

            return ret;
        },
        initvals({polar, azimuth, w}), // start with what we have
        bounds({
            {PI - sm.cfg.bridge_slope, PI}, // Must not exceed the slope limit
            {-PI, PI}, // azimuth can be a full search
            {radius + sm.cfg.head_back_radius_mm,
             fallback_ratio * sm.cfg.max_bridge_length_mm}
        }));

    if (oresult.score >= stopval) {
        polar       = std::get<0>(oresult.optimum);
        azimuth     = std::get<1>(oresult.optimum);
        double t    = std::get<2>(oresult.optimum);
        Vec3d  endp = jp + t * spheric_to_dir(polar, azimuth);

        return DiffBridge(jp, endp, radius, sm.cfg.head_back_radius_mm);
    }

    return {};
}

inline double distance(const SupportPoint &a, const SupportPoint &b)
{
    return (a.pos - b.pos).norm();
}

template<class PtIndex>
std::vector<size_t> non_duplicate_suppt_indices(const PtIndex &index,
                                                const SupportPoints &suppts,
                                                double         eps)
{
    std::vector<bool> to_remove(suppts.size(), false);

    for (size_t i = 0; i < suppts.size(); ++i) {
        size_t closest_idx =
            find_closest_point(index, suppts[i].pos,
                               [&i, &to_remove](size_t i_closest) {
                                   return i_closest != i &&
                                          !to_remove[i_closest];
                               });

        if (closest_idx < suppts.size() &&
            (suppts[i].pos - suppts[closest_idx].pos).norm() < eps)
            to_remove[i] = true;
    }

    auto ret = reserve_vector<size_t>(suppts.size());
    for (size_t i = 0; i < to_remove.size(); i++)
        if (!to_remove[i])
            ret.emplace_back(i);

    return ret;
}

template<class Ex>
bool optimize_pinhead_placement(Ex                     policy,
                                const SupportableMesh &m,
                                Head                  &head)
{
    Vec3d n = get_normal(m.emesh, head.pos);
    assert(std::abs(n.norm() - 1.0) < EPSILON);

    // for all normals the spherical coordinates are generated and
    // the polar angle is saturated to 45 degrees from the bottom then
    // converted back to standard coordinates to get the new normal.
    // Then a simple quaternion is created from the two normals
    // (Quaternion::FromTwoVectors) and the rotation is applied to the
    // pinhead.

    auto [polar, azimuth] = dir_to_spheric(n);

    double back_r = head.r_back_mm;

    // skip if the tilt is not sane
    if (polar < PI - m.cfg.normal_cutoff_angle) return false;

    // We saturate the polar angle to 3pi/4
    polar = std::max(polar, PI - m.cfg.bridge_slope);

    // save the head (pinpoint) position
    Vec3d hp = head.pos;

    double lmin = m.cfg.head_width_mm, lmax = lmin;

    if (back_r < m.cfg.head_back_radius_mm) {
        lmin = 0., lmax = m.cfg.head_penetration_mm;
    }

    // The distance needed for a pinhead to not collide with model.
    double w = lmin + 2 * back_r + 2 * m.cfg.head_front_radius_mm -
               m.cfg.head_penetration_mm;

    double pin_r = head.r_pin_mm;

    // Reassemble the now corrected normal
    auto nn = spheric_to_dir(polar, azimuth).normalized();

    double sd = m.cfg.safety_distance(back_r);

    // check available distance
    Hit t = pinhead_mesh_hit(policy, m.emesh, hp, nn, pin_r, back_r, w, sd);

    if (t.distance() < w) {
        // Let's try to optimize this angle, there might be a
        // viable normal that doesn't collide with the model
        // geometry and its very close to the default.

        Optimizer<opt::AlgNLoptMLSL> solver(get_criteria(m.cfg).stop_score(w).max_iterations(100));
        solver.seed(0); // we want deterministic behavior

        auto oresult = solver.to_max().optimize(
            [&m, pin_r, back_r, hp, sd, policy](const opt::Input<3> &input) {
                auto &[plr, azm, l] = input;

                auto dir = spheric_to_dir(plr, azm).normalized();

                return pinhead_mesh_hit(policy, m.emesh, hp, dir, pin_r,
                                              back_r, l, sd).distance();
            },
            initvals({polar, azimuth,
                      (lmin + lmax) / 2.}), // start with what we have
            bounds({{PI - m.cfg.bridge_slope, PI}, // Must not exceed the slope limit
                    {-PI, PI}, // azimuth can be a full search
                    {lmin, lmax}}));

        if(oresult.score > w) {
            polar = std::get<0>(oresult.optimum);
            azimuth = std::get<1>(oresult.optimum);
            nn = spheric_to_dir(polar, azimuth).normalized();
            lmin = std::get<2>(oresult.optimum);
            t = AABBMesh::hit_result(oresult.score);
        }
    }

    bool ret = false;
    if (t.distance() > w && hp.z() + w * nn.z() >= ground_level(m)) {
        head.dir       = nn;
        head.width_mm  = lmin;
        head.r_back_mm = back_r;

        ret = true;
    } else if (back_r > m.cfg.head_fallback_radius_mm) {
        head.r_back_mm = m.cfg.head_fallback_radius_mm;
        ret = optimize_pinhead_placement(policy, m, head);
    }

    return ret;
}

template<class Ex>
std::optional<Head> calculate_pinhead_placement(Ex                     policy,
                                                const SupportableMesh &sm,
                                                size_t suppt_idx)
{
    if (suppt_idx >= sm.pts.size())
        return {};

    const SupportPoint &sp = sm.pts[suppt_idx];
    Head                head{
        sm.cfg.head_back_radius_mm,
        sp.head_front_radius,
        0.,
        sm.cfg.head_penetration_mm,
        Vec3d::Zero(),        // dir
        sp.pos.cast<double>() // displacement
    };

    if (optimize_pinhead_placement(policy, sm, head)) {
        head.id = long(suppt_idx);

        return head;
    }

    return {};
}

struct GroundConnection {
    // Currently, a ground connection will contain at most 2 additional junctions
    // which will not require any allocations. If I come up with an algo that
    // can produce a route to ground with more junctions, this design will be
    // able to handle that.
    static constexpr size_t MaxExpectedJunctions = 3;

    boost::container::small_vector<Junction, MaxExpectedJunctions> path;
    std::optional<Pedestal> pillar_base;

    operator bool() const { return pillar_base.has_value() && !path.empty(); }
};

template<class Ex>
GroundConnection find_pillar_route(Ex policy,
                                   const SupportableMesh &sm,
                                   const Junction &source,
                                   const Vec3d &sourcedir,
                                   double end_radius)
{
    GroundConnection ret;
    ret.path.emplace_back(source);

    double sd = sm.cfg.safety_distance(source.r);
    auto   gp = Vec3d{source.pos.x(), source.pos.y(), ground_level(sm)};

    auto hit = beam_mesh_hit(policy,
                             sm.emesh,
                             Beam{{source.pos, source.r}, {gp, end_radius}},
                             sd);

    if (std::isinf(hit.distance())) {
        double base_radius = std::max(sm.cfg.base_radius_mm, end_radius);

        ret.pillar_base =
            Pedestal{gp, sm.cfg.base_height_mm, base_radius, end_radius};
    }

    return ret;
}

//template<class Ex>
//GroundConnection find_pillar_route(Ex policy,
//                                   const SupportableMesh &sm,
//                                   const Junction &source,
//                                   const Vec3d &sourcedir,
//                                   double end_radius)
//{
//    GroundConnection ret;

//    ret.path.emplace_back(source);

//    Vec3d  jp           = source.pos, endp = jp, dir = sourcedir;
//    bool   can_add_base = false/*, non_head = false*/;

//    double gndlvl = 0.; // The Z level where pedestals should be
//    double jp_gnd = 0.; // The lowest Z where a junction center can be
//    double gap_dist = 0.; // The gap distance between the model and the pad
//    double radius = source.r;
//    double sd = sm.cfg.safety_distance(radius);

//    double r2 = radius + (end_radius - radius) / (jp.z() - ground_level(sm));

//    auto to_floor = [&gndlvl](const Vec3d &p) { return Vec3d{p.x(), p.y(), gndlvl}; };

//    auto eval_limits = [&sm, &radius, &can_add_base, &gndlvl, &gap_dist, &jp_gnd, end_radius]
//        (bool base_en = true)
//    {
//        can_add_base  = base_en && radius >= sm.cfg.head_back_radius_mm;
//        double base_r = can_add_base ? std::max(sm.cfg.base_radius_mm, end_radius) : end_radius;
//        gndlvl        = ground_level(sm);
//        if (!can_add_base) gndlvl -= sm.pad_cfg.wall_thickness_mm;
//        jp_gnd   = gndlvl + (can_add_base ? 0. : sm.cfg.head_back_radius_mm);
//        gap_dist = sm.cfg.pillar_base_safety_distance_mm + base_r + EPSILON;
//    };

//    eval_limits();

//    // We are dealing with a mini pillar that's potentially too long
//    if (radius < sm.cfg.head_back_radius_mm && jp.z() - gndlvl > 20 * radius)
//    {
//        std::optional<DiffBridge> diffbr =
//            search_widening_path(policy, sm, jp, dir, radius,
//                                 sm.cfg.head_back_radius_mm);

//        if (diffbr && diffbr->endp.z() > jp_gnd) {
//            endp = diffbr->endp;
//            radius = diffbr->end_r;
//            ret.path.emplace_back(endp, radius);
//            dir = diffbr->get_dir();
//            eval_limits();
//        } else return ret;
//    }

//    if (sm.cfg.object_elevation_mm < EPSILON)
//    {
//        // get a suitable direction for the corrector bridge. It is the
//        // original sourcedir's azimuth but the polar angle is saturated to the
//        // configured bridge slope.
//        auto [polar, azimuth] = dir_to_spheric(dir);
//        polar = PI - sm.cfg.bridge_slope;
//        Vec3d d = spheric_to_dir(polar, azimuth).normalized();
//        double t = beam_mesh_hit(policy, sm.emesh, Beam{endp, d, radius, r2}, sd).distance();
//        double tmax = std::min(sm.cfg.max_bridge_length_mm, t);
//        t = 0.;

//        double zd = endp.z() - jp_gnd;
//        double tmax2 = zd / std::sqrt(1 - sm.cfg.bridge_slope * sm.cfg.bridge_slope);
//        tmax = std::min(tmax, tmax2);

//        Vec3d nexp = endp;
//        double dlast = 0.;
//        double rnext = radius;
//        while (((dlast = std::sqrt(sm.emesh.squared_distance(to_floor(nexp)))) < gap_dist ||
//                !std::isinf(beam_mesh_hit(policy, sm.emesh, Beam{nexp, DOWN, rnext, end_radius}, sd).distance())) &&
//               t < tmax)
//        {
//            t += radius;
//            nexp = endp + t * d;
//            double bridge_ratio = dlast / (dlast + (nexp.z() - ground_level(sm)));
//            rnext = rnext + bridge_ratio * (end_radius - rnext);
//        }

//        // If could not find avoidance bridge for the pad gap, try again
//        // without the pillar base
//        if (dlast < gap_dist && can_add_base) {
//            nexp         = endp;
//            t            = 0.;
//            rnext = radius;
//            can_add_base = false;
//            eval_limits(can_add_base);

//            zd = endp.z() - jp_gnd;
//            tmax2 = zd / std::sqrt(1 - sm.cfg.bridge_slope * sm.cfg.bridge_slope);
//            tmax = std::min(tmax, tmax2);

//            while (((dlast = std::sqrt(sm.emesh.squared_distance(to_floor(nexp)))) < gap_dist ||
//                    !std::isinf(beam_mesh_hit(policy, sm.emesh, Beam{nexp, DOWN, rnext, end_radius}, sd).distance())) && t < tmax) {
//                t += radius;
//                nexp = endp + t * d;

//                double bridge_ratio = dlast / (dlast + (nexp.z() - ground_level(sm)));
//                rnext = rnext + bridge_ratio * (end_radius - rnext);
//            }
//        }

//        // Could not find a path to avoid the pad gap
//        if (dlast < gap_dist) {
//            ret.path.clear();
//            return ret;
//        }

//        if (t > 0.) { // Need to make additional bridge
//            ret.path.emplace_back(nexp, rnext);
//            endp = nexp;
//        }
//    }

//    Vec3d gp = to_floor(endp);
//    auto hit = beam_mesh_hit(policy, sm.emesh,
//                             Beam{{endp, radius}, {gp, end_radius}}, sd);

//    if (std::isinf(hit.distance())) {
//        double base_radius = can_add_base ?
//                    std::max(sm.cfg.base_radius_mm, end_radius) : end_radius;

//        Vec3d gp = to_floor(endp);
//        ret.pillar_base =
//            Pedestal{gp, sm.cfg.base_height_mm, base_radius, end_radius};
//    }

//    return ret;
//}

inline long build_ground_connection(SupportTreeBuilder &builder,
                                    const SupportableMesh &sm,
                                    const GroundConnection &conn)
{
    long ret = SupportTreeNode::ID_UNSET;

    if (!conn)
        return ret;

    auto it = conn.path.begin();
    auto itnx = std::next(it);

    while (itnx != conn.path.end()) {
        builder.add_diffbridge(*it, *itnx);
        builder.add_junction(*itnx);
        ++it; ++itnx;
    }

    auto gp = conn.path.back().pos;
    gp.z() = ground_level(sm);
    double h = conn.path.back().pos.z() - gp.z();

// TODO: does not work yet
//    if (conn.path.back().id < 0) {
//        // this is a head
//        long head_id = std::abs(conn.path.back().id);
//        ret = builder.add_pillar(head_id, h);
//    } else
        ret = builder.add_pillar(gp, h, conn.path.back().r, conn.pillar_base->r_top);

    builder.add_pillar_base(ret, conn.pillar_base->height, conn.pillar_base->r_bottom);

    return ret;
}

template<class Ex>
GroundConnection find_ground_connection(
    Ex                     policy,
    const SupportableMesh &sm,
    const Junction        &j,
    const Vec3d           &dir,
    double                 end_r)
{
    auto   hjp = j.pos;
    double r   = j.r;
    auto   sd  = sm.cfg.safety_distance(r);
    double r2  = j.r + (end_r - j.r) / (j.pos.z() - ground_level(sm));

    double t = beam_mesh_hit(policy, sm.emesh, Beam{hjp, dir, r, r2}, sd).distance();
    t = std::min(t, sm.cfg.max_bridge_length_mm);
    double d = 0.;

    GroundConnection gnd_route;

    while (!gnd_route && d < t) {
        Vec3d  endp = hjp + d * dir;
        double bridge_ratio = d / (d + (endp.z() - ground_level(sm)));
        double pill_r = r + bridge_ratio * (end_r - r);
        gnd_route = find_pillar_route(policy, sm, {endp, pill_r}, dir, end_r);

        d += r;
    }

    GroundConnection ret;

    if (d > 0.)
        ret.path.emplace_back(j);

    for (auto &p : gnd_route.path)
        ret.path.emplace_back(p);

    // This will ultimately determine if the route is valid or not
    // but the path junctions will be provided anyways, so invalid paths
    // can be inspected
    ret.pillar_base = gnd_route.pillar_base;

    return ret;
}

template<class Ex>
GroundConnection optimize_ground_connection(
    Ex                     policy,
    const SupportableMesh &sm,
    const Junction        &j,
    double                 end_radius,
    const Vec3d           &init_dir = DOWN)
{
    double downdst = j.pos.z() - ground_level(sm);

    auto res = find_ground_connection(policy, sm, j, init_dir, end_radius);
    if (res)
        return res;

    // Optimize bridge direction:
    // Straight path failed so we will try to search for a suitable
    // direction out of the cavity.
    auto [polar, azimuth] = dir_to_spheric(init_dir);

    Optimizer<opt::AlgNLoptMLSL> solver(get_criteria(sm.cfg).stop_score(1e6));
    solver.seed(0); // we want deterministic behavior

    auto sd      = sm.cfg.safety_distance(j.r);
    auto oresult = solver.to_max().optimize(
        [&j, sd, &policy, &sm, &downdst, &end_radius](const opt::Input<2> &input) {
            auto &[plr, azm] = input;
            Vec3d n = spheric_to_dir(plr, azm).normalized();
            Beam beam{Ball{j.pos, j.r}, Ball{j.pos + downdst * n, end_radius}};
            return beam_mesh_hit(policy, sm.emesh, beam, sd).distance();
        },
        initvals({polar, azimuth}),  // let's start with what we have
        bounds({ {PI - sm.cfg.bridge_slope, PI}, {-PI, PI} })
        );

    Vec3d bridgedir = spheric_to_dir(oresult.optimum).normalized();

    return find_ground_connection(policy, sm, j, bridgedir, end_radius);
}

template<class Ex>
GroundConnection deepsearch_ground_connection(
    Ex                     policy,
    const SupportableMesh &sm,
    const Junction        &j,
    double                 end_radius,
    const Vec3d           &init_dir = DOWN)
{
    // Score is the total lenght of the route. Feasible routes will have
    // infinite length (rays not colliding with model), thus the stop score
    // should be a reasonably big number.
    constexpr double StopScore = 1e6;

    const auto sd          = sm.cfg.safety_distance(j.r);
    const auto gndlvl      = ground_level(sm);
    const double widening  = end_radius - j.r;
    const double base_r    = std::max(sm.cfg.base_radius_mm, end_radius);
    const double zelev_gap = sm.cfg.pillar_base_safety_distance_mm + base_r;

    auto criteria = get_criteria(sm.cfg).stop_score(StopScore);

    Optimizer<opt::AlgNLoptMLSL> solver(criteria);
    solver.seed(0); // enforce deterministic behavior

    auto optfn = [&](const opt::Input<3> &input) {
        double ret = NaNd;

        // solver suggests polar, azimuth and bridge length values:
        auto &[plr, azm, bridge_len] = input;

        Vec3d n = spheric_to_dir(plr, azm);
        Vec3d bridge_end = j.pos + bridge_len * n;

        double full_len   = bridge_len + bridge_end.z() - gndlvl;
        double bridge_r   = j.r + widening * bridge_len / full_len;
        double brhit_dist = 0.;

        if (bridge_len > EPSILON) {
            // beam_mesh_hit with a zero lenght bridge is invalid

            Beam bridgebeam{Ball{j.pos, j.r}, Ball{bridge_end, bridge_r}};
            auto brhit = beam_mesh_hit(policy, sm.emesh, bridgebeam, sd);
            brhit_dist = brhit.distance();
        }

        if (brhit_dist < bridge_len) {
            ret = brhit_dist;
        } else {
            // check if pillar can be placed below
            auto gp = Vec3d{bridge_end.x(), bridge_end.y(), gndlvl};

            Beam gndbeam {{bridge_end, bridge_r}, {gp, end_radius}};
            auto gndhit = beam_mesh_hit(policy, sm.emesh, gndbeam, sd);

            if (std::isinf(gndhit.distance())) {
                // Ground route is free with this bridge

                if (sm.cfg.object_elevation_mm < EPSILON) {
                    // Dealing with zero elevation mode, to not route pillars
                    // into the gap between the optional pad and the model
                    double gap = std::sqrt(sm.emesh.squared_distance(gp));
                    if (gap < zelev_gap)
                        ret = full_len - zelev_gap + gap;
                    else // success
                        ret = StopScore;
                } else {
                    // No zero elevation, return success
                    ret = StopScore;
                }
            } else {
                // Ground route is not free
                ret = bridge_len + gndhit.distance();
            }
        }

        return ret;
    };

    auto [plr_init, azm_init] = dir_to_spheric(init_dir);

    // Saturate the polar angle to max tilt defined in config
    plr_init = std::max(plr_init, PI - sm.cfg.bridge_slope);

    auto oresult = solver.to_max().optimize(
        optfn,
        initvals({plr_init, azm_init, 0.}),      // start with a zero bridge
        bounds({ {PI - sm.cfg.bridge_slope, PI}, // bounds for polar angle
                 {-PI, PI},                      // bounds for azimuth
                 {0., sm.cfg.max_bridge_length_mm} }) // bounds bridge length
        );

    GroundConnection conn;

    if (oresult.score >= StopScore) {
        // search was successful, extract and apply the result
        auto &[plr, azm, bridge_len] = oresult.optimum;

        Vec3d n = spheric_to_dir(plr, azm);
        Vec3d bridge_end = j.pos + bridge_len * n;

        double full_len = bridge_len + bridge_end.z() - gndlvl;
        double bridge_r = j.r + widening * bridge_len / full_len;
        Vec3d  gp{bridge_end.x(), bridge_end.y(), gndlvl};

        conn.path.emplace_back(j);
        conn.path.emplace_back(Junction{bridge_end, bridge_r});

        conn.pillar_base =
                Pedestal{gp, sm.cfg.base_height_mm, base_r, end_radius};
    }

    return conn;
}

template<class Ex>
bool optimize_anchor_placement(Ex                     policy,
                               const SupportableMesh &sm,
                               const Junction        &from,
                               Anchor                &anchor)
{
    Vec3d n = get_normal(sm.emesh, anchor.pos);

    auto [polar, azimuth] = dir_to_spheric(n);

    // Saturate the polar angle to 3pi/4
    polar = std::min(polar, sm.cfg.bridge_slope);

    double lmin = 0;
    double lmax = std::min(sm.cfg.head_width_mm,
                           distance(from.pos, anchor.pos) - 2 * from.r);

    double sd = sm.cfg.safety_distance(anchor.r_back_mm);

    Optimizer<AlgNLoptGenetic> solver(get_criteria(sm.cfg)
                                          .stop_score(anchor.fullwidth())
                                          .max_iterations(100));

    solver.seed(0); // deterministic behavior

    auto oresult = solver.to_max().optimize(
        [&sm, &anchor, sd, policy](const opt::Input<3> &input) {
            auto &[plr, azm, l] = input;

            auto dir = spheric_to_dir(plr, azm).normalized();

            anchor.width_mm = l;
            anchor.dir = dir;

            return pinhead_mesh_hit(policy, sm.emesh, anchor, sd)
                .distance();
        },
        initvals({polar, azimuth, (lmin + lmax) / 2.}),
        bounds({{0., sm.cfg.bridge_slope}, // Must not exceed the slope limit
                {-PI, PI}, // azimuth can be a full search
                {lmin, lmax}}));

    polar = std::get<0>(oresult.optimum);
    azimuth = std::get<1>(oresult.optimum);
    anchor.dir = spheric_to_dir(polar, azimuth).normalized();
    anchor.width_mm = std::get<2>(oresult.optimum);

    if (oresult.score < anchor.fullwidth()) {
        // Unsuccesful search, the anchor does not fit into its intended space.
        return false;
    }

    return true;
}

template<class Ex>
std::optional<Anchor> calculate_anchor_placement(Ex policy,
                                                 const SupportableMesh &sm,
                                                 const Junction        &from,
                                                 const Vec3d &to_hint)
{
    double back_r    = from.r;
    double pin_r     = sm.cfg.head_front_radius_mm;
    double penetr    = sm.cfg.head_penetration_mm;
    double hwidth    = sm.cfg.head_width_mm;
    Vec3d  bridgedir = dirv(from.pos, to_hint);
    Vec3d  anchordir = -bridgedir;

    std::optional<Anchor> ret;

    Anchor anchor(back_r, pin_r, hwidth, penetr, anchordir, to_hint);

    if (optimize_anchor_placement(policy, sm, from, anchor)) {
        ret = anchor;
    } else if (anchor.r_back_mm = sm.cfg.head_fallback_radius_mm;
               optimize_anchor_placement(policy, sm, from, anchor)) {
        // Retrying with the fallback strut radius as a last resort.
        ret = anchor;
    }

    return anchor;
}

}} // namespace Slic3r::sla

#endif // SLASUPPORTTREEUTILS_H
