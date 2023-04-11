#ifndef slic3r_ExtrusionProcessor_hpp_
#define slic3r_ExtrusionProcessor_hpp_

#include "../AABBTreeLines.hpp"
#include "../SupportSpotsGenerator.hpp"
#include "../libslic3r.h"
#include "../ExtrusionEntity.hpp"
#include "../Layer.hpp"
#include "../Point.hpp"
#include "../SVG.hpp"
#include "../BoundingBox.hpp"
#include "../Polygon.hpp"
#include "../ClipperUtils.hpp"
#include "../Flow.hpp"
#include "../Config.hpp"
#include "../Line.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Slic3r {

struct ExtendedPoint
{
    Vec2d  position;
    float  distance;
    size_t nearest_prev_layer_line;
    Vec2d nearest_prev_layer_point;
    float  curvature;
};

template<bool SCALED_INPUT, bool ADD_INTERSECTIONS, bool PREV_LAYER_BOUNDARY_OFFSET, bool SIGNED_DISTANCE, typename P, typename L>
std::vector<ExtendedPoint> estimate_points_properties(const std::vector<P>                   &input_points,
                                                      const AABBTreeLines::LinesDistancer<L> &unscaled_prev_layer,
                                                      float                                   flow_width,
                                                      float                                   max_line_length = -1.0f)
{
    using AABBScalar = typename AABBTreeLines::LinesDistancer<L>::Scalar;
    if (input_points.empty())
        return {};
    float              boundary_offset = PREV_LAYER_BOUNDARY_OFFSET ? 0.5 * flow_width : 0.0f;
    auto maybe_unscale = [](const P &p) { return SCALED_INPUT ? unscaled(p) : p.template cast<double>(); };

    std::vector<ExtendedPoint> points;
    points.reserve(input_points.size() * (ADD_INTERSECTIONS ? 1.5 : 1));

    {
        ExtendedPoint start_point{maybe_unscale(input_points.front())};
        auto [distance, nearest_line, x]    = unscaled_prev_layer.template distance_from_lines_extra<SIGNED_DISTANCE>(start_point.position.cast<AABBScalar>());
        start_point.distance                = distance + boundary_offset;
        start_point.nearest_prev_layer_line = nearest_line;
        start_point.nearest_prev_layer_point = x.template cast<double>();
        points.push_back(start_point);
    }
    for (size_t i = 1; i < input_points.size(); i++) {
        ExtendedPoint next_point{maybe_unscale(input_points[i])};
        auto [distance, nearest_line, x]   = unscaled_prev_layer.template distance_from_lines_extra<SIGNED_DISTANCE>(next_point.position.cast<AABBScalar>());
        next_point.distance                = distance + boundary_offset;
        next_point.nearest_prev_layer_line = nearest_line;
        next_point.nearest_prev_layer_point = x.template cast<double>();

        if (ADD_INTERSECTIONS &&
            ((points.back().distance > boundary_offset + EPSILON) != (next_point.distance > boundary_offset + EPSILON))) {
            const ExtendedPoint &prev_point = points.back();
            auto intersections = unscaled_prev_layer.template intersections_with_line<true>(L{prev_point.position.cast<AABBScalar>(), next_point.position.cast<AABBScalar>()});
            for (const auto &intersection : intersections) {
                ExtendedPoint p{};
                p.position = intersection.first.template cast<double>();
                p.distance = boundary_offset;
                p.nearest_prev_layer_line = intersection.second;
                p.nearest_prev_layer_point = p.position;
                points.push_back(p);
            }
        }
        points.push_back(next_point);
    }

    if (PREV_LAYER_BOUNDARY_OFFSET && ADD_INTERSECTIONS) {
        std::vector<ExtendedPoint> new_points;
        new_points.reserve(points.size()*2);
        new_points.push_back(points.front());
        for (int point_idx = 0; point_idx < int(points.size()) - 1; ++point_idx) {
            const ExtendedPoint &curr = points[point_idx];
            const ExtendedPoint &next = points[point_idx + 1];

            if ((curr.distance > 0 && curr.distance < boundary_offset + 2.0f) ||
                (next.distance > 0 && next.distance < boundary_offset + 2.0f)) {
                double line_len = (next.position - curr.position).norm();
                if (line_len > 4.0f) {
                    double a0 = std::clamp((curr.distance + 2 * boundary_offset) / line_len, 0.0, 1.0);
                    double a1 = std::clamp(1.0f - (next.distance + 2 * boundary_offset) / line_len, 0.0, 1.0);
                    double t0 = std::min(a0, a1);
                    double t1 = std::max(a0, a1);

                    if (t0 < 1.0) {
                        auto p0                         = curr.position + t0 * (next.position - curr.position);
                        auto [p0_dist, p0_near_l, p0_x] = unscaled_prev_layer.template distance_from_lines_extra<SIGNED_DISTANCE>(p0.cast<AABBScalar>());
                        ExtendedPoint new_p{};
                        new_p.position                 = p0;
                        new_p.distance                 = float(p0_dist + boundary_offset);
                        new_p.nearest_prev_layer_line  = p0_near_l;
                        new_p.nearest_prev_layer_point = p0_x.template cast<double>();
                        new_points.push_back(new_p);
                    }
                    if (t1 > 0.0) {
                        auto p1                         = curr.position + t1 * (next.position - curr.position);
                        auto [p1_dist, p1_near_l, p1_x] = unscaled_prev_layer.template distance_from_lines_extra<SIGNED_DISTANCE>(p1.cast<AABBScalar>());
                        ExtendedPoint new_p{};
                        new_p.position                 = p1;
                        new_p.distance                 = float(p1_dist + boundary_offset);
                        new_p.nearest_prev_layer_line  = p1_near_l;
                        new_p.nearest_prev_layer_point = p1_x.template cast<double>();
                        new_points.push_back(new_p);
                    }
                }
            }
            new_points.push_back(next);
        }
        points = new_points;
    }

    if (max_line_length > 0) {
        std::vector<ExtendedPoint> new_points;
        new_points.reserve(points.size()*2);
        {
            for (size_t i = 0; i + 1 < points.size(); i++) {
                const ExtendedPoint &curr = points[i];
                const ExtendedPoint &next = points[i + 1];
                new_points.push_back(curr);
                double len             = (next.position - curr.position).squaredNorm();
                double t               = sqrt((max_line_length * max_line_length) / len);
                size_t new_point_count = 1.0 / t;
                for (size_t j = 1; j < new_point_count + 1; j++) {
                    Vec2d pos  = curr.position * (1.0 - j * t) + next.position * (j * t);
                    auto [p_dist, p_near_l,
                          p_x] = unscaled_prev_layer.template distance_from_lines_extra<SIGNED_DISTANCE>(pos.cast<AABBScalar>());
                    ExtendedPoint new_p{};
                    new_p.position                 = pos;
                    new_p.distance                 = float(p_dist + boundary_offset);
                    new_p.nearest_prev_layer_line  = p_near_l;
                    new_p.nearest_prev_layer_point = p_x.template cast<double>();
                    new_points.push_back(new_p);
                }
            }
            new_points.push_back(points.back());
        }
        points = new_points;
    }

    std::vector<float> angles_for_curvature(points.size());
    std::vector<float> distances_for_curvature(points.size());

    for (int point_idx = 0; point_idx < int(points.size()); ++point_idx) {
        ExtendedPoint &a    = points[point_idx];
        ExtendedPoint &prev = points[point_idx > 0 ? point_idx - 1 : point_idx];

        int prev_point_idx = point_idx;
        while (prev_point_idx > 0) {
            prev_point_idx--;
            if ((a.position - points[prev_point_idx].position).squaredNorm() > EPSILON) {
                break;
            }
        }

        int next_point_index = point_idx;
        while (next_point_index < int(points.size()) - 1) {
            next_point_index++;
            if ((a.position - points[next_point_index].position).squaredNorm() > EPSILON) {
                break;
            }
        }

        distances_for_curvature[point_idx] = (prev.position - a.position).norm();
        if (prev_point_idx != point_idx && next_point_index != point_idx) {
            float alfa = angle(a.position - points[prev_point_idx].position, points[next_point_index].position - a.position);
            angles_for_curvature[point_idx] = alfa;
        } // else keep zero
    }

    for (float window_size : {3.0f, 9.0f, 16.0f}) {
        size_t tail_point      = 0;
        float  tail_window_acc = 0;
        float  tail_angle_acc  = 0;

        size_t head_point      = 0;
        float  head_window_acc = 0;
        float  head_angle_acc  = 0;

        for (int point_idx = 0; point_idx < int(points.size()); ++point_idx) {
            if (point_idx > 0) {
                tail_window_acc += distances_for_curvature[point_idx - 1];
                tail_angle_acc += angles_for_curvature[point_idx - 1];
                head_window_acc -= distances_for_curvature[point_idx - 1];
                head_angle_acc -= angles_for_curvature[point_idx - 1];
            }
            while (tail_window_acc > window_size * 0.5 && tail_point < point_idx) {
                tail_window_acc -= distances_for_curvature[tail_point];
                tail_angle_acc -= angles_for_curvature[tail_point];
                tail_point++;
            }

            while (head_window_acc < window_size * 0.5 && head_point < int(points.size()) - 1) {
                head_window_acc += distances_for_curvature[head_point];
                head_angle_acc += angles_for_curvature[head_point];
                head_point++;
            }

            float curvature = (tail_angle_acc + head_angle_acc) / (tail_window_acc + head_window_acc);
            if (std::abs(curvature) > std::abs(points[point_idx].curvature)) {
                points[point_idx].curvature = curvature;
            }
        }
    }

    return points;
}

struct ProcessedPoint
{
    Point p;
    float speed = 1.0f;
    int fan_speed = 0;
};

class ExtrusionQualityEstimator
{
    std::unordered_map<const PrintObject *, AABBTreeLines::LinesDistancer<Linef>> prev_layer_boundaries;
    std::unordered_map<const PrintObject *, AABBTreeLines::LinesDistancer<Linef>> next_layer_boundaries;
    std::unordered_map<const PrintObject *, AABBTreeLines::LinesDistancer<CurledLine>> prev_curled_extrusions;
    std::unordered_map<const PrintObject *, AABBTreeLines::LinesDistancer<CurledLine>> next_curled_extrusions;
    const PrintObject                                                            *current_object;

public:
    void set_current_object(const PrintObject *object) { current_object = object; }

    void prepare_for_new_layer(const Layer *layer)
    {
        if (layer == nullptr)
            return;
        const PrintObject *object      = layer->object();
        prev_layer_boundaries[object]  = next_layer_boundaries[object];
        next_layer_boundaries[object]  = AABBTreeLines::LinesDistancer<Linef>{to_unscaled_linesf(layer->lslices)};
        prev_curled_extrusions[object] = next_curled_extrusions[object];
        next_curled_extrusions[object] = AABBTreeLines::LinesDistancer<CurledLine>{layer->curled_lines};
    }

    std::vector<ProcessedPoint> estimate_speed_from_extrusion_quality(
        const ExtrusionPath                                          &path,
        const std::vector<std::pair<int, ConfigOptionFloatOrPercent>> overhangs_w_speeds,
        const std::vector<std::pair<int, ConfigOptionInts>>           overhangs_w_fan_speeds,
        size_t                                                        extruder_id,
        float                                                         ext_perimeter_speed,
        float                                                         original_speed)
    {
        float                  speed_base = ext_perimeter_speed > 0 ? ext_perimeter_speed : original_speed;
        std::map<float, float> speed_sections;
        for (size_t i = 0; i < overhangs_w_speeds.size(); i++) {
            float distance           = path.width * (1.0 - (overhangs_w_speeds[i].first / 100.0));
            float speed              = overhangs_w_speeds[i].second.percent ? (speed_base * overhangs_w_speeds[i].second.value / 100.0) :
                                                                              overhangs_w_speeds[i].second.value;
            if (speed < EPSILON) speed = speed_base;
            speed_sections[distance] = speed;
        }

        std::map<float, float> fan_speed_sections;
        for (size_t i = 0; i < overhangs_w_fan_speeds.size(); i++) {
            float distance           = path.width * (1.0 - (overhangs_w_fan_speeds[i].first / 100.0));
            float fan_speed            = overhangs_w_fan_speeds[i].second.get_at(extruder_id);
            fan_speed_sections[distance] = fan_speed;
        }

        std::vector<ExtendedPoint> extended_points =
            estimate_points_properties<true, true, true, true>(path.polyline.points, prev_layer_boundaries[current_object], path.width);

        for (ExtendedPoint &ep : extended_points) {
            // We are going to enforce slowdown over curled extrusions by increasing the point distance. The overhang speed is based on
            // signed distance from the prev layer, where 0 means fully overlapping extrusions and thus no slowdown, while extrusion_width
            // and more means full overhang, thus full slowdown. However, for curling, we take unsinged distance from the curled lines and
            // artifically modifiy the distance
            auto [distance_from_curled, line_idx,
                  p] = prev_curled_extrusions[current_object].distance_from_lines_extra<false>(Point::new_scale(ep.position));
            if (distance_from_curled < scale_(2.0 * path.width)) {
                float artificially_increased_distance = path.width *
                                                        (1.0 - (unscaled(distance_from_curled) / (2.0 * path.width)) *
                                                                   (unscaled(distance_from_curled) / (2.0 * path.width))) *
                                                        (prev_curled_extrusions[current_object].get_line(line_idx).curled_height /
                                                         (path.height * 10.0f)); // max_curled_height_factor from SupportSpotGenerator
                ep.distance = std::max(ep.distance, artificially_increased_distance);
            }
        }

        std::vector<ProcessedPoint> processed_points;
        processed_points.reserve(extended_points.size());
        for (size_t i = 0; i < extended_points.size(); i++) {
            const ExtendedPoint &curr = extended_points[i];
            const ExtendedPoint &next = extended_points[i + 1 < extended_points.size() ? i + 1 : i];

            auto interpolate_speed = [](const std::map<float, float> &values, float distance) {
                auto upper_dist = values.lower_bound(distance);
                if (upper_dist == values.end()) {
                    return values.rbegin()->second;
                }
                if (upper_dist == values.begin()) {
                    return upper_dist->second;
                }

                auto  lower_dist = std::prev(upper_dist);
                float t          = (distance - lower_dist->first) / (upper_dist->first - lower_dist->first);
                return (1.0f - t) * lower_dist->second + t * upper_dist->second;
            };

            float extrusion_speed = std::min(interpolate_speed(speed_sections, curr.distance),
                                             interpolate_speed(speed_sections, next.distance));
            float fan_speed       = std::min(interpolate_speed(fan_speed_sections, curr.distance),
                                             interpolate_speed(fan_speed_sections, next.distance));

            processed_points.push_back({scaled(curr.position), extrusion_speed, int(fan_speed)});
        }
        return processed_points;
    }
};

} // namespace Slic3r

#endif // slic3r_ExtrusionProcessor_hpp_
