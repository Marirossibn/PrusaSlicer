// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoScale.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#if ENABLE_WORLD_COORDINATE
#include "slic3r/GUI/GUI_ObjectManipulation.hpp"
#endif // ENABLE_WORLD_COORDINATE
#if ENABLE_GL_SHADERS_ATTRIBUTES
#include "slic3r/GUI/Plater.hpp"
#endif // ENABLE_GL_SHADERS_ATTRIBUTES

#include <GL/glew.h>

#include <wx/utils.h> 

namespace Slic3r {
namespace GUI {


const double GLGizmoScale3D::Offset = 5.0;

GLGizmoScale3D::GLGizmoScale3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
    , m_scale(Vec3d::Ones())
    , m_offset(Vec3d::Zero())
    , m_snap_step(0.05)
    , m_base_color(DEFAULT_BASE_COLOR)
    , m_drag_color(DEFAULT_DRAG_COLOR)
    , m_highlight_color(DEFAULT_HIGHLIGHT_COLOR)
{
#if ENABLE_LEGACY_OPENGL_REMOVAL
    m_grabber_connections[0].grabber_indices = { 0, 1 };
    m_grabber_connections[1].grabber_indices = { 2, 3 };
    m_grabber_connections[2].grabber_indices = { 4, 5 };
    m_grabber_connections[3].grabber_indices = { 6, 7 };
    m_grabber_connections[4].grabber_indices = { 7, 8 }; 
    m_grabber_connections[5].grabber_indices = { 8, 9 };
    m_grabber_connections[6].grabber_indices = { 9, 6 };
#endif // ENABLE_LEGACY_OPENGL_REMOVAL
}

std::string GLGizmoScale3D::get_tooltip() const
{
    const Selection& selection = m_parent.get_selection();

    Vec3d scale = 100.0 * Vec3d::Ones();
    if (selection.is_single_full_instance())
        scale = 100.0 * selection.get_volume(*selection.get_volume_idxs().begin())->get_instance_scaling_factor();
    else if (selection.is_single_modifier() || selection.is_single_volume())
        scale = 100.0 * selection.get_volume(*selection.get_volume_idxs().begin())->get_volume_scaling_factor();

    if (m_hover_id == 0 || m_hover_id == 1 || m_grabbers[0].dragging || m_grabbers[1].dragging)
        return "X: " + format(scale.x(), 4) + "%";
    else if (m_hover_id == 2 || m_hover_id == 3 || m_grabbers[2].dragging || m_grabbers[3].dragging)
        return "Y: " + format(scale.y(), 4) + "%";
    else if (m_hover_id == 4 || m_hover_id == 5 || m_grabbers[4].dragging || m_grabbers[5].dragging)
        return "Z: " + format(scale.z(), 4) + "%";
    else if (m_hover_id == 6 || m_hover_id == 7 || m_hover_id == 8 || m_hover_id == 9 || 
        m_grabbers[6].dragging || m_grabbers[7].dragging || m_grabbers[8].dragging || m_grabbers[9].dragging)
    {
        std::string tooltip = "X: " + format(scale.x(), 4) + "%\n";
        tooltip += "Y: " + format(scale.y(), 4) + "%\n";
        tooltip += "Z: " + format(scale.z(), 4) + "%";
        return tooltip;
    }
    else
        return "";
}

bool GLGizmoScale3D::on_mouse(const wxMouseEvent &mouse_event)
{
    if (mouse_event.Dragging()) {
        if (m_dragging) {
            // Apply new temporary scale factors
            TransformationType transformation_type(TransformationType::Local_Absolute_Joint);
            if (mouse_event.AltDown()) transformation_type.set_independent();

            Selection &selection = m_parent.get_selection();
            selection.scale(get_scale(), transformation_type);
            if (mouse_event.CmdDown()) selection.translate(m_offset, true);
        }
    }
    return use_grabbers(mouse_event);
}

void GLGizmoScale3D::data_changed()
{
    const Selection &selection        = m_parent.get_selection();
    bool             enable_scale_xyz = selection.is_single_full_instance() ||
                            selection.is_single_volume() ||
                            selection.is_single_modifier();
    for (unsigned int i = 0; i < 6; ++i)
        m_grabbers[i].enabled = enable_scale_xyz;

    if (enable_scale_xyz) {
        // all volumes in the selection belongs to the same instance, any of
        // them contains the needed data, so we take the first
        const GLVolume *volume = selection.get_volume(*selection.get_volume_idxs().begin());
        if (selection.is_single_full_instance()) {
            set_scale(volume->get_instance_scaling_factor());
        } else if (selection.is_single_volume() ||
                   selection.is_single_modifier()) {
            set_scale(volume->get_volume_scaling_factor());
        }
    } else {
        set_scale(Vec3d::Ones());
    }
}

bool GLGizmoScale3D::on_init()
{
    for (int i = 0; i < 10; ++i) {
        m_grabbers.push_back(Grabber());
    }

#if !ENABLE_WORLD_COORDINATE
    double half_pi = 0.5 * (double)PI;

    // x axis
    m_grabbers[0].angles.y() = half_pi;
    m_grabbers[1].angles.y() = half_pi;

    // y axis
    m_grabbers[2].angles.x() = half_pi;
    m_grabbers[3].angles.x() = half_pi;
#endif // !ENABLE_WORLD_COORDINATE

    m_shortcut_key = WXK_CONTROL_S;

    return true;
}

std::string GLGizmoScale3D::on_get_name() const
{
    return _u8L("Scale");
}

bool GLGizmoScale3D::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();
    return !selection.is_empty() && !selection.is_wipe_tower();
}

void GLGizmoScale3D::on_start_dragging()
{
    if (m_hover_id != -1) {
        m_starting.ctrl_down = wxGetKeyState(WXK_CONTROL);
#if ENABLE_WORLD_COORDINATE
        m_starting.drag_position = m_grabbers_transform * m_grabbers[m_hover_id].center;
        m_starting.box = m_box;
        m_starting.center = m_center;
        m_starting.transform = wxGetApp().obj_manipul()->get_world_coordinates() ? Transform3d::Identity() : m_transform;
#else
        m_starting.drag_position = m_grabbers[m_hover_id].center;
        m_starting.box = (m_starting.ctrl_down && m_hover_id < 6) ? m_box : m_parent.get_selection().get_bounding_box();

        const Vec3d center = m_starting.box.center();
        m_starting.pivots[0] = m_transform * Vec3d(m_starting.box.max.x(), center.y(), center.z());
        m_starting.pivots[1] = m_transform * Vec3d(m_starting.box.min.x(), center.y(), center.z());
        m_starting.pivots[2] = m_transform * Vec3d(center.x(), m_starting.box.max.y(), center.z());
        m_starting.pivots[3] = m_transform * Vec3d(center.x(), m_starting.box.min.y(), center.z());
        m_starting.pivots[4] = m_transform * Vec3d(center.x(), center.y(), m_starting.box.max.z());
        m_starting.pivots[5] = m_transform * Vec3d(center.x(), center.y(), m_starting.box.min.z());
#endif // ENABLE_WORLD_COORDINATE
    }
}

void GLGizmoScale3D::on_stop_dragging() {
    m_parent.do_scale(L("Gizmo-Scale"));
}

void GLGizmoScale3D::on_dragging(const UpdateData& data)
{
    if (m_hover_id == 0 || m_hover_id == 1)
        do_scale_along_axis(X, data);
    else if (m_hover_id == 2 || m_hover_id == 3)
        do_scale_along_axis(Y, data);
    else if (m_hover_id == 4 || m_hover_id == 5)
        do_scale_along_axis(Z, data);
    else if (m_hover_id >= 6)
        do_scale_uniform(data);
}

void GLGizmoScale3D::on_render()
{
    const Selection& selection = m_parent.get_selection();

    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_DEPTH_TEST));

    m_box.reset();
    m_transform = Transform3d::Identity();
#if ENABLE_WORLD_COORDINATE
    m_grabbers_transform = Transform3d::Identity();
    bool world_coordinates = wxGetApp().obj_manipul()->get_world_coordinates();
    if (selection.is_single_full_instance() && !world_coordinates) {
#else
    // Transforms grabbers' offsets to world refefence system 
    Transform3d offsets_transform = Transform3d::Identity();
    m_offsets_transform = Transform3d::Identity();
    Vec3d angles = Vec3d::Zero();

    if (selection.is_single_full_instance()) {
#endif // ENABLE_WORLD_COORDINATE    
        // calculate bounding box in instance local reference system
        const Selection::IndicesList& idxs = selection.get_volume_idxs();
        for (unsigned int idx : idxs) {
            const GLVolume* v = selection.get_volume(idx);
#if ENABLE_WORLD_COORDINATE
            m_box.merge(v->transformed_convex_hull_bounding_box(v->get_volume_transformation().get_matrix()));
#else
            m_box.merge(v->transformed_convex_hull_bounding_box(v->get_volume_transformation().get_matrix()));
#endif // ENABLE_WORLD_COORDINATE
        }

#if ENABLE_WORLD_COORDINATE
        m_box = m_box.transformed(selection.get_volume(*idxs.begin())->get_instance_transformation().get_matrix(true, true, false, true));
#endif // ENABLE_WORLD_COORDINATE

        // gets transform from first selected volume
        const GLVolume* v = selection.get_volume(*idxs.begin());
#if ENABLE_WORLD_COORDINATE
        m_transform = v->get_instance_transformation().get_matrix(false, false, true);
        m_grabbers_transform = v->get_instance_transformation().get_matrix(false, false, true) * Geometry::assemble_transform(m_box.center());
        m_center = v->get_instance_offset();
#else
        m_transform = v->get_instance_transformation().get_matrix();

        // gets angles from first selected volume
        angles = v->get_instance_rotation();
        // consider rotation+mirror only components of the transform for offsets
        offsets_transform = Geometry::assemble_transform(Vec3d::Zero(), angles, Vec3d::Ones(), v->get_instance_mirror());
        m_offsets_transform = offsets_transform;
#endif // ENABLE_WORLD_COORDINATE
    }
#if ENABLE_WORLD_COORDINATE
    else if ((selection.is_single_modifier() || selection.is_single_volume()) && !world_coordinates) {
#else
    else if (selection.is_single_modifier() || selection.is_single_volume()) {
#endif // ENABLE_WORLD_COORDINATE
        const GLVolume* v = selection.get_volume(*selection.get_volume_idxs().begin());
#if ENABLE_WORLD_COORDINATE
        m_box.merge(v->transformed_convex_hull_bounding_box(v->get_volume_transformation().get_matrix(true, true, false, true)));
#else
        m_box = v->bounding_box();
#endif // ENABLE_WORLD_COORDINATE
#if ENABLE_WORLD_COORDINATE
        m_transform = v->world_matrix();
        m_grabbers_transform = v->get_instance_transformation().get_matrix(false, false, true) * v->get_volume_transformation().get_matrix(false, false, true);
        m_center = v->world_matrix() * m_box.center();
#else
        m_transform = v->world_matrix();
        angles = Geometry::extract_euler_angles(m_transform);
        // consider rotation+mirror only components of the transform for offsets
        offsets_transform = Geometry::assemble_transform(Vec3d::Zero(), angles, Vec3d::Ones(), v->get_instance_mirror());
        m_offsets_transform = Geometry::assemble_transform(Vec3d::Zero(), v->get_volume_rotation(), Vec3d::Ones(), v->get_volume_mirror());
#endif // ENABLE_WORLD_COORDINATE
    }
#if ENABLE_WORLD_COORDINATE
    else {
        m_box = selection.get_bounding_box();
        m_transform = Geometry::assemble_transform(m_box.center());
        m_grabbers_transform = m_transform;
        m_center = selection.is_single_full_instance() ? selection.get_volume(*selection.get_volume_idxs().begin())->get_instance_offset() : m_box.center();
    }
#else
    else
        m_box = selection.get_bounding_box();

    Vec3d offset_x = offsets_transform * (Offset * Vec3d::UnitX());
    Vec3d offset_y = offsets_transform * (Offset * Vec3d::UnitY());
    Vec3d offset_z = offsets_transform * (Offset * Vec3d::UnitZ());

    bool ctrl_down = m_dragging && m_starting.ctrl_down || !m_dragging && wxGetKeyState(WXK_CONTROL);
#endif // ENABLE_WORLD_COORDINATE

    // x axis
#if ENABLE_WORLD_COORDINATE
    const Vec3d box_half_size = 0.5 * m_box.size();
    bool use_constrain = wxGetKeyState(WXK_CONTROL) && (selection.is_single_full_instance() || selection.is_single_volume() || selection.is_single_modifier());

    m_grabbers[0].center = { -(box_half_size.x() + Offset), 0.0, 0.0 };
    m_grabbers[0].color = (use_constrain && m_hover_id == 1) ? CONSTRAINED_COLOR : AXES_COLOR[0];
    m_grabbers[1].center = { box_half_size.x() + Offset, 0.0, 0.0 };
    m_grabbers[1].color = (use_constrain && m_hover_id == 0) ? CONSTRAINED_COLOR : AXES_COLOR[0];
#else
    const Vec3d center = m_box.center();

    m_grabbers[0].center = m_transform * Vec3d(m_box.min.x(), center.y(), center.z()) - offset_x;
    m_grabbers[0].color = (ctrl_down && m_hover_id == 1) ? CONSTRAINED_COLOR : AXES_COLOR[0];
    m_grabbers[1].center = m_transform * Vec3d(m_box.max.x(), center.y(), center.z()) + offset_x;
    m_grabbers[1].color = (ctrl_down && m_hover_id == 0) ? CONSTRAINED_COLOR : AXES_COLOR[0];
#endif // ENABLE_WORLD_COORDINATE

    // y axis
#if ENABLE_WORLD_COORDINATE
    m_grabbers[2].center = { 0.0, -(box_half_size.y() + Offset), 0.0 };
    m_grabbers[2].color = (use_constrain && m_hover_id == 3) ? CONSTRAINED_COLOR : AXES_COLOR[1];
    m_grabbers[3].center = { 0.0, box_half_size.y() + Offset, 0.0 };
    m_grabbers[3].color = (use_constrain && m_hover_id == 2) ? CONSTRAINED_COLOR : AXES_COLOR[1];
#else
    m_grabbers[2].center = m_transform * Vec3d(center.x(), m_box.min.y(), center.z()) - offset_y;
    m_grabbers[2].color = (ctrl_down && m_hover_id == 3) ? CONSTRAINED_COLOR : AXES_COLOR[1];
    m_grabbers[3].center = m_transform * Vec3d(center.x(), m_box.max.y(), center.z()) + offset_y;
    m_grabbers[3].color = (ctrl_down && m_hover_id == 2) ? CONSTRAINED_COLOR : AXES_COLOR[1];
#endif // ENABLE_WORLD_COORDINATE

    // z axis
#if ENABLE_WORLD_COORDINATE
    m_grabbers[4].center = { 0.0, 0.0, -(box_half_size.z() + Offset) };
    m_grabbers[4].color = (use_constrain && m_hover_id == 5) ? CONSTRAINED_COLOR : AXES_COLOR[2];
    m_grabbers[5].center = { 0.0, 0.0, box_half_size.z() + Offset };
    m_grabbers[5].color = (use_constrain && m_hover_id == 4) ? CONSTRAINED_COLOR : AXES_COLOR[2];
#else
    m_grabbers[4].center = m_transform * Vec3d(center.x(), center.y(), m_box.min.z()) - offset_z;
    m_grabbers[4].color = (ctrl_down && m_hover_id == 5) ? CONSTRAINED_COLOR : AXES_COLOR[2];
    m_grabbers[5].center = m_transform * Vec3d(center.x(), center.y(), m_box.max.z()) + offset_z;
    m_grabbers[5].color = (ctrl_down && m_hover_id == 4) ? CONSTRAINED_COLOR : AXES_COLOR[2];
#endif // ENABLE_WORLD_COORDINATE

    // uniform
#if ENABLE_WORLD_COORDINATE
    m_grabbers[6].center = { -(box_half_size.x() + Offset), -(box_half_size.y() + Offset), 0.0 };
    m_grabbers[6].color = (use_constrain && m_hover_id == 8) ? CONSTRAINED_COLOR : m_highlight_color;
    m_grabbers[7].center = { box_half_size.x() + Offset, -(box_half_size.y() + Offset), 0.0 };
    m_grabbers[7].color = (use_constrain && m_hover_id == 9) ? CONSTRAINED_COLOR : m_highlight_color;
    m_grabbers[8].center = { box_half_size.x() + Offset, box_half_size.y() + Offset, 0.0 };
    m_grabbers[8].color = (use_constrain && m_hover_id == 6) ? CONSTRAINED_COLOR : m_highlight_color;
    m_grabbers[9].center = { -(box_half_size.x() + Offset), box_half_size.y() + Offset, 0.0 };
    m_grabbers[9].color = (use_constrain && m_hover_id == 7) ? CONSTRAINED_COLOR : m_highlight_color;
#else
    m_grabbers[6].center = m_transform * Vec3d(m_box.min.x(), m_box.min.y(), center.z()) - offset_x - offset_y;
    m_grabbers[7].center = m_transform * Vec3d(m_box.max.x(), m_box.min.y(), center.z()) + offset_x - offset_y;
    m_grabbers[8].center = m_transform * Vec3d(m_box.max.x(), m_box.max.y(), center.z()) + offset_x + offset_y;
    m_grabbers[9].center = m_transform * Vec3d(m_box.min.x(), m_box.max.y(), center.z()) - offset_x + offset_y;
    for (int i = 6; i < 10; ++i) {
        m_grabbers[i].color = m_highlight_color;
    }
#endif // ENABLE_WORLD_COORDINATE

#if !ENABLE_WORLD_COORDINATE
    // sets grabbers orientation
    for (int i = 0; i < 10; ++i) {
        m_grabbers[i].angles = angles;
    }
#endif // !ENABLE_WORLD_COORDINATE

    glsafe(::glLineWidth((m_hover_id != -1) ? 2.0f : 1.5f));
#if ENABLE_WORLD_COORDINATE
    glsafe(::glPushMatrix());
    transform_to_local(selection);

    const float grabber_mean_size = (float)((m_box.size().x() + m_box.size().y() + m_box.size().z()) / 3.0);
#else
    const BoundingBoxf3& selection_box = selection.get_bounding_box();
    const float grabber_mean_size = (float)((selection_box.size().x() + selection_box.size().y() + selection_box.size().z()) / 3.0);
#endif // ENABLE_WORLD_COORDINATE

    if (m_hover_id == -1) {
#if ENABLE_LEGACY_OPENGL_REMOVAL
        // draw connections
        GLShaderProgram* shader = wxGetApp().get_shader("flat");
        if (shader != nullptr) {
            shader->start_using();
#if ENABLE_GL_SHADERS_ATTRIBUTES
            const Camera& camera = wxGetApp().plater()->get_camera();
            shader->set_uniform("view_model_matrix", camera.get_view_matrix());
            shader->set_uniform("projection_matrix", camera.get_projection_matrix());
#endif // ENABLE_GL_SHADERS_ATTRIBUTES
            if (m_grabbers[0].enabled && m_grabbers[1].enabled)
                render_grabbers_connection(0, 1, m_grabbers[0].color);
            if (m_grabbers[2].enabled && m_grabbers[3].enabled)
                render_grabbers_connection(2, 3, m_grabbers[2].color);
            if (m_grabbers[4].enabled && m_grabbers[5].enabled)
                render_grabbers_connection(4, 5, m_grabbers[4].color);
            render_grabbers_connection(6, 7, m_base_color);
            render_grabbers_connection(7, 8, m_base_color);
            render_grabbers_connection(8, 9, m_base_color);
            render_grabbers_connection(9, 6, m_base_color);
            shader->stop_using();
        }
#else
        // draw connections
        if (m_grabbers[0].enabled && m_grabbers[1].enabled) {
            glsafe(::glColor4fv(m_grabbers[0].color.data()));
            render_grabbers_connection(0, 1);
        }
        if (m_grabbers[2].enabled && m_grabbers[3].enabled) {
            glsafe(::glColor4fv(m_grabbers[2].color.data()));
            render_grabbers_connection(2, 3);
        }
        if (m_grabbers[4].enabled && m_grabbers[5].enabled) {
            glsafe(::glColor4fv(m_grabbers[4].color.data()));
            render_grabbers_connection(4, 5);
        }
        glsafe(::glColor4fv(m_base_color.data()));
        render_grabbers_connection(6, 7);
        render_grabbers_connection(7, 8);
        render_grabbers_connection(8, 9);
        render_grabbers_connection(9, 6);
#endif // ENABLE_LEGACY_OPENGL_REMOVAL

        // draw grabbers
        render_grabbers(grabber_mean_size);
    }
    else if (m_hover_id == 0 || m_hover_id == 1) {
#if ENABLE_LEGACY_OPENGL_REMOVAL
        // draw connections
        GLShaderProgram* shader = wxGetApp().get_shader("flat");
        if (shader != nullptr) {
            shader->start_using();
#if ENABLE_GL_SHADERS_ATTRIBUTES
            const Camera& camera = wxGetApp().plater()->get_camera();
            shader->set_uniform("view_model_matrix", camera.get_view_matrix());
            shader->set_uniform("projection_matrix", camera.get_projection_matrix());
#endif // ENABLE_GL_SHADERS_ATTRIBUTES
            render_grabbers_connection(0, 1, m_grabbers[0].color);
            shader->stop_using();
        }

        // draw grabbers
        shader = wxGetApp().get_shader("gouraud_light");
#else
        // draw connection
        glsafe(::glColor4fv(AXES_COLOR[0].data()));
        render_grabbers_connection(0, 1);

        // draw grabbers
        GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
#endif // ENABLE_LEGACY_OPENGL_REMOVAL
        if (shader != nullptr) {
            shader->start_using();
            shader->set_uniform("emission_factor", 0.1f);
            m_grabbers[0].render(true, grabber_mean_size);
            m_grabbers[1].render(true, grabber_mean_size);
            shader->stop_using();
        }
    }
    else if (m_hover_id == 2 || m_hover_id == 3) {
#if ENABLE_LEGACY_OPENGL_REMOVAL
        // draw connections
        GLShaderProgram* shader = wxGetApp().get_shader("flat");
        if (shader != nullptr) {
            shader->start_using();
#if ENABLE_GL_SHADERS_ATTRIBUTES
            const Camera& camera = wxGetApp().plater()->get_camera();
            shader->set_uniform("view_model_matrix", camera.get_view_matrix());
            shader->set_uniform("projection_matrix", camera.get_projection_matrix());
#endif // ENABLE_GL_SHADERS_ATTRIBUTES
            render_grabbers_connection(2, 3, m_grabbers[2].color);
            shader->stop_using();
        }

        // draw grabbers
        shader = wxGetApp().get_shader("gouraud_light");
#else
        // draw connection
        glsafe(::glColor4fv(AXES_COLOR[1].data()));
        render_grabbers_connection(2, 3);

        // draw grabbers
        GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
#endif // ENABLE_LEGACY_OPENGL_REMOVAL
        if (shader != nullptr) {
            shader->start_using();
            shader->set_uniform("emission_factor", 0.1f);
            m_grabbers[2].render(true, grabber_mean_size);
            m_grabbers[3].render(true, grabber_mean_size);
            shader->stop_using();
        }
    }
    else if (m_hover_id == 4 || m_hover_id == 5) {
#if ENABLE_LEGACY_OPENGL_REMOVAL
        // draw connections
        GLShaderProgram* shader = wxGetApp().get_shader("flat");
        if (shader != nullptr) {
            shader->start_using();
#if ENABLE_GL_SHADERS_ATTRIBUTES
            const Camera& camera = wxGetApp().plater()->get_camera();
            shader->set_uniform("view_model_matrix", camera.get_view_matrix());
            shader->set_uniform("projection_matrix", camera.get_projection_matrix());
#endif // ENABLE_GL_SHADERS_ATTRIBUTES
            render_grabbers_connection(4, 5, m_grabbers[4].color);
            shader->stop_using();
        }

        // draw grabbers
        shader = wxGetApp().get_shader("gouraud_light");
#else
        // draw connection
        glsafe(::glColor4fv(AXES_COLOR[2].data()));
        render_grabbers_connection(4, 5);

        // draw grabbers
        GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
#endif // ENABLE_LEGACY_OPENGL_REMOVAL
        if (shader != nullptr) {
            shader->start_using();
            shader->set_uniform("emission_factor", 0.1f);
            m_grabbers[4].render(true, grabber_mean_size);
            m_grabbers[5].render(true, grabber_mean_size);
            shader->stop_using();
        }
    }
    else if (m_hover_id >= 6) {
#if ENABLE_LEGACY_OPENGL_REMOVAL
        // draw connections
        GLShaderProgram* shader = wxGetApp().get_shader("flat");
        if (shader != nullptr) {
            shader->start_using();
#if ENABLE_GL_SHADERS_ATTRIBUTES
            const Camera& camera = wxGetApp().plater()->get_camera();
            shader->set_uniform("view_model_matrix", camera.get_view_matrix());
            shader->set_uniform("projection_matrix", camera.get_projection_matrix());
#endif // ENABLE_GL_SHADERS_ATTRIBUTES
            render_grabbers_connection(6, 7, m_drag_color);
            render_grabbers_connection(7, 8, m_drag_color);
            render_grabbers_connection(8, 9, m_drag_color);
            render_grabbers_connection(9, 6, m_drag_color);
            shader->stop_using();
        }

        // draw grabbers
        shader = wxGetApp().get_shader("gouraud_light");
#else
        // draw connection
        glsafe(::glColor4fv(m_drag_color.data()));
        render_grabbers_connection(6, 7);
        render_grabbers_connection(7, 8);
        render_grabbers_connection(8, 9);
        render_grabbers_connection(9, 6);

        // draw grabbers
        GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
#endif // ENABLE_LEGACY_OPENGL_REMOVAL
        if (shader != nullptr) {
            shader->start_using();
            shader->set_uniform("emission_factor", 0.1f);
            for (int i = 6; i < 10; ++i) {
                m_grabbers[i].render(true, grabber_mean_size);
            }
            shader->stop_using();
        }
    }

#if ENABLE_WORLD_COORDINATE
    glsafe(::glPopMatrix());
#endif // ENABLE_WORLD_COORDINATE
}

void GLGizmoScale3D::on_render_for_picking()
{
    glsafe(::glDisable(GL_DEPTH_TEST));
#if ENABLE_WORLD_COORDINATE
    glsafe(::glPushMatrix());
    transform_to_local(m_parent.get_selection());
    render_grabbers_for_picking(m_box);
    glsafe(::glPopMatrix());
#else
    render_grabbers_for_picking(m_parent.get_selection().get_bounding_box());
#endif // ENABLE_WORLD_COORDINATE
}

#if ENABLE_LEGACY_OPENGL_REMOVAL
void GLGizmoScale3D::render_grabbers_connection(unsigned int id_1, unsigned int id_2, const ColorRGBA& color)
{
    auto grabber_connection = [this](unsigned int id_1, unsigned int id_2) {
        for (int i = 0; i < int(m_grabber_connections.size()); ++i) {
            if (m_grabber_connections[i].grabber_indices.first == id_1 && m_grabber_connections[i].grabber_indices.second == id_2)
                return i;
        }
        return -1;
    };

    const int id = grabber_connection(id_1, id_2);
    if (id == -1)
        return;

    if (!m_grabber_connections[id].model.is_initialized() ||
        !m_grabber_connections[id].old_v1.isApprox(m_grabbers[id_1].center) ||
        !m_grabber_connections[id].old_v2.isApprox(m_grabbers[id_2].center)) {
        m_grabber_connections[id].old_v1 = m_grabbers[id_1].center;
        m_grabber_connections[id].old_v2 = m_grabbers[id_2].center;
        m_grabber_connections[id].model.reset();

        GLModel::Geometry init_data;
        init_data.format = { GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
        init_data.reserve_vertices(2);
        init_data.reserve_indices(2);

        // vertices
        init_data.add_vertex((Vec3f)m_grabbers[id_1].center.cast<float>());
        init_data.add_vertex((Vec3f)m_grabbers[id_2].center.cast<float>());

        // indices
        init_data.add_line(0, 1);

        m_grabber_connections[id].model.init_from(std::move(init_data));
    }

    m_grabber_connections[id].model.set_color(color);
    m_grabber_connections[id].model.render();
}
#else
void GLGizmoScale3D::render_grabbers_connection(unsigned int id_1, unsigned int id_2) const
{
    unsigned int grabbers_count = (unsigned int)m_grabbers.size();
    if (id_1 < grabbers_count && id_2 < grabbers_count) {
        ::glBegin(GL_LINES);
        ::glVertex3dv(m_grabbers[id_1].center.data());
        ::glVertex3dv(m_grabbers[id_2].center.data());
        glsafe(::glEnd());
    }
}
#endif // ENABLE_LEGACY_OPENGL_REMOVAL

void GLGizmoScale3D::do_scale_along_axis(Axis axis, const UpdateData& data)
{
#if ENABLE_WORLD_COORDINATE
    double ratio = calc_ratio(data);
    if (ratio > 0.0) {
        Vec3d curr_scale = m_scale;
        Vec3d starting_scale = m_starting.scale;
        const Selection& selection = m_parent.get_selection();
        const bool world_coordinates = wxGetApp().obj_manipul()->get_world_coordinates();
        if (selection.is_single_full_instance() && world_coordinates) {
            const Transform3d m = Geometry::assemble_transform(Vec3d::Zero(), selection.get_volume(*selection.get_volume_idxs().begin())->get_instance_rotation());
            curr_scale = (m * curr_scale).cwiseAbs();
            starting_scale = (m * starting_scale).cwiseAbs();
        }
        else if ((selection.is_single_volume() || selection.is_single_modifier()) && world_coordinates) {
            const Transform3d mi = Geometry::assemble_transform(Vec3d::Zero(), selection.get_volume(*selection.get_volume_idxs().begin())->get_instance_rotation());
            const Transform3d mv = Geometry::assemble_transform(Vec3d::Zero(), selection.get_volume(*selection.get_volume_idxs().begin())->get_volume_rotation());
            const Transform3d m = mi * mv;
            curr_scale = (m * curr_scale).cwiseAbs();
            starting_scale = (m * starting_scale).cwiseAbs();
        }

        curr_scale(axis) = starting_scale(axis) * ratio;

        if (selection.is_single_full_instance() && world_coordinates)
            m_scale = (Geometry::assemble_transform(Vec3d::Zero(), selection.get_volume(*selection.get_volume_idxs().begin())->get_instance_rotation()).inverse() * curr_scale).cwiseAbs();
        else if ((selection.is_single_volume() || selection.is_single_modifier()) && world_coordinates) {
            const Transform3d mi = Geometry::assemble_transform(Vec3d::Zero(), selection.get_volume(*selection.get_volume_idxs().begin())->get_instance_rotation()).inverse();
            const Transform3d mv = Geometry::assemble_transform(Vec3d::Zero(), selection.get_volume(*selection.get_volume_idxs().begin())->get_volume_rotation()).inverse();
            m_scale = (mv * mi * curr_scale).cwiseAbs();
        }
        else
            m_scale = curr_scale;
#else
    const double ratio = calc_ratio(data);
    if (ratio > 0.0) {
        m_scale(axis) = m_starting.scale(axis) * ratio;
#endif // ENABLE_WORLD_COORDINATE
        if (m_starting.ctrl_down) {
#if ENABLE_WORLD_COORDINATE
            double local_offset = 0.5 * (ratio - 1.0) * m_starting.box.size()(axis);
#else
            double local_offset = 0.5 * (m_scale(axis) - m_starting.scale(axis)) * m_starting.box.size()(axis);
#endif // ENABLE_WORLD_COORDINATE

            if (m_hover_id == 2 * axis)
                local_offset *= -1.0;

#if ENABLE_WORLD_COORDINATE
            Vec3d center_offset = m_starting.center - m_starting.transform * m_starting.box.center();
            if (selection.is_single_full_instance() && !world_coordinates) {
                const Transform3d m = Geometry::assemble_transform(Vec3d::Zero(), selection.get_volume(*selection.get_volume_idxs().begin())->get_instance_rotation()).inverse();
                center_offset = m * center_offset;
            }

            local_offset += (ratio - 1.0) * center_offset(axis);

            switch (axis)
            {
            case X:  { m_offset = local_offset * Vec3d::UnitX(); break; }
            case Y:  { m_offset = local_offset * Vec3d::UnitY(); break; }
            case Z:  { m_offset = local_offset * Vec3d::UnitZ(); break; }
            default: { m_offset = Vec3d::Zero(); break; }
            }

            if ((selection.is_single_volume() || selection.is_single_modifier()) && !world_coordinates) {
                const Transform3d m = Geometry::assemble_transform(Vec3d::Zero(), selection.get_volume(*selection.get_volume_idxs().begin())->get_volume_rotation());
                m_offset = m * m_offset;
            }
#else
            Vec3d local_offset_vec;
            switch (axis)
            {
            case X: { local_offset_vec = local_offset * Vec3d::UnitX(); break; }
            case Y: { local_offset_vec = local_offset * Vec3d::UnitY(); break; }
            case Z: { local_offset_vec = local_offset * Vec3d::UnitZ(); break; }
            default: break;
            }

            m_offset = m_offsets_transform * local_offset_vec;
#endif // ENABLE_WORLD_COORDINATE
        }
        else
            m_offset = Vec3d::Zero();
    }
}

void GLGizmoScale3D::do_scale_uniform(const UpdateData& data)
{
    const double ratio = calc_ratio(data);
    if (ratio > 0.0) {
        m_scale = m_starting.scale * ratio;
#if ENABLE_WORLD_COORDINATE
        if (m_starting.ctrl_down) {
            m_offset = 0.5 * (ratio - 1.0) * m_starting.box.size();

            if (m_hover_id == 6 || m_hover_id == 9)
                m_offset.x() *= -1.0;
            if (m_hover_id == 6 || m_hover_id == 7)
                m_offset.y() *= -1.0;

            const Selection& selection = m_parent.get_selection();
            const bool world_coordinates = wxGetApp().obj_manipul()->get_world_coordinates();
            Vec3d center_offset = m_starting.center - m_starting.transform * m_starting.box.center();
            if (selection.is_single_full_instance() && !world_coordinates) {
                const Transform3d m = Geometry::assemble_transform(Vec3d::Zero(), selection.get_volume(*selection.get_volume_idxs().begin())->get_instance_rotation()).inverse();
                center_offset = m * center_offset;
            }
            
            m_offset += (ratio - 1.0) * center_offset;

            if ((selection.is_single_volume() || selection.is_single_modifier()) && !world_coordinates) {
                const Transform3d m = Geometry::assemble_transform(Vec3d::Zero(), selection.get_volume(*selection.get_volume_idxs().begin())->get_volume_rotation());
                m_offset = m * m_offset;
            }
        }
        else
#endif // ENABLE_WORLD_COORDINATE
        m_offset = Vec3d::Zero();
}
}

double GLGizmoScale3D::calc_ratio(const UpdateData& data) const
{
    double ratio = 0.0;

#if ENABLE_WORLD_COORDINATE
    const Vec3d starting_vec = m_starting.drag_position - m_starting.center;
#else
    const Vec3d pivot = (m_starting.ctrl_down && m_hover_id < 6) ? m_starting.pivots[m_hover_id] : m_starting.box.center();
    const Vec3d starting_vec = m_starting.drag_position - pivot;
#endif // ENABLE_WORLD_COORDINATE

    const double len_starting_vec = starting_vec.norm();

    if (len_starting_vec != 0.0) {
        const Vec3d mouse_dir = data.mouse_ray.unit_vector();
        // finds the intersection of the mouse ray with the plane parallel to the camera viewport and passing throught the starting position
        // use ray-plane intersection see i.e. https://en.wikipedia.org/wiki/Line%E2%80%93plane_intersection algebric form
        // in our case plane normal and ray direction are the same (orthogonal view)
        // when moving to perspective camera the negative z unit axis of the camera needs to be transformed in world space and used as plane normal
        const Vec3d inters = data.mouse_ray.a + (m_starting.drag_position - data.mouse_ray.a).dot(mouse_dir) / mouse_dir.squaredNorm() * mouse_dir;
        // vector from the starting position to the found intersection
        const Vec3d inters_vec = inters - m_starting.drag_position;

        // finds projection of the vector along the staring direction
        const double proj = inters_vec.dot(starting_vec.normalized());

        ratio = (len_starting_vec + proj) / len_starting_vec;
    }

    if (wxGetKeyState(WXK_SHIFT))
        ratio = m_snap_step * (double)std::round(ratio / m_snap_step);

    return ratio;
}

#if ENABLE_WORLD_COORDINATE
void GLGizmoScale3D::transform_to_local(const Selection& selection) const
{
    const Vec3d center = selection.get_bounding_box().center();
    glsafe(::glTranslated(center.x(), center.y(), center.z()));

    if (!wxGetApp().obj_manipul()->get_world_coordinates()) {
        Transform3d orient_matrix = selection.get_volume(*selection.get_volume_idxs().begin())->get_instance_transformation().get_matrix(true, false, true, true);
        if (selection.is_single_volume() || selection.is_single_modifier())
            orient_matrix = orient_matrix * selection.get_volume(*selection.get_volume_idxs().begin())->get_volume_transformation().get_matrix(true, false, true, true);
        glsafe(::glMultMatrixd(orient_matrix.data()));
    }
}
#endif // ENABLE_WORLD_COORDINATE

} // namespace GUI
} // namespace Slic3r
