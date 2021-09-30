#ifndef slic3r_GCodeViewer_hpp_
#define slic3r_GCodeViewer_hpp_

#include "3DScene.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "GLModel.hpp"

#include <boost/iostreams/device/mapped_file.hpp>

#include <cstdint>
#include <float.h>
#include <set>
#include <unordered_set>

namespace Slic3r {

class Print;
class TriangleMesh;

namespace GUI {

class GCodeViewer
{
    using IBufferType = unsigned short;
    using Color = std::array<float, 4>;
    using VertexBuffer = std::vector<float>;
    using MultiVertexBuffer = std::vector<VertexBuffer>;
    using IndexBuffer = std::vector<IBufferType>;
    using MultiIndexBuffer = std::vector<IndexBuffer>;
#if ENABLE_SEAMS_USING_MODELS
    using InstanceBuffer = std::vector<float>;
    using InstanceIdBuffer = std::vector<size_t>;
#endif // ENABLE_SEAMS_USING_MODELS
#if ENABLE_FIX_SEAMS_SYNCH
    using InstancesOffsets = std::vector<Vec3f>;
#endif // ENABLE_FIX_SEAMS_SYNCH

    static const std::vector<Color> Extrusion_Role_Colors;
    static const std::vector<Color> Options_Colors;
    static const std::vector<Color> Travel_Colors;
    static const std::vector<Color> Range_Colors;
    static const Color              Wipe_Color;
    static const Color              Neutral_Color;

    enum class EOptionsColors : unsigned char
    {
        Retractions,
        Unretractions,
        Seams,
        ToolChanges,
        ColorChanges,
        PausePrints,
        CustomGCodes
    };

    // vbo buffer containing vertices data used to render a specific toolpath type
    struct VBuffer
    {
        enum class EFormat : unsigned char
        {
            // vertex format: 3 floats -> position.x|position.y|position.z
            Position,
            // vertex format: 4 floats -> position.x|position.y|position.z|normal.x
            PositionNormal1,
            // vertex format: 6 floats -> position.x|position.y|position.z|normal.x|normal.y|normal.z
            PositionNormal3
        };

        EFormat format{ EFormat::Position };
        // vbos id
        std::vector<unsigned int> vbos;
        // sizes of the buffers, in bytes, used in export to obj
        std::vector<size_t> sizes;
        // count of vertices, updated after data are sent to gpu
        size_t count{ 0 };

        size_t data_size_bytes() const { return count * vertex_size_bytes(); }
        // We set 65536 as max count of vertices inside a vertex buffer to allow
        // to use unsigned short in place of unsigned int for indices in the index buffer, to save memory
        size_t max_size_bytes() const { return 65536 * vertex_size_bytes(); }

        size_t vertex_size_floats() const { return position_size_floats() + normal_size_floats(); }
        size_t vertex_size_bytes() const { return vertex_size_floats() * sizeof(float); }

        size_t position_offset_floats() const { return 0; }
        size_t position_offset_bytes() const { return position_offset_floats() * sizeof(float); }

        size_t position_size_floats() const { return 3; }
        size_t position_size_bytes() const { return position_size_floats() * sizeof(float); }

        size_t normal_offset_floats() const {
            assert(format == EFormat::PositionNormal1 || format == EFormat::PositionNormal3);
            return position_size_floats();
        }
        size_t normal_offset_bytes() const { return normal_offset_floats() * sizeof(float); }

        size_t normal_size_floats() const {
            switch (format)
            {
            case EFormat::PositionNormal1: { return 1; }
            case EFormat::PositionNormal3: { return 3; }
            default:                       { return 0; }
            }
        }
        size_t normal_size_bytes() const { return normal_size_floats() * sizeof(float); }

        void reset();
    };

#if ENABLE_SEAMS_USING_MODELS
#if ENABLE_SEAMS_USING_BATCHED_MODELS
    // buffer containing instances data used to render a toolpaths using instanced or batched models
    // instance record format:
    // instanced models: 5 floats -> position.x|position.y|position.z|width|height (which are sent to the shader as -> vec3 (offset) + vec2 (scales) in GLModel::render_instanced())
    // batched models:   3 floats -> position.x|position.y|position.z
#else
    // buffer containing instances data used to render a toolpaths using instanced models
    // instance record format: 5 floats -> position.x|position.y|position.z|width|height
    // which is sent to the shader as -> vec3 (offset) + vec2 (scales) in GLModel::render_instanced()
#endif // ENABLE_SEAMS_USING_BATCHED_MODELS
    struct InstanceVBuffer
    {
        // ranges used to render only subparts of the intances
        struct Ranges
        {
            struct Range
            {
                // offset in bytes of the 1st instance to render
                unsigned int offset;
                // count of instances to render
                unsigned int count;
                // vbo id
                unsigned int vbo{ 0 };
                // Color to apply to the instances
                Color color;
            };

            std::vector<Range> ranges;

            void reset();
        };

#if ENABLE_SEAMS_USING_BATCHED_MODELS
        enum class EFormat : unsigned char
        {
            InstancedModel,
            BatchedModel
        };

        EFormat format;
#endif // ENABLE_SEAMS_USING_BATCHED_MODELS

        // cpu-side buffer containing all instances data
        InstanceBuffer buffer;
        // indices of the moves for all instances
        std::vector<size_t> s_ids;
#if ENABLE_FIX_SEAMS_SYNCH
        // position offsets, used to show the correct value of the tool position
        InstancesOffsets offsets;
#endif // ENABLE_FIX_SEAMS_SYNCH
        Ranges render_ranges;

        size_t data_size_bytes() const { return s_ids.size() * instance_size_bytes(); }

#if ENABLE_SEAMS_USING_BATCHED_MODELS
        size_t instance_size_floats() const {
            switch (format)
            {
            case EFormat::InstancedModel: { return 5; }
            case EFormat::BatchedModel: { return 3; }
            default: { return 0; }
            }
        }
#else
        size_t instance_size_floats() const { return 5; }
#endif // ENABLE_SEAMS_USING_BATCHED_MODELS
        size_t instance_size_bytes() const { return instance_size_floats() * sizeof(float); }

        void reset();
    };
#endif // ENABLE_SEAMS_USING_MODELS

    // ibo buffer containing indices data (for lines/triangles) used to render a specific toolpath type
    struct IBuffer
    {
        // id of the associated vertex buffer
        unsigned int vbo{ 0 };
        // ibo id
        unsigned int ibo{ 0 };
        // count of indices, updated after data are sent to gpu
        size_t count{ 0 };

        void reset();
    };

    // Used to identify different toolpath sub-types inside a IBuffer
    struct Path
    {
        struct Endpoint
        {
            // index of the buffer in the multibuffer vector
            // the buffer type may change:
            // it is the vertex buffer while extracting vertices data,
            // the index buffer while extracting indices data
            unsigned int b_id{ 0 };
            // index into the buffer
            size_t i_id{ 0 };
            // move id
            size_t s_id{ 0 };
            Vec3f position{ Vec3f::Zero() };
        };

        struct Sub_Path
        {
            Endpoint first;
            Endpoint last;

            bool contains(size_t s_id) const {
                return first.s_id <= s_id && s_id <= last.s_id;
            }
        };

        EMoveType type{ EMoveType::Noop };
        ExtrusionRole role{ erNone };
        float delta_extruder{ 0.0f };
        float height{ 0.0f };
        float width{ 0.0f };
        float feedrate{ 0.0f };
        float fan_speed{ 0.0f };
        float temperature{ 0.0f };
        float volumetric_rate{ 0.0f };
        unsigned char extruder_id{ 0 };
        unsigned char cp_color_id{ 0 };
        std::vector<Sub_Path> sub_paths;

        bool matches(const GCodeProcessor::MoveVertex& move) const;
        size_t vertices_count() const {
            return sub_paths.empty() ? 0 : sub_paths.back().last.s_id - sub_paths.front().first.s_id + 1;
        }
        bool contains(size_t s_id) const {
            return sub_paths.empty() ? false : sub_paths.front().first.s_id <= s_id && s_id <= sub_paths.back().last.s_id;
        }
        int get_id_of_sub_path_containing(size_t s_id) const {
            if (sub_paths.empty())
                return -1;
            else {
                for (int i = 0; i < static_cast<int>(sub_paths.size()); ++i) {
                    if (sub_paths[i].contains(s_id))
                        return i;
                }
                return -1;
            }
        }
        void add_sub_path(const GCodeProcessor::MoveVertex& move, unsigned int b_id, size_t i_id, size_t s_id) {
            Endpoint endpoint = { b_id, i_id, s_id, move.position };
            sub_paths.push_back({ endpoint , endpoint });
        }
    };

    // Used to batch the indices needed to render the paths
    struct RenderPath
    {
        // Index of the parent tbuffer
        unsigned char               tbuffer_id;
        // Render path property
        Color                       color;
        // Index of the buffer in TBuffer::indices
        unsigned int                ibuffer_id;
        // Render path content
        // Index of the path in TBuffer::paths
        unsigned int                path_id;
        std::vector<unsigned int>   sizes;
        std::vector<size_t>         offsets; // use size_t because we need an unsigned integer whose size matches pointer's size (used in the call glMultiDrawElements())
        bool contains(size_t offset) const {
            for (size_t i = 0; i < offsets.size(); ++i) {
                if (offsets[i] <= offset && offset <= offsets[i] + static_cast<size_t>(sizes[i] * sizeof(IBufferType)))
                    return true;
            }
            return false;
        }
    };
//    // for unordered_set implementation of render_paths
//    struct RenderPathPropertyHash {
//        size_t operator() (const RenderPath &p) const {
//            // Convert the RGB value to an integer hash.
////            return (size_t(int(p.color[0] * 255) + 255 * int(p.color[1] * 255) + (255 * 255) * int(p.color[2] * 255)) * 7919) ^ size_t(p.ibuffer_id);
//            return size_t(int(p.color[0] * 255) + 255 * int(p.color[1] * 255) + (255 * 255) * int(p.color[2] * 255)) ^ size_t(p.ibuffer_id);
//        }
//    };
    struct RenderPathPropertyLower {
        bool operator() (const RenderPath &l, const RenderPath &r) const {
            if (l.tbuffer_id < r.tbuffer_id)
                return true;
            for (int i = 0; i < 3; ++i) {
                if (l.color[i] < r.color[i])
                    return true;
                else if (l.color[i] > r.color[i])
                    return false;
            }
            return l.ibuffer_id < r.ibuffer_id;
        }
    };
    struct RenderPathPropertyEqual {
        bool operator() (const RenderPath &l, const RenderPath &r) const {
            return l.tbuffer_id == r.tbuffer_id && l.ibuffer_id == r.ibuffer_id && l.color == r.color;
        }
    };

    // buffer containing data for rendering a specific toolpath type
    struct TBuffer
    {
        enum class ERenderPrimitiveType : unsigned char
        {
            Point,
            Line,
#if ENABLE_SEAMS_USING_MODELS
            Triangle,
#if ENABLE_SEAMS_USING_BATCHED_MODELS
            InstancedModel,
            BatchedModel
#else
            Model
#endif // ENABLE_SEAMS_USING_BATCHED_MODELS
#else
            Triangle
#endif // ENABLE_SEAMS_USING_MODELS
        };

        ERenderPrimitiveType render_primitive_type;

        // buffers for point, line and triangle primitive types
        VBuffer vertices;
        std::vector<IBuffer> indices;

#if ENABLE_SEAMS_USING_MODELS
        struct Model
        {
            GLModel model;
            Color color;
            InstanceVBuffer instances;
#if ENABLE_SEAMS_USING_BATCHED_MODELS
            GLModel::InitializationData data;
#endif // ENABLE_SEAMS_USING_BATCHED_MODELS

            void reset();
        };

        // contain the buffer for model primitive types
        Model model;
#endif // ENABLE_SEAMS_USING_MODELS

        std::string shader;
        std::vector<Path> paths;
        // std::set seems to perform significantly better, at least on Windows.
//        std::unordered_set<RenderPath, RenderPathPropertyHash, RenderPathPropertyEqual> render_paths;
        std::set<RenderPath, RenderPathPropertyLower> render_paths;
        bool visible{ false };

        void reset();

        // b_id index of buffer contained in this->indices
        // i_id index of first index contained in this->indices[b_id]
        // s_id index of first vertex contained in this->vertices
        void add_path(const GCodeProcessor::MoveVertex& move, unsigned int b_id, size_t i_id, size_t s_id);

        unsigned int max_vertices_per_segment() const {
            switch (render_primitive_type)
            {
            case ERenderPrimitiveType::Point:    { return 1; }
            case ERenderPrimitiveType::Line:     { return 2; }
            case ERenderPrimitiveType::Triangle: { return 8; }
            default:                             { return 0; }
            }
        }

        size_t max_vertices_per_segment_size_floats() const { return vertices.vertex_size_floats() * static_cast<size_t>(max_vertices_per_segment()); }
        size_t max_vertices_per_segment_size_bytes() const { return max_vertices_per_segment_size_floats() * sizeof(float); }
        unsigned int indices_per_segment() const {
            switch (render_primitive_type)
            {
            case ERenderPrimitiveType::Point:    { return 1; }
            case ERenderPrimitiveType::Line:     { return 2; }
            case ERenderPrimitiveType::Triangle: { return 30; } // 3 indices x 10 triangles
            default:                             { return 0; }
            }
        }
        size_t indices_per_segment_size_bytes() const { return static_cast<size_t>(indices_per_segment() * sizeof(IBufferType)); }
        unsigned int max_indices_per_segment() const {
            switch (render_primitive_type)
            {
            case ERenderPrimitiveType::Point:    { return 1; }
            case ERenderPrimitiveType::Line:     { return 2; }
            case ERenderPrimitiveType::Triangle: { return 36; } // 3 indices x 12 triangles
            default:                             { return 0; }
            }
        }
        size_t max_indices_per_segment_size_bytes() const { return max_indices_per_segment() * sizeof(IBufferType); }

#if ENABLE_SEAMS_USING_MODELS
        bool has_data() const {
            switch (render_primitive_type)
            {
            case ERenderPrimitiveType::Point:
            case ERenderPrimitiveType::Line:
            case ERenderPrimitiveType::Triangle: {
                return !vertices.vbos.empty() && vertices.vbos.front() != 0 && !indices.empty() && indices.front().ibo != 0;
            }
#if ENABLE_SEAMS_USING_BATCHED_MODELS
            case ERenderPrimitiveType::InstancedModel: { return model.model.is_initialized() && !model.instances.buffer.empty(); }
            case ERenderPrimitiveType::BatchedModel: {
                return model.data.vertices_count() > 0 && model.data.indices_count() &&
                    !vertices.vbos.empty() && vertices.vbos.front() != 0 && !indices.empty() && indices.front().ibo != 0;
            }
#else
            case ERenderPrimitiveType::Model: { return model.model.is_initialized() && !model.instances.buffer.empty(); }
#endif // ENABLE_SEAMS_USING_BATCHED_MODELS
            default: { return false; }
            }
        }
#else
        bool has_data() const {
            return !vertices.vbos.empty() && vertices.vbos.front() != 0 && !indices.empty() && indices.front().ibo != 0;
        }
#endif // ENABLE_SEAMS_USING_MODELS
    };

    // helper to render shells
    struct Shells
    {
        GLVolumeCollection volumes;
        bool visible{ false };
    };

    // helper to render extrusion paths
    struct Extrusions
    {
        struct Range
        {
#if ENABLE_PREVIEW_LAYER_TIME
            enum class EType : unsigned char
            {
                Linear,
                Logarithmic
            };
#endif // ENABLE_PREVIEW_LAYER_TIME

            float min;
            float max;
            unsigned int count;

            Range() { reset(); }

            void update_from(const float value) {
                if (value != max && value != min)
                    ++count;
                min = std::min(min, value);
                max = std::max(max, value);
            }
            void reset() { min = FLT_MAX; max = -FLT_MAX; count = 0; }

#if ENABLE_PREVIEW_LAYER_TIME
            float step_size(EType type = EType::Linear) const;
            Color get_color_at(float value, EType type = EType::Linear) const;
#else
            float step_size() const { return (max - min) / (static_cast<float>(Range_Colors.size()) - 1.0f); }
            Color get_color_at(float value) const;
#endif // ENABLE_PREVIEW_LAYER_TIME
        };

        struct Ranges
        {
            // Color mapping by layer height.
            Range height;
            // Color mapping by extrusion width.
            Range width;
            // Color mapping by feedrate.
            Range feedrate;
            // Color mapping by fan speed.
            Range fan_speed;
            // Color mapping by volumetric extrusion rate.
            Range volumetric_rate;
            // Color mapping by extrusion temperature.
            Range temperature;
#if ENABLE_PREVIEW_LAYER_TIME
            // Color mapping by layer time.
            std::array<Range, static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count)> layer_time;
#endif // ENABLE_PREVIEW_LAYER_TIME

            void reset() {
                height.reset();
                width.reset();
                feedrate.reset();
                fan_speed.reset();
                volumetric_rate.reset();
                temperature.reset();
#if ENABLE_PREVIEW_LAYER_TIME
                for (auto& range : layer_time) {
                    range.reset();
                }
#endif // ENABLE_PREVIEW_LAYER_TIME
            }
        };

        unsigned int role_visibility_flags{ 0 };
        Ranges ranges;

        void reset_role_visibility_flags() {
            role_visibility_flags = 0;
            for (unsigned int i = 0; i < erCount; ++i) {
                role_visibility_flags |= 1 << i;
            }
        }

        void reset_ranges() { ranges.reset(); }
    };

    class Layers
    {
    public:
        struct Range
        {
            size_t first{ 0 };
            size_t last{ 0 };

            bool operator == (const Range& other) const { return first == other.first && last == other.last; }
            bool contains(size_t id) const { return first <= id && id <= last; }
        };

    private:
        std::vector<double> m_zs;
        std::vector<Range> m_ranges;

    public:
        void append(double z, const Range& range) {
            m_zs.emplace_back(z);
            m_ranges.emplace_back(range);
        }

        void reset() {
            m_zs = std::vector<double>();
            m_ranges = std::vector<Range>();
        }

        size_t size() const { return m_zs.size(); }
        bool empty() const { return m_zs.empty(); }
        const std::vector<double>& get_zs() const { return m_zs; }
        const std::vector<Range>& get_ranges() const { return m_ranges; }
        std::vector<Range>& get_ranges() { return m_ranges; }
        double get_z_at(unsigned int id) const { return (id < m_zs.size()) ? m_zs[id] : 0.0; }
        Range get_range_at(unsigned int id) const { return (id < m_ranges.size()) ? m_ranges[id] : Range(); }

        bool operator != (const Layers& other) const {
            if (m_zs != other.m_zs)
                return true;
            if (!(m_ranges == other.m_ranges))
                return true;

            return false;
        }
    };

    // used to render the toolpath caps of the current sequential range
    // (i.e. when sliding on the horizontal slider)
    struct SequentialRangeCap
    {
        TBuffer* buffer{ nullptr };
        unsigned int ibo{ 0 };
        unsigned int vbo{ 0 };
        Color color;

        ~SequentialRangeCap();
        bool is_renderable() const { return buffer != nullptr; }
        void reset();
        size_t indices_count() const { return 6; }
    };

#if ENABLE_GCODE_VIEWER_STATISTICS
    struct Statistics
    {
        // time
        int64_t results_time{ 0 };
        int64_t load_time{ 0 };
        int64_t load_vertices{ 0 };
        int64_t smooth_vertices{ 0 };
        int64_t load_indices{ 0 };
        int64_t refresh_time{ 0 };
        int64_t refresh_paths_time{ 0 };
        // opengl calls
        int64_t gl_multi_points_calls_count{ 0 };
        int64_t gl_multi_lines_calls_count{ 0 };
        int64_t gl_multi_triangles_calls_count{ 0 };
        int64_t gl_triangles_calls_count{ 0 };
#if ENABLE_SEAMS_USING_MODELS
        int64_t gl_instanced_models_calls_count{ 0 };
#if ENABLE_SEAMS_USING_BATCHED_MODELS
        int64_t gl_batched_models_calls_count{ 0 };
#endif // ENABLE_SEAMS_USING_BATCHED_MODELS
#endif // ENABLE_SEAMS_USING_MODELS
        // memory
        int64_t results_size{ 0 };
        int64_t total_vertices_gpu_size{ 0 };
        int64_t total_indices_gpu_size{ 0 };
#if ENABLE_SEAMS_USING_MODELS
        int64_t total_instances_gpu_size{ 0 };
#endif // ENABLE_SEAMS_USING_MODELS
        int64_t max_vbuffer_gpu_size{ 0 };
        int64_t max_ibuffer_gpu_size{ 0 };
        int64_t paths_size{ 0 };
        int64_t render_paths_size{ 0 };
#if ENABLE_SEAMS_USING_MODELS
        int64_t models_instances_size{ 0 };
#endif // ENABLE_SEAMS_USING_MODELS
        // other
        int64_t travel_segments_count{ 0 };
        int64_t wipe_segments_count{ 0 };
        int64_t extrude_segments_count{ 0 };
#if ENABLE_SEAMS_USING_MODELS
        int64_t instances_count{ 0 };
#if ENABLE_SEAMS_USING_BATCHED_MODELS
        int64_t batched_count{ 0 };
#endif // ENABLE_SEAMS_USING_BATCHED_MODELS
#endif // ENABLE_SEAMS_USING_MODELS
        int64_t vbuffers_count{ 0 };
        int64_t ibuffers_count{ 0 };

        void reset_all() {
            reset_times();
            reset_opengl();
            reset_sizes();
            reset_others();
        }

        void reset_times() {
            results_time = 0;
            load_time = 0;
            load_vertices = 0;
            smooth_vertices = 0;
            load_indices = 0;
            refresh_time = 0;
            refresh_paths_time = 0;
        }

        void reset_opengl() {
            gl_multi_points_calls_count = 0;
            gl_multi_lines_calls_count = 0;
            gl_multi_triangles_calls_count = 0;
            gl_triangles_calls_count = 0;
#if ENABLE_SEAMS_USING_MODELS
            gl_instanced_models_calls_count = 0;
#if ENABLE_SEAMS_USING_BATCHED_MODELS
            gl_batched_models_calls_count = 0;
#endif // ENABLE_SEAMS_USING_BATCHED_MODELS
#endif // ENABLE_SEAMS_USING_MODELS
        }

        void reset_sizes() {
            results_size = 0;
            total_vertices_gpu_size = 0;
            total_indices_gpu_size = 0;
#if ENABLE_SEAMS_USING_MODELS
            total_instances_gpu_size = 0;
#endif // ENABLE_SEAMS_USING_MODELS
            max_vbuffer_gpu_size = 0;
            max_ibuffer_gpu_size = 0;
            paths_size = 0;
            render_paths_size = 0;
#if ENABLE_SEAMS_USING_MODELS
            models_instances_size = 0;
#endif // ENABLE_SEAMS_USING_MODELS
        }

        void reset_others() {
            travel_segments_count = 0;
            wipe_segments_count = 0;
            extrude_segments_count =  0;
#if ENABLE_SEAMS_USING_MODELS
            instances_count = 0;
#if ENABLE_SEAMS_USING_BATCHED_MODELS
            batched_count = 0;
#endif // ENABLE_SEAMS_USING_BATCHED_MODELS
#endif // ENABLE_SEAMS_USING_MODELS
            vbuffers_count = 0;
            ibuffers_count = 0;
        }
    };
#endif // ENABLE_GCODE_VIEWER_STATISTICS

public:
    struct SequentialView
    {
        class Marker
        {
            GLModel m_model;
            Vec3f m_world_position;
            Transform3f m_world_transform;
#if ENABLE_FIX_SEAMS_SYNCH
            // for seams, the position of the marker is on the last endpoint of the toolpath containing it
            // the offset is used to show the correct value of tool position in the "ToolPosition" window
            // see implementation of render() method
            Vec3f m_world_offset;
#endif // ENABLE_FIX_SEAMS_SYNCH
            float m_z_offset{ 0.5f };
            bool m_visible{ true };

        public:
            void init();

            const BoundingBoxf3& get_bounding_box() const { return m_model.get_bounding_box(); }

            void set_world_position(const Vec3f& position);
#if ENABLE_FIX_SEAMS_SYNCH
            void set_world_offset(const Vec3f& offset) { m_world_offset = offset; }
#endif // ENABLE_FIX_SEAMS_SYNCH

            bool is_visible() const { return m_visible; }
            void set_visible(bool visible) { m_visible = visible; }

            void render() const;
        };

        class GCodeWindow
        {
            struct Line
            {
                std::string command;
                std::string parameters;
                std::string comment;
            };
            bool m_visible{ true };
            uint64_t m_selected_line_id{ 0 };
            size_t m_last_lines_size{ 0 };
            std::string m_filename;
            boost::iostreams::mapped_file_source m_file;
            // map for accessing data in file by line number
            std::vector<size_t> m_lines_ends;
            // current visible lines
            std::vector<Line> m_lines;

        public:
            GCodeWindow() = default;
            ~GCodeWindow() { stop_mapping_file(); }
            void load_gcode(const std::string& filename, std::vector<size_t> &&lines_ends);
            void reset() {
                stop_mapping_file();
                m_lines_ends.clear();
                m_lines.clear();
                m_filename.clear();
            }

            void toggle_visibility() { m_visible = !m_visible; }

            void render(float top, float bottom, uint64_t curr_line_id) const;

            void stop_mapping_file();
        };

        struct Endpoints
        {
            size_t first{ 0 };
            size_t last{ 0 };
        };

        bool skip_invisible_moves{ false };
        Endpoints endpoints;
        Endpoints current;
        Endpoints last_current;
#if ENABLE_SEAMS_USING_MODELS
        Endpoints global;
#endif // ENABLE_SEAMS_USING_MODELS
        Vec3f current_position{ Vec3f::Zero() };
#if ENABLE_FIX_SEAMS_SYNCH
        Vec3f current_offset{ Vec3f::Zero() };
#endif // ENABLE_FIX_SEAMS_SYNCH
        Marker marker;
        GCodeWindow gcode_window;
        std::vector<unsigned int> gcode_ids;

        void render(float legend_height) const;
    };

    enum class EViewType : unsigned char
    {
        FeatureType,
        Height,
        Width,
        Feedrate,
        FanSpeed,
        Temperature,
        VolumetricRate,
#if ENABLE_PREVIEW_LAYER_TIME
        LayerTimeLinear,
        LayerTimeLogarithmic,
#endif // ENABLE_PREVIEW_LAYER_TIME
        Tool,
        ColorPrint,
        Count
    };

private:
    bool m_gl_data_initialized{ false };
    unsigned int m_last_result_id{ 0 };
    size_t m_moves_count{ 0 };
    std::vector<TBuffer> m_buffers{ static_cast<size_t>(EMoveType::Extrude) };
    // bounding box of toolpaths
    BoundingBoxf3 m_paths_bounding_box;
    // bounding box of toolpaths + marker tools
    BoundingBoxf3 m_max_bounding_box;
    std::vector<Color> m_tool_colors;
    Layers m_layers;
    std::array<unsigned int, 2> m_layers_z_range;
    std::vector<ExtrusionRole> m_roles;
    size_t m_extruders_count;
    std::vector<unsigned char> m_extruder_ids;
    std::vector<float> m_filament_diameters;
    std::vector<float> m_filament_densities;
    Extrusions m_extrusions;
    SequentialView m_sequential_view;
    Shells m_shells;
    EViewType m_view_type{ EViewType::FeatureType };
    bool m_legend_enabled{ true };
    PrintEstimatedStatistics m_print_statistics;
    PrintEstimatedStatistics::ETimeMode m_time_estimate_mode{ PrintEstimatedStatistics::ETimeMode::Normal };
#if ENABLE_GCODE_VIEWER_STATISTICS
    Statistics m_statistics;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
    std::array<float, 2> m_detected_point_sizes = { 0.0f, 0.0f };
    GCodeProcessor::Result::SettingsIds m_settings_ids;
    std::array<SequentialRangeCap, 2> m_sequential_range_caps;
#if ENABLE_PREVIEW_LAYER_TIME
    std::array<std::vector<float>, static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count)> m_layers_times;
#endif // ENABLE_PREVIEW_LAYER_TIME

    std::vector<CustomGCode::Item> m_custom_gcode_per_print_z;

public:
    GCodeViewer();
    ~GCodeViewer() { reset(); }

#if ENABLE_SEAMS_USING_MODELS
    void init();
#endif // ENABLE_SEAMS_USING_MODELS

    // extract rendering data from the given parameters
    void load(const GCodeProcessor::Result& gcode_result, const Print& print, bool initialized);
    // recalculate ranges in dependence of what is visible and sets tool/print colors
    void refresh(const GCodeProcessor::Result& gcode_result, const std::vector<std::string>& str_tool_colors);
    void refresh_render_paths();
    void update_shells_color_by_extruder(const DynamicPrintConfig* config);

    void reset();
    void render();

    bool has_data() const { return !m_roles.empty(); }
    bool can_export_toolpaths() const;

    const BoundingBoxf3& get_paths_bounding_box() const { return m_paths_bounding_box; }
    const BoundingBoxf3& get_max_bounding_box() const { return m_max_bounding_box; }
    const std::vector<double>& get_layers_zs() const { return m_layers.get_zs(); }

    const SequentialView& get_sequential_view() const { return m_sequential_view; }
    void update_sequential_view_current(unsigned int first, unsigned int last);

    EViewType get_view_type() const { return m_view_type; }
    void set_view_type(EViewType type) {
        if (type == EViewType::Count)
            type = EViewType::FeatureType;

        m_view_type = type;
    }

    bool is_toolpath_move_type_visible(EMoveType type) const;
    void set_toolpath_move_type_visible(EMoveType type, bool visible);
    unsigned int get_toolpath_role_visibility_flags() const { return m_extrusions.role_visibility_flags; }
    void set_toolpath_role_visibility_flags(unsigned int flags) { m_extrusions.role_visibility_flags = flags; }
    unsigned int get_options_visibility_flags() const;
    void set_options_visibility_from_flags(unsigned int flags);
    void set_layers_z_range(const std::array<unsigned int, 2>& layers_z_range);

    bool is_legend_enabled() const { return m_legend_enabled; }
    void enable_legend(bool enable) { m_legend_enabled = enable; }

    void export_toolpaths_to_obj(const char* filename) const;

    void toggle_gcode_window_visibility() { m_sequential_view.gcode_window.toggle_visibility(); }

    std::vector<CustomGCode::Item>& get_custom_gcode_per_print_z() { return m_custom_gcode_per_print_z; }
    size_t get_extruders_count() { return m_extruders_count; }

private:
    void load_toolpaths(const GCodeProcessor::Result& gcode_result);
    void load_shells(const Print& print, bool initialized);
    void refresh_render_paths(bool keep_sequential_current_first, bool keep_sequential_current_last) const;
    void render_toolpaths();
    void render_shells();
    void render_legend(float& legend_height);
#if ENABLE_GCODE_VIEWER_STATISTICS
    void render_statistics();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
    bool is_visible(ExtrusionRole role) const {
        return role < erCount && (m_extrusions.role_visibility_flags & (1 << role)) != 0;
    }
    bool is_visible(const Path& path) const { return is_visible(path.role); }
    void log_memory_used(const std::string& label, int64_t additional = 0) const;
    Color option_color(EMoveType move_type) const;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GCodeViewer_hpp_

