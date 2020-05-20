#ifndef slic3r_GLShadersManager_hpp_
#define slic3r_GLShadersManager_hpp_

#if ENABLE_SHADERS_MANAGER

#include "GLShader.hpp"

#include <vector>
#include <string>
#include <memory>

namespace Slic3r {

class GLShadersManager
{
    std::vector<std::unique_ptr<GLShaderProgram>> m_shaders;

public:
    std::pair<bool, std::string> init();
    // call this method before to release the OpenGL context
    void shutdown();

    // returns nullptr if not found
    GLShaderProgram* get_shader(const std::string& shader_name);
};

} // namespace Slic3r

#endif // ENABLE_SHADERS_MANAGER

#endif //  slic3r_GLShadersManager_hpp_
