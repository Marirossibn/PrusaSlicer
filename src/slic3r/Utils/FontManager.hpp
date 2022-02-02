#ifndef slic3r_FontManager_hpp_
#define slic3r_FontManager_hpp_

#include <optional>
#include <imgui/imgui.h>
#include <GL/glew.h>
#include "libslic3r/Emboss.hpp"

class wxFont; 

namespace Slic3r::GUI {

/// <summary>
/// GUI list of loaded fonts
/// Keep pointer to ImGui font pointers
/// Keep file data of TTF files
/// Cache wx font objects
/// </summary>
class FontManager
{
public:
    FontManager(const ImWchar *language_glyph_range);
    ~FontManager();

    /// <summary>
    /// Change order of style item in m_font_list.
    /// Fix selected font index when (i1 || i2) == m_font_selected 
    /// </summary>
    /// <param name="i1">First index to m_font_list</param>
    /// <param name="i2">Second index to m_font_list</param>
    void swap(size_t i1, size_t i2);

    /// <summary>
    /// Duplicate selected font style
    /// </summary>
    void duplicate();

    /// <summary>
    /// Remove style from m_font_list.
    /// Fix selected font index when index is under m_font_selected
    /// </summary>
    /// <param name="index">Index of style to be removed</param>
    void erase(size_t index);

    /// <summary>
    /// Actual wx font was changed
    /// Clear caches
    /// </summary>
    /// <param name="font_file">font file created by WxFontUtils::create_font_file(wx_font)</param>
    bool wx_font_changed(std::unique_ptr<Emboss::FontFile> font_file = nullptr);
    
    /// <summary>
    /// Change active font
    /// When font not loaded roll back activ font
    /// </summary>
    /// <param name="font_index">New font index(from m_font_list range)</param>
    /// <returns>True on succes. False on fail load font</returns>
    bool load_font(size_t font_index);
    // fastering load font on index by wxFont, ignore type and descriptor
    bool load_font(size_t font_index, const wxFont &font);
    
    // remove cached imgui font for actual selected font
    void clear_imgui_font();

    // erase font when not possible to load
    // used at initialize phaze - fonts could be modified in appConfig file by user
    bool load_first_valid_font();

    // add font into manager
    void add_font(FontItem font_item);
    // add multiple font into manager
    void add_fonts(FontList font_list);

    // getter on active font file for access to glyphs
    std::shared_ptr<Emboss::FontFile> &get_font_file();

    // getter on active font item for access to font property
    const FontItem &get_font_item() const;
    FontItem &get_font_item();

    // getter on active font property
    const FontProp &get_font_prop() const;
    FontProp &get_font_prop();

    // getter on activ wx font
    const std::optional<wxFont> &get_wx_font() const;
    std::optional<wxFont> &get_wx_font();

    // setter of font for actual selection
    bool set_wx_font(const wxFont &wx_font);

    // Getter for cached trucated name for style list selector
    std::string &get_truncated_name();

    // Getter on acitve font pointer for imgui
    // Initialize imgui font(generate texture) when doesn't exist yet.
    // Extend font atlas when not in glyph range
    ImFont *get_imgui_font(const std::string &text);

    // free used memory and font file data
    void free_except_active_font();

    /// <summary>
    /// initialization texture with rendered font style
    /// </summary>
    void init_style_images(int max_width);
    void free_style_images();
    
    void create_texture(size_t font_index, const std::string &text, GLuint& tex_id, ImVec2& tex_size);

    struct Item;
    // access to all managed fonts
    const std::vector<Item> &get_fonts() const;
    const Item &get_font() const;

    /// <summary>
    /// Describe image in GPU to show settings of style
    /// </summary>
    struct StyleImage
    {
        void* texture_id = 0; // GLuint
        BoundingBox bounding_box;
        ImVec2 tex_size, uv0, uv1;
        Point  offset     = Point(0, 0);
        StyleImage()      = default;
    };

    /// <summary>
    /// All connected with one style 
    /// keep temporary data and caches for style
    /// </summary>
    struct Item
    {
        FontItem font_item;

        // cache for view font name with maximal width in imgui
        std::string truncated_name; 

        // share font file data with emboss job thread
        std::shared_ptr<Emboss::FontFile> font_file = nullptr;

        std::optional<size_t> imgui_font_index;

        // must live same as imgui_font inside of atlas
        ImVector<ImWchar> font_ranges;

        // wx widget font
        std::optional<wxFont> wx_font;

        // visualization of style
        std::optional<StyleImage> image;
    };   

    // check if exist selected font style in manager
    bool is_activ_font();

private:
    ImFontAtlas m_imgui_font_atlas;

    void duplicate(size_t index);
    // load actual selected font
    ImFont *load_imgui_font(size_t index, const std::string &text);

    bool load_activ_font();

    bool set_wx_font(size_t item_index, const wxFont &wx_font);
        
    // getter on index selected font pointer for imgui
    // text could extend font atlas when not in glyph range
    ImFont *get_imgui_font(size_t item_index, const std::string &text = "");

    // extend actual imgui font when exist unknown char in text
    // NOTE: imgui_font has to be unused
    void extend_imgui_font_range(size_t font_index, const std::string &text);

    // Move to imgui utils
    static bool is_text_in_ranges(const ImFont *font, const std::string &text);
    static bool is_text_in_ranges(const ImWchar *ranges, const std::string &text);
    static bool is_char_in_ranges(const ImWchar *ranges, unsigned int letter);

    bool check_imgui_font_range(ImFont *font, const std::string &text);
    void free_imgui_fonts();

    struct Configuration
    {
        // limits for imgui loaded font
        // Value out of limits is crop
        int min_imgui_font_size = 18;
        int max_imgui_font_size = 60;
    } m_cfg;

    bool set_up_font_file(size_t item_index);

    void make_unique_name(std::string &name);

    // Privat member
    std::vector<Item> m_font_list;
    size_t            m_font_selected; // index to m_font_list

    bool m_exist_style_images;

    // store all font GLImages
    //ImFontAtlas    m_imgui_font_atlas;
    const ImWchar *m_imgui_init_glyph_range;
};

} // namespace Slic3r

#endif // slic3r_FontManager_hpp_
