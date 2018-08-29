#ifndef slic3r_GLGizmo_hpp_
#define slic3r_GLGizmo_hpp_

#include "../../slic3r/GUI/GLTexture.hpp"
#include "../../libslic3r/Point.hpp"
#include "../../libslic3r/BoundingBox.hpp"

#include <vector>

namespace Slic3r {

class BoundingBoxf3;
class Linef3;
class ModelObject;

namespace GUI {

class GLCanvas3D;

class GLGizmoBase
{
protected:
    struct Grabber
    {
        static const float HalfSize;
        static const float DraggingScaleFactor;

        Vec3d center;
        Vec3d angles;
        float color[3];
        bool dragging;

        Grabber();

        void render(bool hover) const;
        void render_for_picking() const { render(color, false); }

    private:
        void render(const float* render_color, bool use_lighting) const;
        void render_face(float half_size) const;
    };

public:
    enum EState
    {
        Off,
        Hover,
        On,
        Num_States
    };

protected:
    GLCanvas3D& m_parent;

    int m_group_id;
    EState m_state;
    // textures are assumed to be square and all with the same size in pixels, no internal check is done
    GLTexture m_textures[Num_States];
    int m_hover_id;
    float m_base_color[3];
    float m_drag_color[3];
    float m_highlight_color[3];
    mutable std::vector<Grabber> m_grabbers;

public:
    explicit GLGizmoBase(GLCanvas3D& parent);
    virtual ~GLGizmoBase() {}

    bool init() { return on_init(); }

    int get_group_id() const { return m_group_id; }
    void set_group_id(int id) { m_group_id = id; }

    EState get_state() const { return m_state; }
    void set_state(EState state) { m_state = state; on_set_state(); }

    unsigned int get_texture_id() const { return m_textures[m_state].get_id(); }
    int get_textures_size() const { return m_textures[Off].get_width(); }

    int get_hover_id() const { return m_hover_id; }
    void set_hover_id(int id);

    void set_highlight_color(const float* color);

    void start_dragging();
    void stop_dragging();
    void update(const Linef3& mouse_ray);
    void refresh() { on_refresh(); }

    void render(const BoundingBoxf3& box) const { on_render(box); }
    void render_for_picking(const BoundingBoxf3& box) const { on_render_for_picking(box); }

protected:
    virtual bool on_init() = 0;
    virtual void on_set_state() {}
    virtual void on_set_hover_id() {}
    virtual void on_start_dragging() {}
    virtual void on_stop_dragging() {}
    virtual void on_update(const Linef3& mouse_ray) = 0;
    virtual void on_refresh() {}
    virtual void on_render(const BoundingBoxf3& box) const = 0;
    virtual void on_render_for_picking(const BoundingBoxf3& box) const = 0;

    float picking_color_component(unsigned int id) const;
    void render_grabbers() const;
    void render_grabbers_for_picking() const;

    void set_tooltip(const std::string& tooltip) const;
    std::string format(float value, unsigned int decimals) const;
};

class GLGizmoRotate : public GLGizmoBase
{
    static const float Offset;
    static const unsigned int CircleResolution;
    static const unsigned int AngleResolution;
    static const unsigned int ScaleStepsCount;
    static const float ScaleStepRad;
    static const unsigned int ScaleLongEvery;
    static const float ScaleLongTooth;
    static const float ScaleShortTooth;
    static const unsigned int SnapRegionsCount;
    static const float GrabberOffset;

public:
    enum Axis : unsigned char
    {
        X,
        Y,
        Z
    };

private:
    Axis m_axis;
    double m_angle;

    mutable Vec3d m_center;
    mutable float m_radius;
    mutable bool m_keep_initial_values;

public:
    GLGizmoRotate(GLCanvas3D& parent, Axis axis);

    double get_angle() const { return m_angle; }
    void set_angle(double angle);

protected:
    virtual bool on_init();
    virtual void on_set_state() { m_keep_initial_values = (m_state == On) ? false : true; }
    virtual void on_update(const Linef3& mouse_ray);
    virtual void on_refresh() { m_keep_initial_values = false; }
    virtual void on_render(const BoundingBoxf3& box) const;
    virtual void on_render_for_picking(const BoundingBoxf3& box) const;

private:
    void render_circle() const;
    void render_scale() const;
    void render_snap_radii() const;
    void render_reference_radius() const;
    void render_angle() const;
    void render_grabber() const;

    void transform_to_local() const;
    // returns the intersection of the mouse ray with the plane perpendicular to the gizmo axis, in local coordinate
    Vec3d mouse_position_in_local_plane(const Linef3& mouse_ray) const;
};

class GLGizmoRotate3D : public GLGizmoBase
{
    GLGizmoRotate m_x;
    GLGizmoRotate m_y;
    GLGizmoRotate m_z;

public:
    explicit GLGizmoRotate3D(GLCanvas3D& parent);

    double get_angle_x() const { return m_x.get_angle(); }
    void set_angle_x(double angle) { m_x.set_angle(angle); }

    double get_angle_y() const { return m_y.get_angle(); }
    void set_angle_y(double angle) { m_y.set_angle(angle); }

    double get_angle_z() const { return m_z.get_angle(); }
    void set_angle_z(double angle) { m_z.set_angle(angle); }

protected:
    virtual bool on_init();
    virtual void on_set_state()
    {
        m_x.set_state(m_state);
        m_y.set_state(m_state);
        m_z.set_state(m_state);
    }
    virtual void on_set_hover_id()
    {
        m_x.set_hover_id(m_hover_id == 0 ? 0 : -1);
        m_y.set_hover_id(m_hover_id == 1 ? 0 : -1);
        m_z.set_hover_id(m_hover_id == 2 ? 0 : -1);
    }
    virtual void on_start_dragging();
    virtual void on_stop_dragging();
    virtual void on_update(const Linef3& mouse_ray)
    {
        m_x.update(mouse_ray);
        m_y.update(mouse_ray);
        m_z.update(mouse_ray);
    }
    virtual void on_refresh()
    {
        m_x.refresh();
        m_y.refresh();
        m_z.refresh();
    }
    virtual void on_render(const BoundingBoxf3& box) const;
    virtual void on_render_for_picking(const BoundingBoxf3& box) const
    {
        m_x.render_for_picking(box);
        m_y.render_for_picking(box);
        m_z.render_for_picking(box);
    }
};

class GLGizmoScale3D : public GLGizmoBase
{
    static const float Offset;

    mutable BoundingBoxf3 m_box;

    Vec3d m_scale;

    Vec3d m_starting_scale;
    Vec3d m_starting_drag_position;
    bool m_show_starting_box;
    BoundingBoxf3 m_starting_box;

public:
    explicit GLGizmoScale3D(GLCanvas3D& parent);

    double get_scale_x() const { return m_scale(0); }
    void set_scale_x(double scale) { m_starting_scale(0) = scale; }

    double get_scale_y() const { return m_scale(1); }
    void set_scale_y(double scale) { m_starting_scale(1) = scale; }

    double get_scale_z() const { return m_scale(2); }
    void set_scale_z(double scale) { m_starting_scale(2) = scale; }

    void set_scale(double scale) { m_starting_scale = scale * Vec3d::Ones(); }

protected:
    virtual bool on_init();
    virtual void on_start_dragging();
    virtual void on_stop_dragging() { m_show_starting_box = false; }
    virtual void on_update(const Linef3& mouse_ray);
    virtual void on_render(const BoundingBoxf3& box) const;
    virtual void on_render_for_picking(const BoundingBoxf3& box) const;

private:
    void render_box(const BoundingBoxf3& box) const;
    void render_grabbers_connection(unsigned int id_1, unsigned int id_2) const;

    void do_scale_x(const Linef3& mouse_ray);
    void do_scale_y(const Linef3& mouse_ray);
    void do_scale_z(const Linef3& mouse_ray);
    void do_scale_uniform(const Linef3& mouse_ray);

    double calc_ratio(unsigned int preferred_plane_id, const Linef3& mouse_ray, const Vec3d& center) const;
};

class GLGizmoFlatten : public GLGizmoBase
{
// This gizmo does not use grabbers. The m_hover_id relates to polygon managed by the class itself.

private:
    mutable Vec3d m_normal;

    struct PlaneData {
        std::vector<Vec3d> vertices;
        Vec3d normal;
        float area;
    };
    struct SourceDataSummary {
        std::vector<BoundingBoxf3> bounding_boxes; // bounding boxes of convex hulls of individual volumes
        float scaling_factor;
        float rotation;
        Vec3d mesh_first_point;
    };

    // This holds information to decide whether recalculation is necessary:
    SourceDataSummary m_source_data;

    std::vector<PlaneData> m_planes;
    std::vector<Vec2d> m_instances_positions;
    mutable std::unique_ptr<Vec3d> m_center = nullptr;
    const ModelObject* m_model_object = nullptr;

    void update_planes();
    bool is_plane_update_necessary() const;

public:
    explicit GLGizmoFlatten(GLCanvas3D& parent);

    void set_flattening_data(const ModelObject* model_object);
    Vec3d get_flattening_normal() const;

protected:
    virtual bool on_init();
    virtual void on_start_dragging();
    virtual void on_update(const Linef3& mouse_ray) {}
    virtual void on_render(const BoundingBoxf3& box) const;
    virtual void on_render_for_picking(const BoundingBoxf3& box) const;
    virtual void on_set_state() {
        if (m_state == On && is_plane_update_necessary())
            update_planes();
    }
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmo_hpp_

