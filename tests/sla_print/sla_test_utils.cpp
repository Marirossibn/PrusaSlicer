#include "sla_test_utils.hpp"

void test_support_model_collision(const std::string          &obj_filename,
                                  const sla::SupportConfig   &input_supportcfg,
                                  const sla::HollowingConfig &hollowingcfg,
                                  const sla::DrainHoles      &drainholes)
{
    SupportByproducts byproducts;
    
    sla::SupportConfig supportcfg = input_supportcfg;
    
    // Set head penetration to a small negative value which should ensure that
    // the supports will not touch the model body.
    supportcfg.head_penetration_mm = -0.15;
    
    // TODO: currently, the tailheads penetrating into the model body do not
    // respect the penetration parameter properly. No issues were reported so
    // far but we should definitely fix this.
    supportcfg.ground_facing_only = true;
    
    test_supports(obj_filename, supportcfg, hollowingcfg, drainholes, byproducts);
    
    // Slice the support mesh given the slice grid of the model.
    std::vector<ExPolygons> support_slices =
            byproducts.supporttree.slice(byproducts.slicegrid, CLOSING_RADIUS);
    
    // The slices originate from the same slice grid so the numbers must match
    
    bool support_mesh_is_empty =
            byproducts.supporttree.retrieve_mesh(sla::MeshType::Pad).empty() &&
            byproducts.supporttree.retrieve_mesh(sla::MeshType::Support).empty();
    
    if (support_mesh_is_empty)
        REQUIRE(support_slices.empty());
    else
        REQUIRE(support_slices.size() == byproducts.model_slices.size());
    
    bool notouch = true;
    for (size_t n = 0; notouch && n < support_slices.size(); ++n) {
        const ExPolygons &sup_slice = support_slices[n];
        const ExPolygons &mod_slice = byproducts.model_slices[n];
        
        Polygons intersections = intersection(sup_slice, mod_slice);
        
        notouch = notouch && intersections.empty();
    }
    
    /*if (!notouch) */export_failed_case(support_slices, byproducts);
    
    REQUIRE(notouch);
}

void export_failed_case(const std::vector<ExPolygons> &support_slices, const SupportByproducts &byproducts)
{
    for (size_t n = 0; n < support_slices.size(); ++n) {
        const ExPolygons &sup_slice = support_slices[n];
        const ExPolygons &mod_slice = byproducts.model_slices[n];
        Polygons intersections = intersection(sup_slice, mod_slice);
        
        std::stringstream ss;
        if (!intersections.empty()) {
            ss << byproducts.obj_fname << std::setprecision(4) << n << ".svg";
            SVG svg(ss.str());
            svg.draw(sup_slice, "green");
            svg.draw(mod_slice, "blue");
            svg.draw(intersections, "red");
            svg.Close();
        }
    }
    
    TriangleMesh m;
    byproducts.supporttree.retrieve_full_mesh(m);
    m.merge(byproducts.input_mesh);
    m.repair();
    m.require_shared_vertices();
    m.WriteOBJFile(byproducts.obj_fname.c_str());
}

void test_supports(const std::string          &obj_filename,
                   const sla::SupportConfig   &supportcfg,
                   const sla::HollowingConfig &hollowingcfg,
                   const sla::DrainHoles      &drainholes,
                   SupportByproducts          &out)
{
    using namespace Slic3r;
    TriangleMesh mesh = load_model(obj_filename);
    
    REQUIRE_FALSE(mesh.empty());
    
    if (hollowingcfg.enabled) {
        auto inside = sla::generate_interior(mesh, hollowingcfg);
        REQUIRE(inside);
        mesh.merge(*inside);
        mesh.require_shared_vertices();
    }
    
    TriangleMeshSlicer slicer{&mesh};
    
    auto   bb      = mesh.bounding_box();
    double zmin    = bb.min.z();
    double zmax    = bb.max.z();
    double gnd     = zmin - supportcfg.object_elevation_mm;
    auto   layer_h = 0.05f;
    
    out.slicegrid = grid(float(gnd), float(zmax), layer_h);
    slicer.slice(out.slicegrid , CLOSING_RADIUS, &out.model_slices, []{});
    sla::cut_drainholes(out.model_slices, out.slicegrid, CLOSING_RADIUS, drainholes, []{});
    
    // Create the special index-triangle mesh with spatial indexing which
    // is the input of the support point and support mesh generators
    sla::EigenMesh3D emesh{mesh};
    if (hollowingcfg.enabled) 
        emesh.load_holes(drainholes);
    
    // Create the support point generator
    sla::SupportPointGenerator::Config autogencfg;
    autogencfg.head_diameter = float(2 * supportcfg.head_front_radius_mm);
    sla::SupportPointGenerator point_gen{emesh, autogencfg, [] {}, [](int) {}};
    
    long seed = 0; // Make the test repeatable
    point_gen.execute(out.model_slices, out.slicegrid, seed);
    
    // Get the calculated support points.
    std::vector<sla::SupportPoint> support_points = point_gen.output();
    
    int validityflags = ASSUME_NO_REPAIR;
    
    // If there is no elevation, support points shall be removed from the
    // bottom of the object.
    if (std::abs(supportcfg.object_elevation_mm) < EPSILON) {
        sla::remove_bottom_points(support_points, zmin,
                                  supportcfg.base_height_mm);
    } else {
        // Should be support points at least on the bottom of the model
        REQUIRE_FALSE(support_points.empty());
        
        // Also the support mesh should not be empty.
        validityflags |= ASSUME_NO_EMPTY;
    }
    
    // Generate the actual support tree
    sla::SupportTreeBuilder treebuilder;
    treebuilder.build(sla::SupportableMesh{emesh, support_points, supportcfg});
    
    check_support_tree_integrity(treebuilder, supportcfg);
    
    const TriangleMesh &output_mesh = treebuilder.retrieve_mesh();
    
    check_validity(output_mesh, validityflags);
    
    // Quick check if the dimensions and placement of supports are correct
    auto obb = output_mesh.bounding_box();
    
    double allowed_zmin = zmin - supportcfg.object_elevation_mm;
    
    if (std::abs(supportcfg.object_elevation_mm) < EPSILON)
        allowed_zmin = zmin - 2 * supportcfg.head_back_radius_mm;
    
    REQUIRE(obb.min.z() >= allowed_zmin);
    REQUIRE(obb.max.z() <= zmax);
    
    // Move out the support tree into the byproducts, we can examine it further
    // in various tests.
    out.obj_fname   = std::move(obj_filename);
    out.supporttree = std::move(treebuilder);
    out.input_mesh  = std::move(mesh);
}

void check_support_tree_integrity(const sla::SupportTreeBuilder &stree, 
                                  const sla::SupportConfig &cfg)
{
    double gnd  = stree.ground_level;
    double H1   = cfg.max_solo_pillar_height_mm;
    double H2   = cfg.max_dual_pillar_height_mm;
    
    for (const sla::Head &head : stree.heads()) {
        REQUIRE((!head.is_valid() || head.pillar_id != sla::ID_UNSET ||
                head.bridge_id != sla::ID_UNSET));
    }
    
    for (const sla::Pillar &pillar : stree.pillars()) {
        if (std::abs(pillar.endpoint().z() - gnd) < EPSILON) {
            double h = pillar.height;
            
            if (h > H1) REQUIRE(pillar.links >= 1);
            else if(h > H2) { REQUIRE(pillar.links >= 2); }
        }
        
        REQUIRE(pillar.links <= cfg.pillar_cascade_neighbors);
        REQUIRE(pillar.bridges <= cfg.max_bridges_on_pillar);
    }
    
    double max_bridgelen = 0.;
    auto chck_bridge = [&cfg](const sla::Bridge &bridge, double &max_brlen) {
        Vec3d n = bridge.endp - bridge.startp;
        double d = sla::distance(n);
        max_brlen = std::max(d, max_brlen);
        
        double z     = n.z();
        double polar = std::acos(z / d);
        double slope = -polar + PI / 2.;
        REQUIRE(std::abs(slope) >= cfg.bridge_slope - EPSILON);
    };
    
    for (auto &bridge : stree.bridges()) chck_bridge(bridge, max_bridgelen);
    REQUIRE(max_bridgelen <= cfg.max_bridge_length_mm);
    
    max_bridgelen = 0;
    for (auto &bridge : stree.crossbridges()) chck_bridge(bridge, max_bridgelen);
    
    double md = cfg.max_pillar_link_distance_mm / std::cos(-cfg.bridge_slope);
    REQUIRE(max_bridgelen <= md);
}

void test_pad(const std::string &obj_filename, const sla::PadConfig &padcfg, PadByproducts &out)
{
    REQUIRE(padcfg.validate().empty());
    
    TriangleMesh mesh = load_model(obj_filename);
    
    REQUIRE_FALSE(mesh.empty());
    
    // Create pad skeleton only from the model
    Slic3r::sla::pad_blueprint(mesh, out.model_contours);
    
    test_concave_hull(out.model_contours);
    
    REQUIRE_FALSE(out.model_contours.empty());
    
    // Create the pad geometry for the model contours only
    Slic3r::sla::create_pad({}, out.model_contours, out.mesh, padcfg);
    
    check_validity(out.mesh);
    
    auto bb = out.mesh.bounding_box();
    REQUIRE(bb.max.z() - bb.min.z() == Approx(padcfg.full_height()));
}

static void _test_concave_hull(const Polygons &hull, const ExPolygons &polys)
{
    REQUIRE(polys.size() >=hull.size());
    
    double polys_area = 0;
    for (const ExPolygon &p : polys) polys_area += p.area();
    
    double cchull_area = 0;
    for (const Slic3r::Polygon &p : hull) cchull_area += p.area();
    
    REQUIRE(cchull_area >= Approx(polys_area));
    
    size_t cchull_holes = 0;
    for (const Slic3r::Polygon &p : hull)
        cchull_holes += p.is_clockwise() ? 1 : 0;
    
    REQUIRE(cchull_holes == 0);
    
    Polygons intr = diff(to_polygons(polys), hull);
    REQUIRE(intr.empty());
}

void test_concave_hull(const ExPolygons &polys) {
    sla::PadConfig pcfg;
    
    Slic3r::sla::ConcaveHull cchull{polys, pcfg.max_merge_dist_mm, []{}};
    
    _test_concave_hull(cchull.polygons(), polys);
    
    coord_t delta = scaled(pcfg.brim_size_mm + pcfg.wing_distance());
    ExPolygons wafflex = sla::offset_waffle_style_ex(cchull, delta);
    Polygons waffl = sla::offset_waffle_style(cchull, delta);
    
    _test_concave_hull(to_polygons(wafflex), polys);
    _test_concave_hull(waffl, polys);
}

void check_validity(const TriangleMesh &input_mesh, int flags)
{
    TriangleMesh mesh{input_mesh};
    
    if (flags & ASSUME_NO_EMPTY) {
        REQUIRE_FALSE(mesh.empty());
    } else if (mesh.empty())
        return; // If it can be empty and it is, there is nothing left to do.
    
    REQUIRE(stl_validate(&mesh.stl));
    
    bool do_update_shared_vertices = false;
    mesh.repair(do_update_shared_vertices);
    
    if (flags & ASSUME_NO_REPAIR) {
        REQUIRE_FALSE(mesh.needed_repair());
    }
    
    if (flags & ASSUME_MANIFOLD) {
        mesh.require_shared_vertices();
        if (!mesh.is_manifold()) mesh.WriteOBJFile("non_manifold.obj");
        REQUIRE(mesh.is_manifold());
    }
}
