#ifndef slic3r_GUI_Selection_hpp_
#define slic3r_GUI_Selection_hpp_

#include <set>
#include "libslic3r/Geometry.hpp"
#include "3DScene.hpp"

namespace Slic3r {
namespace GUI {

class TransformationType
{
public:
    enum Enum {
        // Transforming in a world coordinate system
        World = 0,
        // Transforming in a local coordinate system
        Local = 1,
        // Absolute transformations, allowed in local coordinate system only.
        Absolute = 0,
        // Relative transformations, allowed in both local and world coordinate system.
        Relative = 2,
        // For group selection, the transformation is performed as if the group made a single solid body.
        Joint = 0,
        // For group selection, the transformation is performed on each object independently.
        Independent = 4,

        World_Relative_Joint = World | Relative | Joint,
        World_Relative_Independent = World | Relative | Independent,
        Local_Absolute_Joint = Local | Absolute | Joint,
        Local_Absolute_Independent = Local | Absolute | Independent,
        Local_Relative_Joint = Local | Relative | Joint,
        Local_Relative_Independent = Local | Relative | Independent,
    };

    TransformationType() : m_value(World) {}
    TransformationType(Enum value) : m_value(value) {}
    TransformationType& operator=(Enum value) { m_value = value; return *this; }

    Enum operator()() const { return m_value; }
    bool has(Enum v) const { return ((unsigned int)m_value & (unsigned int)v) != 0; }

    void set_world()        { this->remove(Local); }
    void set_local()        { this->add(Local); }
    void set_absolute()     { this->remove(Relative); }
    void set_relative()     { this->add(Relative); }
    void set_joint()        { this->remove(Independent); }
    void set_independent()  { this->add(Independent); }

    bool world()        const { return !this->has(Local); }
    bool local()        const { return this->has(Local); }
    bool absolute()     const { return !this->has(Relative); }
    bool relative()     const { return this->has(Relative); }
    bool joint()        const { return !this->has(Independent); }
    bool independent()  const { return this->has(Independent); }

private:
    void add(Enum v) { m_value = Enum((unsigned int)m_value | (unsigned int)v); }
    void remove(Enum v) { m_value = Enum((unsigned int)m_value & (~(unsigned int)v)); }

    Enum    m_value;
};

class Selection
{
public:
    typedef std::set<unsigned int> IndicesList;

    enum EMode : unsigned char
    {
        Volume,
        Instance
    };

    enum EType : unsigned char
    {
        Invalid,
        Empty,
        WipeTower,
        SingleModifier,
        MultipleModifier,
        SingleVolume,
        MultipleVolume,
        SingleFullObject,
        MultipleFullObject,
        SingleFullInstance,
        MultipleFullInstance,
        Mixed
    };

private:
    struct VolumeCache
    {
    private:
        struct TransformCache
        {
            Vec3d position;
            Vec3d rotation;
            Vec3d scaling_factor;
            Vec3d mirror;
            Transform3d rotation_matrix;
            Transform3d scale_matrix;
            Transform3d mirror_matrix;
            Transform3d full_matrix;

            TransformCache();
            explicit TransformCache(const Geometry::Transformation& transform);
        };

        TransformCache m_volume;
        TransformCache m_instance;

    public:
        VolumeCache() {}
        VolumeCache(const Geometry::Transformation& volume_transform, const Geometry::Transformation& instance_transform);

        const Vec3d& get_volume_position() const { return m_volume.position; }
        const Vec3d& get_volume_rotation() const { return m_volume.rotation; }
        const Vec3d& get_volume_scaling_factor() const { return m_volume.scaling_factor; }
        const Vec3d& get_volume_mirror() const { return m_volume.mirror; }
        const Transform3d& get_volume_rotation_matrix() const { return m_volume.rotation_matrix; }
        const Transform3d& get_volume_scale_matrix() const { return m_volume.scale_matrix; }
        const Transform3d& get_volume_mirror_matrix() const { return m_volume.mirror_matrix; }
        const Transform3d& get_volume_full_matrix() const { return m_volume.full_matrix; }

        const Vec3d& get_instance_position() const { return m_instance.position; }
        const Vec3d& get_instance_rotation() const { return m_instance.rotation; }
        const Vec3d& get_instance_scaling_factor() const { return m_instance.scaling_factor; }
        const Vec3d& get_instance_mirror() const { return m_instance.mirror; }
        const Transform3d& get_instance_rotation_matrix() const { return m_instance.rotation_matrix; }
        const Transform3d& get_instance_scale_matrix() const { return m_instance.scale_matrix; }
        const Transform3d& get_instance_mirror_matrix() const { return m_instance.mirror_matrix; }
        const Transform3d& get_instance_full_matrix() const { return m_instance.full_matrix; }
    };

public:
    typedef std::map<unsigned int, VolumeCache> VolumesCache;
    typedef std::set<int> InstanceIdxsList;
    typedef std::map<int, InstanceIdxsList> ObjectIdxsToInstanceIdxsMap;

private:
    struct Cache
    {
        // Cache of GLVolume derived transformation matrices, valid during mouse dragging.
        VolumesCache volumes_data;
        // Center of the dragged selection, valid during mouse dragging.
        Vec3d dragging_center;
        // Map from indices of ModelObject instances in Model::objects
        // to a set of indices of ModelVolume instances in ModelObject::instances
        // Here the index means a position inside the respective std::vector, not ModelID.
        ObjectIdxsToInstanceIdxsMap content;
    };

    // Volumes owned by GLCanvas3D.
    GLVolumePtrs* m_volumes;
    // Model, not owned.
    Model* m_model;

    bool m_enabled;
    bool m_valid;
    EMode m_mode;
    EType m_type;
    // set of indices to m_volumes
    IndicesList m_list;
    Cache m_cache;
    mutable BoundingBoxf3 m_bounding_box;
    mutable bool m_bounding_box_dirty;

#if ENABLE_RENDER_SELECTION_CENTER
    GLUquadricObj* m_quadric;
#endif // ENABLE_RENDER_SELECTION_CENTER
    mutable GLArrow m_arrow;
    mutable GLCurvedArrow m_curved_arrow;

    mutable float m_scale_factor;

public:
    Selection();
#if ENABLE_RENDER_SELECTION_CENTER
    ~Selection();
#endif // ENABLE_RENDER_SELECTION_CENTER

    void set_volumes(GLVolumePtrs* volumes);
    bool init(bool useVBOs);

    bool is_enabled() const { return m_enabled; }
    void set_enabled(bool enable) { m_enabled = enable; }

    Model* get_model() const { return m_model; }
    void set_model(Model* model);

    EMode get_mode() const { return m_mode; }
    void set_mode(EMode mode) { m_mode = mode; }

    void add(unsigned int volume_idx, bool as_single_selection = true);
    void remove(unsigned int volume_idx);

    void add_object(unsigned int object_idx, bool as_single_selection = true);
    void remove_object(unsigned int object_idx);

    void add_instance(unsigned int object_idx, unsigned int instance_idx, bool as_single_selection = true);
    void remove_instance(unsigned int object_idx, unsigned int instance_idx);

    void add_volume(unsigned int object_idx, unsigned int volume_idx, int instance_idx, bool as_single_selection = true);
    void remove_volume(unsigned int object_idx, unsigned int volume_idx);

    void add_all();

    // Update the selection based on the map from old indices to new indices after m_volumes changed.
    // If the current selection is by instance, this call may select newly added volumes, if they belong to already selected instances.
    void volumes_changed(const std::vector<size_t> &map_volume_old_to_new);
    void clear();

    bool is_empty() const { return m_type == Empty; }
    bool is_wipe_tower() const { return m_type == WipeTower; }
    bool is_modifier() const { return (m_type == SingleModifier) || (m_type == MultipleModifier); }
    bool is_single_modifier() const { return m_type == SingleModifier; }
    bool is_multiple_modifier() const { return m_type == MultipleModifier; }
    bool is_single_full_instance() const;
    bool is_multiple_full_instance() const { return m_type == MultipleFullInstance; }
    bool is_single_full_object() const { return m_type == SingleFullObject; }
    bool is_multiple_full_object() const { return m_type == MultipleFullObject; }
    bool is_single_volume() const { return m_type == SingleVolume; }
    bool is_multiple_volume() const { return m_type == MultipleVolume; }
    bool is_mixed() const { return m_type == Mixed; }
    bool is_from_single_instance() const { return get_instance_idx() != -1; }
    bool is_from_single_object() const;

    bool contains_volume(unsigned int volume_idx) const { return std::find(m_list.begin(), m_list.end(), volume_idx) != m_list.end(); }
    bool requires_uniform_scale() const;

    // Returns the the object id if the selection is from a single object, otherwise is -1
    int get_object_idx() const;
    // Returns the instance id if the selection is from a single object and from a single instance, otherwise is -1
    int get_instance_idx() const;
    // Returns the indices of selected instances.
    // Can only be called if selection is from a single object.
    const InstanceIdxsList& get_instance_idxs() const;

    const IndicesList& get_volume_idxs() const { return m_list; }
    const GLVolume* get_volume(unsigned int volume_idx) const;

    const ObjectIdxsToInstanceIdxsMap& get_content() const { return m_cache.content; }

    unsigned int volumes_count() const { return (unsigned int)m_list.size(); }
    const BoundingBoxf3& get_bounding_box() const;

    void start_dragging();

    void translate(const Vec3d& displacement, bool local = false);
    void rotate(const Vec3d& rotation, TransformationType transformation_type);
    void flattening_rotate(const Vec3d& normal);
    void scale(const Vec3d& scale, bool local);
    void mirror(Axis axis);

    void translate(unsigned int object_idx, const Vec3d& displacement);
    void translate(unsigned int object_idx, unsigned int instance_idx, const Vec3d& displacement);

    void erase();

    void render(float scale_factor = 1.0) const;
#if ENABLE_RENDER_SELECTION_CENTER
    void render_center() const;
#endif // ENABLE_RENDER_SELECTION_CENTER
    void render_sidebar_hints(const std::string& sidebar_field) const;

    bool requires_local_axes() const;

private:
    void update_valid();
    void update_type();
    void set_caches();
    void do_add_volume(unsigned int volume_idx);
    void do_add_instance(unsigned int object_idx, unsigned int instance_idx);
    void do_add_object(unsigned int object_idx);
    void do_remove_volume(unsigned int volume_idx);
    void do_remove_instance(unsigned int object_idx, unsigned int instance_idx);
    void do_remove_object(unsigned int object_idx);
    void calc_bounding_box() const;
    void render_selected_volumes() const;
    void render_synchronized_volumes() const;
    void render_bounding_box(const BoundingBoxf3& box, float* color) const;
    void render_sidebar_position_hints(const std::string& sidebar_field) const;
    void render_sidebar_rotation_hints(const std::string& sidebar_field) const;
    void render_sidebar_scale_hints(const std::string& sidebar_field) const;
    void render_sidebar_size_hints(const std::string& sidebar_field) const;
    void render_sidebar_position_hint(Axis axis) const;
    void render_sidebar_rotation_hint(Axis axis) const;
    void render_sidebar_scale_hint(Axis axis) const;
    void render_sidebar_size_hint(Axis axis, double length) const;
    enum SyncRotationType {
        // Do not synchronize rotation. Either not rotating at all, or rotating by world Z axis.
        SYNC_ROTATION_NONE = 0,
        // Synchronize fully. Used from "place on bed" feature.
        SYNC_ROTATION_FULL = 1,
        // Synchronize after rotation by an axis not parallel with Z.
        SYNC_ROTATION_GENERAL = 2,
    };
    void synchronize_unselected_instances(SyncRotationType sync_rotation_type);
    void synchronize_unselected_volumes();
    void ensure_on_bed();
    bool is_from_fully_selected_instance(unsigned int volume_idx) const;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_Selection_hpp_
