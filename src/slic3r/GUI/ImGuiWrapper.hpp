#ifndef slic3r_ImGuiWrapper_hpp_
#define slic3r_ImGuiWrapper_hpp_

#include <string>
#include <string_view>
#include <map>

#include <imgui/imgui.h>

#include <wx/string.h>

#include "libslic3r/Point.hpp"
#include "libslic3r/Color.hpp"
#include "libslic3r/Polygon.hpp"

namespace Slic3r {
namespace Search {
struct OptionViewParameters;
} // namespace Search
} // namespace Slic3r

class wxString;
class wxMouseEvent;
class wxKeyEvent;

struct IMGUI_API ImGuiWindow;

namespace Slic3r {
namespace GUI {

class ImGuiWrapper
{
    const ImWchar* m_glyph_ranges{ nullptr };
    // Chinese, Japanese, Korean
    bool m_font_cjk{ false };
    float m_font_size{ 18.0 };
    unsigned m_font_texture{ 0 };
    float m_style_scaling{ 1.0 };
    unsigned m_mouse_buttons{ 0 };
    bool m_disabled{ false };
    bool m_new_frame_open{ false };
    bool m_requires_extra_frame{ false };
    std::map<wchar_t, int> m_custom_glyph_rects_ids;
    std::string m_clipboard_text;

public:
    struct LastSliderStatus {
        bool hovered { false };
        bool edited  { false };
        bool clicked { false };
        bool deactivated_after_edit { false };
    };

    ImGuiWrapper();
    ~ImGuiWrapper();

    void set_language(const std::string &language);
    void set_display_size(float w, float h);
    void set_scaling(float font_size, float scale_style, float scale_both);
    bool update_mouse_data(wxMouseEvent &evt);
    bool update_key_data(wxKeyEvent &evt);

    float get_font_size() const { return m_font_size; }
    float get_style_scaling() const { return m_style_scaling; }
    const ImWchar *get_glyph_ranges() const { return m_glyph_ranges; } // language specific

    void new_frame();
    void render();

    float scaled(float x) const { return x * m_font_size; }
    ImVec2 scaled(float x, float y) const { return ImVec2(x * m_font_size, y * m_font_size); }
    /// <summary>
    /// Extend ImGui::CalcTextSize to use string_view
    /// </summary>
    static ImVec2 calc_text_size(std::string_view text, bool  hide_text_after_double_hash = false, float wrap_width = -1.0f);
    static ImVec2 calc_text_size(const std::string& text, bool  hide_text_after_double_hash = false, float wrap_width = -1.0f);
    static ImVec2 calc_text_size(const wxString &text, bool  hide_text_after_double_hash = false, float wrap_width = -1.0f);
    ImVec2 calc_button_size(const wxString &text, const ImVec2 &button_size = ImVec2(0, 0)) const;

    ImVec2 get_item_spacing() const;
    float  get_slider_float_height() const;
    const LastSliderStatus& get_last_slider_status() const { return m_last_slider_status; }

    void set_next_window_pos(float x, float y, int flag, float pivot_x = 0.0f, float pivot_y = 0.0f);
    void set_next_window_bg_alpha(float alpha);
	void set_next_window_size(float x, float y, ImGuiCond cond);

    bool begin(const std::string &name, int flags = 0);
    bool begin(const wxString &name, int flags = 0);
    bool begin(const std::string& name, bool* close, int flags = 0);
    bool begin(const wxString& name, bool* close, int flags = 0);
    void end();

    bool button(const wxString &label);
	bool button(const wxString& label, float width, float height);
    bool button(const wxString& label, const ImVec2 &size, bool enable); // default size = ImVec2(0.f, 0.f)
    bool radio_button(const wxString &label, bool active);
    bool draw_radio_button(const std::string& name, float size, bool active, std::function<void(ImGuiWindow& window, const ImVec2& pos, float size)> draw_callback);
    bool input_double(const std::string &label, const double &value, const std::string &format = "%.3f");
    bool input_double(const wxString &label, const double &value, const std::string &format = "%.3f");
    bool input_vec3(const std::string &label, const Vec3d &value, float width, const std::string &format = "%.3f");
    bool checkbox(const wxString &label, bool &value);
    static void text(const char *label);
    static void text(const std::string &label);
    static void text(const wxString &label);
    static void text_colored(const ImVec4& color, const char* label);
    static void text_colored(const ImVec4& color, const std::string& label);
    static void text_colored(const ImVec4& color, const wxString& label);
    void text_wrapped(const char *label, float wrap_width);
    void text_wrapped(const std::string &label, float wrap_width);
    void text_wrapped(const wxString &label, float wrap_width);
    void tooltip(const char *label, float wrap_width);
    void tooltip(const wxString &label, float wrap_width);

    // Float sliders: Manually inserted values aren't clamped by ImGui.Using this wrapper function does (when clamp==true).
    ImVec2 get_slider_icon_size() const;
    bool slider_float(const char* label, float* v, float v_min, float v_max, const char* format = "%.3f", float power = 1.0f, bool clamp = true, const wxString& tooltip = {}, bool show_edit_btn = true);
    bool slider_float(const std::string& label, float* v, float v_min, float v_max, const char* format = "%.3f", float power = 1.0f, bool clamp = true, const wxString& tooltip = {}, bool show_edit_btn = true);
    bool slider_float(const wxString& label, float* v, float v_min, float v_max, const char* format = "%.3f", float power = 1.0f, bool clamp = true, const wxString& tooltip = {}, bool show_edit_btn = true);

    bool image_button(ImTextureID user_texture_id, const ImVec2& size, const ImVec2& uv0 = ImVec2(0.0, 0.0), const ImVec2& uv1 = ImVec2(1.0, 1.0), int frame_padding = -1, const ImVec4& bg_col = ImVec4(0.0, 0.0, 0.0, 0.0), const ImVec4& tint_col = ImVec4(1.0, 1.0, 1.0, 1.0), ImGuiButtonFlags flags = 0);

    // Use selection = -1 to not mark any option as selected
    bool combo(const wxString& label, const std::vector<std::string>& options, int& selection, ImGuiComboFlags flags = 0);
    bool undo_redo_list(const ImVec2& size, const bool is_undo, bool (*items_getter)(const bool, int, const char**), int& hovered, int& selected, int& mouse_wheel);
    void search_list(const ImVec2& size, bool (*items_getter)(int, const char** label, const char** tooltip), char* search_str,
                     Search::OptionViewParameters& view_params, int& selected, bool& edited, int& mouse_wheel, bool is_localized);
    void title(const std::string& str);

    void disabled_begin(bool disabled);
    void disabled_end();

    bool want_mouse() const;
    bool want_keyboard() const;
    bool want_text_input() const;
    bool want_any_input() const;

    // Optional inputs are used for set up value inside of an optional, with default value
    // 
    // Extended function ImGui::InputInt to work with std::optional<int>, when value == def_val optional is released.
    static bool input_optional_int(const char *label, std::optional<int>& v, int step=1, int step_fast=100, ImGuiInputTextFlags flags=0, int def_val = 0);    
    // Extended function ImGui::InputFloat to work with std::optional<float> value near def_val cause release of optional
    static bool input_optional_float(const char* label, std::optional<float> &v, float step = 0.0f, float step_fast = 0.0f, const char* format = "%.3f", ImGuiInputTextFlags flags = 0, float def_val = .0f);
    // Extended function ImGui::DragFloat to work with std::optional<float> value near def_val cause release of optional
    static bool drag_optional_float(const char* label, std::optional<float> &v, float v_speed, float v_min, float v_max, const char* format, float power, float def_val = .0f);
    // Extended function ImGuiWrapper::slider_float to work with std::optional<float> value near def_val cause release of optional
    bool slider_optional_float(const char* label, std::optional<float> &v, float v_min, float v_max, const char* format = "%.3f", float power = 1.0f, bool clamp = true, const wxString& tooltip = {}, bool show_edit_btn = true, float def_val = .0f);
    // Extended function ImGuiWrapper::slider_float to work with std::optional<int>, when value == def_val than optional release its value
    bool slider_optional_int(const char* label, std::optional<int> &v, int v_min, int v_max, const char* format = "%.3f", float power = 1.0f, bool clamp = true, const wxString& tooltip = {}, bool show_edit_btn = true, int def_val = 0);

    /// <summary>
    /// Truncate text by ImGui draw function to specific width
    /// NOTE 1: ImGui must be initialized
    /// NOTE 2: Calculation for actual acive imgui font
    /// </summary>
    /// <param name="text">Text to be truncated</param>
    /// <param name="width">Maximal width before truncate</param>
    /// <param name="tail">String puted on end of text to be visible truncation</param>
    /// <returns>Truncated text</returns>
    static std::string trunc(const std::string &text,
                             float              width,
                             const char        *tail = " ..");

    /// <summary>
    /// Escape ## in data by add space between hashes
    /// Needed when user written text is visualized by ImGui.
    /// </summary>
    /// <param name="text">In/Out text to be escaped</param>
    static void escape_double_hash(std::string &text);

    /// <summary>
    /// Suggest loacation of dialog window,
    /// dependent on actual visible thing on platter
    /// like Gizmo menu size, notifications, ...
    /// To be near of polygon interest and not over it.
    /// And also not out of visible area.
    /// </summary>
    /// <param name="dialog_size">Define width and height of diaog window</param>
    /// <param name="interest">Area of interest. Result should be close to it</param>
    /// <param name="canvas_size">Available space a.k.a GLCanvas3D::get_current_canvas3D()</param>
    /// <returns>Suggestion for dialog offest</returns>
    static ImVec2 suggest_location(const ImVec2          &dialog_size,
                                   const Slic3r::Polygon &interest,
                                   const ImVec2          &canvas_size);

    /// <summary>
    /// Visualization of polygon
    /// </summary>
    /// <param name="polygon">Define what to draw</param>
    /// <param name="draw_list">Define where to draw it</param>
    /// <param name="color">Color of polygon</param>
    /// <param name="thickness">Width of polygon line</param>
    static void draw(const Polygon &polygon,
                     ImDrawList *   draw_list = ImGui::GetOverlayDrawList(),
                     ImU32 color     = ImGui::GetColorU32(COL_ORANGE_LIGHT),
                     float thickness = 3.f);

    /// <summary>
    /// Check that font ranges contain all chars in string
    /// (rendered Unicodes are stored in GlyphRanges)
    /// </summary>
    /// <param name="font">Contain glyph ranges</param>
    /// <param name="text">Vector of character to check</param>
    /// <returns>True when all glyphs from text are in font ranges</returns>
    static bool contain_all_glyphs(const ImFont *font, const std::string &text);
    static bool is_chars_in_ranges(const ImWchar *ranges, const char *chars_ptr);
    static bool is_char_in_ranges(const ImWchar *ranges, unsigned int letter);

    bool requires_extra_frame() const { return m_requires_extra_frame; }
    void set_requires_extra_frame() { m_requires_extra_frame = true; }
    void reset_requires_extra_frame() { m_requires_extra_frame = false; }

    static ImU32 to_ImU32(const ColorRGBA& color);
    static ImVec4 to_ImVec4(const ColorRGBA& color);
    static ColorRGBA from_ImU32(const ImU32& color);
    static ColorRGBA from_ImVec4(const ImVec4& color);

    ImFontAtlasCustomRect* GetTextureCustomRect(const wchar_t& tex_id);

    static const ImVec4 COL_GREY_DARK;
    static const ImVec4 COL_GREY_LIGHT;
    static const ImVec4 COL_ORANGE_DARK;
    static const ImVec4 COL_ORANGE_LIGHT;
    static const ImVec4 COL_WINDOW_BACKGROUND;
    static const ImVec4 COL_BUTTON_BACKGROUND;
    static const ImVec4 COL_BUTTON_HOVERED;
    static const ImVec4 COL_BUTTON_ACTIVE;

private:
    void init_font(bool compress);
    void init_input();
    void init_style();
    void render_draw_data(ImDrawData *draw_data);
    bool display_initialized() const;
    void destroy_font();
    std::vector<unsigned char> load_svg(const std::string& bitmap_name, unsigned target_width, unsigned target_height);

    static const char* clipboard_get(void* user_data);
    static void clipboard_set(void* user_data, const char* text);

    LastSliderStatus m_last_slider_status;
};


} // namespace GUI
} // namespace Slic3r

#endif // slic3r_ImGuiWrapper_hpp_

