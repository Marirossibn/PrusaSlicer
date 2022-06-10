#include "libslic3r/libslic3r.h"
#include "SceneRaycaster.hpp"

#include "Camera.hpp"
#include "GUI_App.hpp"

#if ENABLE_RAYCAST_PICKING

namespace Slic3r {
namespace GUI {

SceneRaycaster::SceneRaycaster() {
#if ENABLE_RAYCAST_PICKING_DEBUG
    // hit point
    m_sphere.init_from(its_make_sphere(1.0, double(PI) / 16.0));
    m_sphere.set_color(ColorRGBA::YELLOW());

    // hit normal
    GLModel::Geometry init_data;
    init_data.format = { GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
    init_data.color = ColorRGBA::YELLOW();
    init_data.reserve_vertices(2);
    init_data.reserve_indices(2);

    // vertices
    init_data.add_vertex((Vec3f)Vec3f::Zero());
    init_data.add_vertex((Vec3f)Vec3f::UnitZ());

    // indices
    init_data.add_line(0, 1);

    m_line.init_from(std::move(init_data));
#endif // ENABLE_RAYCAST_PICKING_DEBUG
}

int SceneRaycaster::add_raycaster(EType type, PickingId id, const MeshRaycaster& raycaster, const Transform3d& trafo)
{
    switch (type) {
    case EType::Bed: {
        m_bed.emplace_back(encode_id(type, id), raycaster, trafo);
        return m_bed.size() - 1;
    }
    case EType::Volume: {
        m_volumes.emplace_back(encode_id(type, id), raycaster, trafo);
        return m_volumes.size() - 1;
    }
    case EType::Gizmo: {
        m_gizmos.emplace_back(encode_id(type, id), raycaster, trafo);
        return m_gizmos.size() - 1;
    }
    };

    // signal error
    return -1;
}

void SceneRaycaster::set_raycaster_active_state(EType type, int id, bool active)
{
    std::vector<SceneRaycasterItem>* raycasters = get_raycasters(type);
    for (SceneRaycasterItem& item : *raycasters) {
        if (item.get_id() == encode_id(type, id)) {
            item.set_active(active);
            break;
        }
    }
}

void SceneRaycaster::set_raycaster_transform(EType type, int id, const Transform3d& trafo)
{
    std::vector<SceneRaycasterItem>* raycasters = get_raycasters(type);
    for (SceneRaycasterItem& item : *raycasters) {
        if (item.get_id() == encode_id(type, id)) {
            item.set_transform(trafo);
            break;
        }
    }
}

void SceneRaycaster::remove_raycaster(EType type, int id)
{
    std::vector<SceneRaycasterItem>* raycasters = get_raycasters(type);
    if (0 <= id && id < raycasters->size())
        raycasters->erase(raycasters->begin() + id);
}

void SceneRaycaster::reset(EType type)
{
    switch (type) {
    case EType::Bed: {
        m_bed.clear();
        break;
    }
    case EType::Volume: {
        m_volumes.clear();
        break;
    }
    case EType::Gizmo: {
        m_gizmos.clear();
        break;
    }
    };
}

SceneRaycaster::HitResult SceneRaycaster::hit(const Vec2d& mouse_pos, const Camera& camera, const ClippingPlane* clipping_plane)
{
    double closest_hit_squared_distance = std::numeric_limits<double>::max();
    auto is_closest = [&closest_hit_squared_distance](const Camera& camera, const Vec3f& hit) {
        const double hit_squared_distance = (camera.get_position() - hit.cast<double>()).squaredNorm();
        const bool ret = hit_squared_distance < closest_hit_squared_distance;
        if (ret)
            closest_hit_squared_distance = hit_squared_distance;
        return ret;
    };

    m_last_hit.reset();

    HitResult ret;

    auto test_raycasters = [&](EType type) {
        const ClippingPlane* clip_plane = (clipping_plane != nullptr && type == EType::Volume) ? clipping_plane : nullptr;
        const std::vector<SceneRaycasterItem>* raycasters = get_raycasters(type);
        HitResult current_hit = { type };
        for (const SceneRaycasterItem& item : *raycasters) {
            if (!item.is_active())
                continue;

            current_hit.raycaster_id = item.get_id();
            const Transform3d& trafo = item.get_transform();
            if (item.get_raycaster()->closest_hit(mouse_pos, trafo, camera, current_hit.position, current_hit.normal, clip_plane)) {
                current_hit.position = (trafo * current_hit.position.cast<double>()).cast<float>();
                if (is_closest(camera, current_hit.position)) {
                    const Transform3d matrix = camera.get_view_matrix() * trafo;
                    const Matrix3d normal_matrix = (Matrix3d)trafo.matrix().block(0, 0, 3, 3).inverse().transpose();
                    current_hit.normal = (normal_matrix * current_hit.normal.cast<double>()).normalized().cast<float>();
                    ret = current_hit;
                }
            }
        }
    };

    test_raycasters(EType::Gizmo);
    if (!m_gizmos_on_top || ret.is_valid()) {
        if (camera.is_looking_downward())
            test_raycasters(EType::Bed);
        test_raycasters(EType::Volume);
    }

    if (ret.is_valid())
        ret.raycaster_id = decode_id(ret.type, ret.raycaster_id);

    m_last_hit = ret;
    return ret;
}

#if ENABLE_RAYCAST_PICKING_DEBUG
void SceneRaycaster::render_hit(const Camera& camera)
{
    if (!m_last_hit.has_value() || !m_last_hit.value().is_valid())
        return;

    GLShaderProgram* shader = wxGetApp().get_shader("flat");
    shader->start_using();

    shader->set_uniform("projection_matrix", camera.get_projection_matrix());

    const Transform3d sphere_view_model_matrix = camera.get_view_matrix() * Geometry::translation_transform(m_last_hit.value().position.cast<double>()) *
        Geometry::scale_transform(4.0 * camera.get_inv_zoom());
    shader->set_uniform("view_model_matrix", sphere_view_model_matrix);
    m_sphere.render();

    Eigen::Quaterniond q;
    Transform3d m = Transform3d::Identity();
    m.matrix().block(0, 0, 3, 3) = q.setFromTwoVectors(Vec3d::UnitZ(), m_last_hit.value().normal.cast<double>()).toRotationMatrix();

    const Transform3d line_view_model_matrix = sphere_view_model_matrix * m * Geometry::scale_transform(6.25);
    shader->set_uniform("view_model_matrix", line_view_model_matrix);
    m_line.render();

    shader->stop_using();
}
#endif // ENABLE_RAYCAST_PICKING_DEBUG

std::vector<SceneRaycasterItem>* SceneRaycaster::get_raycasters(EType type)
{
    std::vector<SceneRaycasterItem>* ret = nullptr;
    switch (type)
    {
    case EType::Bed:    { ret = &m_bed; break; }
    case EType::Volume: { ret = &m_volumes; break; }
    case EType::Gizmo:  { ret = &m_gizmos; break; }
    }
    assert(ret != nullptr);
    return ret;
}

PickingId SceneRaycaster::base_id(EType type)
{
    switch (type)
    {
    case EType::Bed:    { return PickingId(EPickingIdBase::Bed); }
    case EType::Volume: { return PickingId(EPickingIdBase::Volume); }
    case EType::Gizmo:  { return PickingId(EPickingIdBase::Gizmo); }
    };

    assert(false);
    return -1;
}

PickingId SceneRaycaster::encode_id(EType type, PickingId id)
{
    return base_id(type) + id;
}

PickingId SceneRaycaster::decode_id(EType type, PickingId id)
{
    return id - base_id(type);
}

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_RAYCAST_PICKING
