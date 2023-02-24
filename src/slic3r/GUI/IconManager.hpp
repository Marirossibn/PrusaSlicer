#ifndef slic3r_IconManager_hpp_
#define slic3r_IconManager_hpp_

#include <vector>
#include <memory>
#include "imgui/imgui.h" // ImVec2
#include "slic3r/GUI/GLTexture.hpp" // texture storage

namespace Slic3r::GUI {

/// <summary>
/// Keep texture with icons for UI
/// Manage texture live -> create and destruct texture 
/// by live of icon shared pointers.
/// </summary>
class IconManager
{
public:
    /// <summary>
    /// Release texture
    /// Set shared pointers to invalid texture
    /// </summary>
    ~IconManager();

    /// <summary>
    /// Define way to convert svg data to raster
    /// </summary>
    enum class RasterType: int{
        color           = 1 << 1, 
        white_only_data = 1 << 2, 
        gray_only_data  = 1 << 3,
        color_wite_gray = color | white_only_data | gray_only_data
        // TODO: add type with backgrounds
    };

    struct InitType {
        // path to file with image .. svg
        std::string filepath;

        // resolution of stored rasterized icon
        ImVec2 size; // float will be rounded

        // could contain more than one type
        RasterType type = RasterType::color;
        // together color, white and gray = color | white_only_data | gray_only_data
    };
    using InitTypes = std::vector<InitType>;

    /// <summary>
    /// Data for render texture with icon
    /// </summary>
    struct Icon {
        // stored texture size
        ImVec2 size = ImVec2(-1, -1); // [in px] --> unsigned int values stored as float

        // SubTexture UV coordinate in range from 0. to 1.
        ImVec2 tl; // top left     -> uv0
        ImVec2 br; // bottom right -> uv1

        // OpenGL texture id
        unsigned int tex_id = 0;
        bool is_valid() const { return tex_id != 0;}
        // && size.x > 0 && size.y > 0 && tl.x != br.x && tl.y != br.y;        
    };
    using Icons = std::vector<std::shared_ptr<Icon> >;

    /// <summary>
    /// Initialize raster texture on GPU with given images
    /// NOTE: Have to be called after OpenGL initialization
    /// </summary>
    /// <param name="input">Define files and its </param>
    /// <returns>Rasterized icons stored on GPU,
    /// Same size and order as input, each item of vector is set of texture in order by RasterType</returns>
    std::vector<Icons> init(const InitTypes &input);

    /// <summary>
    /// Initialize multiple icons with same settings for size and type
    /// NOTE: Have to be called after OpenGL initialization
    /// </summary>
    /// <param name="file_paths">Define files with icon</param>
    /// <param name="size">Size of stored texture[in px], float will be rounded</param>
    /// <param name="type">Define way to rasterize icon,
    /// together color, white and gray = RasterType::color | RasterType::white_only_data | RasterType::gray_only_data</param>
    /// <returns>Rasterized icons stored on GPU,
    /// Same size and order as file_paths, each item of vector is set of texture in order by RasterType</returns>
    std::vector<Icons> init(const std::vector<std::string> &file_paths, const ImVec2 &size, RasterType type = RasterType::color);
    
    /// <summary>
    /// Release icons which are hold only by this manager
    /// May change texture and position of icons.
    /// </summary>
    void release();

    /// <summary>
    /// Draw imgui image with icon
    /// </summary>
    /// <param name="icon">Place in texture</param>
    /// <param name="size">[optional]Size of image, wen zero than use same size as stored texture</param>
    /// <param name="tint_col">viz ImGui::Image </param>
    /// <param name="border_col">viz ImGui::Image </param>
    static void draw(const Icon &icon, const ImVec2 &size = ImVec2(0, 0), const ImVec4& tint_col = ImVec4(1,1,1,1), const ImVec4& border_col = ImVec4(0,0,0,0));

private:        
    // keep data stored on GPU
    GLTexture m_icons_texture;
    Icons m_icons;
};

} // namespace Slic3r::GUI
#endif // slic3r_IconManager_hpp_