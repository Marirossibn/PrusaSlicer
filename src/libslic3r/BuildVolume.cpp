#include "BuildVolume.hpp"
#include "ClipperUtils.hpp"
#include "Geometry/ConvexHull.hpp"
#include "GCode/GCodeProcessor.hpp"
#include "Point.hpp"

#include <boost/log/trivial.hpp>

namespace Slic3r {

BuildVolume::BuildVolume(const std::vector<Vec2d> &bed_shape, const double max_print_height) : m_bed_shape(bed_shape), m_max_print_height(max_print_height)
{
    assert(max_print_height >= 0);

    m_polygon     = Polygon::new_scale(bed_shape);

    // Calcuate various metrics of the input polygon.
    m_convex_hull = Geometry::convex_hull(m_polygon.points);
    m_bbox        = get_extents(m_convex_hull);
    m_area        = m_polygon.area();

    BoundingBoxf bboxf = get_extents(bed_shape);
    m_bboxf = BoundingBoxf3{ to_3d(bboxf.min, 0.), to_3d(bboxf.max, max_print_height) };

    if (bed_shape.size() >= 4 && std::abs((m_area - double(m_bbox.size().x()) * double(m_bbox.size().y()))) < sqr(SCALED_EPSILON)) {
        // Square print bed, use the bounding box for collision detection.
        m_type = Type::Rectangle;
        m_circle.center = 0.5 * (m_bbox.min.cast<double>() + m_bbox.max.cast<double>());
        m_circle.radius = 0.5 * m_bbox.size().cast<double>().norm();
    } else if (bed_shape.size() > 3) {
        // Circle was discretized, formatted into text with limited accuracy, thus the circle was deformed.
        // RANSAC is slightly more accurate than the iterative Taubin / Newton method with such an input.
//        m_circle = Geometry::circle_taubin_newton(bed_shape);
        m_circle = Geometry::circle_ransac(bed_shape);
        bool is_circle = true;
#ifndef NDEBUG
        // Measuring maximum absolute error of interpolating an input polygon with circle.
        double max_error = 0;
#endif // NDEBUG
        Vec2d prev = bed_shape.back();
        for (const Vec2d &p : bed_shape) {
#ifndef NDEBUG
            max_error = std::max(max_error, std::abs((p - m_circle.center).norm() - m_circle.radius));
#endif // NDEBUG
            if (// Polygon vertices must lie very close the circle.
                std::abs((p - m_circle.center).norm() - m_circle.radius) > 0.005 ||
                // Midpoints of polygon edges must not undercat more than 3mm. This corresponds to 72 edges per circle generated by BedShapePanel::update_shape().
                m_circle.radius - (0.5 * (prev + p) - m_circle.center).norm() > 3.) {
                is_circle = false;
                break;
            }
            prev = p;
        }
        if (is_circle) {
            m_type = Type::Circle;
            m_circle.center = scaled<double>(m_circle.center);
            m_circle.radius = scaled<double>(m_circle.radius);
        }
    }

    if (bed_shape.size() >= 3 && m_type == Type::Invalid) {
        // Circle check is not used for Convex / Custom shapes, fill it with something reasonable.
        m_circle = Geometry::smallest_enclosing_circle_welzl(m_convex_hull.points);
        m_type   = (m_convex_hull.area() - m_area) < sqr(SCALED_EPSILON) ? Type::Convex : Type::Custom;
        // Initialize the top / bottom decomposition for inside convex polygon check. Do it with two different epsilons applied.
        auto convex_decomposition = [](const Polygon &in, double epsilon) {
            Polygon src = expand(in, float(epsilon)).front();
            std::vector<Vec2d> pts;
            pts.reserve(src.size());
            for (const Point &pt : src.points)
                pts.emplace_back(unscaled<double>(pt.cast<double>().eval()));
            return Geometry::decompose_convex_polygon_top_bottom(pts);
        };
        m_top_bottom_convex_hull_decomposition_scene = convex_decomposition(m_convex_hull, SceneEpsilon);
        m_top_bottom_convex_hull_decomposition_bed   = convex_decomposition(m_convex_hull, BedEpsilon);
    }

    BOOST_LOG_TRIVIAL(debug) << "BuildVolume bed_shape clasified as: " << this->type_name();
}

#if 0
// Tests intersections of projected triangles, not just their vertices against a bounding box.
// This test also correctly evaluates collision of a non-convex object with the bounding box.
// Not used, slower than simple bounding box collision check and nobody complained about the inaccuracy of the simple test.
static inline BuildVolume::ObjectState rectangle_test(const indexed_triangle_set &its, const Transform3f &trafo, const Vec2f min, const Vec2f max, const float max_z)
{
    bool inside = false;
    bool outside = false;

    auto sign = [](const Vec3f& pt) -> char { return pt.z() > 0 ? 1 : pt.z() < 0 ? -1 : 0; };

    // Returns true if both inside and outside are set, thus early exit.
    auto test_intersection = [&inside, &outside, min, max, max_z](const Vec3f& p1, const Vec3f& p2, const Vec3f& p3) -> bool {
        // First test whether the triangle is completely inside or outside the bounding box.
        Vec3f pmin = p1.cwiseMin(p2).cwiseMin(p3);
        Vec3f pmax = p1.cwiseMax(p2).cwiseMax(p3);
        bool tri_inside = false;
        bool tri_outside = false;
        if (pmax.x() < min.x() || pmin.x() > max.x() || pmax.y() < min.y() || pmin.y() > max.y()) {
            // Separated by one of the rectangle sides.
            tri_outside = true;
        } else if (pmin.x() >= min.x() && pmax.x() <= max.x() && pmin.y() >= min.y() && pmax.y() <= max.y()) {
            // Fully inside the rectangle.
            tri_inside = true;
        } else {
            // Bounding boxes overlap. Test triangle sides against the bbox corners.
            Vec2f v1(- p2.y() + p1.y(), p2.x() - p1.x());
            Vec2f v2(- p2.y() + p2.y(), p3.x() - p2.x());
            Vec2f v3(- p1.y() + p3.y(), p1.x() - p3.x());
            bool  ccw = cross2(v1, v2) > 0;
            for (const Vec2f &p : { Vec2f{ min.x(), min.y() }, Vec2f{ min.x(), max.y() }, Vec2f{ max.x(), min.y() }, Vec2f{ max.x(), max.y() } }) {
                auto dot = v1.dot(p);
                if (ccw ? dot >= 0 : dot <= 0)
                    tri_inside = true;
                else
                    tri_outside = true;
            }
        }
        inside  |= tri_inside;
        outside |= tri_outside;
        return inside && outside;
    };

    // Edge crosses the z plane. Calculate intersection point with the plane.
    auto clip_edge = [](const Vec3f &p1, const Vec3f &p2) -> Vec3f {
        const float t = (world_min_z - p1.z()) / (p2.z() - p1.z());
        return { p1.x() + (p2.x() - p1.x()) * t, p1.y() + (p2.y() - p1.y()) * t, world_min_z };
    };

    // Clip at (p1, p2), p3 must be on the clipping plane.
    // Returns true if both inside and outside are set, thus early exit.
    auto clip_and_test1 = [&test_intersection, &clip_edge](const Vec3f &p1, const Vec3f &p2, const Vec3f &p3, bool p1above) -> bool {
        Vec3f pa = clip_edge(p1, p2);
        return p1above ? test_intersection(p1, pa, p3) : test_intersection(pa, p2, p3);
    };

    // Clip at (p1, p2) and (p2, p3).
    // Returns true if both inside and outside are set, thus early exit.
    auto clip_and_test2 = [&test_intersection, &clip_edge](const Vec3f &p1, const Vec3f &p2, const Vec3f &p3, bool p2above) -> bool {
        Vec3f pa = clip_edge(p1, p2);
        Vec3f pb = clip_edge(p2, p3);
        return p2above ? test_intersection(pa, p2, pb) : test_intersection(p1, pa, p3) || test_intersection(p3, pa, pb);
    };

    for (const stl_triangle_vertex_indices &tri : its.indices) {
        const Vec3f pts[3] = { trafo * its.vertices[tri(0)], trafo * its.vertices[tri(1)], trafo * its.vertices[tri(2)] };
        char signs[3] = { sign(pts[0]), sign(pts[1]), sign(pts[2]) };
        bool clips[3] = { signs[0] * signs[1] == -1, signs[1] * signs[2] == -1, signs[2] * signs[0] == -1 };
        if (clips[0]) {
            if (clips[1]) {
                // Clipping at (pt0, pt1) and (pt1, pt2).
                if (clip_and_test2(pts[0], pts[1], pts[2], signs[1] > 0))
                    break;
            } else if (clips[2]) {
                // Clipping at (pt0, pt1) and (pt0, pt2).
                if (clip_and_test2(pts[2], pts[0], pts[1], signs[0] > 0))
                    break;
            } else {
                // Clipping at (pt0, pt1), pt2 must be on the clipping plane.
                if (clip_and_test1(pts[0], pts[1], pts[2], signs[0] > 0))
                    break;
            }
        } else if (clips[1]) {
            if (clips[2]) {
                // Clipping at (pt1, pt2) and (pt0, pt2).
                if (clip_and_test2(pts[0], pts[1], pts[2], signs[1] > 0))
                    break;
            } else {
                // Clipping at (pt1, pt2), pt0 must be on the clipping plane.
                if (clip_and_test1(pts[1], pts[2], pts[0], signs[1] > 0))
                    break;
            }
        } else if (clips[2]) {
            // Clipping at (pt0, pt2), pt1 must be on the clipping plane.
            if (clip_and_test1(pts[2], pts[0], pts[1], signs[2] > 0))
                break;
        } else if (signs[0] >= 0 && signs[1] >= 0 && signs[2] >= 0) {
            // The triangle is above or on the clipping plane.
            if (test_intersection(pts[0], pts[1], pts[2]))
                break;
        }
    }
    return inside ? (outside ? BuildVolume::ObjectState::Colliding : BuildVolume::ObjectState::Inside) : BuildVolume::ObjectState::Outside;
}
#endif

// Trim the input transformed triangle mesh with print bed and test the remaining vertices with is_inside callback.
// Return inside / colliding / outside state.
template<typename InsideFn>
BuildVolume::ObjectState object_state_templ(const indexed_triangle_set &its, const Transform3f &trafo, bool may_be_below_bed, InsideFn is_inside)
{
    size_t num_inside = 0;
    size_t num_above  = 0;
    bool   inside     = false;
    bool   outside    = false;
    static constexpr const auto world_min_z = float(-BuildVolume::SceneEpsilon);

    if (may_be_below_bed) 
    {
        // Slower test, needs to clip the object edges with the print bed plane.
        // 1) Allocate transformed vertices with their position with respect to print bed surface.
        std::vector<char> sides;
        sides.reserve(its.vertices.size());

        const auto sign = [](const stl_vertex& pt) { return pt.z() > world_min_z ? 1 : pt.z() < world_min_z ? -1 : 0; };

        for (const stl_vertex &v : its.vertices) {
            const stl_vertex pt = trafo * v;
            const int        s = sign(pt);
            sides.emplace_back(s);
            if (s >= 0) {
                // Vertex above or on print bed surface. Test whether it is inside the build volume.
                ++ num_above;
                if (is_inside(pt))
                    ++ num_inside;
            }
        }

        if (num_above == 0)
            // Special case, the object is completely below the print bed, thus it is outside,
            // however we want to allow an object to be still printable if some of its parts are completely below the print bed.
            return BuildVolume::ObjectState::Below;

        // 2) Calculate intersections of triangle edges with the build surface.
        inside  = num_inside > 0;
        outside = num_inside < num_above;
        if (num_above < its.vertices.size() && ! (inside && outside)) {
            // Not completely above the build surface and status may still change by testing edges intersecting the build platform.
            for (const stl_triangle_vertex_indices &tri : its.indices) {
                const int s[3] = { sides[tri(0)], sides[tri(1)], sides[tri(2)] };
                if (std::min(s[0], std::min(s[1], s[2])) < 0 && std::max(s[0], std::max(s[1], s[2])) > 0) {
                    // Some edge of this triangle intersects the build platform. Calculate the intersection.
                    int iprev = 2;
                    for (int iedge = 0; iedge < 3; ++ iedge) {
                        if (s[iprev] * s[iedge] == -1) {
                            // edge intersects the build surface. Calculate intersection point.
                            const stl_vertex p1 = trafo * its.vertices[tri(iprev)];
                            const stl_vertex p2 = trafo * its.vertices[tri(iedge)];
                            assert(sign(p1) == s[iprev]);
                            assert(sign(p2) == s[iedge]);
                            assert(p1.z() * p2.z() < 0);
                            // Edge crosses the z plane. Calculate intersection point with the plane.
                            const float t = (world_min_z - p1.z()) / (p2.z() - p1.z());
                            (is_inside(Vec3f(p1.x() + (p2.x() - p1.x()) * t, p1.y() + (p2.y() - p1.y()) * t, world_min_z)) ? inside : outside) = true;
                        }
                        iprev = iedge;
                    }
                    if (inside && outside)
                        break;
                }
            }
        }
    }
    else 
    {
        // Much simpler and faster code, not clipping the object with the print bed.
        assert(! may_be_below_bed);
        num_above = its.vertices.size();
        for (const stl_vertex &v : its.vertices) {
            const stl_vertex pt = trafo * v;
            assert(pt.z() >= world_min_z);
            if (is_inside(pt))
                ++ num_inside;
        }
        inside  = num_inside > 0;
        outside = num_inside < num_above;
    }

    return inside ? (outside ? BuildVolume::ObjectState::Colliding : BuildVolume::ObjectState::Inside) : BuildVolume::ObjectState::Outside;
}

BuildVolume::ObjectState BuildVolume::object_state(const indexed_triangle_set& its, const Transform3f& trafo, bool may_be_below_bed, bool ignore_bottom) const
{
    switch (m_type) {
    case Type::Rectangle:
    {
        BoundingBox3Base<Vec3d> build_volume = this->bounding_volume().inflated(SceneEpsilon);
        if (m_max_print_height == 0.0)
            build_volume.max.z() = std::numeric_limits<double>::max();
        if (ignore_bottom)
            build_volume.min.z() = -std::numeric_limits<double>::max();
        BoundingBox3Base<Vec3f> build_volumef(build_volume.min.cast<float>(), build_volume.max.cast<float>());
        // The following test correctly interprets intersection of a non-convex object with a rectangular build volume.
        //return rectangle_test(its, trafo, to_2d(build_volume.min), to_2d(build_volume.max), build_volume.max.z());
        //FIXME This test does NOT correctly interprets intersection of a non-convex object with a rectangular build volume.
        return object_state_templ(its, trafo, may_be_below_bed, [build_volumef](const Vec3f &pt) { return build_volumef.contains(pt); });
    }
    case Type::Circle:
    {
        Geometry::Circlef circle { unscaled<float>(m_circle.center), unscaled<float>(m_circle.radius + SceneEpsilon) };
        return m_max_print_height == 0.0 ? 
            object_state_templ(its, trafo, may_be_below_bed, [circle](const Vec3f &pt) { return circle.contains(to_2d(pt)); }) :
            object_state_templ(its, trafo, may_be_below_bed, [circle, z = m_max_print_height + SceneEpsilon](const Vec3f &pt) { return pt.z() < z && circle.contains(to_2d(pt)); });
    }
    case Type::Convex:
    //FIXME doing test on convex hull until we learn to do test on non-convex polygons efficiently.
    case Type::Custom:
        return m_max_print_height == 0.0 ? 
            object_state_templ(its, trafo, may_be_below_bed, [this](const Vec3f &pt) { return Geometry::inside_convex_polygon(m_top_bottom_convex_hull_decomposition_scene, to_2d(pt).cast<double>()); }) :
            object_state_templ(its, trafo, may_be_below_bed, [this, z = m_max_print_height + SceneEpsilon](const Vec3f &pt) { return pt.z() < z && Geometry::inside_convex_polygon(m_top_bottom_convex_hull_decomposition_scene, to_2d(pt).cast<double>()); });
    case Type::Invalid:
    default:
        return ObjectState::Inside;
    }
}

BuildVolume::ObjectState BuildVolume::volume_state_bbox(const BoundingBoxf3& volume_bbox, bool ignore_bottom) const
{
    assert(m_type == Type::Rectangle);
    BoundingBox3Base<Vec3d> build_volume = this->bounding_volume().inflated(SceneEpsilon);
    if (m_max_print_height == 0.0)
        build_volume.max.z() = std::numeric_limits<double>::max();
    if (ignore_bottom)
        build_volume.min.z() = -std::numeric_limits<double>::max();
    return build_volume.max.z() <= - SceneEpsilon ? ObjectState::Below :
           build_volume.contains(volume_bbox) ? ObjectState::Inside : 
           build_volume.intersects(volume_bbox) ? ObjectState::Colliding : ObjectState::Outside;
}

bool BuildVolume::all_paths_inside(const GCodeProcessorResult& paths, const BoundingBoxf3& paths_bbox, bool ignore_bottom) const
{
    auto move_valid = [](const GCodeProcessorResult::MoveVertex &move) {
        return move.type == EMoveType::Extrude && move.extrusion_role != erCustom && move.width != 0.f && move.height != 0.f;
    };
    static constexpr const double epsilon = BedEpsilon;

    switch (m_type) {
    case Type::Rectangle:
    {
        BoundingBox3Base<Vec3d> build_volume = this->bounding_volume().inflated(epsilon);
        if (m_max_print_height == 0.0)
            build_volume.max.z() = std::numeric_limits<double>::max();
        if (ignore_bottom)
            build_volume.min.z() = -std::numeric_limits<double>::max();
        return build_volume.contains(paths_bbox);
    }
    case Type::Circle:
    {
        const Vec2f c = unscaled<float>(m_circle.center);
        const float r = unscaled<double>(m_circle.radius) + epsilon;
        const float r2 = sqr(r);
        return m_max_print_height == 0.0 ? 
            std::all_of(paths.moves.begin(), paths.moves.end(), [move_valid, c, r2](const GCodeProcessorResult::MoveVertex &move)
                { return ! move_valid(move) || (to_2d(move.position) - c).squaredNorm() <= r2; }) :
            std::all_of(paths.moves.begin(), paths.moves.end(), [move_valid, c, r2, z = m_max_print_height + epsilon](const GCodeProcessorResult::MoveVertex& move)
                { return ! move_valid(move) || ((to_2d(move.position) - c).squaredNorm() <= r2 && move.position.z() <= z); });
    }
    case Type::Convex:
    //FIXME doing test on convex hull until we learn to do test on non-convex polygons efficiently.
    case Type::Custom:
        return m_max_print_height == 0.0 ?
            std::all_of(paths.moves.begin(), paths.moves.end(), [move_valid, this](const GCodeProcessorResult::MoveVertex &move) 
                { return ! move_valid(move) || Geometry::inside_convex_polygon(m_top_bottom_convex_hull_decomposition_bed, to_2d(move.position).cast<double>()); }) :
            std::all_of(paths.moves.begin(), paths.moves.end(), [move_valid, this, z = m_max_print_height + epsilon](const GCodeProcessorResult::MoveVertex &move)
                { return ! move_valid(move) || (Geometry::inside_convex_polygon(m_top_bottom_convex_hull_decomposition_bed, to_2d(move.position).cast<double>()) && move.position.z() <= z); });
    default:
        return true;
    }
}

template<typename Fn>
inline bool all_inside_vertices_normals_interleaved(const std::vector<float> &paths, Fn fn)
{
    for (auto it = paths.begin(); it != paths.end(); ) {
        it += 3;
        if (! fn({ *it, *(it + 1), *(it + 2) }))
            return false;
        it += 3;
    }
    return true;
}

bool BuildVolume::all_paths_inside_vertices_and_normals_interleaved(const std::vector<float>& paths, const Eigen::AlignedBox<float, 3>& paths_bbox, bool ignore_bottom) const
{
    assert(paths.size() % 6 == 0);
    static constexpr const double epsilon = BedEpsilon;
    switch (m_type) {
    case Type::Rectangle:
    {
        BoundingBox3Base<Vec3d> build_volume = this->bounding_volume().inflated(epsilon);
        if (m_max_print_height == 0.0)
            build_volume.max.z() = std::numeric_limits<double>::max();
        if (ignore_bottom)
            build_volume.min.z() = -std::numeric_limits<double>::max();
        return build_volume.contains(paths_bbox.min().cast<double>()) && build_volume.contains(paths_bbox.max().cast<double>());
    }
    case Type::Circle:
    {
        const Vec2f c = unscaled<float>(m_circle.center);
        const float r = unscaled<double>(m_circle.radius) + float(epsilon);
        const float r2 = sqr(r);
        return m_max_print_height == 0.0 ?
            all_inside_vertices_normals_interleaved(paths, [c, r2](Vec3f p) { return (to_2d(p) - c).squaredNorm() <= r2; }) :
            all_inside_vertices_normals_interleaved(paths, [c, r2, z = m_max_print_height + epsilon](Vec3f p) { return (to_2d(p) - c).squaredNorm() <= r2 && p.z() <= z; });
    }
    case Type::Convex:
        //FIXME doing test on convex hull until we learn to do test on non-convex polygons efficiently.
    case Type::Custom:
        return m_max_print_height == 0.0 ?
            all_inside_vertices_normals_interleaved(paths, [this](Vec3f p) { return Geometry::inside_convex_polygon(m_top_bottom_convex_hull_decomposition_bed, to_2d(p).cast<double>()); }) :
            all_inside_vertices_normals_interleaved(paths, [this, z = m_max_print_height + epsilon](Vec3f p) { return Geometry::inside_convex_polygon(m_top_bottom_convex_hull_decomposition_bed, to_2d(p).cast<double>()) && p.z() <= z; });
    default:
        return true;
    }
}

std::string_view BuildVolume::type_name(Type type)
{
    using namespace std::literals;
    switch (type) {
    case Type::Invalid:   return "Invalid"sv;
    case Type::Rectangle: return "Rectangle"sv;
    case Type::Circle:    return "Circle"sv;
    case Type::Convex:    return "Convex"sv;
    case Type::Custom:    return "Custom"sv;
    }
    // make visual studio happy
    assert(false);
    return {};
}

} // namespace Slic3r
