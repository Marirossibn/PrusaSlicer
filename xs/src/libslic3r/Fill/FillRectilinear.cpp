#include "../ClipperUtils.hpp"
#include "../ExPolygon.hpp"
#include "../PolylineCollection.hpp"
#include "../Surface.hpp"

#include "FillRectilinear.hpp"

namespace Slic3r {

Polylines FillRectilinear::fill_surface(const Surface *surface, const FillParams &params)
{
    // rotate polygons so that we can work with vertical lines here
    ExPolygon expolygon = surface->expolygon;
    std::pair<float, Point> rotate_vector = this->infill_direction(surface);
    expolygon.rotate(- rotate_vector.first);
    // No need to translate the polygon anyhow for the infill.
    // The infill will be performed inside a bounding box of the expolygon and its absolute position does not matter.
//    expolygon.translate(rotate_vector.second.x, rotate_vector.second.y);

    this->_min_spacing = scale_(this->spacing);
    assert(params.density > 0.0001f && params.density <= 1.f);
    this->_line_spacing = coord_t(coordf_t(this->_min_spacing) / params.density);
    this->_diagonal_distance = this->_line_spacing * 2;
    this->_line_oscillation = this->_line_spacing - this->_min_spacing; // only for Line infill
    BoundingBox bounding_box = expolygon.contour.bounding_box();
    
    // define flow spacing according to requested density
    if (params.density > 0.9999f && !params.dont_adjust) {
        this->_line_spacing = this->adjust_solid_spacing(bounding_box.size().x, this->_line_spacing);
        this->spacing = unscale(this->_line_spacing);
    } else {
        // extend bounding box so that our pattern will be aligned with other layers
        bounding_box.merge(Point(
            bounding_box.min.x - (bounding_box.min.x % this->_line_spacing),
            bounding_box.min.y - (bounding_box.min.y % this->_line_spacing)));
    }
    
    // generate the basic pattern
    coord_t x_max = bounding_box.max.x + SCALED_EPSILON;
    Lines lines;
    for (coord_t x = bounding_box.min.x; x <= x_max; x += this->_line_spacing)
        lines.push_back(this->_line(lines.size(), x, bounding_box.min.y, bounding_box.max.y));
    if (this->_horizontal_lines()) {
        coord_t y_max = bounding_box.max.y + SCALED_EPSILON;
        for (coord_t y = bounding_box.min.y; y <= y_max; y += this->_line_spacing)
            lines.push_back(Line(Point(bounding_box.min.x, y), Point(bounding_box.max.x, y)));
    }

    // clip paths against a slightly larger expolygon, so that the first and last paths
    // are kept even if the expolygon has vertical sides
    // the minimum offset for preventing edge lines from being clipped is SCALED_EPSILON;
    // however we use a larger offset to support expolygons with slightly skewed sides and 
    // not perfectly straight
    //FIXME Vojtech: Update the intersecton function to work directly with lines.
    Polylines polylines_src;
    polylines_src.reserve(lines.size());
    for (Lines::const_iterator it = lines.begin(); it != lines.end(); ++ it) {
        polylines_src.push_back(Polyline());
        Points &pts = polylines_src.back().points;
        pts.reserve(2);
        pts.push_back(it->a);
        pts.push_back(it->b);
    }
    Polylines polylines = intersection(polylines_src, offset((Polygons)expolygon, scale_(0.02)), false);

    // FIXME Vojtech: This is only performed for horizontal lines, not for the vertical lines!
    const float INFILL_OVERLAP_OVER_SPACING = 0.3f;
    coord_t extra = coord_t(floor(this->_min_spacing * INFILL_OVERLAP_OVER_SPACING + 0.5f));
    for (Polylines::iterator it_polyline = polylines.begin(); it_polyline != polylines.end(); ++ it_polyline) {
        Point *first_point = &it_polyline->points.front();
        Point *last_point  = &it_polyline->points.back();
        if (first_point->y > last_point->y)
            std::swap(first_point, last_point);
        first_point->y -= extra;
        last_point->y += extra;
    }

    // connect lines
    if (! params.dont_connect && ! polylines.empty()) { // prevent calling leftmost_point() on empty collections
        // offset the expolygon by max(min_spacing/2, extra)
        ExPolygon expolygon_off;
        {
            ExPolygons expolygons_off = offset_ex(expolygon, this->_min_spacing/2);
            if (! expolygons_off.empty()) {
                // When expanding a polygon, the number of islands could only shrink. Therefore the offset_ex shall generate exactly one expanded island for one input island.
                assert(expolygons_off.size() == 1);
                std::swap(expolygon_off, expolygons_off.front());
            }
        }
        Polylines chained = PolylineCollection::chained_path_from(
#if SLIC3R_CPPVER >= 11
            std::move(polylines), 
#else
            polylines,
#endif
            PolylineCollection::leftmost_point(polylines), false); // reverse allowed
#if SLIC3R_CPPVER >= 11
            assert(polylines.empty());
#else
            polylines.clear();
#endif
        for (Polylines::iterator it_polyline = chained.begin(); it_polyline != chained.end(); ++ it_polyline) {
            if (! polylines.empty()) {
                // Try to connect the lines.
                Points &pts_end = polylines.back().points;
                const Point &first_point = it_polyline->points.front();
                const Point &last_point = pts_end.back();
                // Distance in X, Y.
                const Vector distance = first_point.vector_to(last_point);
                // TODO: we should also check that both points are on a fill_boundary to avoid 
                // connecting paths on the boundaries of internal regions
                if (this->_can_connect(std::abs(distance.x), std::abs(distance.y)) && 
                    expolygon_off.contains(Line(last_point, first_point))) {
                    // Append the polyline.
                    pts_end.insert(pts_end.end(), it_polyline->points.begin(), it_polyline->points.end());
                    continue;
                }
            }
            // The lines cannot be connected.
#if SLIC3R_CPPVER >= 11
            polylines.push_back(std::move(*it_polyline));
#else
            polylines.push_back(Polyline());
            std::swap(polylines.back(), *it_polyline);
#endif
        }
    }

    // paths must be rotated back
    for (Polylines::iterator it = polylines.begin(); it != polylines.end(); ++ it) {
        // No need to translate, the absolute position is irrelevant.
        // it->translate(- rotate_vector.second.x, - rotate_vector.second.y);
        it->rotate(rotate_vector.first);
    }
    return polylines;
}

} // namespace Slic3r
