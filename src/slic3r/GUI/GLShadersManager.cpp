#include "libslic3r/libslic3r.h"
#include "libslic3r/Platform.hpp"
#include "GLShadersManager.hpp"
#include "3DScene.hpp"
#include "GUI_App.hpp"
#if ENABLE_GL_CORE_PROFILE
#include "OpenGLManager.hpp"
#endif // ENABLE_GL_CORE_PROFILE

#include <cassert>
#include <algorithm>
#include <string_view>
using namespace std::literals;

#include <GL/glew.h>

namespace Slic3r {

std::pair<bool, std::string> GLShadersManager::init()
{
    std::string error;

    auto append_shader = [this, &error](const std::string& name, const GLShaderProgram::ShaderFilenames& filenames, 
        const std::initializer_list<std::string_view> &defines = {}) {
        m_shaders.push_back(std::make_unique<GLShaderProgram>());
        if (!m_shaders.back()->init_from_files(name, filenames, defines)) {
            error += name + "\n";
            // if any error happens while initializating the shader, we remove it from the list
            m_shaders.pop_back();
            return false;
        }
        return true;
    };

    assert(m_shaders.empty());

    bool valid = true;

#if ENABLE_LEGACY_OPENGL_REMOVAL
#if ENABLE_OPENGL_ES
    const std::string prefix = "ES/";
    // used to render wireframed triangles
    valid &= append_shader("wireframe", { prefix + "wireframe.vs", prefix + "wireframe.fs" });
#else
    const std::string prefix = GUI::wxGetApp().is_gl_version_greater_or_equal_to(3, 1) ? "140/" : "110/";
#endif // ENABLE_OPENGL_ES
    // imgui shader
    valid &= append_shader("imgui", { prefix + "imgui.vs", prefix + "imgui.fs" });
    // basic shader, used to render all what was previously rendered using the immediate mode
    valid &= append_shader("flat", { prefix + "flat.vs", prefix + "flat.fs" });
    // basic shader with plane clipping, used to render volumes in picking pass
    valid &= append_shader("flat_clip", { prefix + "flat_clip.vs", prefix + "flat_clip.fs" });
    // basic shader for textures, used to render textures
    valid &= append_shader("flat_texture", { prefix + "flat_texture.vs", prefix + "flat_texture.fs" });
    // used to render 3D scene background
    valid &= append_shader("background", { prefix + "background.vs", prefix + "background.fs" });
#if ENABLE_OPENGL_ES
    // used to render dashed lines
    valid &= append_shader("dashed_lines", { prefix + "dashed_lines.vs", prefix + "dashed_lines.fs" });
#elif ENABLE_GL_CORE_PROFILE
    if (GUI::OpenGLManager::get_gl_info().is_core_profile())
        // used to render thick and/or dashed lines
        valid &= append_shader("dashed_thick_lines", { prefix + "dashed_thick_lines.vs", prefix + "dashed_thick_lines.fs", prefix + "dashed_thick_lines.gs" });
#endif // ENABLE_OPENGL_ES
#endif // ENABLE_LEGACY_OPENGL_REMOVAL
    // used to render toolpaths center of gravity
#if ENABLE_LEGACY_OPENGL_REMOVAL
    valid &= append_shader("toolpaths_cog", { prefix + "toolpaths_cog.vs", prefix + "toolpaths_cog.fs" });
#else
    valid &= append_shader("toolpaths_cog", { "toolpaths_cog.vs", "toolpaths_cog.fs" });
#endif // ENABLE_LEGACY_OPENGL_REMOVAL
#if ENABLE_LEGACY_OPENGL_REMOVAL
    // used to render bed axes and model, selection hints, gcode sequential view marker model, preview shells, options in gcode preview
    valid &= append_shader("gouraud_light", { prefix + "gouraud_light.vs", prefix + "gouraud_light.fs" });
    // extend "gouraud_light" by adding clipping, used in sla gizmos
    valid &= append_shader("gouraud_light_clip", { prefix + "gouraud_light_clip.vs", prefix + "gouraud_light_clip.fs" });
    // used to render printbed
    valid &= append_shader("printbed", { prefix + "printbed.vs", prefix + "printbed.fs" });
#else
    // used to render bed axes and model, selection hints, gcode sequential view marker model, preview shells, options in gcode preview
    valid &= append_shader("gouraud_light", { "gouraud_light.vs", "gouraud_light.fs" });
    // used to render printbed
    valid &= append_shader("printbed", { "printbed.vs", "printbed.fs" });
#endif // ENABLE_LEGACY_OPENGL_REMOVAL
    // used to render options in gcode preview
    if (GUI::wxGetApp().is_gl_version_greater_or_equal_to(3, 3)) {
#if ENABLE_LEGACY_OPENGL_REMOVAL
        valid &= append_shader("gouraud_light_instanced", { prefix + "gouraud_light_instanced.vs", prefix + "gouraud_light_instanced.fs" });
#else
        valid &= append_shader("gouraud_light_instanced", { "gouraud_light_instanced.vs", "gouraud_light_instanced.fs" });
#endif // ENABLE_LEGACY_OPENGL_REMOVAL
    }
#if ENABLE_LEGACY_OPENGL_REMOVAL
    // used to render objects in 3d editor
    valid &= append_shader("gouraud", { prefix + "gouraud.vs", prefix + "gouraud.fs" }
#else
    // used to render extrusion and travel paths as lines in gcode preview
    valid &= append_shader("toolpaths_lines", { "toolpaths_lines.vs", "toolpaths_lines.fs" });
    // used to render objects in 3d editor
    valid &= append_shader("gouraud", { "gouraud.vs", "gouraud.fs" }
#endif // ENABLE_LEGACY_OPENGL_REMOVAL
#if ENABLE_ENVIRONMENT_MAP
        , { "ENABLE_ENVIRONMENT_MAP"sv }
#endif // ENABLE_ENVIRONMENT_MAP
        );
#if ENABLE_LEGACY_OPENGL_REMOVAL
    // used to render variable layers heights in 3d editor
    valid &= append_shader("variable_layer_height", { prefix + "variable_layer_height.vs", prefix + "variable_layer_height.fs" });
    // used to render highlight contour around selected triangles inside the multi-material gizmo
    valid &= append_shader("mm_contour", { prefix + "mm_contour.vs", prefix + "mm_contour.fs" });
#else
    // used to render variable layers heights in 3d editor
    valid &= append_shader("variable_layer_height", { "variable_layer_height.vs", "variable_layer_height.fs" });
    // used to render highlight contour around selected triangles inside the multi-material gizmo
    valid &= append_shader("mm_contour", { "mm_contour.vs", "mm_contour.fs" });
#endif // ENABLE_LEGACY_OPENGL_REMOVAL
    // Used to render painted triangles inside the multi-material gizmo. Triangle normals are computed inside fragment shader.
    // For Apple's on Arm CPU computed triangle normals inside fragment shader using dFdx and dFdy has the opposite direction.
    // Because of this, objects had darker colors inside the multi-material gizmo.
    // Based on https://stackoverflow.com/a/66206648, the similar behavior was also spotted on some other devices with Arm CPU.
    // Since macOS 12 (Monterey), this issue with the opposite direction on Apple's Arm CPU seems to be fixed, and computed
    // triangle normals inside fragment shader have the right direction.
#if ENABLE_LEGACY_OPENGL_REMOVAL
    if (platform_flavor() == PlatformFlavor::OSXOnArm && wxPlatformInfo::Get().GetOSMajorVersion() < 12)
        valid &= append_shader("mm_gouraud", { prefix + "mm_gouraud.vs", prefix + "mm_gouraud.fs" }, { "FLIP_TRIANGLE_NORMALS"sv });
    else
        valid &= append_shader("mm_gouraud", { prefix + "mm_gouraud.vs", prefix + "mm_gouraud.fs" });
#else
    if (platform_flavor() == PlatformFlavor::OSXOnArm && wxPlatformInfo::Get().GetOSMajorVersion() < 12)
        valid &= append_shader("mm_gouraud", {"mm_gouraud.vs", "mm_gouraud.fs"}, {"FLIP_TRIANGLE_NORMALS"sv});
    else
        valid &= append_shader("mm_gouraud", {"mm_gouraud.vs", "mm_gouraud.fs"});
#endif // ENABLE_LEGACY_OPENGL_REMOVAL

    return { valid, error };
}

void GLShadersManager::shutdown()
{
    m_shaders.clear();
}

GLShaderProgram* GLShadersManager::get_shader(const std::string& shader_name)
{
    auto it = std::find_if(m_shaders.begin(), m_shaders.end(), [&shader_name](std::unique_ptr<GLShaderProgram>& p) { return p->get_name() == shader_name; });
    return (it != m_shaders.end()) ? it->get() : nullptr;
}

GLShaderProgram* GLShadersManager::get_current_shader()
{
    GLint id = 0;
    glsafe(::glGetIntegerv(GL_CURRENT_PROGRAM, &id));
    if (id == 0)
        return nullptr;

    auto it = std::find_if(m_shaders.begin(), m_shaders.end(), [id](std::unique_ptr<GLShaderProgram>& p) { return static_cast<GLint>(p->get_id()) == id; });
    return (it != m_shaders.end()) ? it->get() : nullptr;
}

} // namespace Slic3r

