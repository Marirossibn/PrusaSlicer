#include <catch2/catch.hpp>
#include <test_utils.hpp>

#include <libslic3r/QuadricEdgeCollapse.hpp>
#include <libslic3r/TriangleMesh.hpp> // its - indexed_triangle_set
#include <libslic3r/SimplifyMesh.hpp> // no priority queue

#include "libslic3r/AABBTreeIndirect.hpp" // is similar

using namespace Slic3r;

namespace Private {

struct Similarity
{
    float max_distance = 0.f;
    float average_distance = 0.f;

    Similarity() = default;
    Similarity(float max_distance, float average_distance)
        : max_distance(max_distance), average_distance(average_distance)
    {}
};

// border for our algorithm with frog_leg model and decimation to 5%
Similarity frog_leg_5(0.32f, 0.043f);

Similarity get_similarity(const indexed_triangle_set &from,
                             const indexed_triangle_set &to)
{
    // create ABBTree
    auto tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(
        from.vertices, from.indices);
    float sum_distance = 0.f;
    
    float max_distance = 0.f;
    auto collect_distances = [&](const Vec3f &surface_point) {
        size_t hit_idx;
        Vec3f  hit_point;
        float  distance2 =
            AABBTreeIndirect::squared_distance_to_indexed_triangle_set(
                from.vertices, from.indices, tree, surface_point, hit_idx,
                hit_point);
        float distance = sqrt(distance2);
        if (max_distance < distance) max_distance = distance;
        sum_distance += distance;
    };

    for (const Vec3f &vertex : to.vertices) { collect_distances(vertex); }
    for (const Vec3i &t : to.indices) {
        Vec3f center(0, 0, 0);
        for (size_t i = 0; i < 3; ++i) { center += to.vertices[t[i]] / 3; }
        collect_distances(center);
    }

    size_t count = to.vertices.size() + to.indices.size();
    float average_distance = sum_distance / count;

    std::cout << "max_distance = " << max_distance << ", average_distance = " << average_distance << std::endl;
    return Similarity(max_distance, average_distance);
}

void is_better_similarity(const indexed_triangle_set &its_first,
                          const indexed_triangle_set &its_second,
                          const Similarity &          compare)
{
    Similarity s1 = get_similarity(its_first, its_second);
    Similarity s2 = get_similarity(its_second, its_first);

    CHECK(s1.average_distance < compare.average_distance);
    CHECK(s1.max_distance     < compare.max_distance);
    CHECK(s2.average_distance < compare.average_distance);
    CHECK(s2.max_distance     < compare.max_distance);
}

void is_worse_similarity(const indexed_triangle_set &its_first,
                         const indexed_triangle_set &its_second,
                         const Similarity &          compare)
{
    Similarity s1 = get_similarity(its_first, its_second);
    Similarity s2 = get_similarity(its_second, its_first);

    if (s1.max_distance < compare.max_distance &&
        s2.max_distance < compare.max_distance)
        CHECK(false);
}
    
bool exist_triangle_with_twice_vertices(const std::vector<stl_triangle_vertex_indices> &indices)
{
    for (const auto &face : indices)
        if (face[0] == face[1] || face[0] == face[2] || face[1] == face[2])
            return true;
    return false;
}

} // namespace Private

TEST_CASE("Reduce one edge by Quadric Edge Collapse", "[its]")
{
    indexed_triangle_set its;
    its.vertices = {Vec3f(-1.f, 0.f, 0.f), Vec3f(0.f, 1.f, 0.f),
                    Vec3f(1.f, 0.f, 0.f), Vec3f(0.f, 0.f, 1.f),
                    // vertex to be removed
                    Vec3f(0.9f, .1f, -.1f)};
    its.indices  = {Vec3i(1, 0, 3), Vec3i(2, 1, 3), Vec3i(0, 2, 3),
                   Vec3i(0, 1, 4), Vec3i(1, 2, 4), Vec3i(2, 0, 4)};
    // edge to remove is between vertices 2 and 4 on trinagles 4 and 5

    indexed_triangle_set its_ = its; // copy
    // its_write_obj(its, "tetrhedron_in.obj");
    uint32_t wanted_count = its.indices.size() - 1;
    its_quadric_edge_collapse(its, wanted_count);
    // its_write_obj(its, "tetrhedron_out.obj");
    CHECK(its.indices.size() == 4);
    CHECK(its.vertices.size() == 4);

    for (size_t i = 0; i < 3; i++) {
        CHECK(its.indices[i] == its_.indices[i]);
    }

    for (size_t i = 0; i < 4; i++) {
        if (i == 2) continue;
        CHECK(its.vertices[i] == its_.vertices[i]);
    }

    const Vec3f &v  = its.vertices[2];  // new vertex
    const Vec3f &v2 = its_.vertices[2]; // moved vertex
    const Vec3f &v4 = its_.vertices[4]; // removed vertex
    for (size_t i = 0; i < 3; i++) {
        bool is_between = (v[i] < v4[i] && v[i] > v2[i]) ||
                          (v[i] > v4[i] && v[i] < v2[i]);
        CHECK(is_between);
    }
    Private::Similarity max_similarity(0.75f, 0.014f);
    Private::is_better_similarity(its, its_, max_similarity);
}

TEST_CASE("Simplify frog_legs.obj to 5% by Quadric edge collapse", "[its][quadric_edge_collapse]")
{
    TriangleMesh mesh            = load_model("frog_legs.obj");
    double       original_volume = its_volume(mesh.its);
    uint32_t     wanted_count    = mesh.its.indices.size() * 0.05;
    REQUIRE_FALSE(mesh.empty());
    indexed_triangle_set its       = mesh.its; // copy
    float                max_error = std::numeric_limits<float>::max();
    its_quadric_edge_collapse(its, wanted_count, &max_error);
    // its_write_obj(its, "frog_legs_qec.obj");
    CHECK(its.indices.size() <= wanted_count);
    double volume = its_volume(its);
    CHECK(fabs(original_volume - volume) < 33.);

    Private::is_better_similarity(mesh.its, its, Private::frog_leg_5);
}

#include <libigl/igl/qslim.h>
#include "Simplify.h"
TEST_CASE("Simplify frog_legs.obj to 5% by IGL/qslim", "[]")
{
    std::string  obj_filename    = "frog_legs.obj";
    TriangleMesh mesh            = load_model(obj_filename);
    REQUIRE_FALSE(mesh.empty());
    indexed_triangle_set &its = mesh.its;
    double       original_volume = its_volume(its);
    uint32_t     wanted_count    = its.indices.size() * 0.05;
    
    Eigen::MatrixXd V(its.vertices.size(), 3);
    Eigen::MatrixXi F(its.indices.size(), 3);
    for (size_t j = 0; j < its.vertices.size(); ++j) {
        Vec3d vd = its.vertices[j].cast<double>();
        for (int i = 0; i < 3; ++i) V(j, i) = vd(i);
    }

    for (size_t j = 0; j < its.indices.size(); ++j) {
        const auto &f = its.indices[j];
        for (int i = 0; i < 3; ++i) F(j, i) = f(i);
    }

    size_t max_m = wanted_count;
    Eigen::MatrixXd U;
    Eigen::MatrixXi G;
    Eigen::VectorXi J, I;
    CHECK(igl::qslim(V, F, max_m, U, G, J, I));

    // convert to its
    indexed_triangle_set its_out;
    its_out.vertices.reserve(U.size()/3);
    its_out.indices.reserve(G.size()/3);
    for (size_t i = 0; i < U.size()/3; i++)
        its_out.vertices.emplace_back(U(i, 0), U(i, 1), U(i, 2));
    for (size_t i = 0; i < G.size()/3; i++)
        its_out.indices.emplace_back(G(i, 0), G(i, 1), G(i, 2));

    // check if algorithm is still worse than our
    Private::is_worse_similarity(its_out, its, Private::frog_leg_5);
    // its_out, its --> avg_distance: 0.0351217, max_distance 0.364316
    // its, its_out --> avg_distance: 0.0412358, max_distance 0.238913
}

TEST_CASE("Simplify frog_legs.obj to 5% by simplify", "[]") {
    std::string obj_filename = "frog_legs.obj";
    TriangleMesh mesh = load_model(obj_filename);
    uint32_t     wanted_count = mesh.its.indices.size() * 0.05;
    Simplify::load_obj((TEST_DATA_DIR PATH_SEPARATOR + obj_filename).c_str());
    Simplify::simplify_mesh(wanted_count, 5, true);

     // convert to its
    indexed_triangle_set its_out;
    its_out.vertices.reserve(Simplify::vertices.size());
    its_out.indices.reserve(Simplify::triangles.size());
    for (size_t i = 0; i < Simplify::vertices.size(); i++) {
        const Simplify::Vertex &v = Simplify::vertices[i];
        its_out.vertices.emplace_back(v.p.x, v.p.y, v.p.z);
    }
    for (size_t i = 0; i < Simplify::triangles.size(); i++) {
        const Simplify::Triangle &t = Simplify::triangles[i];
        its_out.indices.emplace_back(t.v[0], t.v[1], t.v[2]);    
    }

    // check if algorithm is still worse than our
    Private::is_worse_similarity(its_out, mesh.its, Private::frog_leg_5);
    // its_out, mesh.its --> max_distance = 0.700494, average_distance = 0.0902524 
    // mesh.its, its_out --> max_distance = 0.393184, average_distance = 0.0537392
}

TEST_CASE("Simplify trouble case", "[its]")
{
    TriangleMesh tm = load_model("simplification.obj");
    REQUIRE_FALSE(tm.empty());
    float    max_error    = std::numeric_limits<float>::max();
    uint32_t wanted_count = 0;
    its_quadric_edge_collapse(tm.its, wanted_count, &max_error);
    CHECK(!Private::exist_triangle_with_twice_vertices(tm.its.indices));
}

TEST_CASE("Simplified cube should not be empty.", "[its]")
{
    auto     its          = its_make_cube(1, 2, 3);
    float    max_error    = std::numeric_limits<float>::max();
    uint32_t wanted_count = 0;
    its_quadric_edge_collapse(its, wanted_count, &max_error);
    CHECK(!its.indices.empty());
}
