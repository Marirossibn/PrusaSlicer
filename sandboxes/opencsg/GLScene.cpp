#include "GLScene.hpp"
#include <libslic3r/Utils.hpp>
#include <libslic3r/SLAPrint.hpp>
#include <libslic3r/MTUtils.hpp>

#include <GL/glew.h>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

#include <boost/log/trivial.hpp>

#ifndef NDEBUG
#define HAS_GLSAFE
#endif

#ifdef HAS_GLSAFE
extern void glAssertRecentCallImpl(const char *file_name, unsigned int line, const char *function_name);
inline void glAssertRecentCall() { glAssertRecentCallImpl(__FILE__, __LINE__, __FUNCTION__); }
#define glsafe(cmd) do { cmd; glAssertRecentCallImpl(__FILE__, __LINE__, __FUNCTION__); } while (false)
#define glcheck() do { glAssertRecentCallImpl(__FILE__, __LINE__, __FUNCTION__); } while (false)

void glAssertRecentCallImpl(const char *file_name, unsigned int line, const char *function_name)
{
    GLenum err = glGetError();
    if (err == GL_NO_ERROR)
        return;
    const char *sErr = 0;
    switch (err) {
    case GL_INVALID_ENUM:       sErr = "Invalid Enum";      break;
    case GL_INVALID_VALUE:      sErr = "Invalid Value";     break;
    // be aware that GL_INVALID_OPERATION is generated if glGetError is executed between the execution of glBegin and the corresponding execution of glEnd 
    case GL_INVALID_OPERATION:  sErr = "Invalid Operation"; break;
    case GL_STACK_OVERFLOW:     sErr = "Stack Overflow";    break;
    case GL_STACK_UNDERFLOW:    sErr = "Stack Underflow";   break;
    case GL_OUT_OF_MEMORY:      sErr = "Out Of Memory";     break;
    default:                    sErr = "Unknown";           break;
    }
    BOOST_LOG_TRIVIAL(error) << "OpenGL error in " << file_name << ":" << line << ", function " << function_name << "() : " << (int)err << " - " << sErr;
    assert(false);
}

#else
inline void glAssertRecentCall() { }
#define glsafe(cmd) cmd
#define glcheck()
#endif

namespace Slic3r { namespace GL {

Scene::Scene() = default;

Scene::~Scene() = default;

void renderfps () {
    static std::ostringstream fpsStream;
    static int fps = 0;
    static int ancient = 0;
    static int last = 0;
    static int msec = 0;
    
    last = msec;
    msec = glutGet(GLUT_ELAPSED_TIME);
    if (last / 1000 != msec / 1000) {
        
        float correctedFps = fps * 1000.0f / float(msec - ancient);
        fpsStream.str("");
        fpsStream << "fps: " << correctedFps << std::ends;
        
        ancient = msec;
        fps = 0;
    }
    glDisable(GL_DEPTH_TEST);
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glColor3f(0.0f, 0.0f, 0.0f);
    glRasterPos2f(-1.0f, -1.0f);
    glDisable(GL_LIGHTING);
    std::string s = fpsStream.str();
    for (unsigned int i=0; i<s.size(); ++i) {
        glutBitmapCharacter(GLUT_BITMAP_8_BY_13, s[i]);
    }
    glEnable(GL_LIGHTING);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);
    
    ++fps;
    glFlush();
}

void Display::render_scene()
{
    GLfloat color[] = {1.f, 1.f, 0.f, 0.f};
    glsafe(::glColor4fv(color));
    
    OpenCSG::render(m_scene->csg_primitives());
    
    glDepthFunc(GL_EQUAL);
    for (auto& p : m_scene->csg_primitives()) p->render();
    glDepthFunc(GL_LESS);
    
    for (auto& p : m_scene->free_primitives()) p->render();
    
    glFlush();
}

template<class It,
         class Trafo,
         class GetPt,
         class V = typename std::iterator_traits<It>::value_type>
std::vector<V> transform_pts(
    It from, It to, Trafo &&tr, GetPt &&point)
{
    auto ret = reserve_vector<V>(to - from);
    for(auto it = from; it != to; ++it) {
        V v = *it;
        v.pos = tr * point(*it);
        ret.emplace_back(std::move(v));
    }
    return ret;
}

void Scene::set_print(uqptr<SLAPrint> &&print)
{
    m_print = std::move(print);
    
    for (const SLAPrintObject *po : m_print->objects()) {
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
            add_mesh(mshinst, OpenCSG::Intersection, 15);

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
            add_mesh(holemesh, OpenCSG::Subtraction, 1);
        }
    }
        
    // Notify displays
    call(&Display::on_scene_updated, m_displays);
}

BoundingBoxf3 Scene::get_bounding_box() const
{
    return m_print->model().bounding_box();
}

shptr<Primitive> Scene::add_mesh(const TriangleMesh &mesh)
{
    auto p = std::make_shared<Primitive>();
    p->load_mesh(mesh);
    m_primitives.emplace_back(p);
    m_primitives_free.emplace_back(p.get());
    return p;
}

shptr<Primitive> Scene::add_mesh(const TriangleMesh &mesh, OpenCSG::Operation o, unsigned c)
{
    auto p = std::make_shared<Primitive>(o, c);
    p->load_mesh(mesh);
    m_primitives.emplace_back(p);
    m_primitives_csg.emplace_back(p.get());
    return p;
}

void IndexedVertexArray::push_geometry(float x, float y, float z, float nx, float ny, float nz)
{
    assert(this->vertices_and_normals_interleaved_VBO_id == 0);
    if (this->vertices_and_normals_interleaved_VBO_id != 0)
        return;
    
    if (this->vertices_and_normals_interleaved.size() + 6 > this->vertices_and_normals_interleaved.capacity())
        this->vertices_and_normals_interleaved.reserve(next_highest_power_of_2(this->vertices_and_normals_interleaved.size() + 6));
    this->vertices_and_normals_interleaved.emplace_back(nx);
    this->vertices_and_normals_interleaved.emplace_back(ny);
    this->vertices_and_normals_interleaved.emplace_back(nz);
    this->vertices_and_normals_interleaved.emplace_back(x);
    this->vertices_and_normals_interleaved.emplace_back(y);
    this->vertices_and_normals_interleaved.emplace_back(z);
    
    this->vertices_and_normals_interleaved_size = this->vertices_and_normals_interleaved.size();
}

void IndexedVertexArray::push_triangle(int idx1, int idx2, int idx3) {
    assert(this->vertices_and_normals_interleaved_VBO_id == 0);
    if (this->vertices_and_normals_interleaved_VBO_id != 0)
        return;
    
    if (this->triangle_indices.size() + 3 > this->vertices_and_normals_interleaved.capacity())
        this->triangle_indices.reserve(next_highest_power_of_2(this->triangle_indices.size() + 3));
    this->triangle_indices.emplace_back(idx1);
    this->triangle_indices.emplace_back(idx2);
    this->triangle_indices.emplace_back(idx3);
    this->triangle_indices_size = this->triangle_indices.size();
}

void IndexedVertexArray::load_mesh(const TriangleMesh &mesh)
{
    assert(triangle_indices.empty() && vertices_and_normals_interleaved_size == 0);
    assert(quad_indices.empty() && triangle_indices_size == 0);
    assert(vertices_and_normals_interleaved.size() % 6 == 0 && quad_indices_size == vertices_and_normals_interleaved.size());
    
    this->vertices_and_normals_interleaved.reserve(this->vertices_and_normals_interleaved.size() + 3 * 3 * 2 * mesh.facets_count());
    
    int vertices_count = 0;
    for (size_t i = 0; i < mesh.stl.stats.number_of_facets; ++i) {
        const stl_facet &facet = mesh.stl.facet_start[i];
        for (int j = 0; j < 3; ++j)
            this->push_geometry(facet.vertex[j](0), facet.vertex[j](1), facet.vertex[j](2), facet.normal(0), facet.normal(1), facet.normal(2));
                
                this->push_triangle(vertices_count, vertices_count + 1, vertices_count + 2);
        vertices_count += 3;
    }
}

void IndexedVertexArray::finalize_geometry()
{
    assert(this->vertices_and_normals_interleaved_VBO_id == 0);
    assert(this->triangle_indices_VBO_id == 0);
    assert(this->quad_indices_VBO_id == 0);

    if (!this->vertices_and_normals_interleaved.empty()) {
        glsafe(
            ::glGenBuffers(1, &this->vertices_and_normals_interleaved_VBO_id));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER,
                              this->vertices_and_normals_interleaved_VBO_id));
        glsafe(
            ::glBufferData(GL_ARRAY_BUFFER,
                           GLsizeiptr(
                               this->vertices_and_normals_interleaved.size() *
                               4),
                           this->vertices_and_normals_interleaved.data(),
                           GL_STATIC_DRAW));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
        this->vertices_and_normals_interleaved.clear();
    }
    if (!this->triangle_indices.empty()) {
        glsafe(::glGenBuffers(1, &this->triangle_indices_VBO_id));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                              this->triangle_indices_VBO_id));
        glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                              GLsizeiptr(this->triangle_indices.size() * 4),
                              this->triangle_indices.data(), GL_STATIC_DRAW));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
        this->triangle_indices.clear();
    }
    if (!this->quad_indices.empty()) {
        glsafe(::glGenBuffers(1, &this->quad_indices_VBO_id));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                              this->quad_indices_VBO_id));
        glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                              GLsizeiptr(this->quad_indices.size() * 4),
                              this->quad_indices.data(), GL_STATIC_DRAW));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
        this->quad_indices.clear();
    }
}

void IndexedVertexArray::release_geometry()
{
    if (this->vertices_and_normals_interleaved_VBO_id) {
        glsafe(
            ::glDeleteBuffers(1,
                              &this->vertices_and_normals_interleaved_VBO_id));
        this->vertices_and_normals_interleaved_VBO_id = 0;
    }
    if (this->triangle_indices_VBO_id) {
        glsafe(::glDeleteBuffers(1, &this->triangle_indices_VBO_id));
        this->triangle_indices_VBO_id = 0;
    }
    if (this->quad_indices_VBO_id) {
        glsafe(::glDeleteBuffers(1, &this->quad_indices_VBO_id));
        this->quad_indices_VBO_id = 0;
    }
    this->clear();
}

void IndexedVertexArray::render() const
{
    assert(this->vertices_and_normals_interleaved_VBO_id != 0);
    assert(this->triangle_indices_VBO_id != 0 ||
           this->quad_indices_VBO_id != 0);

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER,
                          this->vertices_and_normals_interleaved_VBO_id));
    glsafe(::glVertexPointer(3, GL_FLOAT, 6 * sizeof(float),
                             reinterpret_cast<const void *>(3 * sizeof(float))));
    glsafe(::glNormalPointer(GL_FLOAT, 6 * sizeof(float), nullptr));

    glsafe(::glEnableClientState(GL_VERTEX_ARRAY));
    glsafe(::glEnableClientState(GL_NORMAL_ARRAY));

    // Render using the Vertex Buffer Objects.
    if (this->triangle_indices_size > 0) {
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                              this->triangle_indices_VBO_id));
        glsafe(::glDrawElements(GL_TRIANGLES,
                                GLsizei(this->triangle_indices_size),
                                GL_UNSIGNED_INT, nullptr));
        glsafe(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
    }
    if (this->quad_indices_size > 0) {
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                              this->quad_indices_VBO_id));
        glsafe(::glDrawElements(GL_QUADS, GLsizei(this->quad_indices_size),
                                GL_UNSIGNED_INT, nullptr));
        glsafe(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
    }

    glsafe(::glDisableClientState(GL_VERTEX_ARRAY));
    glsafe(::glDisableClientState(GL_NORMAL_ARRAY));

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
}

void IndexedVertexArray::clear() {
    this->vertices_and_normals_interleaved.clear();
    this->triangle_indices.clear();
    this->quad_indices.clear();
    vertices_and_normals_interleaved_size = 0;
    triangle_indices_size = 0;
    quad_indices_size = 0;
}

void IndexedVertexArray::shrink_to_fit() {
    this->vertices_and_normals_interleaved.shrink_to_fit();
    this->triangle_indices.shrink_to_fit();
    this->quad_indices.shrink_to_fit();
}

void Primitive::render()
{
    glsafe(::glPushMatrix());
    glsafe(::glMultMatrixd(m_trafo.get_matrix().data()));
    m_geom.render();
    glsafe(::glPopMatrix());
}

void Display::clear_screen()
{
    glViewport(0, 0, GLsizei(m_size.x()), GLsizei(m_size.y()));
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void Display::set_active(long width, long height)
{
    static int argc = 0;
    
    if (!m_initialized) {
        glewInit();
        glutInit(&argc, nullptr);
        m_initialized = true;
    }
    
    m_size = {width, height};
    
    // gray background
    glClearColor(0.9f, 0.9f, 0.9f, 1.0f);
    
    // Enable two OpenGL lights
    GLfloat light_diffuse[]   = { 1.0f,  1.0f,  0.0f,  1.0f};  // White diffuse light
    GLfloat light_position0[] = {-1.0f, -1.0f, -1.0f,  0.0f};  // Infinite light location
    GLfloat light_position1[] = { 1.0f,  1.0f,  1.0f,  0.0f};  // Infinite light location
    
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light_diffuse);
    glLightfv(GL_LIGHT0, GL_POSITION, light_position0);
    glEnable(GL_LIGHT0);  
    glLightfv(GL_LIGHT1, GL_DIFFUSE, light_diffuse);
    glLightfv(GL_LIGHT1, GL_POSITION, light_position1);
    glEnable(GL_LIGHT1);
    glEnable(GL_LIGHTING);
    glEnable(GL_NORMALIZE);
    
    // Use depth buffering for hidden surface elimination
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_STENCIL_TEST);
    
    m_camera->set_screen(width, height);
}

void Display::repaint(long width, long height)
{
    if (m_size.x() != width || m_size.y() != height)
        m_camera->set_screen(width, height);
    
    m_size = {width, height};
    
    clear_screen();
    
    m_camera->view();
    render_scene();
    
    renderfps(); 
    
    swap_buffers();
}

void Display::on_scroll(long v, long d, MouseInput::WheelAxis wa)
{
    m_wheel_pos += v / d;
    
    m_camera->set_zoom(m_wheel_pos);
    
    m_scene->on_scroll(v, d, wa);
    
    repaint(m_size.x(), m_size.y());
}

void Display::on_moved_to(long x, long y)
{
    if (m_left_btn) {
        m_camera->rotate((Vec2i{x, y} - m_mouse_pos).cast<float>());
        repaint();
    }
    m_mouse_pos = {x, y};
}

void CSGSettings::set_csg_algo(OpenCSG::Algorithm alg) { m_csgalg = alg; }

void Display::on_scene_updated()
{
    auto bb = m_scene->get_bounding_box();
    double d = std::max(std::max(bb.size().x(), bb.size().y()), bb.size().z());
    m_wheel_pos = long(2 * d);
    m_camera->set_zoom(m_wheel_pos);
    repaint();
}

void Display::set_scene(shptr<Scene> scene)
{
    m_scene = scene;
    m_scene->add_display(shared_from_this());
}

void Camera::view()
{
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(0.0, m_zoom, 0.0,  /* eye is at (0,zoom,0) */
              m_referene.x(), m_referene.y(), m_referene.z(),
              0.0, 0.0, 1.0); /* up is in positive Y direction */
    
    // TODO Could have been set in prevoius gluLookAt in first argument
    glRotatef(m_rot.y(), 1.0, 0.0, 0.0);
    glRotatef(m_rot.x(), 0.0, 0.0, 1.0);
    
    // glClipPlane()
}

void PerspectiveCamera::set_screen(long width, long height)
{
    // Setup the view of the CSG shape
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0, width / double(height), .1, 200.0);
    glMatrixMode(GL_MODELVIEW);
}

bool enable_multisampling(bool e)
{
    if (!e) { glDisable(GL_MULTISAMPLE); return false; }
    
    GLint is_ms_context;
    glGetIntegerv(GL_SAMPLE_BUFFERS, &is_ms_context);
    
    if (is_ms_context) { glEnable(GL_MULTISAMPLE); return true; }
    else return false;
}

}} // namespace Slic3r::GL
