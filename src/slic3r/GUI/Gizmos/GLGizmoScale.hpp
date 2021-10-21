#ifndef slic3r_GLGizmoScale_hpp_
#define slic3r_GLGizmoScale_hpp_

#include "GLGizmoBase.hpp"

#include "libslic3r/BoundingBox.hpp"

namespace Slic3r {
namespace GUI {

class GLGizmoScale3D : public GLGizmoBase
{
    static const double Offset;

    struct StartingData
    {
        bool ctrl_down{ false };
        Vec3d scale{ Vec3d::Ones() };
        Vec3d drag_position{ Vec3d::Zero() };
#if ENABLE_WORLD_COORDINATE
        Vec3d center{ Vec3d::Zero() };
#endif // ENABLE_WORLD_COORDINATE
        BoundingBoxf3 box;
        std::array<Vec3d, 10> pivots{ Vec3d::Zero(), Vec3d::Zero(), Vec3d::Zero(), Vec3d::Zero(), Vec3d::Zero(),
            Vec3d::Zero(), Vec3d::Zero(), Vec3d::Zero(), Vec3d::Zero(),Vec3d::Zero() };
    };

    BoundingBoxf3 m_box;
    Transform3d m_transform;
#if ENABLE_WORLD_COORDINATE
    Transform3d m_grabbers_transform;
    Vec3d m_center{ Vec3d::Zero() };
#else
    // Transforms grabbers offsets to the proper reference system (world for instances, instance for volumes)
    Transform3d m_offsets_transform;
#endif // ENABLE_WORLD_COORDINATE
    Vec3d m_scale{ Vec3d::Ones() };
    Vec3d m_offset{ Vec3d::Zero() };
    double m_snap_step{ 0.05 };
    StartingData m_starting;

public:
    GLGizmoScale3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    double get_snap_step(double step) const { return m_snap_step; }
    void set_snap_step(double step) { m_snap_step = step; }

    const Vec3d& get_scale() const { return m_scale; }
    void set_scale(const Vec3d& scale) { m_starting.scale = scale; m_scale = scale; }

    const Vec3d& get_offset() const { return m_offset; }

    std::string get_tooltip() const override;

protected:
    virtual bool on_init() override;
    virtual std::string on_get_name() const override;
    virtual bool on_is_activable() const override;
    virtual void on_start_dragging() override;
    virtual void on_update(const UpdateData& data) override;
    virtual void on_render() override;
    virtual void on_render_for_picking() override;

private:
    void render_grabbers_connection(unsigned int id_1, unsigned int id_2) const;

    void do_scale_along_axis(Axis axis, const UpdateData& data);
    void do_scale_uniform(const UpdateData& data);

    double calc_ratio(const UpdateData& data) const;
#if ENABLE_WORLD_COORDINATE
    void transform_to_local(const Selection& selection) const;
#endif // ENABLE_WORLD_COORDINATE
};


} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoScale_hpp_
