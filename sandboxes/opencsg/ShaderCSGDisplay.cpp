#include "ShaderCSGDisplay.hpp"
#include "libslic3r/SLAPrint.hpp"
#include <GL/glew.h>

namespace Slic3r { namespace GL {

void ShaderCSGDisplay::add_mesh(const TriangleMesh &mesh)
{
    auto v = std::make_shared<CSGVolume>();
    v->load_mesh(mesh);
    m_volumes.emplace_back(v);
}

void ShaderCSGDisplay::render_scene()
{
    GLfloat color[] = {1.f, 1.f, 0.f, 0.f};
    glColor4fv(color);
    glDepthFunc(GL_LESS);
    for (auto &v : m_volumes) v->render();
    glFlush();
}

void ShaderCSGDisplay::on_scene_updated(const Scene &scene)
{
    // TriangleMesh mesh = print->objects().front()->hollowed_interior_mesh();
    // Look at CSGDisplay::on_scene_updated to see how its done there.
    
    const SLAPrint *print = scene.get_print();
    if (!print) return;
    
    m_volumes.clear();
    
    for (const SLAPrintObject *po : print->objects()) {
        const ModelObject *mo = po->model_object();
        TriangleMesh msh = mo->raw_mesh();
        
        sla::DrainHoles holedata = mo->sla_drain_holes;
        
        for (const ModelInstance *mi : mo->instances) {
            
            TriangleMesh mshinst = msh;
            auto interior = po->hollowed_interior_mesh();
            interior.transform(po->trafo().inverse());
            
            mshinst.merge(interior);
            mshinst.require_shared_vertices();
            
            mi->transform_mesh(&mshinst);
            
            auto bb = mshinst.bounding_box();
            auto center = bb.center().cast<float>();
            mshinst.translate(-center);
            
            mshinst.require_shared_vertices();
            add_mesh(mshinst);
            
            auto tr = Transform3f::Identity();
            tr.translate(-center);
            
            transform_pts(holedata.begin(), holedata.end(), tr,
                          [](const sla::DrainHole &dh) {
                              return dh.pos;
                          });
            
            transform_pts(holedata.begin(), holedata.end(), tr,
                          [](const sla::DrainHole &dh) {
                              return dh.normal;
                          });
        }
        
        for (const sla::DrainHole &holept : holedata) {
            TriangleMesh holemesh = sla::to_triangle_mesh(holept.to_mesh());
            holemesh.require_shared_vertices();
            add_mesh(holemesh);
        }
    }
    
    repaint();
}

}} // namespace Slic3r::GL
