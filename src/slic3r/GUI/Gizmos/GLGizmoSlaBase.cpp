#include "libslic3r/libslic3r.h"
#include "GLGizmoSlaBase.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"

namespace Slic3r {
namespace GUI {

static const ColorRGBA DISABLED_COLOR = ColorRGBA::DARK_GRAY();
#if ENABLE_RAYCAST_PICKING
static const int VOLUME_RAYCASTERS_BASE_ID = (int)SceneRaycaster::EIdBase::Gizmo;
#endif // ENABLE_RAYCAST_PICKING

GLGizmoSlaBase::GLGizmoSlaBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id, SLAPrintObjectStep min_step)
: GLGizmoBase(parent, icon_filename, sprite_id)
, m_min_sla_print_object_step((int)min_step)
{}

void GLGizmoSlaBase::reslice_until_step(SLAPrintObjectStep step, bool postpone_error_messages)
{
    wxGetApp().CallAfter([this, step, postpone_error_messages]() {
        wxGetApp().plater()->reslice_SLA_until_step(step, *m_c->selection_info()->model_object(), postpone_error_messages);
        });
}

CommonGizmosDataID GLGizmoSlaBase::on_get_requirements() const
{
    return CommonGizmosDataID(
                int(CommonGizmosDataID::SelectionInfo)
              | int(CommonGizmosDataID::InstancesHider)
              | int(CommonGizmosDataID::Raycaster)
              | int(CommonGizmosDataID::ObjectClipper));
}

void GLGizmoSlaBase::update_volumes()
{
    m_volumes.clear();
    unregister_volume_raycasters_for_picking();

    const ModelObject* mo = m_c->selection_info()->model_object();
    if (mo == nullptr)
        return;

    const SLAPrintObject* po = m_c->selection_info()->print_object();
    if (po == nullptr)
        return;

    m_input_enabled = false;

    TriangleMesh backend_mesh = po->get_mesh_to_print();
    if (!backend_mesh.empty()) {
        // The backend has generated a valid mesh. Use it
        backend_mesh.transform(po->trafo().inverse());
        m_volumes.volumes.emplace_back(new GLVolume());
        GLVolume* new_volume = m_volumes.volumes.back();
        new_volume->model.init_from(backend_mesh);
        new_volume->set_instance_transformation(po->model_object()->instances[m_parent.get_selection().get_instance_idx()]->get_transformation());
        new_volume->set_sla_shift_z(po->get_current_elevation());
        new_volume->mesh_raycaster = std::make_unique<GUI::MeshRaycaster>(backend_mesh);
        m_input_enabled = last_completed_step(*m_c->selection_info()->print_object()->print()) >= m_min_sla_print_object_step;
        if (m_input_enabled)
            new_volume->selected = true; // to set the proper color
        else
            new_volume->set_color(DISABLED_COLOR);
    }

    if (m_volumes.volumes.empty()) {
        // No valid mesh found in the backend. Use the selection to duplicate the volumes
        const Selection& selection = m_parent.get_selection();
        const Selection::IndicesList& idxs = selection.get_volume_idxs();
        for (unsigned int idx : idxs) {
            const GLVolume* v = selection.get_volume(idx);
            if (!v->is_modifier) {
                m_volumes.volumes.emplace_back(new GLVolume());
                GLVolume* new_volume = m_volumes.volumes.back();
                const TriangleMesh& mesh = mo->volumes[v->volume_idx()]->mesh();
                new_volume->model.init_from(mesh);
                new_volume->set_instance_transformation(v->get_instance_transformation());
                new_volume->set_volume_transformation(v->get_volume_transformation());
                new_volume->set_sla_shift_z(v->get_sla_shift_z());
                new_volume->set_color(DISABLED_COLOR);
                new_volume->mesh_raycaster = std::make_unique<GUI::MeshRaycaster>(mesh);
            }
        }
    }

#if ENABLE_RAYCAST_PICKING
    register_volume_raycasters_for_picking();
#endif // ENABLE_RAYCAST_PICKING
}

void GLGizmoSlaBase::render_volumes()
{
    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light_clip");
    if (shader == nullptr)
        return;

    shader->start_using();
    shader->set_uniform("emission_factor", 0.0f);
    const Camera& camera = wxGetApp().plater()->get_camera();

    ClippingPlane clipping_plane = (m_c->object_clipper()->get_position() == 0.0) ? ClippingPlane::ClipsNothing() : *m_c->object_clipper()->get_clipping_plane();
    clipping_plane.set_normal(-clipping_plane.get_normal());
    m_volumes.set_clipping_plane(clipping_plane.get_data());

    m_volumes.render(GLVolumeCollection::ERenderType::Opaque, false, camera.get_view_matrix(), camera.get_projection_matrix());
    shader->stop_using();

}

#if ENABLE_RAYCAST_PICKING
void GLGizmoSlaBase::register_volume_raycasters_for_picking()
{
    for (size_t i = 0; i < m_volumes.volumes.size(); ++i) {
        const GLVolume* v = m_volumes.volumes[i];
        m_volume_raycasters.emplace_back(m_parent.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, VOLUME_RAYCASTERS_BASE_ID + (int)i, *v->mesh_raycaster, v->world_matrix()));
    }
}

void GLGizmoSlaBase::unregister_volume_raycasters_for_picking()
{
    for (size_t i = 0; i < m_volume_raycasters.size(); ++i) {
        m_parent.remove_raycasters_for_picking(SceneRaycaster::EType::Gizmo, VOLUME_RAYCASTERS_BASE_ID + (int)i);
    }
    m_volume_raycasters.clear();
}
#endif // ENABLE_RAYCAST_PICKING

int GLGizmoSlaBase::last_completed_step(const SLAPrint& sla)
{
    int step = -1;
    for (int i = 0; i < (int)SLAPrintObjectStep::slaposCount; ++i) {
        if (sla.is_step_done((SLAPrintObjectStep)i))
            ++step;
    }
    return step;
}

// Unprojects the mouse position on the mesh and saves hit point and normal of the facet into pos_and_normal
// Return false if no intersection was found, true otherwise.
bool GLGizmoSlaBase::unproject_on_mesh(const Vec2d& mouse_pos, std::pair<Vec3f, Vec3f>& pos_and_normal)
{
    if (m_c->raycaster()->raycasters().size() != 1)
        return false;
    if (!m_c->raycaster()->raycaster())
        return false;
    if (m_volumes.volumes.empty())
        return false;

    // The raycaster query
    Vec3f hit;
    Vec3f normal;
    if (m_c->raycaster()->raycaster()->unproject_on_mesh(
        mouse_pos,
        m_volumes.volumes.front()->world_matrix(),
        wxGetApp().plater()->get_camera(),
        hit,
        normal,
        m_c->object_clipper()->get_position() != 0.0 ? m_c->object_clipper()->get_clipping_plane() : nullptr)) {
        // Return both the point and the facet normal.
        pos_and_normal = std::make_pair(hit, normal);
        return true;
    }
    return false;
}

} // namespace GUI
} // namespace Slic3r
