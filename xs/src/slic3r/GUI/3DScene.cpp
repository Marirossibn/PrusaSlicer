#include "3DScene.hpp"

#include "../../libslic3r/libslic3r.h"
#include "../../libslic3r/ExtrusionEntity.hpp"
#include "../../libslic3r/ExtrusionEntityCollection.hpp"
#include "../../libslic3r/Geometry.hpp"
#include "../../libslic3r/Print.hpp"
#include "../../libslic3r/Slicing.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utility>
#include <assert.h>

#include <boost/log/trivial.hpp>

#include <tbb/parallel_for.h>

namespace Slic3r {

void GLIndexedVertexArray::load_mesh_flat_shading(const TriangleMesh &mesh)
{
    this->vertices_and_normals_interleaved.reserve(this->vertices_and_normals_interleaved.size() + 3 * 3 * 2 * mesh.facets_count());
    
    for (int i = 0; i < mesh.stl.stats.number_of_facets; ++ i) {
        const stl_facet &facet = mesh.stl.facet_start[i];
        for (int j = 0; j < 3; ++ j)
            this->push_geometry(facet.vertex[j].x, facet.vertex[j].y, facet.vertex[j].z, facet.normal.x, facet.normal.y, facet.normal.z);
    }
}

void GLVolume::set_range(double min_z, double max_z)
{
    this->qverts_range.first  = 0;
    this->qverts_range.second = this->indexed_vertex_array.quad_indices.size();
    this->tverts_range.first  = 0;
    this->tverts_range.second = this->indexed_vertex_array.triangle_indices.size();
    if (! this->print_zs.empty()) {
        // The Z layer range is specified.
        // First test whether the Z span of this object is not out of (min_z, max_z) completely.
        if (this->print_zs.front() > max_z || this->print_zs.back() < min_z) {
            this->qverts_range.second = 0;
            this->tverts_range.second = 0;
        } else {
            // Then find the lowest layer to be displayed.
            size_t i = 0;
            for (; i < this->print_zs.size() && this->print_zs[i] < min_z; ++ i);
            if (i == this->print_zs.size()) {
                // This shall not happen.
                this->qverts_range.second = 0;
                this->tverts_range.second = 0;
            } else {
                // Remember start of the layer.
                this->qverts_range.first = this->offsets[i * 2];
                this->tverts_range.first = this->offsets[i * 2 + 1];
                // Some layers are above $min_z. Which?
                for (; i < this->print_zs.size() && this->print_zs[i] <= max_z; ++ i);
                if (i < this->print_zs.size()) {
                    this->qverts_range.second = this->offsets[i * 2];
                    this->tverts_range.second = this->offsets[i * 2 + 1];
                }
            }
        }
    }
}

void GLVolume::generate_layer_height_texture(PrintObject *print_object, bool force)
{
    GLTexture *tex = this->layer_height_texture.get();
	if (tex == nullptr)
		// No layer_height_texture is assigned to this GLVolume, therefore the layer height texture cannot be filled.
		return;

	// Always try to update the layer height profile.
	bool update = print_object->update_layer_height_profile(print_object->model_object()->layer_height_profile) || force;
	// Update if the layer height profile was changed, or when the texture is not valid.
	if (! update && ! tex->data.empty() && tex->cells > 0)
        // Texture is valid, don't update.
        return; 

    if (tex->data.empty()) {
        tex->width  = 1024;
        tex->height = 1024;
        tex->levels = 2;
        tex->data.assign(tex->width * tex->height * 5, 0);
    }

    SlicingParameters slicing_params = print_object->slicing_parameters();
    bool level_of_detail_2nd_level = true;
    tex->cells = Slic3r::generate_layer_height_texture(
        slicing_params, 
        Slic3r::generate_object_layers(slicing_params, print_object->model_object()->layer_height_profile), 
        tex->data.data(), tex->height, tex->width, level_of_detail_2nd_level);
}

// 512x512 bitmaps are supported everywhere, but that may not be sufficent for super large print volumes.
#define LAYER_HEIGHT_TEXTURE_WIDTH  1024
#define LAYER_HEIGHT_TEXTURE_HEIGHT 1024

std::vector<int> GLVolumeCollection::load_object(
    const ModelObject       *model_object, 
    int                      obj_idx,
    const std::vector<int>  &instance_idxs,
    const std::string       &color_by,
    const std::string       &select_by,
    const std::string       &drag_by)
{
    static float colors[4][4] = {
        { 1.0f, 1.0f, 0.0f, 1.f }, 
        { 1.0f, 0.5f, 0.5f, 1.f },
        { 0.5f, 1.0f, 0.5f, 1.f }, 
        { 0.5f, 0.5f, 1.0f, 1.f }
    };

    // Object will have a single common layer height texture for all volumes.
    std::shared_ptr<GLTexture> layer_height_texture = std::make_shared<GLTexture>();
    
    std::vector<int> volumes_idx;
    for (int volume_idx = 0; volume_idx < int(model_object->volumes.size()); ++ volume_idx) {
        const ModelVolume *model_volume = model_object->volumes[volume_idx];
        for (int instance_idx : instance_idxs) {
            const ModelInstance *instance = model_object->instances[instance_idx];
            TriangleMesh mesh = model_volume->mesh;
            instance->transform_mesh(&mesh);
            volumes_idx.push_back(int(this->volumes.size()));
            float color[4];
            memcpy(color, colors[((color_by == "volume") ? volume_idx : obj_idx) % 4], sizeof(float) * 3);
            color[3] = model_volume->modifier ? 0.5f : 1.f;
            this->volumes.emplace_back(new GLVolume(color));
            GLVolume &v = *this->volumes.back();
			v.indexed_vertex_array.load_mesh_flat_shading(mesh);
            v.bounding_box = v.indexed_vertex_array.bounding_box();
            v.composite_id = obj_idx * 1000000 + volume_idx * 1000 + instance_idx;
            if (select_by == "object")
                v.select_group_id = obj_idx * 1000000;
            else if (select_by == "volume")
                v.select_group_id = obj_idx * 1000000 + volume_idx * 1000;
            else if (select_by == "instance")
                v.select_group_id = v.composite_id;
            if (drag_by == "object")
                v.drag_group_id = obj_idx * 1000;
            else if (drag_by == "instance")
                v.drag_group_id = obj_idx * 1000 + instance_idx;
            if (! model_volume->modifier)
                v.layer_height_texture = layer_height_texture;
        }
    }
    
    return volumes_idx; 
}

// caller is responsible for supplying NO lines with zero length
static void thick_lines_to_indexed_vertex_array(
    const Lines                 &lines, 
    const std::vector<double>   &widths,
    const std::vector<double>   &heights, 
    bool                         closed,
    double                       top_z,
    GLIndexedVertexArray        &volume)
{
    assert(! lines.empty());
    if (lines.empty())
        return;

#define LEFT    0
#define RIGHT   1
#define TOP     2
#define BOTTOM  3

    Line prev_line;
    // right, left, top, bottom
    int     idx_prev[4]      = { -1, -1, -1, -1 };
    double  width_prev       = 0.;
    double  bottom_z_prev    = 0.;
    Pointf  b1_prev;
    Pointf  b2_prev;
    Vectorf v_prev;
    int     idx_initial[4]   = { -1, -1, -1, -1 };
    double  width_initial    = 0.;
    double  bottom_z_initial = 0.;

    // loop once more in case of closed loops
    size_t lines_end = closed ? (lines.size() + 1) : lines.size();
    for (size_t ii = 0; ii < lines_end; ++ ii) {
        size_t i = (ii == lines.size()) ? 0 : ii;
        const Line &line = lines[i];
        double len = unscale(line.length());
        double bottom_z = top_z - heights[i];
        double middle_z = (top_z + bottom_z) / 2.;
        double width = widths[i];
        
        Vectorf v = Vectorf::new_unscale(line.vector());
        v.scale(1. / len);
        
        Pointf a = Pointf::new_unscale(line.a);
        Pointf b = Pointf::new_unscale(line.b);
        Pointf a1 = a;
        Pointf a2 = a;
        Pointf b1 = b;
        Pointf b2 = b;
        {
            double dist = width / 2.;  // scaled
            a1.translate(+dist*v.y, -dist*v.x);
            a2.translate(-dist*v.y, +dist*v.x);
            b1.translate(+dist*v.y, -dist*v.x);
            b2.translate(-dist*v.y, +dist*v.x);
        }

        // calculate new XY normals
        Vector n = line.normal();
        Vectorf3 xy_right_normal = Vectorf3::new_unscale(n.x, n.y, 0);
        xy_right_normal.scale(1.f / len);

        int idx_a[4];
        int idx_b[4];
        int idx_last = int(volume.vertices_and_normals_interleaved.size() / 6);

        bool width_different    = width_prev != width;
        bool bottom_z_different = bottom_z_prev != bottom_z;
        width_prev    = width;
        bottom_z_prev = bottom_z;

        // Share top / bottom vertices if possible.
        if (ii == 0) {
            idx_a[TOP] = idx_last ++;
            volume.push_geometry(a.x, a.y, top_z   , 0., 0.,  1.); 
        } else {
            idx_a[TOP] = idx_prev[TOP];
        }
        if (ii == 0 || bottom_z_different) {
            idx_a[BOTTOM] = idx_last ++;
            volume.push_geometry(a.x, a.y, bottom_z, 0., 0., -1.);
        } else {
            idx_a[BOTTOM] = idx_prev[BOTTOM];
        }

        bool sharp = true;
        if (ii == 0) {
            // Start of the 1st line segment.
            idx_a[LEFT ] = idx_last ++;
            volume.push_geometry(a2.x, a2.y, middle_z, -xy_right_normal.x, -xy_right_normal.y, -xy_right_normal.z);
            idx_a[RIGHT] = idx_last ++;
            volume.push_geometry(a1.x, a1.y, middle_z, xy_right_normal.x, xy_right_normal.y, xy_right_normal.z);
            width_initial    = width;
            bottom_z_initial = bottom_z;
            memcpy(idx_initial, idx_a, sizeof(int) * 4);
        } else {
            // Continuing a previous segment.
            // Share left / right vertices if possible.
			double v_dot    = dot(v_prev, v);
            bool   sharp    = v_dot < 0.707; // sin(45 degrees)
            if (sharp) {
                // Allocate new left / right points for the start of this segment as these points will receive their own normals to indicate a sharp turn.
                idx_a[RIGHT] = idx_last ++;
                volume.push_geometry(a1.x, a1.y, middle_z, xy_right_normal.x, xy_right_normal.y, xy_right_normal.z);
                idx_a[LEFT ] = idx_last ++;
                volume.push_geometry(a2.x, a2.y, middle_z, -xy_right_normal.x, -xy_right_normal.y, -xy_right_normal.z);
            }
            if (v_dot > 0.9) {
                // The two successive segments are nearly collinear.
                idx_a[LEFT ] = idx_prev[LEFT];
                idx_a[RIGHT] = idx_prev[RIGHT];
            } else if (! sharp) {
                // Create a sharp corner with an overshot and average the left / right normals.
                // At the crease angle of 45 degrees, the overshot at the corner will be less than (1-1/cos(PI/8)) = 8.2% over an arc.
                Pointf intersection;
                Geometry::ray_ray_intersection(b1_prev, v_prev, a1, v, intersection);
                a1 = intersection;
                a2 = 2. * a - intersection;
                assert(length(a1.vector_to(a)) < width);
                assert(length(a2.vector_to(a)) < width);
                float *n_left_prev  = volume.vertices_and_normals_interleaved.data() + idx_prev[LEFT ] * 6;
                float *p_left_prev  = n_left_prev  + 3;
                float *n_right_prev = volume.vertices_and_normals_interleaved.data() + idx_prev[RIGHT] * 6;
                float *p_right_prev = n_right_prev + 3;
                p_left_prev [0] = float(a2.x);
                p_left_prev [1] = float(a2.y);
                p_right_prev[0] = float(a1.x);
                p_right_prev[1] = float(a1.y);
                xy_right_normal.x += n_right_prev[0];
                xy_right_normal.y += n_right_prev[1];
                xy_right_normal.scale(1. / length(xy_right_normal));
                n_left_prev [0] = float(-xy_right_normal.x);
                n_left_prev [1] = float(-xy_right_normal.y);
                n_right_prev[0] = float( xy_right_normal.x);
                n_right_prev[1] = float( xy_right_normal.y);
                idx_a[LEFT ] = idx_prev[LEFT ];
                idx_a[RIGHT] = idx_prev[RIGHT];
            } else if (cross(v_prev, v) > 0.) {
                // Right turn. Fill in the right turn wedge.
                volume.triangle_indices.push_back(idx_prev[RIGHT]);
                volume.triangle_indices.push_back(idx_a   [RIGHT]);
                volume.triangle_indices.push_back(idx_prev[TOP]);
                volume.triangle_indices.push_back(idx_prev[RIGHT]);
                volume.triangle_indices.push_back(idx_prev[BOTTOM]);
                volume.triangle_indices.push_back(idx_a   [RIGHT]);
            } else {
                // Left turn. Fill in the left turn wedge.
                volume.triangle_indices.push_back(idx_prev[LEFT]);
                volume.triangle_indices.push_back(idx_prev[TOP]);
                volume.triangle_indices.push_back(idx_a   [LEFT]);
                volume.triangle_indices.push_back(idx_prev[LEFT]);
                volume.triangle_indices.push_back(idx_a   [LEFT]);
                volume.triangle_indices.push_back(idx_prev[BOTTOM]);
            }
            if (ii == lines.size()) {
                if (! sharp) {
                    // Closing a loop with smooth transition. Unify the closing left / right vertices.
                    memcpy(volume.vertices_and_normals_interleaved.data() + idx_initial[LEFT ] * 6, volume.vertices_and_normals_interleaved.data() + idx_prev[LEFT ] * 6, sizeof(float) * 6);
                    memcpy(volume.vertices_and_normals_interleaved.data() + idx_initial[RIGHT] * 6, volume.vertices_and_normals_interleaved.data() + idx_prev[RIGHT] * 6, sizeof(float) * 6);
                    volume.vertices_and_normals_interleaved.erase(volume.vertices_and_normals_interleaved.end() - 12, volume.vertices_and_normals_interleaved.end());
                    // Replace the left / right vertex indices to point to the start of the loop. 
                    for (size_t u = volume.quad_indices.size() - 16; u < volume.quad_indices.size(); ++ u) {
                        if (volume.quad_indices[u] == idx_prev[LEFT])
                            volume.quad_indices[u] = idx_initial[LEFT];
                        else if (volume.quad_indices[u] == idx_prev[RIGHT])
                            volume.quad_indices[u] = idx_initial[RIGHT];
                    }
                }
                // This is the last iteration, only required to solve the transition.
                break;
            }
        }

        // Only new allocate top / bottom vertices, if not closing a loop.
        if (closed && ii + 1 == lines.size()) {
            idx_b[TOP] = idx_initial[TOP];
        } else {
            idx_b[TOP] = idx_last ++;
            volume.push_geometry(b.x, b.y, top_z   , 0., 0.,  1.);
        }
        if (closed && ii + 1 == lines.size() && width == width_initial) {
            idx_b[BOTTOM] = idx_initial[BOTTOM];
        } else {
            idx_b[BOTTOM] = idx_last ++;
            volume.push_geometry(b.x, b.y, bottom_z, 0., 0., -1.);
        }
        // Generate new vertices for the end of this line segment.
        idx_b[LEFT  ] = idx_last ++;
        volume.push_geometry(b2.x, b2.y, middle_z, -xy_right_normal.x, -xy_right_normal.y, -xy_right_normal.z);
        idx_b[RIGHT ] = idx_last ++;
        volume.push_geometry(b1.x, b1.y, middle_z, xy_right_normal.x, xy_right_normal.y, xy_right_normal.z);

        prev_line = line;
        memcpy(idx_prev, idx_b, 4 * sizeof(int));
        width_prev = width;
        bottom_z_prev = bottom_z;
        b1_prev = b1;
        b2_prev = b2;
        v_prev  = v;

        if (! closed) {
            // Terminate open paths with caps.
            if (i == 0) {
                volume.quad_indices.push_back(idx_a[BOTTOM]);
                volume.quad_indices.push_back(idx_a[RIGHT]);
                volume.quad_indices.push_back(idx_a[TOP]);
                volume.quad_indices.push_back(idx_a[LEFT]);
            }
            // We don't use 'else' because both cases are true if we have only one line.
            if (i + 1 == lines.size()) {
                volume.quad_indices.push_back(idx_b[BOTTOM]);
                volume.quad_indices.push_back(idx_b[LEFT]);
                volume.quad_indices.push_back(idx_b[TOP]);
                volume.quad_indices.push_back(idx_b[RIGHT]);
            }
        }
        
        // Add quads for a straight hollow tube-like segment.
        // bottom-right face
        volume.quad_indices.push_back(idx_a[BOTTOM]);
        volume.quad_indices.push_back(idx_b[BOTTOM]);
        volume.quad_indices.push_back(idx_b[RIGHT]);
        volume.quad_indices.push_back(idx_a[RIGHT]);
        // top-right face
        volume.quad_indices.push_back(idx_a[RIGHT]);
        volume.quad_indices.push_back(idx_b[RIGHT]);
        volume.quad_indices.push_back(idx_b[TOP]);
        volume.quad_indices.push_back(idx_a[TOP]);
        // top-left face
        volume.quad_indices.push_back(idx_a[TOP]);
        volume.quad_indices.push_back(idx_b[TOP]);
        volume.quad_indices.push_back(idx_b[LEFT]);
        volume.quad_indices.push_back(idx_a[LEFT]);
        // bottom-left face
        volume.quad_indices.push_back(idx_a[LEFT]);
        volume.quad_indices.push_back(idx_b[LEFT]);
        volume.quad_indices.push_back(idx_b[BOTTOM]);
        volume.quad_indices.push_back(idx_a[BOTTOM]);
    }

#undef LEFT
#undef RIGHT
#undef TOP
#undef BOTTOM
}

static void thick_lines_to_verts(
    const Lines                 &lines, 
    const std::vector<double>   &widths,
    const std::vector<double>   &heights, 
    bool                         closed,
    double                       top_z,
    GLVolume                    &volume)
{
    thick_lines_to_indexed_vertex_array(lines, widths, heights, closed, top_z, volume.indexed_vertex_array);
}

// Fill in the qverts and tverts with quads and triangles for the extrusion_path.
static inline void extrusionentity_to_verts(const ExtrusionPath &extrusion_path, float print_z, const Point &copy, GLVolume &volume)
{
    Polyline            polyline = extrusion_path.polyline;
    polyline.remove_duplicate_points();
    polyline.translate(copy);
    Lines               lines = polyline.lines();
    std::vector<double> widths(lines.size(), extrusion_path.width);
    std::vector<double> heights(lines.size(), extrusion_path.height);
    thick_lines_to_verts(lines, widths, heights, false, print_z, volume);
}

// Fill in the qverts and tverts with quads and triangles for the extrusion_loop.
static inline void extrusionentity_to_verts(const ExtrusionLoop &extrusion_loop, float print_z, const Point &copy, GLVolume &volume)
{
    Lines               lines;
    std::vector<double> widths;
    std::vector<double> heights;
    for (const ExtrusionPath &extrusion_path : extrusion_loop.paths) {
        Polyline            polyline = extrusion_path.polyline;
        polyline.remove_duplicate_points();
        polyline.translate(copy);
        Lines lines_this = polyline.lines();
        append(lines, lines_this);
        widths.insert(widths.end(), lines_this.size(), extrusion_path.width);
        heights.insert(heights.end(), lines_this.size(), extrusion_path.height);
    }
    thick_lines_to_verts(lines, widths, heights, true, print_z, volume);
}

// Fill in the qverts and tverts with quads and triangles for the extrusion_multi_path.
static inline void extrusionentity_to_verts(const ExtrusionMultiPath &extrusion_multi_path, float print_z, const Point &copy, GLVolume &volume)
{
    Lines               lines;
    std::vector<double> widths;
    std::vector<double> heights;
    for (const ExtrusionPath &extrusion_path : extrusion_multi_path.paths) {
        Polyline            polyline = extrusion_path.polyline;
        polyline.remove_duplicate_points();
        polyline.translate(copy);
        Lines lines_this = polyline.lines();
        append(lines, lines_this);
        widths.insert(widths.end(), lines_this.size(), extrusion_path.width);
        heights.insert(heights.end(), lines_this.size(), extrusion_path.height);
    }
    thick_lines_to_verts(lines, widths, heights, false, print_z, volume);
}

static void extrusionentity_to_verts(const ExtrusionEntity *extrusion_entity, float print_z, const Point &copy, GLVolume &volume);

static inline void extrusionentity_to_verts(const ExtrusionEntityCollection &extrusion_entity_collection, float print_z, const Point &copy, GLVolume &volume)
{
    for (const ExtrusionEntity *extrusion_entity : extrusion_entity_collection.entities)
        extrusionentity_to_verts(extrusion_entity, print_z, copy, volume);
}

static void extrusionentity_to_verts(const ExtrusionEntity *extrusion_entity, float print_z, const Point &copy, GLVolume &volume)
{
    if (extrusion_entity != nullptr) {
        auto *extrusion_path = dynamic_cast<const ExtrusionPath*>(extrusion_entity);
        if (extrusion_path != nullptr)
            extrusionentity_to_verts(*extrusion_path, print_z, copy, volume);
        else {
            auto *extrusion_loop = dynamic_cast<const ExtrusionLoop*>(extrusion_entity);
            if (extrusion_loop != nullptr)
                extrusionentity_to_verts(*extrusion_loop, print_z, copy, volume);
            else {
                auto *extrusion_multi_path = dynamic_cast<const ExtrusionMultiPath*>(extrusion_entity);
                if (extrusion_multi_path != nullptr)
                    extrusionentity_to_verts(*extrusion_multi_path, print_z, copy, volume);
                else {
                    auto *extrusion_entity_collection = dynamic_cast<const ExtrusionEntityCollection*>(extrusion_entity);
                    if (extrusion_entity_collection != nullptr)
                        extrusionentity_to_verts(*extrusion_entity_collection, print_z, copy, volume);
                    else {
                        CONFESS("Unexpected extrusion_entity type in to_verts()");
                    }
                }
            }
        }
    }
}

// Create 3D thick extrusion lines for a skirt and brim.
// Adds a new Slic3r::GUI::3DScene::Volume to volumes.
void _3DScene::_load_print_toolpaths(
    const Print         *print, 
    GLVolumeCollection  *volumes,
    bool                 use_VBOs)
{
    if (! print->has_skirt() && print->config.brim_width.value == 0)
        return;
    
    const float color[] = { 0.5f, 1.0f, 0.5f, 1.f }; // greenish

    // number of skirt layers
    size_t total_layer_count = 0;
    for (const PrintObject *print_object : print->objects)
        total_layer_count = std::max(total_layer_count, print_object->total_layer_count());
    size_t skirt_height = print->has_infinite_skirt() ? 
        total_layer_count :
        std::min<size_t>(print->config.skirt_height.value, total_layer_count);
    if (skirt_height == 0 && print->config.brim_width.value > 0)
        skirt_height = 1;

    // get first skirt_height layers (maybe this should be moved to a PrintObject method?)
    const PrintObject *object0 = print->objects.front();
    std::vector<float> print_zs;
    print_zs.reserve(skirt_height * 2);
    for (size_t i = 0; i < std::min(skirt_height, object0->layers.size()); ++ i)
        print_zs.push_back(float(object0->layers[i]->print_z));
    //FIXME why there are support layers?
    for (size_t i = 0; i < std::min(skirt_height, object0->support_layers.size()); ++ i)
        print_zs.push_back(float(object0->support_layers[i]->print_z));
    std::sort(print_zs.begin(), print_zs.end());
    print_zs.erase(std::unique(print_zs.begin(), print_zs.end()), print_zs.end());
    if (print_zs.size() > skirt_height)
        print_zs.erase(print_zs.begin() + skirt_height, print_zs.end());
    
    volumes->volumes.emplace_back(new GLVolume(color));
    GLVolume &volume = *volumes->volumes.back();
    for (size_t i = 0; i < skirt_height; ++ i) {
        volume.print_zs.push_back(print_zs[i]);
        volume.offsets.push_back(volume.indexed_vertex_array.quad_indices.size());
        volume.offsets.push_back(volume.indexed_vertex_array.triangle_indices.size());
        if (i == 0)
            extrusionentity_to_verts(print->brim, print_zs[i], Point(0, 0), volume);
        extrusionentity_to_verts(print->skirt, print_zs[i], Point(0, 0), volume);
    }
    auto bb = print->bounding_box();
    volume.bounding_box.merge(Pointf3(unscale(bb.min.x), unscale(bb.min.y), 0.f));
    volume.bounding_box.merge(Pointf3(unscale(bb.max.x), unscale(bb.max.y), 0.f));
}

// Create 3D thick extrusion lines for object forming extrusions.
// Adds a new Slic3r::GUI::3DScene::Volume to $self->volumes,
// one for perimeters, one for infill and one for supports.
void _3DScene::_load_print_object_toolpaths(
    const PrintObject   *print_object,
    GLVolumeCollection  *volumes,
    bool                 use_VBOs)
{
    struct Ctxt
    {
        const Points                *shifted_copies;
        std::vector<const Layer*>    layers;
        // Bounding box of the object and its copies.
        BoundingBoxf3                bbox;
        bool                         has_perimeters;
        bool                         has_infill;
        bool                         has_support;

//        static const size_t          alloc_size_max    () { return 32 * 1048576 / 4; }
        static const size_t          alloc_size_max    () { return 4 * 1048576 / 4; }
        static const size_t          alloc_size_reserve() { return alloc_size_max() * 2; }

        static const float*          color_perimeters  () { static float color[4] = { 1.0f, 1.0f, 0.0f, 1.f }; return color; } // yellow
        static const float*          color_infill      () { static float color[4] = { 1.0f, 0.5f, 0.5f, 1.f }; return color; } // redish
        static const float*          color_support     () { static float color[4] = { 0.5f, 1.0f, 0.5f, 1.f }; return color; } // greenish
    } ctxt;

    ctxt.shifted_copies = &print_object->_shifted_copies;

    // order layers by print_z
    ctxt.layers.reserve(print_object->layers.size() + print_object->support_layers.size());
    for (const Layer *layer : print_object->layers)
        ctxt.layers.push_back(layer);
    for (const Layer *layer : print_object->support_layers)
        ctxt.layers.push_back(layer);
    std::sort(ctxt.layers.begin(), ctxt.layers.end(), [](const Layer *l1, const Layer *l2) { return l1->print_z < l2->print_z; });
    
    for (const Point &copy: print_object->_shifted_copies) {
        BoundingBox cbb = print_object->bounding_box();
        cbb.translate(copy.x, copy.y);
        ctxt.bbox.merge(Pointf3(unscale(cbb.min.x), unscale(cbb.min.y), 0.f));
        ctxt.bbox.merge(Pointf3(unscale(cbb.max.x), unscale(cbb.max.y), 0.f));
    }

    // Maximum size of an allocation block: 32MB / sizeof(float)
    ctxt.has_perimeters = print_object->state.is_done(posPerimeters);
    ctxt.has_infill     = print_object->state.is_done(posInfill);
    ctxt.has_support    = print_object->state.is_done(posSupportMaterial);
    
    BOOST_LOG_TRIVIAL(debug) << "Loading print object toolpaths in parallel - start";

    //FIXME Improve the heuristics for a grain size.
    size_t grain_size = std::max(ctxt.layers.size() / 16, size_t(1));
    std::vector<GLVolumeCollection> volumes_per_thread(ctxt.layers.size());
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, ctxt.layers.size(), grain_size),
        [&ctxt, &volumes_per_thread](const tbb::blocked_range<size_t>& range) {
            std::vector<GLVolume*> &volumes = volumes_per_thread[range.begin()].volumes;
            volumes.emplace_back(new GLVolume(ctxt.color_perimeters()));
            volumes.emplace_back(new GLVolume(ctxt.color_infill()));
            volumes.emplace_back(new GLVolume(ctxt.color_support()));
            size_t vols[3] = { 0, 1, 2 };
            for (size_t i = 0; i < 3; ++ i) {
                GLVolume &volume = *volumes[i];
                volume.bounding_box = ctxt.bbox;
                volume.indexed_vertex_array.reserve(ctxt.alloc_size_reserve());
            }
            for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++ idx_layer) {
                const Layer *layer = ctxt.layers[idx_layer];
                for (size_t i = 0; i < 3; ++ i) {
                    GLVolume &vol = *volumes[vols[i]];
                    if (vol.print_zs.empty() || vol.print_zs.back() != layer->print_z) {
                        vol.print_zs.push_back(layer->print_z);
                        vol.offsets.push_back(vol.indexed_vertex_array.quad_indices.size());
                        vol.offsets.push_back(vol.indexed_vertex_array.triangle_indices.size());
                    }
                }
                for (const Point &copy: *ctxt.shifted_copies) {
                    for (const LayerRegion *layerm : layer->regions) {
                        if (ctxt.has_perimeters)
                            extrusionentity_to_verts(layerm->perimeters, float(layer->print_z), copy, *volumes[vols[0]]);
                        if (ctxt.has_infill)
                            extrusionentity_to_verts(layerm->fills, float(layer->print_z), copy, *volumes[vols[1]]);
                    }
                    if (ctxt.has_support) {
                        const SupportLayer *support_layer = dynamic_cast<const SupportLayer*>(layer);
                        if (support_layer) {
                            extrusionentity_to_verts(support_layer->support_fills, float(layer->print_z), copy, *volumes[vols[2]]);
                            extrusionentity_to_verts(support_layer->support_interface_fills, float(layer->print_z), copy, *volumes[vols[2]]);
                        }
                    }
                }
                for (size_t i = 0; i < 3; ++ i) {
                    GLVolume &vol = *volumes[vols[i]];
                    if (vol.indexed_vertex_array.vertices_and_normals_interleaved.size() / 6 > ctxt.alloc_size_max()) {
                        // Store the vertex arrays and restart their containers, 
                        vols[i] = volumes.size();
                        volumes.emplace_back(new GLVolume(vol.color));
                        GLVolume &vol_new = *volumes.back();
                        vol_new.bounding_box = ctxt.bbox;
                        // Assign the large pre-allocated buffers to the new GLVolume.
                        vol_new.indexed_vertex_array = std::move(vol.indexed_vertex_array);
                        // Copy the content back to the old GLVolume.
                        vol.indexed_vertex_array = vol_new.indexed_vertex_array;
                        // Clear the buffers, but keep them pre-allocated.
                        vol_new.indexed_vertex_array.clear();
                        // Just make sure that clear did not clear the reserved memory.
                        vol_new.indexed_vertex_array.reserve(ctxt.alloc_size_reserve());
                    }
                }
            }
            for (size_t i = 0; i < 3; ++ i)
                volumes[vols[i]]->indexed_vertex_array.shrink_to_fit();
            while (! volumes.empty() && volumes.back()->empty()) {
                delete volumes.back();
                volumes.pop_back();
            }
        });

    BOOST_LOG_TRIVIAL(debug) << "Loading print object toolpaths in parallel - merging results";

    size_t volume_ptr  = volumes->volumes.size();
    size_t num_volumes = volume_ptr;
    for (const GLVolumeCollection &v : volumes_per_thread)
        num_volumes += v.volumes.size();
    volumes->volumes.resize(num_volumes, nullptr);
    for (GLVolumeCollection &v : volumes_per_thread) {
        memcpy(volumes->volumes.data() + volume_ptr, v.volumes.data(), v.volumes.size() * sizeof(void*));
        volume_ptr += v.volumes.size();
        v.volumes.clear();
    }
  
    BOOST_LOG_TRIVIAL(debug) << "Loading print object toolpaths in parallel - end"; 
}

}
