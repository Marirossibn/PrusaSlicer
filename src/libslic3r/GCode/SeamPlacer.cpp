#include "SeamPlacer.hpp"

#include "tbb/parallel_for.h"
#include "tbb/blocked_range.h"
#include "tbb/parallel_reduce.h"
#include <boost/log/trivial.hpp>
#include <random>
#include <algorithm>
#include <queue>


//For polynomial fitting
#include <Eigen/Dense>
#include <Eigen/QR>

#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/EdgeGrid.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/QuadricEdgeCollapse.hpp"
#include "libslic3r/Subdivide.hpp"

//#define DEBUG_FILES

#ifdef DEBUG_FILES
#include <boost/nowide/cstdio.hpp>
#include <SVG.hpp>
#endif

namespace Slic3r {

namespace SeamPlacerImpl {

// base function: ((e^(((1)/(x^(2)+1)))-1)/(e-1))
// checkout e.g. here: https://www.geogebra.org/calculator
float gauss(float value, float mean_x_coord, float mean_value, float falloff_speed) {
    float shifted = value - mean_x_coord;
    float denominator = falloff_speed * shifted * shifted + 1.0f;
    float exponent = 1.0f / denominator;
    return mean_value * (std::exp(exponent) - 1.0f) / (std::exp(1.0f) - 1.0f);
}

Vec3f value_to_rgbf(float minimum, float maximum, float value) {
    float ratio = 2.0f * (value - minimum) / (maximum - minimum);
    float b = std::max(0.0f, (1.0f - ratio));
    float r = std::max(0.0f, (ratio - 1.0f));
    float g = 1.0f - b - r;
    return Vec3f { r, g, b };
}

Vec3i value_rgbi(float minimum, float maximum, float value) {
    return (value_to_rgbf(minimum, maximum, value) * 255).cast<int>();
}

//https://towardsdatascience.com/least-square-polynomial-fitting-using-c-eigen-package-c0673728bd01
// interpolates points in z (treats z coordinates as time) and returns coefficients for axis x and y
std::vector<Vec2f> polyfit(const std::vector<Vec3f> &points, const std::vector<float> &weights, size_t order) {
    // check to make sure inputs are correct
    assert(points.size() >= order + 1);
    assert(points.size() == weights.size());

    std::vector<float> squared_weights(weights.size());
    for (size_t index = 0; index < weights.size(); ++index) {
        squared_weights[index] = sqrt(weights[index]);
    }

    Eigen::VectorXf V0(points.size());
    Eigen::VectorXf V1(points.size());
    Eigen::VectorXf V2(points.size());
    for (size_t index = 0; index < points.size(); index++) {
        V0(index) = points[index].x() * squared_weights[index];
        V1(index) = points[index].y() * squared_weights[index];
        V2(index) = points[index].z();
    }

    // Create Matrix Placeholder of size n x k, n= number of datapoints, k = order of polynomial, for exame k = 3 for cubic polynomial
    Eigen::MatrixXf T(points.size(), order + 1);
    // Populate the matrix
    for (size_t i = 0; i < points.size(); ++i)
            {
        for (size_t j = 0; j < order + 1; ++j)
                {
            T(i, j) = pow(V2(i), j) * squared_weights[i];
        }
    }

    // Solve for linear least square fit
    const auto QR = T.householderQr();
    Eigen::VectorXf result0 = QR.solve(V0);
    Eigen::VectorXf result1 = QR.solve(V1);
    std::vector<Vec2f> coeff { order + 1 };
    for (size_t k = 0; k < order + 1; k++) {
        coeff[k] = Vec2f { result0[k], result1[k] };
    }
    return coeff;
}

Vec3f get_fitted_point(const std::vector<Vec2f> &coefficients, float z) {
    size_t order = coefficients.size() - 1;
    float fitted_x = 0;
    float fitted_y = 0;
    for (size_t index = 0; index < order + 1; ++index) {
        float z_pow = pow(z, index);
        fitted_x += coefficients[index].x() * z_pow;
        fitted_y += coefficients[index].y() * z_pow;
    }

    return Vec3f { fitted_x, fitted_y, z };
}

/// Coordinate frame
class Frame {
public:
    Frame() {
        mX = Vec3f(1, 0, 0);
        mY = Vec3f(0, 1, 0);
        mZ = Vec3f(0, 0, 1);
    }

    Frame(const Vec3f &x, const Vec3f &y, const Vec3f &z) :
            mX(x), mY(y), mZ(z) {
    }

    void set_from_z(const Vec3f &z) {
        mZ = z.normalized();
        Vec3f tmpZ = mZ;
        Vec3f tmpX = (std::abs(tmpZ.x()) > 0.99f) ? Vec3f(0, 1, 0) : Vec3f(1, 0, 0);
        mY = (tmpZ.cross(tmpX)).normalized();
        mX = mY.cross(tmpZ);
    }

    Vec3f to_world(const Vec3f &a) const {
        return a.x() * mX + a.y() * mY + a.z() * mZ;
    }

    Vec3f to_local(const Vec3f &a) const {
        return Vec3f(mX.dot(a), mY.dot(a), mZ.dot(a));
    }

    const Vec3f& binormal() const {
        return mX;
    }

    const Vec3f& tangent() const {
        return mY;
    }

    const Vec3f& normal() const {
        return mZ;
    }

private:
    Vec3f mX, mY, mZ;
};

Vec3f sample_sphere_uniform(const Vec2f &samples) {
    float term1 = 2.0f * float(PI) * samples.x();
    float term2 = 2.0f * sqrt(samples.y() - samples.y() * samples.y());
    return {cos(term1) * term2, sin(term1) * term2,
        1.0f - 2.0f * samples.y()};
}

Vec3f sample_hemisphere_uniform(const Vec2f &samples) {
    float term1 = 2.0f * float(PI) * samples.x();
    float term2 = 2.0f * sqrt(samples.y() - samples.y() * samples.y());
    return {cos(term1) * term2, sin(term1) * term2,
        abs(1.0f - 2.0f * samples.y())};
}

Vec3f sample_power_cosine_hemisphere(const Vec2f &samples, float power) {
    float term1 = 2.f * float(PI) * samples.x();
    float term2 = pow(samples.y(), 1.f / (power + 1.f));
    float term3 = sqrt(1.f - term2 * term2);

    return Vec3f(cos(term1) * term3, sin(term1) * term3, term2);
}

std::vector<FaceVisibilityInfo> raycast_visibility(const AABBTreeIndirect::Tree<3, float> &raycasting_tree,
        const indexed_triangle_set &triangles) {
    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: raycast visibility for " << triangles.indices.size() << " triangles: start";

    float step_size = 1.0f / SeamPlacer::sqr_rays_per_triangle;
    std::vector<Vec3f> precomputed_sample_directions(
            SeamPlacer::sqr_rays_per_triangle * SeamPlacer::sqr_rays_per_triangle);
    for (size_t x_idx = 0; x_idx < SeamPlacer::sqr_rays_per_triangle; ++x_idx) {
        float sample_x = x_idx * step_size + step_size / 2.0;
        for (size_t y_idx = 0; y_idx < SeamPlacer::sqr_rays_per_triangle; ++y_idx) {
            size_t dir_index = x_idx * SeamPlacer::sqr_rays_per_triangle + y_idx;
            float sample_y = y_idx * step_size + step_size / 2.0;
            precomputed_sample_directions[dir_index] = sample_hemisphere_uniform( { sample_x, sample_y });
        }
    }

    std::vector<FaceVisibilityInfo> result(triangles.indices.size());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, result.size()),
            [&](tbb::blocked_range<size_t> r) {
                for (size_t face_index = r.begin(); face_index < r.end(); ++face_index) {
                    FaceVisibilityInfo &dest = result[face_index];
                    dest.visibility = 1.0f;
                    constexpr float decrease = 1.0f / (SeamPlacer::sqr_rays_per_triangle * SeamPlacer::sqr_rays_per_triangle);

                    Vec3i face = triangles.indices[face_index];
                    Vec3f A = triangles.vertices[face.x()];
                    Vec3f B = triangles.vertices[face.y()];
                    Vec3f C = triangles.vertices[face.z()];
                    Vec3f center = (A + B + C) / 3.0f;
                    Vec3f normal = ((B - A).cross(C - B)).normalized();
                    // apply the local direction via Frame struct - the local_dir is with respect to +Z being forward
                    Frame f;
                    f.set_from_z(normal);

                    for (const auto &dir : precomputed_sample_directions) {
                        Vec3f final_ray_dir = (f.to_world(dir));
                        igl::Hit hitpoint;
                        // FIXME: This AABBTTreeIndirect query will not compile for float ray origin and
                        // direction.
                        Vec3d ray_origin_d = (center + normal).cast<double>(); // start one mm above surface.
                        Vec3d final_ray_dir_d = final_ray_dir.cast<double>();
                        bool hit = AABBTreeIndirect::intersect_ray_first_hit(triangles.vertices,
                                triangles.indices, raycasting_tree, ray_origin_d, final_ray_dir_d, hitpoint);

                        if (hit) {
                            dest.visibility -= decrease;
                        }
                    }
                }
            });

    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: raycast visibility for " << triangles.indices.size() << " triangles: end";

    return result;
}

std::vector<float> calculate_polygon_angles_at_vertices(const Polygon &polygon, const std::vector<float> &lengths,
        float min_arm_length) {
    std::vector<float> result(polygon.size());

    if (polygon.size() == 1) {
        result[0] = 0.0f;
    }

    auto make_idx_circular = [&](int index) {
        while (index < 0) {
            index += polygon.size();
        }
        return index % polygon.size();
    };

    int idx_prev = 0;
    int idx_curr = 0;
    int idx_next = 0;

    float distance_to_prev = 0;
    float distance_to_next = 0;

    //push idx_prev far enough back as initialization
    while (distance_to_prev < min_arm_length) {
        idx_prev = make_idx_circular(idx_prev - 1);
        distance_to_prev += lengths[idx_prev];
    }

    for (size_t _i = 0; _i < polygon.size(); ++_i) {
        // pull idx_prev to current as much as possible, while respecting the min_arm_length
        while (distance_to_prev - lengths[idx_prev] > min_arm_length) {
            distance_to_prev -= lengths[idx_prev];
            idx_prev = make_idx_circular(idx_prev + 1);
        }

        //push idx_next forward as far as needed
        while (distance_to_next < min_arm_length) {
            distance_to_next += lengths[idx_next];
            idx_next = make_idx_circular(idx_next + 1);
        }

        // Calculate angle between idx_prev, idx_curr, idx_next.
        const Point &p0 = polygon.points[idx_prev];
        const Point &p1 = polygon.points[idx_curr];
        const Point &p2 = polygon.points[idx_next];
        const Point v1 = p1 - p0;
        const Point v2 = p2 - p1;
        int64_t dot = int64_t(v1(0)) * int64_t(v2(0)) + int64_t(v1(1)) * int64_t(v2(1));
        int64_t cross = int64_t(v1(0)) * int64_t(v2(1)) - int64_t(v1(1)) * int64_t(v2(0));
        float angle = float(atan2(float(cross), float(dot)));
        result[idx_curr] = angle;

        // increase idx_curr by one
        float curr_distance = lengths[idx_curr];
        idx_curr++;
        distance_to_prev += curr_distance;
        distance_to_next -= curr_distance;
    }

    return result;
}

// structure to store global information about the model - occlusion hits, enforcers, blockers
struct GlobalModelInfo {
    indexed_triangle_set model;
    AABBTreeIndirect::Tree<3, float> model_tree;
    std::vector<FaceVisibilityInfo> visiblity_info;
    indexed_triangle_set enforcers;
    indexed_triangle_set blockers;
    AABBTreeIndirect::Tree<3, float> enforcers_tree;
    AABBTreeIndirect::Tree<3, float> blockers_tree;

    bool is_enforced(const Vec3f &position, float radius) const {
        if (enforcers.empty()) {
            return false;
        }
        float radius_sqr = radius * radius;
        return AABBTreeIndirect::is_any_triangle_in_radius(enforcers.vertices, enforcers.indices,
                enforcers_tree, position, radius_sqr);
    }

    bool is_blocked(const Vec3f &position, float radius) const {
        if (blockers.empty()) {
            return false;
        }
        float radius_sqr = radius * radius;
        return AABBTreeIndirect::is_any_triangle_in_radius(blockers.vertices, blockers.indices,
                blockers_tree, position, radius_sqr);
    }

    float calculate_point_visibility(const Vec3f &position) const {
        size_t hit_idx;
        Vec3f hit_point;
        if (AABBTreeIndirect::squared_distance_to_indexed_triangle_set(model.vertices, model.indices, model_tree,
                position, hit_idx, hit_point) >= 0) {
            return visiblity_info[hit_idx].visibility;
        } else {
            return 0.0f;
        }

    }

#ifdef DEBUG_FILES
    void debug_export(const indexed_triangle_set &obj_mesh, const char *file_name) const {
        indexed_triangle_set divided_mesh = obj_mesh;
        Slic3r::CNumericLocalesSetter locales_setter;

        FILE *fp = boost::nowide::fopen(file_name, "w");
        if (fp == nullptr) {
            BOOST_LOG_TRIVIAL(error)
            << "stl_write_obj: Couldn't open " << file_name << " for writing";
            return;
        }

        for (size_t i = 0; i < divided_mesh.vertices.size(); ++i) {
            float visibility = calculate_point_visibility(divided_mesh.vertices[i]);
            Vec3f color = value_to_rgbf(0.0f, 1.0f,
                    visibility);
            fprintf(fp, "v %f %f %f  %f %f %f\n",
                    divided_mesh.vertices[i](0), divided_mesh.vertices[i](1), divided_mesh.vertices[i](2),
                    color(0), color(1), color(2)
                            );
        }
        for (size_t i = 0; i < divided_mesh.indices.size(); ++i)
            fprintf(fp, "f %d %d %d\n", divided_mesh.indices[i][0] + 1, divided_mesh.indices[i][1] + 1,
                    divided_mesh.indices[i][2] + 1);
        fclose(fp);

    }
#endif
}
;

//Extract perimeter polygons of the given layer
Polygons extract_perimeter_polygons(const Layer *layer) {
    Polygons polygons;
    for (const LayerRegion *layer_region : layer->regions()) {
        for (const ExtrusionEntity *ex_entity : layer_region->perimeters.entities) {
            if (ex_entity->is_collection()) { //collection of inner, outer, and overhang perimeters
                for (const ExtrusionEntity *perimeter : static_cast<const ExtrusionEntityCollection*>(ex_entity)->entities) {
                    if (perimeter->role() == ExtrusionRole::erExternalPerimeter) {
                        Points p;
                        perimeter->collect_points(p);
                        polygons.emplace_back(p);
                    }
                }
                if (polygons.empty()) {
                    Points p;
                    ex_entity->collect_points(p);
                    polygons.emplace_back(p);
                }
            } else {
                Points p;
                ex_entity->collect_points(p);
                polygons.emplace_back(p);
            }
        }
    }

    if (polygons.empty()) { // If there are no perimeter polygons for whatever reason (disabled perimeters .. ) insert dummy point
        // it is easier than checking everywhere if the layer is not emtpy, no seam will be placed to this layer anyway
        polygons.emplace_back(std::vector { Point { 0, 0 } });
    }

    return polygons;
}

// Insert SeamCandidates created from perimeter polygons in to the result vector.
// Compute its type (Enfrocer,Blocker), angle, and position
//each SeamCandidate also contains pointer to shared Perimeter structure representing the polygon
// if Custom Seam modifiers are present, oversamples the polygon if necessary to better fit user intentions
void process_perimeter_polygon(const Polygon &orig_polygon, float z_coord, std::vector<SeamCandidate> &result_vec,
        const GlobalModelInfo &global_model_info) {
    if (orig_polygon.size() == 0) {
        return;
    }

    Polygon polygon = orig_polygon;
    bool was_clockwise = polygon.make_counter_clockwise();

    std::vector<float> lengths { };
    for (size_t point_idx = 0; point_idx < polygon.size() - 1; ++point_idx) {
        lengths.push_back(std::max((unscale(polygon[point_idx]) - unscale(polygon[point_idx + 1])).norm(), 0.01));
    }
    lengths.push_back(std::max((unscale(polygon[0]) - unscale(polygon[polygon.size() - 1])).norm(), 0.01));

    std::vector<float> local_angles = calculate_polygon_angles_at_vertices(polygon, lengths,
            SeamPlacer::polygon_local_angles_arm_distance);
    std::shared_ptr<Perimeter> perimeter = std::make_shared<Perimeter>();

    std::queue<Vec3f> orig_polygon_points { };
    for (size_t index = 0; index < polygon.size(); ++index) {
        Vec2f unscaled_p = unscale(polygon[index]).cast<float>();
        orig_polygon_points.emplace(unscaled_p.x(), unscaled_p.y(), z_coord);
    }
    Vec3f first = orig_polygon_points.front();
    std::queue<Vec3f> oversampled_points { };
    size_t orig_angle_index = 0;
    perimeter->start_index = result_vec.size();
    while (!orig_polygon_points.empty() || !oversampled_points.empty()) {
        EnforcedBlockedSeamPoint type = EnforcedBlockedSeamPoint::Neutral;
        Vec3f position;
        float local_ccw_angle = 0;
        bool orig_point = false;
        if (!oversampled_points.empty()) {
            position = oversampled_points.front();
            oversampled_points.pop();
        } else {
            position = orig_polygon_points.front();
            orig_polygon_points.pop();
            local_ccw_angle = was_clockwise ? -local_angles[orig_angle_index] : local_angles[orig_angle_index];
            orig_angle_index++;
            orig_point = true;
        }

        if (global_model_info.is_enforced(position, SeamPlacer::enforcer_blocker_distance_tolerance)) {
            type = EnforcedBlockedSeamPoint::Enforced;
        }

        if (global_model_info.is_blocked(position, SeamPlacer::enforcer_blocker_distance_tolerance)) {
            type = EnforcedBlockedSeamPoint::Blocked;
        }

        if (orig_point) {
            Vec3f pos_of_next = orig_polygon_points.empty() ? first : orig_polygon_points.front();
            float distance_to_next = (position - pos_of_next).norm();
            if (global_model_info.is_enforced(position, distance_to_next)
                    || global_model_info.is_blocked(position, distance_to_next)) {
                Vec3f vec_to_next = (pos_of_next - position).normalized();
                float step_size = SeamPlacer::enforcer_blocker_oversampling_distance;
                float step = step_size;
                while (step < distance_to_next) {
                    oversampled_points.push(position + vec_to_next * step);
                    step += step_size;
                }
            }
        }

        result_vec.emplace_back(position, perimeter, local_ccw_angle, type);

    }

    perimeter->end_index = result_vec.size() - 1;
}

// Get index of previous and next perimeter point of the layer. Because SeamCandidates of all polygons of the given layer
// are sequentially stored in the vector, each perimeter contains info about start and end index. These vales are used to
// deduce index of previous and next neigbour in the corresponding perimeter.
std::pair<size_t, size_t> find_previous_and_next_perimeter_point(const std::vector<SeamCandidate> &perimeter_points,
        size_t point_index) {
    const SeamCandidate &current = perimeter_points[point_index];
    int prev = point_index - 1; //for majority of points, it is true that neighbours lie behind and in front of them in the vector
    int next = point_index + 1;

    if (point_index == current.perimeter->start_index) {
        // if point_index is equal to start, it means that the previous neighbour is at the end
        prev = current.perimeter->end_index;
    }

    if (point_index == current.perimeter->end_index) {
        // if point_index is equal to end, than next neighbour is at the start
        next = current.perimeter->start_index;
    }

    assert(prev >= 0);
    assert(next >= 0);
    return {size_t(prev),size_t(next)};
}

//NOTE: only rough esitmation of overhang distance
// value represents distance from edge, positive is overhang, negative is inside shape
float calculate_overhang(const SeamCandidate &point, const SeamCandidate &under_a, const SeamCandidate &under_b,
        const SeamCandidate &under_c) {
    auto p = Vec2d { point.position.x(), point.position.y() };
    auto a = Vec2d { under_a.position.x(), under_a.position.y() };
    auto b = Vec2d { under_b.position.x(), under_b.position.y() };
    auto c = Vec2d { under_c.position.x(), under_c.position.y() };

    auto oriented_line_dist = [](const Vec2d a, const Vec2d b, const Vec2d p) { //signed distance from line
        return -((b.x() - a.x()) * (a.y() - p.y()) - (a.x() - p.x()) * (b.y() - a.y())) / (a - b).norm();
    };

    auto dist_ab = oriented_line_dist(a, b, p);
    auto dist_bc = oriented_line_dist(b, c, p);

    // from angle and signed distances from the arms of the points on the previous layer, we
    // can deduce if it is overhang and give estimation of the size.
    // However, the size of the overhang is rough estimation, the sign is more reliable
    if (under_b.local_ccw_angle > 0 && dist_ab > 0 && dist_bc > 0) { //convex shape, p is inside
        return -((p - b).norm() + dist_ab + dist_bc) / 3.0;
    }

    if (under_b.local_ccw_angle < 0 && (dist_ab < 0 || dist_bc < 0)) { //concave shape, p is inside
        return -((p - b).norm() + dist_ab + dist_bc) / 3.0;
    }

    return ((p - b).norm() + dist_ab + dist_bc) / 3.0;
}

// Computes all global model info - transforms object, performs raycasting,
// stores enforces and blockers
void compute_global_occlusion(GlobalModelInfo &result, const PrintObject *po) {
    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: build AABB tree for raycasting and gather occlusion info: start";
// Build AABB tree for raycasting
    auto obj_transform = po->trafo_centered();
    indexed_triangle_set triangle_set;
    //add all parts
    for (const ModelVolume *model_volume : po->model_object()->volumes) {
        if (model_volume->type() == ModelVolumeType::MODEL_PART) {
            auto model_transformation = model_volume->get_matrix();
            indexed_triangle_set model_its = model_volume->mesh().its;
            its_transform(model_its, model_transformation);
            its_merge(triangle_set, model_its);
        }
    }

    float target_error = SeamPlacer::raycasting_decimation_target_error;
    its_quadric_edge_collapse(triangle_set, 0, &target_error, nullptr, nullptr);
    triangle_set = its_subdivide(triangle_set, SeamPlacer::raycasting_subdivision_target_length);
    its_transform(triangle_set, obj_transform);

    auto raycasting_tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(triangle_set.vertices,
            triangle_set.indices);

    result.model = triangle_set;
    result.model_tree = raycasting_tree;
    result.visiblity_info = raycast_visibility(raycasting_tree, triangle_set);

    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: build AABB tree for raycasting and gather occlusion info: end";

#ifdef DEBUG_FILES
    auto filename = debug_out_path(("visiblity_of_" + std::to_string(po->id().id) + ".obj").c_str());
    result.debug_export(triangle_set, filename.c_str());
#endif
}

void gather_enforcers_blockers(GlobalModelInfo &result, const PrintObject *po) {
    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: build AABB trees for raycasting enforcers/blockers: start";

    auto obj_transform = po->trafo();

    for (const ModelVolume *mv : po->model_object()->volumes) {
        if (mv->is_seam_painted()) {
            auto model_transformation = mv->get_matrix();

            indexed_triangle_set enforcers = mv->seam_facets.get_facets(*mv, EnforcerBlockerType::ENFORCER);
            its_transform(enforcers, model_transformation);
            its_merge(result.enforcers, enforcers);

            indexed_triangle_set blockers = mv->seam_facets.get_facets(*mv, EnforcerBlockerType::BLOCKER);
            its_transform(blockers, model_transformation);
            its_merge(result.blockers, blockers);
        }
    }
    its_transform(result.enforcers, obj_transform);
    its_transform(result.blockers, obj_transform);

    result.enforcers_tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(result.enforcers.vertices,
            result.enforcers.indices);
    result.blockers_tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(result.blockers.vertices,
            result.blockers.indices);

    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: build AABB trees for raycasting enforcers/blockers: end";
}

//Comparator of seam points. It has two necessary methods: is_first_better and is_first_not_much_worse
struct SeamComparator {
    SeamPosition setup;

    SeamComparator(SeamPosition setup) :
            setup(setup) {
    }

    float compute_angle_penalty(float ccw_angle) const {
        // This function is used:
        // ((ℯ^(((1)/(x^(2)*3+1)))-1)/(ℯ-1))*1+((1)/(2+ℯ^(-x)))
        // looks terribly, but it is gaussian combined with sigmoid,
        // so that concave points have much smaller penalty over convex ones

        return gauss(ccw_angle, 0.0f, 1.0f, 3.0f) +
                1.0f / (2 + std::exp(-ccw_angle)); // sigmoid, which heavily favourizes concave angles
    }

    // Standard comparator, must respect the requirements of comparators (e.g. give same result on same inputs) for sorting usage
    // should return if a is better seamCandidate than b
    bool is_first_better(const SeamCandidate &a, const SeamCandidate &b, const Vec2f &preffered_location = Vec2f { 0.0f,
            0.0f }) const {
        // Blockers/Enforcers discrimination, top priority
        if (a.type > b.type) {
            return true;
        }
        if (b.type > a.type) {
            return false;
        }

        //avoid overhangs
        if (a.overhang > 0.1f && b.overhang < a.overhang) {
            return false;
        }

        if (setup == SeamPosition::spRear) {
            return a.position.y() > b.position.y();
        }

        float distance_penalty_a = 1.0f;
        float distance_penalty_b = 1.0f;
        if (setup == spNearest) {
            distance_penalty_a = 1.1f - gauss((a.position.head<2>() - preffered_location).norm(), 0.0f, 1.0f, 0.005f);
            distance_penalty_b = 1.1f - gauss((a.position.head<2>() - preffered_location).norm(), 0.0f, 1.0f, 0.005f);
        }

        //ranges:          [0 - 1]                                              (0 - 1.3]                               [0.1 - 1.1)
        float penalty_a = (a.visibility + SeamPlacer::additional_angle_importance) * compute_angle_penalty(a.local_ccw_angle)
                * distance_penalty_a;
        float penalty_b = (b.visibility + SeamPlacer::additional_angle_importance) * compute_angle_penalty(b.local_ccw_angle)
                * distance_penalty_b;

        return penalty_a < penalty_b;
    }

    // Comparator used during alignment. If there is close potential aligned point, it is comapred to the current
    // sema point of the perimeter, to find out if the aligned point is not much worse than the current seam
    bool is_first_not_much_worse(const SeamCandidate &a, const SeamCandidate &b) const {
        // Blockers/Enforcers discrimination, top priority
        if (a.type == EnforcedBlockedSeamPoint::Enforced) {
            return true;
        }

        if (a.type == EnforcedBlockedSeamPoint::Blocked) {
            return false;
        }

        if (a.type > b.type) {
            return true;
        }
        if (b.type > a.type) {
            return false;
        }

        //avoid overhangs
        if (a.overhang > 0.1f && b.overhang < a.overhang) {
            return false;
        }

        if (setup == SeamPosition::spRandom) {
            return true;
        }

        if (setup == SeamPosition::spRear) {
            return a.position.y() > b.position.y();
        }

        //ranges:          [0 - 1]                                          (0 - 1.3]                  ;
        float penalty_a = (a.visibility + SeamPlacer::additional_angle_importance) * compute_angle_penalty(a.local_ccw_angle);
        float penalty_b = (b.visibility + SeamPlacer::additional_angle_importance) * compute_angle_penalty(b.local_ccw_angle);

        return penalty_a <= penalty_b || std::abs(penalty_a - penalty_b) < SeamPlacer::seam_align_score_tolerance;
    }

    //always nonzero, positive
    float get_penalty(const SeamCandidate &a) const {
        if (setup == SeamPosition::spRear) {
            return a.position.y();
        }

        return (a.visibility + SeamPlacer::additional_angle_importance) * compute_angle_penalty(a.local_ccw_angle);
    }
}
;

#ifdef DEBUG_FILES
void debug_export_points(const std::vector<std::vector<SeamPlacerImpl::SeamCandidate>> &object_perimter_points,
        const BoundingBox &bounding_box, std::string object_name, const SeamComparator &comparator) {
    for (size_t layer_idx = 0; layer_idx < object_perimter_points.size(); ++layer_idx) {
        std::string angles_file_name = debug_out_path((object_name + "_angles_" + std::to_string(layer_idx) + ".svg").c_str());
        SVG angles_svg { angles_file_name, bounding_box };
        float min_vis = 0;
        float max_vis = min_vis;

        float min_weight = std::numeric_limits<float>::min();
        float max_weight = min_weight;

        for (const SeamCandidate &point : object_perimter_points[layer_idx]) {
            Vec3i color = value_rgbi(-PI, PI, point.local_ccw_angle);
            std::string fill = "rgb(" + std::to_string(color.x()) + "," + std::to_string(color.y()) + ","
                    + std::to_string(color.z()) + ")";
            angles_svg.draw(scaled(Vec2f(point.position.head<2>())), fill);
            min_vis = std::min(min_vis, point.visibility);
            max_vis = std::max(max_vis, point.visibility);

            min_weight = std::min(min_weight, -comparator.get_penalty(point));
            max_weight = std::max(max_weight, -comparator.get_penalty(point));

        }

        std::string visiblity_file_name = debug_out_path((object_name + "_visibility_" + std::to_string(layer_idx) + ".svg").c_str());
        SVG visibility_svg { visiblity_file_name, bounding_box };
        std::string weights_file_name = debug_out_path((object_name + "_weight_" + std::to_string(layer_idx) + ".svg").c_str());
        SVG weight_svg {weights_file_name, bounding_box };
        for (const SeamCandidate &point : object_perimter_points[layer_idx]) {
            Vec3i color = value_rgbi(min_vis, max_vis, point.visibility);
            std::string visibility_fill = "rgb(" + std::to_string(color.x()) + "," + std::to_string(color.y()) + ","
                    + std::to_string(color.z()) + ")";
            visibility_svg.draw(scaled(Vec2f(point.position.head<2>())), visibility_fill);

            Vec3i weight_color = value_rgbi(min_weight, max_weight, comparator.get_penalty(point));
            std::string weight_fill = "rgb(" + std::to_string(weight_color.x()) + "," + std::to_string(weight_color.y())
                    + ","
                    + std::to_string(weight_color.z()) + ")";
            weight_svg.draw(scaled(Vec2f(point.position.head<2>())), weight_fill);
        }
    }
}
#endif

// Pick best seam point based on the given comparator
void pick_seam_point(std::vector<SeamCandidate> &perimeter_points, size_t start_index,
        const SeamComparator &comparator) {
    size_t end_index = perimeter_points[start_index].perimeter->end_index;

    size_t seam_index = start_index;
    for (size_t index = start_index; index <= end_index; ++index) {
        if (comparator.is_first_better(perimeter_points[index], perimeter_points[seam_index])) {
            seam_index = index;
        }
    }
    perimeter_points[start_index].perimeter->seam_index = seam_index;
}

size_t pick_nearest_seam_point_index(const std::vector<SeamCandidate> &perimeter_points, size_t start_index,
        const Vec2f &preffered_location) {
    size_t end_index = perimeter_points[start_index].perimeter->end_index;
    SeamComparator comparator { spNearest };

    size_t seam_index = start_index;
    for (size_t index = start_index; index <= end_index; ++index) {
        if (comparator.is_first_better(perimeter_points[index], perimeter_points[seam_index], preffered_location)) {
            seam_index = index;
        }
    }
    return seam_index;
}

// picks random seam point uniformly, respecting enforcers blockers and overhang avoidance.
void pick_random_seam_point(std::vector<SeamCandidate> &perimeter_points, size_t start_index) {
    SeamComparator comparator { spRandom };

    // algorithm keeps a list of viable points and their lengths. If it finds a point
    // that is much better than the viable_example_index (e.g. better type, no overhang; see is_first_not_much_worse)
    // then it throws away stored lists and starts from start
    // in the end, he list should contain points with same type (Enforced > Neutral > Blocked) and also only those which are not
    // big overhang.
    size_t viable_example_index = start_index;
    size_t end_index = perimeter_points[start_index].perimeter->end_index;
    std::vector<size_t> viable_indices;
    std::vector<float> viable_edges_lengths;
    std::vector<Vec3f> viable_edges;

    for (size_t index = start_index; index <= end_index; ++index) {
        if (comparator.is_first_not_much_worse(perimeter_points[index], perimeter_points[viable_example_index]) &&
                comparator.is_first_not_much_worse(perimeter_points[viable_example_index], perimeter_points[index])) {
            // index ok, push info into respective vectors
            Vec3f edge_to_next;
            if (index == end_index) {
                edge_to_next = (perimeter_points[start_index].position - perimeter_points[index].position);
            } else
            {
                edge_to_next = (perimeter_points[index + 1].position - perimeter_points[index].position);
            }
            float dist_to_next = edge_to_next.norm();
            viable_indices.push_back(index);
            viable_edges_lengths.push_back(dist_to_next);
            viable_edges.push_back(edge_to_next);
        } else if (comparator.is_first_not_much_worse(perimeter_points[viable_example_index],
                perimeter_points[index])) {
            // index is worse then viable_example_index, skip this point
        } else {
            // index is better than viable example index, update example, clear gathered info, start again
            // clear up all gathered info, start from scratch, update example index
            viable_example_index = index;
            viable_indices.clear();
            viable_edges_lengths.clear();
            viable_edges.clear();

            Vec3f edge_to_next;
            if (index == end_index) {
                edge_to_next = (perimeter_points[start_index].position - perimeter_points[index].position);
            } else {
                edge_to_next = (perimeter_points[index + 1].position - perimeter_points[index].position);
            }
            float dist_to_next = edge_to_next.norm();
            viable_indices.push_back(index);
            viable_edges_lengths.push_back(dist_to_next);
            viable_edges.push_back(edge_to_next);
        }
    }

    // now pick random point from the stored options
    float len_sum = std::accumulate(viable_edges_lengths.begin(), viable_edges_lengths.end(), 0.0f);
    float picked_len = len_sum * (rand() / (float(RAND_MAX) + 1));

    size_t point_idx = 0;
    while (picked_len - viable_edges_lengths[point_idx] > 0) {
        picked_len = picked_len - viable_edges_lengths[point_idx];
        point_idx++;
    }

    Perimeter *perimeter = perimeter_points[start_index].perimeter.get();
    perimeter->seam_index = viable_indices[point_idx];
    perimeter->final_seam_position = perimeter_points[perimeter->seam_index].position
            + viable_edges[point_idx].normalized() * picked_len;
    perimeter->finalized = true;

}

} // namespace SeamPlacerImpl

// Parallel process and extract each perimeter polygon of the given print object.
// Gather SeamCandidates of each layer into vector and build KDtree over them
// Store results in the SeamPlacer varaibles m_perimeter_points_per_object and m_perimeter_points_trees_per_object
void SeamPlacer::gather_seam_candidates(const PrintObject *po,
        const SeamPlacerImpl::GlobalModelInfo &global_model_info) {
    using namespace SeamPlacerImpl;

    m_perimeter_points_per_object.emplace(po, po->layer_count());
    m_perimeter_points_trees_per_object.emplace(po, po->layer_count());

    tbb::parallel_for(tbb::blocked_range<size_t>(0, po->layers().size()),
            [&](tbb::blocked_range<size_t> r) {
                for (size_t layer_idx = r.begin(); layer_idx < r.end(); ++layer_idx) {
                    std::vector<SeamCandidate> &layer_candidates =
                            m_perimeter_points_per_object[po][layer_idx];
                    const Layer *layer = po->get_layer(layer_idx);
                    auto unscaled_z = layer->slice_z;
                    Polygons polygons = extract_perimeter_polygons(layer);
                    for (const auto &poly : polygons) {
                        process_perimeter_polygon(poly, unscaled_z, layer_candidates,
                                global_model_info);
                    }
                    auto functor = SeamCandidateCoordinateFunctor { &layer_candidates };
                    m_perimeter_points_trees_per_object[po][layer_idx] = std::make_unique<SeamCandidatesTree
                            >(
                                    functor, layer_candidates.size());
                }
            }
    );
}

void SeamPlacer::calculate_candidates_visibility(const PrintObject *po,
        const SeamPlacerImpl::GlobalModelInfo &global_model_info) {
    using namespace SeamPlacerImpl;

    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_perimeter_points_per_object[po].size()),
            [&](tbb::blocked_range<size_t> r) {
                for (size_t layer_idx = r.begin(); layer_idx < r.end(); ++layer_idx) {
                    for (auto &perimeter_point : m_perimeter_points_per_object[po][layer_idx]) {
                        perimeter_point.visibility = global_model_info.calculate_point_visibility(
                                perimeter_point.position);
                    }
                }
            });
}

void SeamPlacer::calculate_overhangs(const PrintObject *po) {
    using namespace SeamPlacerImpl;

    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_perimeter_points_per_object[po].size()),
            [&](tbb::blocked_range<size_t> r) {
                for (size_t layer_idx = r.begin(); layer_idx < r.end(); ++layer_idx) {
                    for (SeamCandidate &perimeter_point : m_perimeter_points_per_object[po][layer_idx]) {
                        const auto calculate_layer_overhang = [&](size_t other_layer_idx) {
                            size_t closest_supporter = find_closest_point(
                                    *m_perimeter_points_trees_per_object[po][other_layer_idx],
                                    perimeter_point.position);
                            const SeamCandidate &supporter_point =
                                    m_perimeter_points_per_object[po][other_layer_idx][closest_supporter];

                            auto prev_next = find_previous_and_next_perimeter_point(m_perimeter_points_per_object[po][other_layer_idx], closest_supporter);
                            const SeamCandidate &prev_point =
                                    m_perimeter_points_per_object[po][other_layer_idx][prev_next.first];
                            const SeamCandidate &next_point =
                                    m_perimeter_points_per_object[po][other_layer_idx][prev_next.second];

                            return calculate_overhang(perimeter_point, prev_point,
                                    supporter_point, next_point);
                        };

                        if (layer_idx > 0) { //calculate overhang
                            perimeter_point.overhang = calculate_layer_overhang(layer_idx-1);
                        }
                    }
                }
            });
        }

// Estimates, if there is good seam point in the layer_idx which is close to last_point_pos
// uses comparator.is_first_not_much_worse method to compare current seam with the closest point
// (if current seam is too far away )
// If the current chosen stream is close enough, it is stored in seam_string. returns true and updates last_point_pos
// If the closest point is good enough to replace current chosen seam, it is stored in potential_string_seams, returns true and updates last_point_pos
// Otherwise does nothing, returns false
// sadly cannot be const because map access operator[] is not const, since it can create new object
bool SeamPlacer::find_next_seam_in_layer(const PrintObject *po,
        std::pair<size_t, size_t> &last_point_indexes,
        size_t layer_idx, const SeamPlacerImpl::SeamComparator &comparator,
        std::vector<std::pair<size_t, size_t>> &seam_string) {
    using namespace SeamPlacerImpl;

    const SeamCandidate &last_point =
            m_perimeter_points_per_object[po][last_point_indexes.first][last_point_indexes.second];

    Vec3f projected_position { last_point.position.x(), last_point.position.y(), float(
            po->get_layer(layer_idx)->slice_z) };
    //find closest point in next layer
    size_t closest_point_index = find_closest_point(
            *m_perimeter_points_trees_per_object[po][layer_idx], projected_position);

    SeamCandidate &closest_point = m_perimeter_points_per_object[po][layer_idx][closest_point_index];

    if (closest_point.perimeter->finalized) { //already finalized, skip
        return false;
    }

    //from the closest point, deduce index of seam in the next layer
    SeamCandidate &next_layer_seam =
            m_perimeter_points_per_object[po][layer_idx][closest_point.perimeter->seam_index];

    auto are_similar = [&](const SeamCandidate &a, const SeamCandidate &b) {
        return comparator.is_first_not_much_worse(a, b) && comparator.is_first_not_much_worse(b, a);
    };

    if ((closest_point.position - projected_position).norm() < SeamPlacer::seam_align_tolerable_dist
            && comparator.is_first_not_much_worse(closest_point, next_layer_seam)
            && are_similar(last_point, closest_point)) {
        seam_string.push_back({ layer_idx, closest_point_index });
        last_point_indexes = std::pair<size_t, size_t> { layer_idx, closest_point_index };
        return true;
    } else {
        return false;
    }

}

// clusters already chosen seam points into strings across multiple layers, and then
// aligns the strings via polynomial fit
// Does not change the positions of the SeamCandidates themselves, instead stores
// the new aligned position into the shared Perimeter structure of each perimeter
// Note that this position does not necesarilly lay on the perimeter.
void SeamPlacer::align_seam_points(const PrintObject *po, const SeamPlacerImpl::SeamComparator &comparator) {
    using namespace SeamPlacerImpl;

    // Prepares Debug files for writing.
#ifdef DEBUG_FILES
    Slic3r::CNumericLocalesSetter locales_setter;
    auto clusters_f = debug_out_path(("seam_clusters_of_" + std::to_string(po->id().id) + ".obj").c_str());
    FILE *clusters = boost::nowide::fopen(clusters_f.c_str(), "w");
    if (clusters == nullptr) {
        BOOST_LOG_TRIVIAL(error)
        << "stl_write_obj: Couldn't open " << clusters_f << " for writing";
        return;
    }
    auto aligned_f = debug_out_path(("aligned_clusters_of_" + std::to_string(po->id().id) + ".obj").c_str());
    FILE *aligns = boost::nowide::fopen(aligned_f.c_str(), "w");
    if (aligns == nullptr) {
        BOOST_LOG_TRIVIAL(error)
        << "stl_write_obj: Couldn't open " << clusters_f << " for writing";
        return;
    }
#endif

    //gather vector of all seams on the print_object - pair of layer_index and seam__index within that layer
    std::vector<std::pair<size_t, size_t>> seams;
    for (size_t layer_idx = 0; layer_idx < m_perimeter_points_per_object[po].size(); ++layer_idx) {
        std::vector<SeamCandidate> &layer_perimeter_points =
                m_perimeter_points_per_object[po][layer_idx];
        size_t current_point_index = 0;
        while (current_point_index < layer_perimeter_points.size()) {
            seams.emplace_back(layer_idx, layer_perimeter_points[current_point_index].perimeter->seam_index);
            current_point_index = layer_perimeter_points[current_point_index].perimeter->end_index + 1;
        }
    }

    //sort them before alignment. Alignment is sensitive to intitializaion, this gives it better chance to choose something nice
    std::sort(seams.begin(), seams.end(),
            [&](const std::pair<size_t, size_t> &left, const std::pair<size_t, size_t> &right) {
                return comparator.is_first_better(m_perimeter_points_per_object[po][left.first][left.second],
                        m_perimeter_points_per_object[po][right.first][right.second]);
            }
    );

    //align the seam points - start with the best, and check if they are aligned, if yes, skip, else start alignment
    for (const std::pair<size_t, size_t> &seam : seams) {
        size_t layer_idx = seam.first;
        size_t seam_index = seam.second;
        std::vector<SeamCandidate> &layer_perimeter_points =
                m_perimeter_points_per_object[po][layer_idx];
        if (layer_perimeter_points[seam_index].perimeter->finalized) {
            // This perimeter is already aligned, skip seam
            continue;
        } else {

            //initialize searching for seam string - cluster of nearby seams on previous and next layers
            int skips = SeamPlacer::seam_align_tolerable_skips / 2;
            int next_layer = layer_idx + 1;
            std::pair<size_t, size_t> last_point_indexes = std::pair<size_t, size_t>(layer_idx, seam_index);

            std::vector<std::pair<size_t, size_t>> seam_string { std::pair<size_t, size_t>(layer_idx, seam_index) };

            //find seams or potential seams in forward direction; there is a budget of skips allowed
            while (skips >= 0 && next_layer < int(m_perimeter_points_per_object[po].size())) {
                if (find_next_seam_in_layer(po, last_point_indexes, next_layer, comparator, seam_string)) {
                    //String added, last_point_pos updated, nothing to be done
                } else {
                    // Layer skipped, reduce number of available skips
                    skips--;
                }
                next_layer++;
            }

            //do additional check in back direction
            next_layer = layer_idx - 1;
            skips = SeamPlacer::seam_align_tolerable_skips / 2;
            last_point_indexes = std::pair<size_t, size_t>(layer_idx, seam_index);
            while (skips >= 0 && next_layer >= 0) {
                if (find_next_seam_in_layer(po, last_point_indexes, next_layer, comparator,seam_string)) {
                    //String added, last_point_pos updated, nothing to be done
                } else {
                    // Layer skipped, reduce number of available skips
                    skips--;
                }
                next_layer--;
            }

            if (seam_string.size() < seam_align_minimum_string_seams) {
                //string NOT long enough to be worth aligning, skip
                continue;
            }

            // String is long engouh, all string seams and potential string seams gathered, now do the alignment
            //sort by layer index
            std::sort(seam_string.begin(), seam_string.end(),
                    [](const std::pair<size_t, size_t> &left, const std::pair<size_t, size_t> &right) {
                        return left.first < right.first;
                    });

            // gather all positions of seams and their weights (weights are derived as negative penalty, they are made positive in next step)
            std::vector<Vec3f> points(seam_string.size());
            std::vector<float> weights(seam_string.size());

            //init min_weight by the first point
            float min_weight = -comparator.get_penalty(
                    m_perimeter_points_per_object[po][seam_string[0].first][seam_string[0].second]);

            // In the sorted seam_string array, point which started the alignment - the best candidate
            size_t best_candidate_point_index = 0;

            //gather points positions and weights, update min_weight in each step, and find the best candidate
            for (size_t index = 0; index < seam_string.size(); ++index) {
                points[index] =
                        m_perimeter_points_per_object[po][seam_string[index].first][seam_string[index].second].position;
                weights[index] = -comparator.get_penalty(
                        m_perimeter_points_per_object[po][seam_string[index].first][seam_string[index].second]);
                min_weight = std::min(min_weight, weights[index]);
                // find the best candidate by comparing the layer indexes
                if (seam_string[index].first == layer_idx) {
                    best_candidate_point_index = index;
                }
            }

            //makes all weights positive
            for (float &w : weights) {
                w = w - min_weight + 0.01;
            }

            //NOTE: the following commented block does polynomial line fitting of the seam string.
            // pre-smoothen by Laplace
//            for (size_t iteration = 0; iteration < SeamPlacer::seam_align_laplace_smoothing_iterations; ++iteration) {
//                std::vector<Vec3f> new_points(seam_string.size());
//                for (int point_index = 0; point_index < points.size(); ++point_index) {
//                    size_t prev_idx = point_index > 0 ? point_index - 1 : point_index;
//                    size_t next_idx = point_index < points.size() - 1 ? point_index + 1 : point_index;
//
//                    new_points[point_index] = (points[prev_idx] * weights[prev_idx]
//                            + points[point_index] * weights[point_index] + points[next_idx] * weights[next_idx]) /
//                            (weights[prev_idx] + weights[point_index] + weights[next_idx]);
//                }
//                points = new_points;
//            }
//
            // find coefficients of polynomial fit. Z coord is treated as parameter along which to fit both X and Y coords.
//            std::vector<Vec2f> coefficients = polyfit(points, weights, 4);
//
//            // Do alignment - compute fitted point for each point in the string from its Z coord, and store the position into
//            // Perimeter structure of the point; also set flag aligned to true
//            for (const auto &pair : seam_string) {
//                float current_height = m_perimeter_points_per_object[po][pair.first][pair.second].position.z();
//                Vec3f seam_pos = get_fitted_point(coefficients, current_height);
//
//                Perimeter *perimeter =
//                        m_perimeter_points_per_object[po][pair.first][pair.second].perimeter.get();
//                perimeter->final_seam_position = seam_pos;
//                perimeter->finalized = true;
//            }
//
//            for (Vec3f &p : points) {
//                p = get_fitted_point(coefficients, p.z());
//            }

            // LaPlace smoothing iterations over the gathered points. New positions from each iteration are stored in the new_points vector
            // and assigned to points at the end of iteration
            for (size_t iteration = 0; iteration < SeamPlacer::seam_align_laplace_smoothing_iterations; ++iteration) {
                std::vector<Vec3f> new_points(seam_string.size());
                // start from the best candidate, and smoothen down
                for (int point_index = best_candidate_point_index; point_index >= 0; --point_index) {
                    int prev_idx = point_index > 0 ? point_index - 1 : point_index;
                    size_t next_idx = point_index < int(points.size()) - 1 ? point_index + 1 : point_index;

                    new_points[point_index] = (points[prev_idx] * weights[prev_idx]
                            + points[point_index] * weights[point_index] + points[next_idx] * weights[next_idx]) /
                            (weights[prev_idx] + weights[point_index] + weights[next_idx]);
                }
                // smoothen up the rest of the points
                for (size_t point_index = best_candidate_point_index + 1; point_index < points.size(); ++point_index) {
                    size_t prev_idx = point_index > 0 ? point_index - 1 : point_index;
                    size_t next_idx = point_index < points.size() - 1 ? point_index + 1 : point_index;

                    new_points[point_index] = (points[prev_idx] * weights[prev_idx]
                            + points[point_index] * weights[point_index] + points[next_idx] * weights[next_idx]) /
                            (weights[prev_idx] + weights[point_index] + weights[next_idx]);
                }
                points = new_points;
            }

            // Assign smoothened posiiton to each participating perimeter and set finalized flag
            for (size_t index = 0; index < seam_string.size(); ++index) {
                Perimeter *perimeter =
                        m_perimeter_points_per_object[po][seam_string[index].first][seam_string[index].second].perimeter.get();
                perimeter->final_seam_position = points[index];
                perimeter->finalized = true;
            }

#ifdef DEBUG_FILES
            auto randf = []() {
                return float(rand()) / float(RAND_MAX);
            };
            Vec3f color { randf(), randf(), randf() };
            for (size_t i = 0; i < seam_string.size(); ++i) {
                auto orig_seam = m_perimeter_points_per_object[po][seam_string[i].first][seam_string[i].second];
                fprintf(clusters, "v %f %f %f %f %f %f \n", orig_seam.position[0],
                        orig_seam.position[1],
                        orig_seam.position[2], color[0], color[1],
                        color[2]);
            }

            color = Vec3f { randf(), randf(), randf() };
            for (size_t i = 0; i < seam_string.size(); ++i) {
                Perimeter *perimeter =
                        m_perimeter_points_per_object[po][seam_string[i].first][seam_string[i].second].perimeter.get();
                fprintf(aligns, "v %f %f %f %f %f %f \n", perimeter->final_seam_position[0],
                        perimeter->final_seam_position[1],
                        perimeter->final_seam_position[2], color[0], color[1],
                        color[2]);
            }
#endif
        }
    }

#ifdef DEBUG_FILES
    fclose(clusters);
    fclose(aligns);
#endif

}

void SeamPlacer::init(const Print &print) {
    using namespace SeamPlacerImpl;
    m_perimeter_points_trees_per_object.clear();
    m_perimeter_points_per_object.clear();

    for (const PrintObject *po : print.objects()) {

        SeamPosition configured_seam_preference = po->config().seam_position.value;
        SeamComparator comparator { configured_seam_preference };

        GlobalModelInfo global_model_info { };
        gather_enforcers_blockers(global_model_info, po);

        if (configured_seam_preference == spAligned || configured_seam_preference == spNearest) {
            compute_global_occlusion(global_model_info, po);
        }

        BOOST_LOG_TRIVIAL(debug)
        << "SeamPlacer: gather_seam_candidates: start";
        gather_seam_candidates(po, global_model_info);
        BOOST_LOG_TRIVIAL(debug)
        << "SeamPlacer: gather_seam_candidates: end";

        if (configured_seam_preference == spAligned || configured_seam_preference == spNearest) {
            BOOST_LOG_TRIVIAL(debug)
            << "SeamPlacer: calculate_candidates_visibility : start";
            calculate_candidates_visibility(po, global_model_info);
            BOOST_LOG_TRIVIAL(debug)
            << "SeamPlacer: calculate_candidates_visibility : end";
        }

        BOOST_LOG_TRIVIAL(debug)
        << "SeamPlacer: calculate_overhangs : start";
        calculate_overhangs(po);
        BOOST_LOG_TRIVIAL(debug)
        << "SeamPlacer: calculate_overhangs : end";

        BOOST_LOG_TRIVIAL(debug)
        << "SeamPlacer: pick_seam_point : start";
        //pick seam point
        tbb::parallel_for(tbb::blocked_range<size_t>(0, m_perimeter_points_per_object[po].size()),
                [&](tbb::blocked_range<size_t> r) {
                    for (size_t layer_idx = r.begin(); layer_idx < r.end(); ++layer_idx) {
                        std::vector<SeamCandidate> &layer_perimeter_points =
                                m_perimeter_points_per_object[po][layer_idx];
                        size_t current = 0;
                        while (current < layer_perimeter_points.size()) {
                            if (configured_seam_preference == spRandom) {
                                pick_random_seam_point(layer_perimeter_points, current);
                            } else {
                                pick_seam_point(layer_perimeter_points, current, comparator);
                            }
                            current = layer_perimeter_points[current].perimeter->end_index + 1;
                        }
                    }
                });
        BOOST_LOG_TRIVIAL(debug)
        << "SeamPlacer: pick_seam_point : end";

        if (configured_seam_preference == spAligned) {
            BOOST_LOG_TRIVIAL(debug)
            << "SeamPlacer: align_seam_points : start";
            align_seam_points(po, comparator);
            BOOST_LOG_TRIVIAL(debug)
            << "SeamPlacer: align_seam_points : end";
        }

#ifdef DEBUG_FILES
        debug_export_points(m_perimeter_points_per_object[po], po->bounding_box(), std::to_string(po->id().id),
                comparator);
#endif
    }
}

void SeamPlacer::place_seam(const Layer *layer, ExtrusionLoop &loop, bool external_first, const Point &last_pos) const {
    using namespace SeamPlacerImpl;
    const PrintObject *po = layer->object();
//NOTE this is necessary, since layer->id() is quite unreliable
    size_t layer_index = std::max(0, int(layer->id()) - int(po->slicing_parameters().raft_layers()));
    double unscaled_z = layer->slice_z;

    const auto &perimeter_points_tree = *m_perimeter_points_trees_per_object.find(po)->second[layer_index];
    const auto &perimeter_points = m_perimeter_points_per_object.find(po)->second[layer_index];

    const Point &fp = loop.first_point();

    Vec2f unscaled_p = unscale(fp).cast<float>();
    size_t closest_perimeter_point_index = find_closest_point(perimeter_points_tree,
            Vec3f { unscaled_p.x(), unscaled_p.y(), float(unscaled_z) });
    const Perimeter *perimeter = perimeter_points[closest_perimeter_point_index].perimeter.get();

    size_t seam_index;
    if (po->config().seam_position == spNearest) {
        seam_index = pick_nearest_seam_point_index(perimeter_points, perimeter->start_index,
                unscale(last_pos).cast<float>());
    } else {
        seam_index = perimeter->seam_index;
    }

    Vec3f seam_position = perimeter_points[seam_index].position;
    if (perimeter->finalized) {
        seam_position = perimeter->final_seam_position;
    }
    Point seam_point = scaled(Vec2d { seam_position.x(), seam_position.y() });

    if (!loop.split_at_vertex(seam_point))
// The point is not in the original loop.
// Insert it.
        loop.split_at(seam_point, true);
}

} // namespace Slic3r
