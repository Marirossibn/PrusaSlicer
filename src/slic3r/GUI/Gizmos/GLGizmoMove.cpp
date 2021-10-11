// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoMove.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#if ENABLE_WORLD_COORDINATE
#include "slic3r/GUI/GUI_ObjectManipulation.hpp"
#endif // ENABLE_WORLD_COORDINATE

#include <GL/glew.h>

#include <wx/utils.h> 

namespace Slic3r {
namespace GUI {

const double GLGizmoMove3D::Offset = 10.0;

GLGizmoMove3D::GLGizmoMove3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
{
    m_vbo_cone.init_from(its_make_cone(1., 1., 2*PI/36));
}

std::string GLGizmoMove3D::get_tooltip() const
{
    const Selection& selection = m_parent.get_selection();
    const bool show_position = selection.is_single_full_instance();
#if ENABLE_WORLD_COORDINATE
    const bool world_coordinates = wxGetApp().obj_manipul()->get_world_coordinates();
    Vec3d position = Vec3d::Zero();
    if (!world_coordinates) {
        if (selection.is_single_modifier() || selection.is_single_volume() || selection.is_wipe_tower())
            position = selection.get_volume(*selection.get_volume_idxs().begin())->get_volume_offset();
    }
    else
        position = selection.get_bounding_box().center();

    if (m_hover_id == 0)
        return m_grabbers[0].dragging ? "DX: " + format(m_displacement.x(), 2) : "X: " + format(position.x(), 2);
    else if (m_hover_id == 1)
        return m_grabbers[1].dragging ? "DY: " + format(m_displacement.y(), 2) : "Y: " + format(position.y(), 2);
    else if (m_hover_id == 2)
        return m_grabbers[2].dragging ? "DZ: " + format(m_displacement.z(), 2) : "Z: " + format(position.z(), 2);
    else
        return "";
#else
    const Vec3d& position = selection.get_bounding_box().center();

    if (m_hover_id == 0 || m_grabbers[0].dragging)
        return "X: " + format(show_position ? position.x() : m_displacement.x(), 2);
    else if (m_hover_id == 1 || m_grabbers[1].dragging)
        return "Y: " + format(show_position ? position.y() : m_displacement.y(), 2);
    else if (m_hover_id == 2 || m_grabbers[2].dragging)
        return "Z: " + format(show_position ? position.z() : m_displacement.z(), 2);
    else
        return "";
#endif // ENABLE_WORLD_COORDINATE
}

bool GLGizmoMove3D::on_init()
{
    for (int i = 0; i < 3; ++i) {
        m_grabbers.push_back(Grabber());
    }

    m_shortcut_key = WXK_CONTROL_M;

    return true;
}

std::string GLGizmoMove3D::on_get_name() const
{
    return _u8L("Move");
}

bool GLGizmoMove3D::on_is_activable() const
{
    return !m_parent.get_selection().is_empty();
}

void GLGizmoMove3D::on_start_dragging()
{
    if (m_hover_id != -1) {
        m_displacement = Vec3d::Zero();
        const BoundingBoxf3& box = m_parent.get_selection().get_bounding_box();
#if ENABLE_WORLD_COORDINATE
        const Vec3d center = box.center();
        m_starting_drag_position = center + m_grabbers[m_hover_id].center;
        m_starting_box_center = center;
        m_starting_box_bottom_center = center;
        m_starting_box_bottom_center.z() = box.min.z();
#else
        m_starting_drag_position = m_grabbers[m_hover_id].center;
        m_starting_box_center = box.center();
        m_starting_box_bottom_center = box.center();
        m_starting_box_bottom_center.z() = box.min.z();
#endif // ENABLE_WORLD_COORDINATE
    }
}

#if !ENABLE_WORLD_COORDINATE
void GLGizmoMove3D::on_stop_dragging()
{
    m_displacement = Vec3d::Zero();
}
#endif // !ENABLE_WORLD_COORDINATE

void GLGizmoMove3D::on_update(const UpdateData& data)
{
    if (m_hover_id == 0)
        m_displacement.x() = calc_projection(data);
    else if (m_hover_id == 1)
        m_displacement.y() = calc_projection(data);
    else if (m_hover_id == 2)
        m_displacement.z() = calc_projection(data);
}

void GLGizmoMove3D::on_render()
{
    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_DEPTH_TEST));

    const Selection& selection = m_parent.get_selection();
    const BoundingBoxf3& box = selection.get_bounding_box();
#if ENABLE_WORLD_COORDINATE
    glsafe(::glPushMatrix());
    transform_to_local(selection);

    const Vec3d zero = Vec3d::Zero();
    const Vec3d half_box_size = 0.5 * box.size();

    // x axis
    m_grabbers[0].center = { half_box_size.x() + Offset, 0.0, 0.0 };
    m_grabbers[0].color = AXES_COLOR[0];

    // y axis
    m_grabbers[1].center = { 0.0, half_box_size.y() + Offset, 0.0 };
    m_grabbers[1].color = AXES_COLOR[1];

    // z axis
    m_grabbers[2].center = { 0.0, 0.0, half_box_size.z() + Offset };
    m_grabbers[2].color = AXES_COLOR[2];
#else
    const Vec3d& center = box.center();

    // x axis
    m_grabbers[0].center = { box.max.x() + Offset, center.y(), center.z() };
    m_grabbers[0].color = AXES_COLOR[0];

    // y axis
    m_grabbers[1].center = { center.x(), box.max.y() + Offset, center.z() };
    m_grabbers[1].color = AXES_COLOR[1];

    // z axis
    m_grabbers[2].center = { center.x(), center.y(), box.max.z() + Offset };
    m_grabbers[2].color = AXES_COLOR[2];
#endif // ENABLE_WORLD_COORDINATE

    glsafe(::glLineWidth((m_hover_id != -1) ? 2.0f : 1.5f));

    if (m_hover_id == -1) {
        // draw axes
        for (unsigned int i = 0; i < 3; ++i) {
            if (m_grabbers[i].enabled) {
                glsafe(::glColor4fv(AXES_COLOR[i].data()));
                ::glBegin(GL_LINES);
#if ENABLE_WORLD_COORDINATE
                ::glVertex3dv(zero.data());
#else
                ::glVertex3dv(center.data());
#endif // ENABLE_WORLD_COORDINATE
                ::glVertex3dv(m_grabbers[i].center.data());
                glsafe(::glEnd());
            }
        }

        // draw grabbers
        render_grabbers(box);
        for (unsigned int i = 0; i < 3; ++i) {
            if (m_grabbers[i].enabled)
                render_grabber_extension((Axis)i, box, false);
        }
    }
    else {
        // draw axis
        glsafe(::glColor4fv(AXES_COLOR[m_hover_id].data()));
        ::glBegin(GL_LINES);
#if ENABLE_WORLD_COORDINATE
        ::glVertex3dv(zero.data());
#else
        ::glVertex3dv(center.data());
#endif // ENABLE_WORLD_COORDINATE
        ::glVertex3dv(m_grabbers[m_hover_id].center.data());
        glsafe(::glEnd());

        GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
        if (shader != nullptr) {
            shader->start_using();
            shader->set_uniform("emission_factor", 0.1f);
            // draw grabber
            const Vec3d box_size = box.size();
            const float mean_size = (float)((box_size.x() + box_size.y() + box_size.z()) / 3.0);
            m_grabbers[m_hover_id].render(true, mean_size);
            shader->stop_using();
        }
        render_grabber_extension((Axis)m_hover_id, box, false);
    }

#if ENABLE_WORLD_COORDINATE
    glsafe(::glPopMatrix());
#endif // ENABLE_WORLD_COORDINATE
}

void GLGizmoMove3D::on_render_for_picking()
{
    glsafe(::glDisable(GL_DEPTH_TEST));

#if ENABLE_WORLD_COORDINATE
    const Selection& selection = m_parent.get_selection();
    const BoundingBoxf3& box = selection.get_bounding_box();
    glsafe(::glPushMatrix());
    transform_to_local(selection);
#else
    const BoundingBoxf3& box = m_parent.get_selection().get_bounding_box();
#endif // ENABLE_WORLD_COORDINATE

    render_grabbers_for_picking(box);
    render_grabber_extension(X, box, true);
    render_grabber_extension(Y, box, true);
    render_grabber_extension(Z, box, true);

#if ENABLE_WORLD_COORDINATE
    glsafe(::glPopMatrix());
#endif // ENABLE_WORLD_COORDINATE
}

double GLGizmoMove3D::calc_projection(const UpdateData& data) const
{
    double projection = 0.0;

    const Vec3d starting_vec = m_starting_drag_position - m_starting_box_center;
    const double len_starting_vec = starting_vec.norm();
    if (len_starting_vec != 0.0) {
        const Vec3d mouse_dir = data.mouse_ray.unit_vector();
        // finds the intersection of the mouse ray with the plane parallel to the camera viewport and passing throught the starting position
        // use ray-plane intersection see i.e. https://en.wikipedia.org/wiki/Line%E2%80%93plane_intersection algebric form
        // in our case plane normal and ray direction are the same (orthogonal view)
        // when moving to perspective camera the negative z unit axis of the camera needs to be transformed in world space and used as plane normal
        const Vec3d inters = data.mouse_ray.a + (m_starting_drag_position - data.mouse_ray.a).dot(mouse_dir) / mouse_dir.squaredNorm() * mouse_dir;
        // vector from the starting position to the found intersection
        const Vec3d inters_vec = inters - m_starting_drag_position;

        // finds projection of the vector along the staring direction
        projection = inters_vec.dot(starting_vec.normalized());
    }

    if (wxGetKeyState(WXK_SHIFT))
        projection = m_snap_step * (double)std::round(projection / m_snap_step);

    return projection;
}

void GLGizmoMove3D::render_grabber_extension(Axis axis, const BoundingBoxf3& box, bool picking) const
{
    const Vec3d box_size = box.size();
    const float mean_size = (float)((box_size.x() + box_size.y() + box_size.z()) / 3.0);
    const double size = m_dragging ? (double)m_grabbers[axis].get_dragging_half_size(mean_size) : (double)m_grabbers[axis].get_half_size(mean_size);

    std::array<float, 4> color = m_grabbers[axis].color;
    if (!picking && m_hover_id != -1) {
        color[0] = 1.0f - color[0];
        color[1] = 1.0f - color[1];
        color[2] = 1.0f - color[2];
        color[3] = color[3];
    }

    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr)
        return;

    const_cast<GLModel*>(&m_vbo_cone)->set_color(-1, color);
    if (!picking) {
        shader->start_using();
        shader->set_uniform("emission_factor", 0.1f);
    }

    glsafe(::glPushMatrix());
    glsafe(::glTranslated(m_grabbers[axis].center.x(), m_grabbers[axis].center.y(), m_grabbers[axis].center.z()));
    if (axis == X)
        glsafe(::glRotated(90.0, 0.0, 1.0, 0.0));
    else if (axis == Y)
        glsafe(::glRotated(-90.0, 1.0, 0.0, 0.0));

    glsafe(::glTranslated(0.0, 0.0, 2.0 * size));
    glsafe(::glScaled(0.75 * size, 0.75 * size, 3.0 * size));
    m_vbo_cone.render();
    glsafe(::glPopMatrix());

    if (! picking)
        shader->stop_using();
}

#if ENABLE_WORLD_COORDINATE
void GLGizmoMove3D::transform_to_local(const Selection& selection) const
{
    const Vec3d center = selection.get_bounding_box().center();
    glsafe(::glTranslated(center.x(), center.y(), center.z()));

    if (!wxGetApp().obj_manipul()->get_world_coordinates()) {
        const Transform3d orient_matrix = selection.get_volume(*selection.get_volume_idxs().begin())->get_instance_transformation().get_matrix(true, false, true, true);
        glsafe(::glMultMatrixd(orient_matrix.data()));
    }
}
#endif // ENABLE_WORLD_COORDINATE


} // namespace GUI
} // namespace Slic3r
