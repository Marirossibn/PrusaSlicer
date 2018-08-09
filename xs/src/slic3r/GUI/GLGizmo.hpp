#ifndef slic3r_GLGizmo_hpp_
#define slic3r_GLGizmo_hpp_

#include "../../slic3r/GUI/GLTexture.hpp"
#include "../../libslic3r/Point.hpp"

#include <vector>

namespace Slic3r {

class BoundingBoxf3;
class Pointf3;

namespace GUI {

class GLGizmoBase
{
protected:
    static const float BaseColor[3];
    static const float HighlightColor[3];

    struct Grabber
    {
        static const float HalfSize;
        static const float HoverOffset;

        Pointf center;
        float angle_z;
        float color[3];

        Grabber();
        void render(bool hover) const;
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
    EState m_state;
    // textures are assumed to be square and all with the same size in pixels, no internal check is done
    GLTexture m_textures[Num_States];
    int m_hover_id;
    mutable std::vector<Grabber> m_grabbers;

public:
    GLGizmoBase();
    virtual ~GLGizmoBase();

    bool init();

    EState get_state() const;
    void set_state(EState state);

    unsigned int get_texture_id() const;
    int get_textures_size() const;

    int get_hover_id() const;
    void set_hover_id(int id);

    void start_dragging();
    void stop_dragging();
    void update(const Pointf& mouse_pos);
    void refresh();

    void render(const BoundingBoxf3& box) const;
    void render_for_picking(const BoundingBoxf3& box) const;

protected:
    virtual bool on_init() = 0;
    virtual void on_set_state();
    virtual void on_start_dragging();
    virtual void on_stop_dragging();
    virtual void on_update(const Pointf& mouse_pos) = 0;
    virtual void on_refresh();
    virtual void on_render(const BoundingBoxf3& box) const = 0;
    virtual void on_render_for_picking(const BoundingBoxf3& box) const = 0;

    void render_grabbers() const;
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

    float m_angle_z;

    mutable Pointf m_center;
    mutable float m_radius;
    mutable bool m_keep_radius;

public:
    GLGizmoRotate();

    float get_angle_z() const;
    void set_angle_z(float angle_z);

protected:
    virtual bool on_init();
    virtual void on_set_state();
    virtual void on_update(const Pointf& mouse_pos);
    virtual void on_refresh();
    virtual void on_render(const BoundingBoxf3& box) const;
    virtual void on_render_for_picking(const BoundingBoxf3& box) const;

private:
    void _render_circle() const;
    void _render_scale() const;
    void _render_snap_radii() const;
    void _render_reference_radius() const;
    void _render_angle_z() const;
    void _render_grabber() const;
};

class GLGizmoScale : public GLGizmoBase
{
    static const float Offset;

    float m_scale;
    float m_starting_scale;

    Pointf m_starting_drag_position;

public:
    GLGizmoScale();

    float get_scale() const;
    void set_scale(float scale);

protected:
    virtual bool on_init();
    virtual void on_start_dragging();
    virtual void on_update(const Pointf& mouse_pos);
    virtual void on_render(const BoundingBoxf3& box) const;
    virtual void on_render_for_picking(const BoundingBoxf3& box) const;
};


class GLGizmoFlatten : public GLGizmoBase
{
// This gizmo does not use grabbers. The m_hover_id relates to polygon managed by the class itself.

private:
    mutable Pointf3 m_normal;

    struct PlaneData {
        std::vector<Pointf3> vertices;
        Pointf3 normal;
        float color[3];
    };

    std::vector<PlaneData> m_planes;

public:
    GLGizmoFlatten();

    void set_flattening_data(std::vector<Pointf3s> vertices_list);
    Pointf3 get_flattening_normal() const;

protected:
    virtual bool on_init();
    virtual void on_start_dragging();
    virtual void on_update(const Pointf& mouse_pos);
    virtual void on_render(const BoundingBoxf3& box) const;
    virtual void on_render_for_picking(const BoundingBoxf3& box) const;
};



} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmo_hpp_

