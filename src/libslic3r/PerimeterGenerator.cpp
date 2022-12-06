#include "PerimeterGenerator.hpp"
#include "AABBTreeIndirect.hpp"
#include "AABBTreeLines.hpp"
#include "BoundingBox.hpp"
#include "BridgeDetector.hpp"
#include "ClipperUtils.hpp"
#include "ExPolygon.hpp"
#include "ExtrusionEntity.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "Geometry/MedialAxis.hpp"
#include "KDTreeIndirect.hpp"
#include "MultiPoint.hpp"
#include "Point.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"
#include "PrintConfig.hpp"
#include "ShortestPath.hpp"
#include "Surface.hpp"

#include "Geometry/ConvexHull.hpp"
#include "SurfaceCollection.hpp"
#include "clipper/clipper_z.hpp"

#include "Arachne/WallToolPaths.hpp"
#include "Arachne/utils/ExtrusionLine.hpp"
#include "Arachne/utils/ExtrusionJunction.hpp"
#include "libslic3r.h"

#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <limits>
#include <math.h>
#include <ostream>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// #define ARACHNE_DEBUG

#ifdef ARACHNE_DEBUG
#include "SVG.hpp"
#include "Utils.hpp"
#endif

#include "SVG.hpp"

namespace Slic3r {

ExtrusionMultiPath PerimeterGenerator::thick_polyline_to_multi_path(const ThickPolyline &thick_polyline, ExtrusionRole role, const Flow &flow, const float tolerance, const float merge_tolerance)
{
    ExtrusionMultiPath multi_path;
    ExtrusionPath      path(role);
    ThickLines         lines = thick_polyline.thicklines();

    for (int i = 0; i < (int)lines.size(); ++i) {
        const ThickLine& line = lines[i];
        assert(line.a_width >= SCALED_EPSILON && line.b_width >= SCALED_EPSILON);

        const coordf_t line_len = line.length();
        if (line_len < SCALED_EPSILON) {
            // The line is so tiny that we don't care about its width when we connect it to another line.
            if (!path.empty())
                path.polyline.points.back() = line.b; // If the variable path is non-empty, connect this tiny line to it.
            else if (i + 1 < (int)lines.size()) // If there is at least one following line, connect this tiny line to it.
                lines[i + 1].a = line.a;
            else if (!multi_path.paths.empty())
                multi_path.paths.back().polyline.points.back() = line.b; // Connect this tiny line to the last finished path.

            // If any of the above isn't satisfied, then remove this tiny line.
            continue;
        }

        double thickness_delta = fabs(line.a_width - line.b_width);
        if (thickness_delta > tolerance) {
            const auto segments = (unsigned int)ceil(thickness_delta / tolerance);
            const coordf_t seg_len = line_len / segments;
            Points pp;
            std::vector<coordf_t> width;
            {
                pp.push_back(line.a);
                width.push_back(line.a_width);
                for (size_t j = 1; j < segments; ++j) {
                    pp.push_back((line.a.cast<double>() + (line.b - line.a).cast<double>().normalized() * (j * seg_len)).cast<coord_t>());

                    coordf_t w = line.a_width + (j*seg_len) * (line.b_width-line.a_width) / line_len;
                    width.push_back(w);
                    width.push_back(w);
                }
                pp.push_back(line.b);
                width.push_back(line.b_width);

                assert(pp.size() == segments + 1u);
                assert(width.size() == segments*2);
            }

            // delete this line and insert new ones
            lines.erase(lines.begin() + i);
            for (size_t j = 0; j < segments; ++j) {
                ThickLine new_line(pp[j], pp[j+1]);
                new_line.a_width = width[2*j];
                new_line.b_width = width[2*j+1];
                lines.insert(lines.begin() + i + j, new_line);
            }

            -- i;
            continue;
        }

        const double w        = fmax(line.a_width, line.b_width);
        const Flow   new_flow = (role == erOverhangPerimeter && flow.bridge()) ? flow : flow.with_width(unscale<float>(w) + flow.height() * float(1. - 0.25 * PI));
        if (path.polyline.points.empty()) {
            path.polyline.append(line.a);
            path.polyline.append(line.b);
            // Convert from spacing to extrusion width based on the extrusion model
            // of a square extrusion ended with semi circles.
            #ifdef SLIC3R_DEBUG
            printf("  filling %f gap\n", flow.width);
            #endif
            path.mm3_per_mm  = new_flow.mm3_per_mm();
            path.width       = new_flow.width();
            path.height      = new_flow.height();
        } else {
            assert(path.width >= EPSILON);
            thickness_delta = scaled<double>(fabs(path.width - new_flow.width()));
            if (thickness_delta <= merge_tolerance) {
                // the width difference between this line and the current flow
                // (of the previous line) width is within the accepted tolerance
                path.polyline.append(line.b);
            } else {
                // we need to initialize a new line
                multi_path.paths.emplace_back(std::move(path));
                path = ExtrusionPath(role);
                -- i;
            }
        }
    }
    if (path.polyline.is_valid())
        multi_path.paths.emplace_back(std::move(path));
    return multi_path;
}

static void variable_width_classic(const ThickPolylines &polylines, ExtrusionRole role, const Flow &flow, std::vector<ExtrusionEntity *> &out)
{
    // This value determines granularity of adaptive width, as G-code does not allow
    // variable extrusion within a single move; this value shall only affect the amount
    // of segments, and any pruning shall be performed before we apply this tolerance.
    const auto tolerance = float(scale_(0.05));
    for (const ThickPolyline &p : polylines) {
        ExtrusionMultiPath multi_path = PerimeterGenerator::thick_polyline_to_multi_path(p, role, flow, tolerance, tolerance);
        // Append paths to collection.
        if (!multi_path.paths.empty()) {
            for (auto it = std::next(multi_path.paths.begin()); it != multi_path.paths.end(); ++it) {
                assert(it->polyline.points.size() >= 2);
                assert(std::prev(it)->polyline.last_point() == it->polyline.first_point());
            }

            if (multi_path.paths.front().first_point() == multi_path.paths.back().last_point())
                out.emplace_back(new ExtrusionLoop(std::move(multi_path.paths)));
            else
                out.emplace_back(new ExtrusionMultiPath(std::move(multi_path)));
        }
    }
}

// Hierarchy of perimeters.
class PerimeterGeneratorLoop {
public:
    // Polygon of this contour.
    Polygon                             polygon;
    // Is it a contour or a hole?
    // Contours are CCW oriented, holes are CW oriented.
    bool                                is_contour;
    // Depth in the hierarchy. External perimeter has depth = 0. An external perimeter could be both a contour and a hole.
    unsigned short                      depth;
    // Should this contur be fuzzyfied on path generation?
    bool                                fuzzify;
    // Children contour, may be both CCW and CW oriented (outer contours or holes).
    std::vector<PerimeterGeneratorLoop> children;
    
    PerimeterGeneratorLoop(const Polygon &polygon, unsigned short depth, bool is_contour, bool fuzzify) : 
        polygon(polygon), is_contour(is_contour), depth(depth), fuzzify(fuzzify) {}
    // External perimeter. It may be CCW or CW oriented (outer contour or hole contour).
    bool is_external() const { return this->depth == 0; }
    // An island, which may have holes, but it does not have another internal island.
    bool is_internal_contour() const {
        // An internal contour is a contour containing no other contours
        if (! this->is_contour)
            return false;
        for (const PerimeterGeneratorLoop &loop : this->children)
            if (loop.is_contour)
                return false;
        return true;
    }
};

// Thanks Cura developers for this function.
static void fuzzy_polygon(Polygon &poly, double fuzzy_skin_thickness, double fuzzy_skin_point_dist)
{
    const double min_dist_between_points = fuzzy_skin_point_dist * 3. / 4.; // hardcoded: the point distance may vary between 3/4 and 5/4 the supplied value
    const double range_random_point_dist = fuzzy_skin_point_dist / 2.;
    double dist_left_over = double(rand()) * (min_dist_between_points / 2) / double(RAND_MAX); // the distance to be traversed on the line before making the first new point
    Point* p0 = &poly.points.back();
    Points out;
    out.reserve(poly.points.size());
    for (Point &p1 : poly.points)
    { // 'a' is the (next) new point between p0 and p1
        Vec2d  p0p1      = (p1 - *p0).cast<double>();
        double p0p1_size = p0p1.norm();
        // so that p0p1_size - dist_last_point evaulates to dist_left_over - p0p1_size
        double dist_last_point = dist_left_over + p0p1_size * 2.;
        for (double p0pa_dist = dist_left_over; p0pa_dist < p0p1_size;
            p0pa_dist += min_dist_between_points + double(rand()) * range_random_point_dist / double(RAND_MAX))
        {
            double r = double(rand()) * (fuzzy_skin_thickness * 2.) / double(RAND_MAX) - fuzzy_skin_thickness;
            out.emplace_back(*p0 + (p0p1 * (p0pa_dist / p0p1_size) + perp(p0p1).cast<double>().normalized() * r).cast<coord_t>());
            dist_last_point = p0pa_dist;
        }
        dist_left_over = p0p1_size - dist_last_point;
        p0 = &p1;
    }
    while (out.size() < 3) {
        size_t point_idx = poly.size() - 2;
        out.emplace_back(poly[point_idx]);
        if (point_idx == 0)
            break;
        -- point_idx;
    }
    if (out.size() >= 3)
        poly.points = std::move(out);
}

// Thanks Cura developers for this function.
static void fuzzy_extrusion_line(Arachne::ExtrusionLine &ext_lines, double fuzzy_skin_thickness, double fuzzy_skin_point_dist)
{
    const double min_dist_between_points = fuzzy_skin_point_dist * 3. / 4.; // hardcoded: the point distance may vary between 3/4 and 5/4 the supplied value
    const double range_random_point_dist = fuzzy_skin_point_dist / 2.;
    double       dist_left_over          = double(rand()) * (min_dist_between_points / 2) / double(RAND_MAX); // the distance to be traversed on the line before making the first new point

    auto                                   *p0 = &ext_lines.front();
    std::vector<Arachne::ExtrusionJunction> out;
    out.reserve(ext_lines.size());
    for (auto &p1 : ext_lines) {
        if (p0->p == p1.p) { // Connect endpoints.
            out.emplace_back(p1.p, p1.w, p1.perimeter_index);
            continue;
        }

        // 'a' is the (next) new point between p0 and p1
        Vec2d  p0p1      = (p1.p - p0->p).cast<double>();
        double p0p1_size = p0p1.norm();
        // so that p0p1_size - dist_last_point evaulates to dist_left_over - p0p1_size
        double dist_last_point = dist_left_over + p0p1_size * 2.;
        for (double p0pa_dist = dist_left_over; p0pa_dist < p0p1_size; p0pa_dist += min_dist_between_points + double(rand()) * range_random_point_dist / double(RAND_MAX)) {
            double r = double(rand()) * (fuzzy_skin_thickness * 2.) / double(RAND_MAX) - fuzzy_skin_thickness;
            out.emplace_back(p0->p + (p0p1 * (p0pa_dist / p0p1_size) + perp(p0p1).cast<double>().normalized() * r).cast<coord_t>(), p1.w, p1.perimeter_index);
            dist_last_point = p0pa_dist;
        }
        dist_left_over = p0p1_size - dist_last_point;
        p0             = &p1;
    }

    while (out.size() < 3) {
        size_t point_idx = ext_lines.size() - 2;
        out.emplace_back(ext_lines[point_idx].p, ext_lines[point_idx].w, ext_lines[point_idx].perimeter_index);
        if (point_idx == 0)
            break;
        -- point_idx;
    }

    if (ext_lines.back().p == ext_lines.front().p) // Connect endpoints.
        out.front().p = out.back().p;

    if (out.size() >= 3)
        ext_lines.junctions = std::move(out);
}

using PerimeterGeneratorLoops = std::vector<PerimeterGeneratorLoop>;

static ExtrusionEntityCollection traverse_loops_classic(const PerimeterGenerator::Parameters &params, const Polygons &lower_slices_polygons_cache, const PerimeterGeneratorLoops &loops, ThickPolylines &thin_walls)
{
    // loops is an arrayref of ::Loop objects
    // turn each one into an ExtrusionLoop object
    ExtrusionEntityCollection   coll;
    Polygon                     fuzzified;
    for (const PerimeterGeneratorLoop &loop : loops) {
        bool is_external = loop.is_external();
        
        ExtrusionRole role;
        ExtrusionLoopRole loop_role;
        role = is_external ? erExternalPerimeter : erPerimeter;
        if (loop.is_internal_contour()) {
            // Note that we set loop role to ContourInternalPerimeter
            // also when loop is both internal and external (i.e.
            // there's only one contour loop).
            loop_role = elrContourInternalPerimeter;
        } else {
            loop_role = elrDefault;
        }
        
        // detect overhanging/bridging perimeters
        ExtrusionPaths paths;
        const Polygon &polygon = loop.fuzzify ? fuzzified : loop.polygon;
        if (loop.fuzzify) {
            fuzzified = loop.polygon;
            fuzzy_polygon(fuzzified, scaled<float>(params.config.fuzzy_skin_thickness.value), scaled<float>(params.config.fuzzy_skin_point_dist.value));
        }
        if (params.config.overhangs && params.layer_id > params.object_config.raft_layers
            && ! ((params.object_config.support_material || params.object_config.support_material_enforce_layers > 0) && 
                  params.object_config.support_material_contact_distance.value == 0)) {
            BoundingBox bbox(polygon.points);
            bbox.offset(SCALED_EPSILON);
            Polygons lower_slices_polygons_clipped = ClipperUtils::clip_clipper_polygons_with_subject_bbox(lower_slices_polygons_cache, bbox);
            // get non-overhang paths by intersecting this loop with the grown lower slices
            extrusion_paths_append(
                paths,
                intersection_pl({ polygon }, lower_slices_polygons_clipped),
                role,
                is_external ? params.ext_mm3_per_mm           : params.mm3_per_mm,
                is_external ? params.ext_perimeter_flow.width() : params.perimeter_flow.width(),
                float(params.layer_height));
            
            // get overhang paths by checking what parts of this loop fall 
            // outside the grown lower slices (thus where the distance between
            // the loop centerline and original lower slices is >= half nozzle diameter
            extrusion_paths_append(
                paths,
                diff_pl({ polygon }, lower_slices_polygons_clipped),
                erOverhangPerimeter,
                params.mm3_per_mm_overhang,
                params.overhang_flow.width(),
                params.overhang_flow.height());
            
            // Reapply the nearest point search for starting point.
            // We allow polyline reversal because Clipper may have randomly reversed polylines during clipping.
            chain_and_reorder_extrusion_paths(paths, &paths.front().first_point());
        } else {
            ExtrusionPath path(role);
            path.polyline   = polygon.split_at_first_point();
            path.mm3_per_mm = is_external ? params.ext_mm3_per_mm             : params.mm3_per_mm;
            path.width      = is_external ? params.ext_perimeter_flow.width() : params.perimeter_flow.width();
            path.height     = float(params.layer_height);
            paths.push_back(path);
        }
        
        coll.append(ExtrusionLoop(std::move(paths), loop_role));
    }
    
    // Append thin walls to the nearest-neighbor search (only for first iteration)
    if (! thin_walls.empty()) {
        variable_width_classic(thin_walls, erExternalPerimeter, params.ext_perimeter_flow, coll.entities);
        thin_walls.clear();
    }
    
    // Traverse children and build the final collection.
	Point zero_point(0, 0);
	std::vector<std::pair<size_t, bool>> chain = chain_extrusion_entities(coll.entities, &zero_point);
    ExtrusionEntityCollection out;
    for (const std::pair<size_t, bool> &idx : chain) {
		assert(coll.entities[idx.first] != nullptr);
        if (idx.first >= loops.size()) {
            // This is a thin wall.
			out.entities.reserve(out.entities.size() + 1);
            out.entities.emplace_back(coll.entities[idx.first]);
			coll.entities[idx.first] = nullptr;
            if (idx.second)
				out.entities.back()->reverse();
        } else {
            const PerimeterGeneratorLoop &loop = loops[idx.first];
            assert(thin_walls.empty());
            ExtrusionEntityCollection children = traverse_loops_classic(params, lower_slices_polygons_cache, loop.children, thin_walls);
            out.entities.reserve(out.entities.size() + children.entities.size() + 1);
            ExtrusionLoop *eloop = static_cast<ExtrusionLoop*>(coll.entities[idx.first]);
            coll.entities[idx.first] = nullptr;
            if (loop.is_contour) {
                eloop->make_counter_clockwise();
                out.append(std::move(children.entities));
                out.entities.emplace_back(eloop);
            } else {
                eloop->make_clockwise();
                out.entities.emplace_back(eloop);
                out.append(std::move(children.entities));
            }
        }
    }
    return out;
}

static ClipperLib_Z::Paths clip_extrusion(const ClipperLib_Z::Path &subject, const ClipperLib_Z::Paths &clip, ClipperLib_Z::ClipType clipType)
{
    ClipperLib_Z::Clipper clipper;
    clipper.ZFillFunction([](const ClipperLib_Z::IntPoint &e1bot, const ClipperLib_Z::IntPoint &e1top, const ClipperLib_Z::IntPoint &e2bot,
                             const ClipperLib_Z::IntPoint &e2top, ClipperLib_Z::IntPoint &pt) {
        // The clipping contour may be simplified by clipping it with a bounding box of "subject" path.
        // The clipping function used may produce self intersections outside of the "subject" bounding box. Such self intersections are 
        // harmless to the result of the clipping operation,
        // Both ends of each edge belong to the same source: Either they are from subject or from clipping path.
        assert(e1bot.z() >= 0 && e1top.z() >= 0);
        assert(e2bot.z() >= 0 && e2top.z() >= 0);
        assert((e1bot.z() == 0) == (e1top.z() == 0));
        assert((e2bot.z() == 0) == (e2top.z() == 0));

        // Start & end points of the clipped polyline (extrusion path with a non-zero width).
        ClipperLib_Z::IntPoint start = e1bot;
        ClipperLib_Z::IntPoint end   = e1top;
        if (start.z() <= 0 && end.z() <= 0) {
            start = e2bot;
            end   = e2top;
        }

        if (start.z() <= 0 && end.z() <= 0) {
            // Self intersection on the source contour.
            assert(start.z() == 0 && end.z() == 0);
            pt.z() = 0;
        } else {
            // Interpolate extrusion line width.
            assert(start.z() > 0 && end.z() > 0);

            double length_sqr = (end - start).cast<double>().squaredNorm();
            double dist_sqr   = (pt - start).cast<double>().squaredNorm();
            double t          = std::sqrt(dist_sqr / length_sqr);

            pt.z() = start.z() + coord_t((end.z() - start.z()) * t);
        }
    });

    clipper.AddPath(subject, ClipperLib_Z::ptSubject, false);
    clipper.AddPaths(clip, ClipperLib_Z::ptClip, true);

    ClipperLib_Z::PolyTree clipped_polytree;
    ClipperLib_Z::Paths    clipped_paths;
    clipper.Execute(clipType, clipped_polytree, ClipperLib_Z::pftNonZero, ClipperLib_Z::pftNonZero);
    ClipperLib_Z::PolyTreeToPaths(clipped_polytree, clipped_paths);

    // Clipped path could contain vertices from the clip with a Z coordinate equal to zero.
    // For those vertices, we must assign value based on the subject.
    // This happens only in sporadic cases.
    for (ClipperLib_Z::Path &path : clipped_paths)
        for (ClipperLib_Z::IntPoint &c_pt : path)
            if (c_pt.z() == 0) {
                // Now we must find the corresponding line on with this point is located and compute line width (Z coordinate).
                if (subject.size() <= 2)
                    continue;

                const Point pt(c_pt.x(), c_pt.y());
                Point       projected_pt_min;
                auto        it_min       = subject.begin();
                auto        dist_sqr_min = std::numeric_limits<double>::max();
                Point       prev(subject.front().x(), subject.front().y());
                for (auto it = std::next(subject.begin()); it != subject.end(); ++it) {
                    Point curr(it->x(), it->y());
                    Point projected_pt;
                    if (double dist_sqr = line_alg::distance_to_squared(Line(prev, curr), pt, &projected_pt); dist_sqr < dist_sqr_min) {
                        dist_sqr_min     = dist_sqr;
                        projected_pt_min = projected_pt;
                        it_min           = std::prev(it);
                    }
                    prev = curr;
                }

                assert(dist_sqr_min <= SCALED_EPSILON);
                assert(std::next(it_min) != subject.end());

                const Point  pt_a(it_min->x(), it_min->y());
                const Point  pt_b(std::next(it_min)->x(), std::next(it_min)->y());
                const double line_len = (pt_b - pt_a).cast<double>().norm();
                const double dist     = (projected_pt_min - pt_a).cast<double>().norm();
                c_pt.z()              = coord_t(double(it_min->z()) + (dist / line_len) * double(std::next(it_min)->z() - it_min->z()));
            }

    assert([&clipped_paths = std::as_const(clipped_paths)]() -> bool {
        for (const ClipperLib_Z::Path &path : clipped_paths)
            for (const ClipperLib_Z::IntPoint &pt : path)
                if (pt.z() <= 0)
                    return false;
        return true;
    }());

    return clipped_paths;
}

struct PerimeterGeneratorArachneExtrusion
{
    Arachne::ExtrusionLine *extrusion = nullptr;
    // Indicates if closed ExtrusionLine is a contour or a hole. Used it only when ExtrusionLine is a closed loop.
    bool is_contour = false;
    // Should this extrusion be fuzzyfied on path generation?
    bool fuzzify = false;
};

static ExtrusionEntityCollection traverse_extrusions(const PerimeterGenerator::Parameters &params, const Polygons &lower_slices_polygons_cache, std::vector<PerimeterGeneratorArachneExtrusion> &pg_extrusions)
{
    ExtrusionEntityCollection extrusion_coll;
    for (PerimeterGeneratorArachneExtrusion &pg_extrusion : pg_extrusions) {
        Arachne::ExtrusionLine *extrusion = pg_extrusion.extrusion;
        if (extrusion->empty())
            continue;

        const bool    is_external = extrusion->inset_idx == 0;
        ExtrusionRole role        = is_external ? erExternalPerimeter : erPerimeter;

        if (pg_extrusion.fuzzify)
            fuzzy_extrusion_line(*extrusion, scaled<float>(params.config.fuzzy_skin_thickness.value), scaled<float>(params.config.fuzzy_skin_point_dist.value));

        ExtrusionPaths paths;
        // detect overhanging/bridging perimeters
        if (params.config.overhangs && params.layer_id > params.object_config.raft_layers
            && ! ((params.object_config.support_material || params.object_config.support_material_enforce_layers > 0) &&
                 params.object_config.support_material_contact_distance.value == 0)) {

            ClipperLib_Z::Path extrusion_path;
            extrusion_path.reserve(extrusion->size());
            BoundingBox extrusion_path_bbox;
            for (const Arachne::ExtrusionJunction &ej : extrusion->junctions) {
                extrusion_path.emplace_back(ej.p.x(), ej.p.y(), ej.w);
                extrusion_path_bbox.merge(Point{ej.p.x(), ej.p.y()});
            }

            ClipperLib_Z::Paths lower_slices_paths;
            lower_slices_paths.reserve(lower_slices_polygons_cache.size());
            {
                Points clipped;
                extrusion_path_bbox.offset(SCALED_EPSILON);
                for (const Polygon &poly : lower_slices_polygons_cache) {
                    clipped.clear();
                    ClipperUtils::clip_clipper_polygon_with_subject_bbox(poly.points, extrusion_path_bbox, clipped);
                    if (! clipped.empty()) {
                        lower_slices_paths.emplace_back();
                        ClipperLib_Z::Path &out = lower_slices_paths.back();
                        out.reserve(clipped.size());
                        for (const Point &pt : clipped)
                            out.emplace_back(pt.x(), pt.y(), 0);
                    }
                }
            }

            // get non-overhang paths by intersecting this loop with the grown lower slices
            extrusion_paths_append(paths, clip_extrusion(extrusion_path, lower_slices_paths, ClipperLib_Z::ctIntersection), role,
                                   is_external ? params.ext_perimeter_flow : params.perimeter_flow);

            // get overhang paths by checking what parts of this loop fall
            // outside the grown lower slices (thus where the distance between
            // the loop centerline and original lower slices is >= half nozzle diameter
            extrusion_paths_append(paths, clip_extrusion(extrusion_path, lower_slices_paths, ClipperLib_Z::ctDifference), erOverhangPerimeter,
                                   params.overhang_flow);

            // Reapply the nearest point search for starting point.
            // We allow polyline reversal because Clipper may have randomly reversed polylines during clipping.
            // Arachne sometimes creates extrusion with zero-length (just two same endpoints);
            if (!paths.empty()) {
                Point start_point = paths.front().first_point();
                if (!extrusion->is_closed) {
                    // Especially for open extrusion, we need to select a starting point that is at the start
                    // or the end of the extrusions to make one continuous line. Also, we prefer a non-overhang
                    // starting point.
                    struct PointInfo
                    {
                        size_t occurrence  = 0;
                        bool   is_overhang = false;
                    };
                    std::unordered_map<Point, PointInfo, PointHash> point_occurrence;
                    for (const ExtrusionPath &path : paths) {
                        ++point_occurrence[path.polyline.first_point()].occurrence;
                        ++point_occurrence[path.polyline.last_point()].occurrence;
                        if (path.role() == erOverhangPerimeter) {
                            point_occurrence[path.polyline.first_point()].is_overhang = true;
                            point_occurrence[path.polyline.last_point()].is_overhang  = true;
                        }
                    }

                    // Prefer non-overhang point as a starting point.
                    for (const std::pair<Point, PointInfo> pt : point_occurrence)
                        if (pt.second.occurrence == 1) {
                            start_point = pt.first;
                            if (!pt.second.is_overhang) {
                                start_point = pt.first;
                                break;
                            }
                        }
                }

                chain_and_reorder_extrusion_paths(paths, &start_point);
            }
        } else {
            extrusion_paths_append(paths, *extrusion, role, is_external ? params.ext_perimeter_flow : params.perimeter_flow);
        }

        // Append paths to collection.
        if (!paths.empty()) {
            if (extrusion->is_closed) {
                ExtrusionLoop extrusion_loop(std::move(paths));
                // Restore the orientation of the extrusion loop.
                if (pg_extrusion.is_contour)
                    extrusion_loop.make_counter_clockwise();
                else
                    extrusion_loop.make_clockwise();

                for (auto it = std::next(extrusion_loop.paths.begin()); it != extrusion_loop.paths.end(); ++it) {
                    assert(it->polyline.points.size() >= 2);
                    assert(std::prev(it)->polyline.last_point() == it->polyline.first_point());
                }
                assert(extrusion_loop.paths.front().first_point() == extrusion_loop.paths.back().last_point());

                extrusion_coll.append(std::move(extrusion_loop));
            } else {
                // Because we are processing one ExtrusionLine all ExtrusionPaths should form one connected path.
                // But there is possibility that due to numerical issue there is poss
                assert([&paths = std::as_const(paths)]() -> bool {
                    for (auto it = std::next(paths.begin()); it != paths.end(); ++it)
                        if (std::prev(it)->polyline.last_point() != it->polyline.first_point())
                            return false;
                    return true;
                }());
                ExtrusionMultiPath multi_path;
                multi_path.paths.emplace_back(std::move(paths.front()));

                for (auto it_path = std::next(paths.begin()); it_path != paths.end(); ++it_path) {
                    if (multi_path.paths.back().last_point() != it_path->first_point()) {
                        extrusion_coll.append(ExtrusionMultiPath(std::move(multi_path)));
                        multi_path = ExtrusionMultiPath();
                    }
                    multi_path.paths.emplace_back(std::move(*it_path));
                }

                extrusion_coll.append(ExtrusionMultiPath(std::move(multi_path)));
            }
        }
    }

    return extrusion_coll;
}

#ifdef ARACHNE_DEBUG
static void export_perimeters_to_svg(const std::string &path, const Polygons &contours, const std::vector<Arachne::VariableWidthLines> &perimeters, const ExPolygons &infill_area)
{
    coordf_t    stroke_width = scale_(0.03);
    BoundingBox bbox         = get_extents(contours);
    bbox.offset(scale_(1.));
    ::Slic3r::SVG svg(path.c_str(), bbox);

    svg.draw(infill_area, "cyan");

    for (const Arachne::VariableWidthLines &perimeter : perimeters)
        for (const Arachne::ExtrusionLine &extrusion_line : perimeter) {
            ThickPolyline thick_polyline = to_thick_polyline(extrusion_line);
            svg.draw({thick_polyline}, "green", "blue", stroke_width);
        }

    for (const Line &line : to_lines(contours))
        svg.draw(line, "red", stroke_width);
}
#endif

// find out if paths touch - at least one point of one path is within limit distance of second path
bool paths_touch(const ExtrusionPath &path_one, const ExtrusionPath &path_two, double limit_distance)
{
    AABBTreeLines::LinesDistancer<Line> lines_one{path_one.as_polyline().lines()};
    AABBTreeLines::LinesDistancer<Line> lines_two{path_two.as_polyline().lines()};

    for (size_t pt_idx = 0; pt_idx < path_one.polyline.size(); pt_idx++) {
        if (std::abs(lines_two.signed_distance_from_lines(path_one.polyline.points[pt_idx])) < limit_distance) { return true; }
    }

    for (size_t pt_idx = 0; pt_idx < path_two.polyline.size(); pt_idx++) {
        if (std::abs(lines_one.signed_distance_from_lines(path_two.polyline.points[pt_idx])) < limit_distance) { return true; }
    }
    return false;
}

ExtrusionPaths reconnect_extrusion_paths(const ExtrusionPaths& paths, double limit_distance) {
    if (paths.empty()) return paths;
    ExtrusionPaths result;
    result.push_back(paths.front());
    for (size_t pidx = 1; pidx < paths.size(); pidx++) {
        if ((result.back().last_point() - paths[pidx].first_point()).cast<double>().squaredNorm() < limit_distance * limit_distance) {
            result.back().polyline.points.insert(result.back().polyline.points.end(), paths[pidx].polyline.points.begin(),
                                                 paths[pidx].polyline.points.end());
        } else {
            result.push_back(paths[pidx]);
        }
    }
    return result;
}

ExtrusionPaths sort_and_connect_extra_perimeters(const std::vector<ExtrusionPaths> &extra_perims, double touch_distance)
{
    std::vector<ExtrusionPaths> connected_shells;
    for (const ExtrusionPaths& ps : extra_perims) {
        connected_shells.push_back(reconnect_extrusion_paths(ps, touch_distance));
    }
    struct Pidx
    {
        size_t shell;
        size_t path;
        bool   operator==(const Pidx &rhs) const { return shell == rhs.shell && path == rhs.path; }
    };
    struct PidxHash
    {
        size_t operator()(const Pidx &i) const { return std::hash<size_t>{}(i.shell) ^ std::hash<size_t>{}(i.path); }
    };

    auto get_path = [&](Pidx i) { return connected_shells[i.shell][i.path]; };

    Point current_point{};
    bool any_point_found = false;
    std::vector<std::unordered_map<Pidx, std::unordered_set<Pidx, PidxHash>, PidxHash>> dependencies;
    for (size_t shell = 0; shell < connected_shells.size(); shell++) {
        dependencies.push_back({});
        auto &current_shell = dependencies[shell];
        for (size_t path = 0; path < connected_shells[shell].size(); path++) {
            Pidx                               current_path{shell, path};
            std::unordered_set<Pidx, PidxHash> current_dependencies{};
            if (shell > 0) {
                for (const auto &prev_path : dependencies[shell - 1]) {
                    if (paths_touch(get_path(current_path), get_path(prev_path.first), touch_distance)) {
                        current_dependencies.insert(prev_path.first);
                    };
                }
                current_shell[current_path] = current_dependencies;
                if (!any_point_found) {
                    current_point = get_path(current_path).first_point();
                    any_point_found = true;
                }
            }
        }
    }

    ExtrusionPaths sorted_paths{};
    Pidx           npidx         = Pidx{size_t(-1), 0};
    Pidx           next_pidx     = npidx;
    bool           reverse       = false;
    while (true) {
        if (next_pidx == npidx) { // find next pidx to print
            double dist = std::numeric_limits<double>::max();
            for (size_t shell = 0; shell < dependencies.size(); shell++) {
                for (const auto &p : dependencies[shell]) {
                    if (!p.second.empty()) continue;
                    const auto &path   = get_path(p.first);
                    double      dist_a = (path.first_point() - current_point).cast<double>().squaredNorm();
                    if (dist_a < dist) {
                        dist      = dist_a;
                        next_pidx = p.first;
                        reverse   = false;
                    }
                    double dist_b = (path.last_point() - current_point).cast<double>().squaredNorm();
                    if (dist_b < dist) {
                        dist      = dist_b;
                        next_pidx = p.first;
                        reverse   = true;
                    }
                }
            }
            if (next_pidx == npidx) { break; }
        } else { // we have valid next_pidx, add it to the sorted paths, update dependencies, update current point and potentialy set new next_pidx
            ExtrusionPath path = get_path(next_pidx);
            if (reverse) { path.reverse(); }
            sorted_paths.push_back(path);
            current_point = sorted_paths.back().last_point();
            if (next_pidx.shell < dependencies.size() - 1) {
                for (auto &p : dependencies[next_pidx.shell + 1]) { p.second.erase(next_pidx); }
            }
            dependencies[next_pidx.shell].erase(next_pidx);
            // check current and next shell for next pidx
            double dist          = std::numeric_limits<double>::max();
            size_t current_shell = next_pidx.shell;
            next_pidx            = npidx;
            for (size_t shell = current_shell; shell < std::min(current_shell + 2, dependencies.size()); shell++) {
                for (const auto &p : dependencies[shell]) {
                    if (!p.second.empty()) continue;
                    const ExtrusionPath &next_path   = get_path(p.first);
                    double      dist_a = (next_path.first_point() - current_point).cast<double>().squaredNorm();
                    if (dist_a < dist) {
                        dist      = dist_a;
                        next_pidx = p.first;
                        reverse   = false;
                    }
                    double dist_b = (next_path.last_point() - current_point).cast<double>().squaredNorm();
                    if (dist_b < dist) {
                        dist      = dist_b;
                        next_pidx = p.first;
                        reverse   = true;
                    }
                }
            }
            if (dist > scaled(5.0)){
                next_pidx = npidx;
            }
        }
    }

    ExtrusionPaths reconnected = reconnect_extrusion_paths(sorted_paths, touch_distance);
    ExtrusionPaths filtered;
    filtered.reserve(reconnected.size());
    for (ExtrusionPath &p : reconnected) {
        if (p.length() > touch_distance) { filtered.push_back(p); }
    }

    return filtered;
}

#define EXTRA_PERIMETER_OFFSET_PARAMETERS ClipperLib::jtSquare, 0.
// #define EXTRA_PERIM_DEBUG_FILES
// Function will generate extra perimeters clipped over nonbridgeable areas of the provided surface and returns both the new perimeters and
// Polygons filled by those clipped perimeters
std::tuple<std::vector<ExtrusionPaths>, Polygons> generate_extra_perimeters_over_overhangs(ExPolygons      infill_area,
                                                                                           const Polygons &lower_slices_polygons,
                                                                                           const Flow     &overhang_flow,
                                                                                           double          scaled_resolution,
                                                                                           const PrintObjectConfig &object_config,
                                                                                           const PrintConfig       &print_config)
{
    coord_t anchors_size = scale_(EXTERNAL_INFILL_MARGIN);

    Polygons anchors    = intersection(infill_area, lower_slices_polygons);
    Polygons overhangs  = diff(infill_area, lower_slices_polygons);
    if (overhangs.empty()) { return {}; }

    Polygons inset_anchors; // anchored area inset by the anchor length
    {
        std::vector<double> deltas{anchors_size * 0.15 + 0.5 * overhang_flow.scaled_spacing(),
                                   anchors_size * 0.33 + 0.5 * overhang_flow.scaled_spacing(),
                                   anchors_size * 0.66 + 0.5 * overhang_flow.scaled_spacing(), anchors_size * 1.00};

        std::vector<Polygons> anchor_areas_w_delta_anchor_size{};
        for (double delta : deltas) {
            anchor_areas_w_delta_anchor_size.push_back(diff(anchors, expand(overhangs, delta, EXTRA_PERIMETER_OFFSET_PARAMETERS)));
        }

        for (size_t i = 0; i < anchor_areas_w_delta_anchor_size.size() - 1; i++) {
            Polygons clipped = diff(anchor_areas_w_delta_anchor_size[i], expand(anchor_areas_w_delta_anchor_size[i + 1],
                                                                                        deltas[i + 1], EXTRA_PERIMETER_OFFSET_PARAMETERS));
            anchor_areas_w_delta_anchor_size[i] = intersection(anchor_areas_w_delta_anchor_size[i],
                                                                  expand(clipped, deltas[i+1] + 0.1*overhang_flow.scaled_spacing(),
                                                                            EXTRA_PERIMETER_OFFSET_PARAMETERS));
        }

        for (size_t i = 0; i < anchor_areas_w_delta_anchor_size.size(); i++) {
            inset_anchors = union_(inset_anchors,  anchor_areas_w_delta_anchor_size[i]);
        }

        inset_anchors = opening(inset_anchors, 0.8 * deltas[0], EXTRA_PERIMETER_OFFSET_PARAMETERS);
        inset_anchors = closing(inset_anchors, 0.8 * deltas[0], EXTRA_PERIMETER_OFFSET_PARAMETERS);

#ifdef EXTRA_PERIM_DEBUG_FILES
        {
            std::vector<std::string> colors = {"blue", "purple", "orange", "red"};
            BoundingBox              bbox   = get_extents(anchors);
            bbox.offset(scale_(1.));
            ::Slic3r::SVG svg(debug_out_path("anchored").c_str(), bbox);
            for (const Line &line : to_lines(inset_anchors)) svg.draw(line, "green", scale_(0.2));
            for (size_t i = 0; i < anchor_areas_w_delta_anchor_size.size(); i++) {
                for (const Line &line : to_lines(anchor_areas_w_delta_anchor_size[i])) svg.draw(line, colors[i], scale_(0.1));
            }
            svg.Close();
        }
#endif
    }

    Polygons inset_overhang_area = diff(infill_area, inset_anchors);

#ifdef EXTRA_PERIM_DEBUG_FILES
    {
        BoundingBox bbox = get_extents(inset_overhang_area);
        bbox.offset(scale_(1.));
        ::Slic3r::SVG svg(debug_out_path("inset_overhang_area").c_str(), bbox);
        for (const Line &line : to_lines(inset_anchors)) svg.draw(line, "purple", scale_(0.25));
        for (const Line &line : to_lines(inset_overhang_area)) svg.draw(line, "red", scale_(0.15));
        svg.Close();
    }
#endif

    Polygons inset_overhang_area_left_unfilled;

    std::vector<std::vector<ExtrusionPaths>> extra_perims; // overhang region -> shell -> shell parts
    for (const ExPolygon &overhang : union_ex(to_expolygons(inset_overhang_area))) {
        Polygons overhang_to_cover = to_polygons(overhang);
        Polygons expanded_overhang_to_cover = expand(overhang_to_cover, 1.1 * overhang_flow.scaled_spacing());
        Polygons shrinked_overhang_to_cover = shrink(overhang_to_cover, 0.1 * overhang_flow.scaled_spacing());

        Polygons real_overhang = intersection(overhang_to_cover, overhangs);
        if (real_overhang.empty()) {
            inset_overhang_area_left_unfilled.insert(inset_overhang_area_left_unfilled.end(), overhang_to_cover.begin(),
                                                     overhang_to_cover.end());
            continue;
        }

        extra_perims.emplace_back();
        std::vector<ExtrusionPaths> &overhang_region = extra_perims.back();

        Polygons anchoring         = intersection(expanded_overhang_to_cover, inset_anchors);
        Polygons perimeter_polygon = offset(union_(expand(overhang_to_cover, 0.1 * overhang_flow.scaled_spacing()), anchoring),
                                            -overhang_flow.scaled_spacing() * 0.6);

        Polygon anchoring_convex_hull = Geometry::convex_hull(anchoring);
        double  unbridgeable_area     = area(diff(real_overhang, {anchoring_convex_hull}));
        // penalize also holes
        for (const Polygon &poly : perimeter_polygon) {
            if (poly.is_clockwise()) { // hole, penalize bridges.
                unbridgeable_area += std::abs(area(poly));
            }
        }

        auto [dir, unsupp_dist] = detect_bridging_direction(real_overhang, anchors);

#ifdef EXTRA_PERIM_DEBUG_FILES
        {
            BoundingBox bbox = get_extents(anchoring_convex_hull);
            bbox.offset(scale_(1.));
            ::Slic3r::SVG svg(debug_out_path("bridge_check").c_str(), bbox);
            for (const Line &line : to_lines(perimeter_polygon)) svg.draw(line, "purple", scale_(0.25));
            for (const Line &line : to_lines(real_overhang)) svg.draw(line, "red", scale_(0.20));
            for (const Line &line : to_lines(anchoring_convex_hull)) svg.draw(line, "green", scale_(0.15));
            for (const Line &line : to_lines(anchoring)) svg.draw(line, "yellow", scale_(0.10));
            for (const Line &line : to_lines(diff_ex(perimeter_polygon, {anchoring_convex_hull}))) svg.draw(line, "black", scale_(0.10));
            svg.Close();
        }
#endif
        if (unbridgeable_area < 0.2 * area(real_overhang) && unsupp_dist < total_length(real_overhang) * 0.125) {
            inset_overhang_area_left_unfilled.insert(inset_overhang_area_left_unfilled.end(),overhang_to_cover.begin(),overhang_to_cover.end());
            perimeter_polygon.clear();
        } else {
            //  fill the overhang with perimeters
            int continuation_loops = 2;
            while (continuation_loops > 0) {
                auto prev = perimeter_polygon;
                // prepare next perimeter lines
                Polylines perimeter = intersection_pl(to_polylines(perimeter_polygon), shrinked_overhang_to_cover);

                // do not add the perimeter to result yet, first check if perimeter_polygon is not empty after shrinking - this would mean
                //  that the polygon was possibly too small for full perimeter loop and in that case try gap fill first
                perimeter_polygon = union_(perimeter_polygon, anchoring);
                perimeter_polygon = intersection(offset(perimeter_polygon, -overhang_flow.scaled_spacing()), expanded_overhang_to_cover);

                if (perimeter_polygon.empty()) { // fill possible gaps of single extrusion width
                    Polygons shrinked = offset(prev, -0.4 * overhang_flow.scaled_spacing());
                    if (!shrinked.empty()) {
                        overhang_region.emplace_back();
                        extrusion_paths_append(overhang_region.back(), perimeter, erOverhangPerimeter, overhang_flow.mm3_per_mm(),
                                               overhang_flow.width(), overhang_flow.height());
                    }

                    Polylines  fills;
                    ExPolygons gap = shrinked.empty() ? offset_ex(prev, overhang_flow.scaled_spacing() * 0.5) :
                                                        offset_ex(prev, -overhang_flow.scaled_spacing() * 0.5);

                    //gap = expolygons_simplify(gap, overhang_flow.scaled_spacing());
                    for (const ExPolygon &ep : gap) {
                        ep.medial_axis(overhang_flow.scaled_spacing() * 2.0, 0.3 * overhang_flow.scaled_width(), &fills);
                    }
                    if (!fills.empty()) {
                        fills = intersection_pl(fills, inset_overhang_area);
                        overhang_region.emplace_back();
                        extrusion_paths_append(overhang_region.back(), fills, erOverhangPerimeter, overhang_flow.mm3_per_mm(),
                                               overhang_flow.width(), overhang_flow.height());
                    }
                    break;
                } else {
                    overhang_region.emplace_back();
                    extrusion_paths_append(overhang_region.back(), perimeter, erOverhangPerimeter, overhang_flow.mm3_per_mm(),
                                           overhang_flow.width(), overhang_flow.height());
                }

                if (intersection(perimeter_polygon, real_overhang).empty()) { continuation_loops--; }

                if (prev == perimeter_polygon) {
#ifdef EXTRA_PERIM_DEBUG_FILES
                    BoundingBox bbox = get_extents(perimeter_polygon);
                    bbox.offset(scale_(5.));
                    ::Slic3r::SVG svg(debug_out_path("perimeter_polygon").c_str(), bbox);
                    for (const Line &line : to_lines(perimeter_polygon)) svg.draw(line, "blue", scale_(0.25));
                    for (const Line &line : to_lines(overhang_to_cover)) svg.draw(line, "red", scale_(0.20));
                    for (const Line &line : to_lines(real_overhang)) svg.draw(line, "green", scale_(0.15));
                    for (const Line &line : to_lines(anchoring)) svg.draw(line, "yellow", scale_(0.10));
                    svg.Close();
#endif
                    break;
                }
            }
            Polylines perimeter = intersection_pl(to_polylines(perimeter_polygon), shrinked_overhang_to_cover);
            overhang_region.emplace_back();
            extrusion_paths_append(overhang_region.back(), perimeter, erOverhangPerimeter, overhang_flow.mm3_per_mm(),
                                   overhang_flow.width(), overhang_flow.height());

            perimeter_polygon = expand(perimeter_polygon, 0.5 * overhang_flow.scaled_spacing());
            perimeter_polygon = union_(perimeter_polygon, anchoring);
            inset_overhang_area_left_unfilled.insert(inset_overhang_area_left_unfilled.end(), perimeter_polygon.begin(),perimeter_polygon.end());

#ifdef EXTRA_PERIM_DEBUG_FILES
            BoundingBox bbox = get_extents(inset_overhang_area);
            bbox.offset(scale_(2.));
            ::Slic3r::SVG svg(debug_out_path("pre_final").c_str(), bbox);
            for (const Line &line : to_lines(perimeter_polygon)) svg.draw(line, "blue", scale_(0.05));
            for (const Line &line : to_lines(anchoring)) svg.draw(line, "green", scale_(0.05));
            for (const Line &line : to_lines(overhang_to_cover)) svg.draw(line, "yellow", scale_(0.05));
            for (const Line &line : to_lines(inset_overhang_area_left_unfilled)) svg.draw(line, "red", scale_(0.05));
            svg.Close();
#endif

            std::reverse(overhang_region.begin(), overhang_region.end()); // reverse the order, It shall be printed from inside out
        }
    }

    std::vector<ExtrusionPaths> result{};
    for (const std::vector<ExtrusionPaths> &paths : extra_perims) {
        result.push_back(sort_and_connect_extra_perimeters(paths, 2.0 * overhang_flow.scaled_spacing()));
    }

#ifdef EXTRA_PERIM_DEBUG_FILES
    BoundingBox bbox = get_extents(inset_overhang_area);
    bbox.offset(scale_(2.));
    ::Slic3r::SVG svg(debug_out_path(("final" + std::to_string(rand())).c_str()).c_str(), bbox);
    for (const Line &line : to_lines(inset_overhang_area_left_unfilled)) svg.draw(line, "blue", scale_(0.05));
    for (const Line &line : to_lines(inset_overhang_area)) svg.draw(line, "green", scale_(0.05));
    for (const Line &line : to_lines(diff(inset_overhang_area, inset_overhang_area_left_unfilled))) svg.draw(line, "yellow", scale_(0.05));
    svg.Close();
#endif

    inset_overhang_area_left_unfilled = union_(inset_overhang_area_left_unfilled);

    return {result, diff(inset_overhang_area, inset_overhang_area_left_unfilled)};
}

// Thanks, Cura developers, for implementing an algorithm for generating perimeters with variable width (Arachne) that is based on the paper
// "A framework for adaptive width control of dense contour-parallel toolpaths in fused deposition modeling"
void PerimeterGenerator::process_arachne(
    // Inputs:
    const Parameters           &params,
    const Surface              &surface,
    const ExPolygons           *lower_slices,
    // Cache:
    Polygons                   &lower_slices_polygons_cache,
    // Output:
    // Loops with the external thin walls
    ExtrusionEntityCollection  &out_loops,
    // Gaps without the thin walls
    ExtrusionEntityCollection  & /* out_gap_fill */,
    // Infills without the gap fills
    ExPolygons                 &out_fill_expolygons)
{
    // other perimeters
    coord_t perimeter_spacing     = params.perimeter_flow.scaled_spacing();
    // external perimeters
    coord_t ext_perimeter_width    = params.ext_perimeter_flow.scaled_width();
    coord_t ext_perimeter_spacing  = params.ext_perimeter_flow.scaled_spacing();
    coord_t ext_perimeter_spacing2 = scaled<coord_t>(0.5f * (params.ext_perimeter_flow.spacing() + params.perimeter_flow.spacing()));
    // solid infill
    coord_t solid_infill_spacing  = params.solid_infill_flow.scaled_spacing();

    // prepare grown lower layer slices for overhang detection
    if (params.config.overhangs && lower_slices != nullptr && lower_slices_polygons_cache.empty()) {
        // We consider overhang any part where the entire nozzle diameter is not supported by the
        // lower layer, so we take lower slices and offset them by half the nozzle diameter used
        // in the current layer
        double nozzle_diameter = params.print_config.nozzle_diameter.get_at(params.config.perimeter_extruder-1);
        lower_slices_polygons_cache = offset(*lower_slices, float(scale_(+nozzle_diameter/2)));
    }

    // we need to process each island separately because we might have different
    // extra perimeters for each one
    // detect how many perimeters must be generated for this island
    int        loop_number = params.config.perimeters + surface.extra_perimeters - 1; // 0-indexed loops
    ExPolygons last        = offset_ex(surface.expolygon.simplify_p(params.scaled_resolution), - float(ext_perimeter_width / 2. - ext_perimeter_spacing / 2.));
    Polygons   last_p      = to_polygons(last);

    Arachne::WallToolPaths wallToolPaths(last_p, ext_perimeter_spacing, perimeter_spacing, coord_t(loop_number + 1), 0, params.layer_height, params.object_config, params.print_config);
    std::vector<Arachne::VariableWidthLines> perimeters = wallToolPaths.getToolPaths();
    loop_number = int(perimeters.size()) - 1;

#ifdef ARACHNE_DEBUG
    {
        static int iRun = 0;
        export_perimeters_to_svg(debug_out_path("arachne-perimeters-%d-%d.svg", layer_id, iRun++), to_polygons(last), perimeters, union_ex(wallToolPaths.getInnerContour()));
    }
#endif

    // All closed ExtrusionLine should have the same the first and the last point.
    // But in rare cases, Arachne produce ExtrusionLine marked as closed but without
    // equal the first and the last point.
    assert([&perimeters = std::as_const(perimeters)]() -> bool {
        for (const Arachne::VariableWidthLines &perimeter : perimeters)
            for (const Arachne::ExtrusionLine &el : perimeter)
                if (el.is_closed && el.junctions.front().p != el.junctions.back().p)
                    return false;
        return true;
    }());

    int start_perimeter = int(perimeters.size()) - 1;
    int end_perimeter   = -1;
    int direction       = -1;

    if (params.config.external_perimeters_first) {
        start_perimeter = 0;
        end_perimeter   = int(perimeters.size());
        direction       = 1;
    }

    std::vector<Arachne::ExtrusionLine *> all_extrusions;
    for (int perimeter_idx = start_perimeter; perimeter_idx != end_perimeter; perimeter_idx += direction) {
        if (perimeters[perimeter_idx].empty())
            continue;
        for (Arachne::ExtrusionLine &wall : perimeters[perimeter_idx])
            all_extrusions.emplace_back(&wall);
    }

    // Find topological order with constraints from extrusions_constrains.
    std::vector<size_t>              blocked(all_extrusions.size(), 0); // Value indicating how many extrusions it is blocking (preceding extrusions) an extrusion.
    std::vector<std::vector<size_t>> blocking(all_extrusions.size());   // Each extrusion contains a vector of extrusions that are blocked by this extrusion.
    std::unordered_map<const Arachne::ExtrusionLine *, size_t> map_extrusion_to_idx;
    for (size_t idx = 0; idx < all_extrusions.size(); idx++)
        map_extrusion_to_idx.emplace(all_extrusions[idx], idx);

    auto extrusions_constrains = Arachne::WallToolPaths::getRegionOrder(all_extrusions, params.config.external_perimeters_first);
    for (auto [before, after] : extrusions_constrains) {
        auto after_it = map_extrusion_to_idx.find(after);
        ++blocked[after_it->second];
        blocking[map_extrusion_to_idx.find(before)->second].emplace_back(after_it->second);
    }

    std::vector<bool> processed(all_extrusions.size(), false);          // Indicate that the extrusion was already processed.
    Point             current_position = all_extrusions.empty() ? Point::Zero() : all_extrusions.front()->junctions.front().p; // Some starting position.
    std::vector<PerimeterGeneratorArachneExtrusion> ordered_extrusions;         // To store our result in. At the end we'll std::swap.
    ordered_extrusions.reserve(all_extrusions.size());

    while (ordered_extrusions.size() < all_extrusions.size()) {
        size_t best_candidate    = 0;
        double best_distance_sqr = std::numeric_limits<double>::max();
        bool   is_best_closed    = false;

        std::vector<size_t> available_candidates;
        for (size_t candidate = 0; candidate < all_extrusions.size(); ++candidate) {
            if (processed[candidate] || blocked[candidate])
                continue; // Not a valid candidate.
            available_candidates.push_back(candidate);
        }

        std::sort(available_candidates.begin(), available_candidates.end(), [&all_extrusions](const size_t a_idx, const size_t b_idx) -> bool {
            return all_extrusions[a_idx]->is_closed < all_extrusions[b_idx]->is_closed;
        });

        for (const size_t candidate_path_idx : available_candidates) {
            auto& path = all_extrusions[candidate_path_idx];

            if (path->junctions.empty()) { // No vertices in the path. Can't find the start position then or really plan it in. Put that at the end.
                if (best_distance_sqr == std::numeric_limits<double>::max()) {
                    best_candidate = candidate_path_idx;
                    is_best_closed = path->is_closed;
                }
                continue;
            }

            const Point candidate_position = path->junctions.front().p;
            double      distance_sqr       = (current_position - candidate_position).cast<double>().norm();
            if (distance_sqr < best_distance_sqr) { // Closer than the best candidate so far.
                if (path->is_closed || (!path->is_closed && best_distance_sqr != std::numeric_limits<double>::max()) || (!path->is_closed && !is_best_closed)) {
                    best_candidate    = candidate_path_idx;
                    best_distance_sqr = distance_sqr;
                    is_best_closed    = path->is_closed;
                }
            }
        }

        auto &best_path = all_extrusions[best_candidate];
        ordered_extrusions.push_back({best_path, best_path->is_contour(), false});
        processed[best_candidate] = true;
        for (size_t unlocked_idx : blocking[best_candidate])
            blocked[unlocked_idx]--;

        if (!best_path->junctions.empty()) { //If all paths were empty, the best path is still empty. We don't upate the current position then.
            if(best_path->is_closed)
                current_position = best_path->junctions[0].p; //We end where we started.
            else
                current_position = best_path->junctions.back().p; //Pick the other end from where we started.
        }
    }

    if (params.layer_id > 0 && params.config.fuzzy_skin != FuzzySkinType::None) {
        std::vector<PerimeterGeneratorArachneExtrusion *> closed_loop_extrusions;
        for (PerimeterGeneratorArachneExtrusion &extrusion : ordered_extrusions)
            if (extrusion.extrusion->inset_idx == 0) {
                if (extrusion.extrusion->is_closed && params.config.fuzzy_skin == FuzzySkinType::External) {
                    closed_loop_extrusions.emplace_back(&extrusion);
                } else {
                    extrusion.fuzzify = true;
                }
            }

        if (params.config.fuzzy_skin == FuzzySkinType::External) {
            ClipperLib_Z::Paths loops_paths;
            loops_paths.reserve(closed_loop_extrusions.size());
            for (const auto &cl_extrusion : closed_loop_extrusions) {
                assert(cl_extrusion->extrusion->junctions.front() == cl_extrusion->extrusion->junctions.back());
                size_t             loop_idx = &cl_extrusion - &closed_loop_extrusions.front();
                ClipperLib_Z::Path loop_path;
                loop_path.reserve(cl_extrusion->extrusion->junctions.size() - 1);
                for (auto junction_it = cl_extrusion->extrusion->junctions.begin(); junction_it != std::prev(cl_extrusion->extrusion->junctions.end()); ++junction_it)
                    loop_path.emplace_back(junction_it->p.x(), junction_it->p.y(), loop_idx);
                loops_paths.emplace_back(loop_path);
            }

            ClipperLib_Z::Clipper clipper;
            clipper.AddPaths(loops_paths, ClipperLib_Z::ptSubject, true);
            ClipperLib_Z::PolyTree loops_polytree;
            clipper.Execute(ClipperLib_Z::ctUnion, loops_polytree, ClipperLib_Z::pftEvenOdd, ClipperLib_Z::pftEvenOdd);

            for (const ClipperLib_Z::PolyNode *child_node : loops_polytree.Childs) {
                // The whole contour must have the same index.
                coord_t polygon_idx  = child_node->Contour.front().z();
                bool    has_same_idx = std::all_of(child_node->Contour.begin(), child_node->Contour.end(),
                                                   [&polygon_idx](const ClipperLib_Z::IntPoint &point) -> bool { return polygon_idx == point.z(); });
                if (has_same_idx)
                    closed_loop_extrusions[polygon_idx]->fuzzify = true;
            }
        }
    }

    if (ExtrusionEntityCollection extrusion_coll = traverse_extrusions(params, lower_slices_polygons_cache, ordered_extrusions); !extrusion_coll.empty())
        out_loops.append(extrusion_coll);

    ExPolygons    infill_contour = union_ex(wallToolPaths.getInnerContour());
    const coord_t spacing        = (perimeters.size() == 1) ? ext_perimeter_spacing2 : perimeter_spacing;
    if (offset_ex(infill_contour, -float(spacing / 2.)).empty())
        infill_contour.clear(); // Infill region is too small, so let's filter it out.

    // create one more offset to be used as boundary for fill
    // we offset by half the perimeter spacing (to get to the actual infill boundary)
    // and then we offset back and forth by half the infill spacing to only consider the
    // non-collapsing regions
    coord_t inset =
        (loop_number < 0) ? 0 :
        (loop_number == 0) ?
                            // one loop
            ext_perimeter_spacing:
            // two or more loops?
            perimeter_spacing;

    inset = coord_t(scale_(params.config.get_abs_value("infill_overlap", unscale<double>(inset))));
    Polygons pp;
    for (ExPolygon &ex : infill_contour)
        ex.simplify_p(params.scaled_resolution, &pp);
    // collapse too narrow infill areas
    const auto    min_perimeter_infill_spacing = coord_t(solid_infill_spacing * (1. - INSET_OVERLAP_TOLERANCE));
    // append infill areas to fill_surfaces
    ExPolygons infill_areas =
        offset2_ex(
            union_ex(pp),
            float(- min_perimeter_infill_spacing / 2.),
            float(inset + min_perimeter_infill_spacing / 2.));

    if (lower_slices != nullptr && params.config.overhangs && params.config.extra_perimeters_on_overhangs &&
        params.config.perimeters > 0 && params.layer_id > params.object_config.raft_layers) {
        // Generate extra perimeters on overhang areas, and cut them to these parts only, to save print time and material
        auto [extra_perimeters, filled_area] = generate_extra_perimeters_over_overhangs(infill_areas,
                                                                                        lower_slices_polygons_cache,
                                                                                        params.overhang_flow, params.scaled_resolution,
                                                                                        params.object_config, params.print_config);
        if (!extra_perimeters.empty()) {
            ExtrusionEntityCollection &this_islands_perimeters = static_cast<ExtrusionEntityCollection&>(*out_loops.entities.back());
            ExtrusionEntitiesPtr       old_entities;
            old_entities.swap(this_islands_perimeters.entities);
            for (ExtrusionPaths &paths : extra_perimeters) 
                this_islands_perimeters.append(std::move(paths));
            append(this_islands_perimeters.entities, old_entities);
            infill_areas = diff_ex(infill_areas, filled_area);
        }
    }
    
    append(out_fill_expolygons, std::move(infill_areas));
}

void PerimeterGenerator::process_classic(
    // Inputs:
    const Parameters           &params,
    const Surface              &surface,
    const ExPolygons           *lower_slices,
    // Cache:
    Polygons                   &lower_slices_polygons_cache,
    // Output:
    // Loops with the external thin walls
    ExtrusionEntityCollection  &out_loops,
    // Gaps without the thin walls
    ExtrusionEntityCollection  &out_gap_fill,
    // Infills without the gap fills
    ExPolygons                 &out_fill_expolygons)
{
    // other perimeters
    coord_t perimeter_width         = params.perimeter_flow.scaled_width();
    coord_t perimeter_spacing       = params.perimeter_flow.scaled_spacing();
    // external perimeters
    coord_t ext_perimeter_width     = params.ext_perimeter_flow.scaled_width();
    coord_t ext_perimeter_spacing   = params.ext_perimeter_flow.scaled_spacing();
    coord_t ext_perimeter_spacing2  = scaled<coord_t>(0.5f * (params.ext_perimeter_flow.spacing() + params.perimeter_flow.spacing()));
    // solid infill
    coord_t solid_infill_spacing    = params.solid_infill_flow.scaled_spacing();
    
    // Calculate the minimum required spacing between two adjacent traces.
    // This should be equal to the nominal flow spacing but we experiment
    // with some tolerance in order to avoid triggering medial axis when
    // some squishing might work. Loops are still spaced by the entire
    // flow spacing; this only applies to collapsing parts.
    // For ext_min_spacing we use the ext_perimeter_spacing calculated for two adjacent
    // external loops (which is the correct way) instead of using ext_perimeter_spacing2
    // which is the spacing between external and internal, which is not correct
    // and would make the collapsing (thus the details resolution) dependent on 
    // internal flow which is unrelated.
    coord_t min_spacing         = coord_t(perimeter_spacing      * (1 - INSET_OVERLAP_TOLERANCE));
    coord_t ext_min_spacing     = coord_t(ext_perimeter_spacing  * (1 - INSET_OVERLAP_TOLERANCE));
    bool    has_gap_fill 		= params.config.gap_fill_enabled.value && params.config.gap_fill_speed.value > 0;

    // prepare grown lower layer slices for overhang detection
    if (params.config.overhangs && lower_slices != nullptr && lower_slices_polygons_cache.empty()) {
        // We consider overhang any part where the entire nozzle diameter is not supported by the
        // lower layer, so we take lower slices and offset them by half the nozzle diameter used 
        // in the current layer
        double nozzle_diameter = params.print_config.nozzle_diameter.get_at(params.config.perimeter_extruder-1);
        lower_slices_polygons_cache = offset(*lower_slices, float(scale_(+nozzle_diameter/2)));
    }

    // we need to process each island separately because we might have different
    // extra perimeters for each one
    // detect how many perimeters must be generated for this island
    int        loop_number = params.config.perimeters + surface.extra_perimeters - 1;  // 0-indexed loops
    ExPolygons last        = union_ex(surface.expolygon.simplify_p(params.scaled_resolution));
    ExPolygons gaps;
    if (loop_number >= 0) {
        // In case no perimeters are to be generated, loop_number will equal to -1.
        std::vector<PerimeterGeneratorLoops> contours(loop_number+1);    // depth => loops
        std::vector<PerimeterGeneratorLoops> holes(loop_number+1);       // depth => loops
        ThickPolylines thin_walls;
        // we loop one time more than needed in order to find gaps after the last perimeter was applied
        for (int i = 0;; ++ i) {  // outer loop is 0
            // Calculate next onion shell of perimeters.
            ExPolygons offsets;
            if (i == 0) {
                // the minimum thickness of a single loop is:
                // ext_width/2 + ext_spacing/2 + spacing/2 + width/2
                offsets = params.config.thin_walls ? 
                    offset2_ex(
                        last,
                        - float(ext_perimeter_width / 2. + ext_min_spacing / 2. - 1),
                        + float(ext_min_spacing / 2. - 1)) :
                    offset_ex(last, - float(ext_perimeter_width / 2.));
                // look for thin walls
                if (params.config.thin_walls) {
                    // the following offset2 ensures almost nothing in @thin_walls is narrower than $min_width
                    // (actually, something larger than that still may exist due to mitering or other causes)
                    coord_t min_width = coord_t(scale_(params.ext_perimeter_flow.nozzle_diameter() / 3));
                    ExPolygons expp = opening_ex(
                        // medial axis requires non-overlapping geometry
                        diff_ex(last, offset(offsets, float(ext_perimeter_width / 2.) + ClipperSafetyOffset)),
                        float(min_width / 2.));
                    // the maximum thickness of our thin wall area is equal to the minimum thickness of a single loop
                    for (ExPolygon &ex : expp)
                        ex.medial_axis(min_width, ext_perimeter_width + ext_perimeter_spacing2, &thin_walls);
                }
                if (params.spiral_vase && offsets.size() > 1) {
                	// Remove all but the largest area polygon.
                	keep_largest_contour_only(offsets);
                }
            } else {
                //FIXME Is this offset correct if the line width of the inner perimeters differs
                // from the line width of the infill?
                coord_t distance = (i == 1) ? ext_perimeter_spacing2 : perimeter_spacing;
                offsets = params.config.thin_walls ?
                    // This path will ensure, that the perimeters do not overfill, as in 
                    // prusa3d/Slic3r GH #32, but with the cost of rounding the perimeters
                    // excessively, creating gaps, which then need to be filled in by the not very 
                    // reliable gap fill algorithm.
                    // Also the offset2(perimeter, -x, x) may sometimes lead to a perimeter, which is larger than
                    // the original.
                    offset2_ex(last,
                            - float(distance + min_spacing / 2. - 1.),
                            float(min_spacing / 2. - 1.)) :
                    // If "detect thin walls" is not enabled, this paths will be entered, which 
                    // leads to overflows, as in prusa3d/Slic3r GH #32
                    offset_ex(last, - float(distance));
                // look for gaps
                if (has_gap_fill)
                    // not using safety offset here would "detect" very narrow gaps
                    // (but still long enough to escape the area threshold) that gap fill
                    // won't be able to fill but we'd still remove from infill area
                    append(gaps, diff_ex(
                        offset(last,    - float(0.5 * distance)),
                        offset(offsets,   float(0.5 * distance + 10))));  // safety offset
            }
            if (offsets.empty()) {
                // Store the number of loops actually generated.
                loop_number = i - 1;
                // No region left to be filled in.
                last.clear();
                break;
            } else if (i > loop_number) {
                // If i > loop_number, we were looking just for gaps.
                break;
            }
            {
                const bool fuzzify_contours = params.config.fuzzy_skin != FuzzySkinType::None && i == 0 && params.layer_id > 0;
                const bool fuzzify_holes    = fuzzify_contours && params.config.fuzzy_skin == FuzzySkinType::All;
                for (const ExPolygon &expolygon : offsets) {
	                // Outer contour may overlap with an inner contour,
	                // inner contour may overlap with another inner contour,
	                // outer contour may overlap with itself.
	                //FIXME evaluate the overlaps, annotate each point with an overlap depth,
                    // compensate for the depth of intersection.
                    contours[i].emplace_back(expolygon.contour, i, true, fuzzify_contours);

                    if (! expolygon.holes.empty()) {
                        holes[i].reserve(holes[i].size() + expolygon.holes.size());
                        for (const Polygon &hole : expolygon.holes)
                            holes[i].emplace_back(hole, i, false, fuzzify_holes);
                    }
                }
            }
            last = std::move(offsets);
            if (i == loop_number && (! has_gap_fill || params.config.fill_density.value == 0)) {
            	// The last run of this loop is executed to collect gaps for gap fill.
            	// As the gap fill is either disabled or not 
            	break;
            }
        }

        // nest loops: holes first
        for (int d = 0; d <= loop_number; ++ d) {
            PerimeterGeneratorLoops &holes_d = holes[d];
            // loop through all holes having depth == d
            for (int i = 0; i < (int)holes_d.size(); ++ i) {
                const PerimeterGeneratorLoop &loop = holes_d[i];
                // find the hole loop that contains this one, if any
                for (int t = d + 1; t <= loop_number; ++ t) {
                    for (int j = 0; j < (int)holes[t].size(); ++ j) {
                        PerimeterGeneratorLoop &candidate_parent = holes[t][j];
                        if (candidate_parent.polygon.contains(loop.polygon.first_point())) {
                            candidate_parent.children.push_back(loop);
                            holes_d.erase(holes_d.begin() + i);
                            -- i;
                            goto NEXT_LOOP;
                        }
                    }
                }
                // if no hole contains this hole, find the contour loop that contains it
                for (int t = loop_number; t >= 0; -- t) {
                    for (int j = 0; j < (int)contours[t].size(); ++ j) {
                        PerimeterGeneratorLoop &candidate_parent = contours[t][j];
                        if (candidate_parent.polygon.contains(loop.polygon.first_point())) {
                            candidate_parent.children.push_back(loop);
                            holes_d.erase(holes_d.begin() + i);
                            -- i;
                            goto NEXT_LOOP;
                        }
                    }
                }
                NEXT_LOOP: ;
            }
        }
        // nest contour loops
        for (int d = loop_number; d >= 1; -- d) {
            PerimeterGeneratorLoops &contours_d = contours[d];
            // loop through all contours having depth == d
            for (int i = 0; i < (int)contours_d.size(); ++ i) {
                const PerimeterGeneratorLoop &loop = contours_d[i];
                // find the contour loop that contains it
                for (int t = d - 1; t >= 0; -- t) {
                    for (size_t j = 0; j < contours[t].size(); ++ j) {
                        PerimeterGeneratorLoop &candidate_parent = contours[t][j];
                        if (candidate_parent.polygon.contains(loop.polygon.first_point())) {
                            candidate_parent.children.push_back(loop);
                            contours_d.erase(contours_d.begin() + i);
                            -- i;
                            goto NEXT_CONTOUR;
                        }
                    }
                }
                NEXT_CONTOUR: ;
            }
        }
        // at this point, all loops should be in contours[0]
        ExtrusionEntityCollection entities = traverse_loops_classic(params, lower_slices_polygons_cache, contours.front(), thin_walls);
        // if brim will be printed, reverse the order of perimeters so that
        // we continue inwards after having finished the brim
        // TODO: add test for perimeter order
        if (params.config.external_perimeters_first || 
            (params.layer_id == 0 && params.object_config.brim_width.value > 0))
            entities.reverse();
        // append perimeters for this slice as a collection
        if (! entities.empty())
            out_loops.append(entities);
    } // for each loop of an island

    // fill gaps
    if (! gaps.empty()) {
        // collapse 
        double min = 0.2 * perimeter_width * (1 - INSET_OVERLAP_TOLERANCE);
        double max = 2. * perimeter_spacing;
        ExPolygons gaps_ex = diff_ex(
            //FIXME offset2 would be enough and cheaper.
            opening_ex(gaps, float(min / 2.)),
            offset2_ex(gaps, - float(max / 2.), float(max / 2. + ClipperSafetyOffset)));
        ThickPolylines polylines;
        for (const ExPolygon &ex : gaps_ex)
            ex.medial_axis(min, max, &polylines);
        if (! polylines.empty()) {
			ExtrusionEntityCollection gap_fill;
			variable_width_classic(polylines, erGapFill, params.solid_infill_flow, gap_fill.entities);
            /*  Make sure we don't infill narrow parts that are already gap-filled
                (we only consider this surface's gaps to reduce the diff() complexity).
                Growing actual extrusions ensures that gaps not filled by medial axis
                are not subtracted from fill surfaces (they might be too short gaps
                that medial axis skips but infill might join with other infill regions
                and use zigzag).  */
            //FIXME Vojtech: This grows by a rounded extrusion width, not by line spacing,
            // therefore it may cover the area, but no the volume.
            last = diff_ex(last, gap_fill.polygons_covered_by_width(10.f));
			out_gap_fill.append(std::move(gap_fill.entities));
		}
    }

    // create one more offset to be used as boundary for fill
    // we offset by half the perimeter spacing (to get to the actual infill boundary)
    // and then we offset back and forth by half the infill spacing to only consider the
    // non-collapsing regions
    coord_t inset = 
        (loop_number < 0) ? 0 :
        (loop_number == 0) ?
            // one loop
            ext_perimeter_spacing / 2 :
            // two or more loops?
            perimeter_spacing / 2;
    // only apply infill overlap if we actually have one perimeter
    if (inset > 0)
        inset -= coord_t(scale_(params.config.get_abs_value("infill_overlap", unscale<double>(inset + solid_infill_spacing / 2))));
    // simplify infill contours according to resolution
    Polygons pp;
    for (ExPolygon &ex : last)
        ex.simplify_p(params.scaled_resolution, &pp);
    // collapse too narrow infill areas
    coord_t min_perimeter_infill_spacing = coord_t(solid_infill_spacing * (1. - INSET_OVERLAP_TOLERANCE));
    // append infill areas to fill_surfaces
    ExPolygons infill_areas =
        offset2_ex(
            union_ex(pp),
            float(- inset - min_perimeter_infill_spacing / 2.),
            float(min_perimeter_infill_spacing / 2.));

    if (lower_slices != nullptr && params.config.overhangs && params.config.extra_perimeters_on_overhangs &&
        params.config.perimeters > 0 && params.layer_id > params.object_config.raft_layers) {
        // Generate extra perimeters on overhang areas, and cut them to these parts only, to save print time and material
        auto [extra_perimeters, filled_area] = generate_extra_perimeters_over_overhangs(infill_areas,
                                                                                        lower_slices_polygons_cache,
                                                                                        params.overhang_flow, params.scaled_resolution,
                                                                                        params.object_config, params.print_config);
        if (!extra_perimeters.empty()) {
            ExtrusionEntityCollection &this_islands_perimeters = static_cast<ExtrusionEntityCollection&>(*out_loops.entities.back());
            ExtrusionEntitiesPtr       old_entities;
            old_entities.swap(this_islands_perimeters.entities);
            for (ExtrusionPaths &paths : extra_perimeters) 
                this_islands_perimeters.append(std::move(paths));
            append(this_islands_perimeters.entities, old_entities);
            infill_areas = diff_ex(infill_areas, filled_area);
        }
    }
    
    append(out_fill_expolygons, std::move(infill_areas));
}

}
