#ifndef slic3r_GLGizmoEmboss_hpp_
#define slic3r_GLGizmoEmboss_hpp_

// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code,
// which overrides our localization "L" macro.
#include "GLGizmoBase.hpp"
#include "GLGizmoRotate.hpp"
#include "slic3r/GUI/GLTexture.hpp"
#include "slic3r/Utils/RaycastManager.hpp"
#include "slic3r/Utils/EmbossStyleManager.hpp"

#include "admesh/stl.h" // indexed_triangle_set
#include <optional>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>

#include "libslic3r/Emboss.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TextConfiguration.hpp"

#include <imgui/imgui.h>
#include <GL/glew.h>

class wxFont;
namespace Slic3r{
    class AppConfig;
    class GLVolume;

    enum class ModelVolumeType : int;
}

namespace Slic3r::GUI {
class MeshRaycaster;
class GLGizmoEmboss : public GLGizmoBase
{
public:
    GLGizmoEmboss(GLCanvas3D& parent);

    /// <summary>
    /// Create new embossed text volume by type on position of mouse
    /// </summary>
    /// <param name="volume_type">Object part / Negative volume / Modifier</param>
    /// <param name="mouse_pos">Define position of new volume</param>
    void create_volume(ModelVolumeType volume_type, const Vec2d &mouse_pos = Vec2d(-1,-1));

    /// <summary>
    /// Move window for edit emboss text near to embossed object
    /// NOTE: embossed object must be selected
    /// </summary>
    void set_fine_position();

protected:
    bool on_init() override;
    std::string on_get_name() const override;
    void on_render() override;
#if ENABLE_RAYCAST_PICKING
    virtual void on_register_raycasters_for_picking() override;
    virtual void on_unregister_raycasters_for_picking() override;
#else // !ENABLE_RAYCAST_PICKING
    void on_render_for_picking() override;
#endif // ENABLE_RAYCAST_PICKING
    void on_render_input_window(float x, float y, float bottom_limit) override;
    bool on_is_activable() const override { return true; }
    bool on_is_selectable() const override { return false; }
    void on_set_state() override;    
    
    void on_set_hover_id() override{ m_rotate_gizmo.set_hover_id(m_hover_id); }
    void on_enable_grabber(unsigned int id) override { m_rotate_gizmo.enable_grabber(); }
    void on_disable_grabber(unsigned int id) override { m_rotate_gizmo.disable_grabber(); }
    void on_start_dragging() override;
    void on_stop_dragging() override;
    void on_dragging(const UpdateData &data) override;    

    /// <summary>
    /// Rotate by text on dragging rotate grabers
    /// </summary>
    /// <param name="mouse_event">Information about mouse</param>
    /// <returns>Propagete normaly return false.</returns>
    bool on_mouse(const wxMouseEvent &mouse_event) override;

    bool wants_enter_leave_snapshots() const override { return true; }
    std::string get_gizmo_entering_text() const override { return _u8L("Enter emboss gizmo"); }
    std::string get_gizmo_leaving_text() const override { return _u8L("Leave emboss gizmo"); }
    std::string get_action_snapshot_name() override { return _u8L("Embossing actions"); }
private:
    void initialize();
    static EmbossStyles create_default_styles();
    // localized default text
    void set_default_text();

    bool start_volume_creation(ModelVolumeType volume_type, const Vec2d &screen_coor);

    void check_selection();
    ModelVolume *get_selected_volume();
    // create volume from text - main functionality
    bool process();
    void close();
    void discard_and_close();
    void draw_window();
    void draw_text_input();
    void draw_model_type();
    void fix_transformation(const FontProp &from, const FontProp &to);
    void draw_style_list();
    void draw_delete_style_button();
    void draw_revert_all_styles_button();
    void draw_style_rename_popup();
    void draw_style_rename_button();
    void draw_style_save_button(bool is_modified);
    void draw_style_save_as_popup();
    void draw_style_add_button();
    void init_font_name_texture();
    struct FaceName;
    void draw_font_preview(FaceName &face);
    void draw_font_list();
    void draw_style_edit();
    bool draw_italic_button();
    bool draw_bold_button();
    void draw_advanced();

    bool select_facename(const wxString& facename);
    void init_face_names();

    void do_translate(const Vec3d& relative_move);
    void do_rotate(float relative_z_angle);

    /// <summary>
    /// Reversible input float with option to restor default value
    /// TODO: make more general, static and move to ImGuiWrapper 
    /// </summary>
    /// <returns>True when value changed otherwise FALSE.</returns>
    bool rev_input(const std::string &name, float &value, const float *default_value, 
        const std::string &undo_tooltip, float step, float step_fast, const char *format, 
        ImGuiInputTextFlags flags = 0);
    bool rev_checkbox(const std::string &name, bool &value, const bool* default_value, const std::string  &undo_tooltip);
    bool rev_slider(const std::string &name, std::optional<int>& value, const std::optional<int> *default_value,
        const std::string &undo_tooltip, int v_min, int v_max, const std::string &format, const wxString &tooltip);
    bool rev_slider(const std::string &name, std::optional<float>& value, const std::optional<float> *default_value,
        const std::string &undo_tooltip, float v_min, float v_max, const std::string &format, const wxString &tooltip);
    bool rev_slider(const std::string &name, float &value, const float *default_value, 
        const std::string &undo_tooltip, float v_min, float v_max, const std::string &format, const wxString &tooltip);
    template<typename T, typename Draw>
    bool revertible(const std::string &name, T &value, const T *default_value, const std::string &undo_tooltip, float undo_offset, Draw draw);

    void set_minimal_window_size(bool is_advance_edit_style);
    const ImVec2 &get_minimal_window_size() const;

    // process mouse event
    bool on_mouse_for_rotation(const wxMouseEvent &mouse_event);
    bool on_mouse_for_translate(const wxMouseEvent &mouse_event);

    bool choose_font_by_wxdialog();
    bool choose_true_type_file();
    bool choose_svg_file();

    bool load_configuration(ModelVolume *volume);

    // When open text loaded from .3mf it could be written with unknown font
    bool m_is_unknown_font;
    void create_notification_not_valid_font(const TextConfiguration& tc);
    void remove_notification_not_valid_font();

    // This configs holds GUI layout size given by translated texts.
    // etc. When language changes, GUI is recreated and this class constructed again,
    // so the change takes effect. (info by GLGizmoFdmSupports.hpp)
    struct GuiCfg
    {
        // Zero means it is calculated in init function
        ImVec2 minimal_window_size              = ImVec2(0, 0);
        ImVec2 minimal_window_size_with_advance = ImVec2(0, 0);
        ImVec2 minimal_window_size_with_collections = ImVec2(0, 0);
        float        input_width                      = 0.f;
        float        delete_pos_x                     = 0.f;
        float        max_style_name_width             = 0.f;
        unsigned int icon_width                       = 0;

        // maximal width and height of style image
        Vec2i max_style_image_size = Vec2i(0, 0);

        float style_offset          = 0.f;
        float input_offset          = 0.f;
        float advanced_input_offset = 0.f;

        ImVec2 text_size;

        // maximal size of face name image
        Vec2i face_name_size = Vec2i(100, 0);
        float face_name_max_width = 100.f;
        float face_name_texture_offset_x = 105.f;

        // maximal texture generate jobs running at once
        unsigned int max_count_opened_font_files = 10;

        // Only translations needed for calc GUI size
        struct Translations
        {
            std::string type;
            std::string style;
            std::string font;
            std::string size;
            std::string depth;
            std::string use_surface;

            // advanced
            std::string char_gap;
            std::string line_gap;
            std::string boldness;
            std::string italic;
            std::string surface_distance;
            std::string angle;
            std::string collection;
        };
        Translations translations;
                
        GuiCfg() = default;
    };
    std::optional<const GuiCfg> m_gui_cfg;
    // setted only when wanted to use - not all the time
    std::optional<ImVec2> m_set_window_offset;
    bool m_is_advanced_edit_style = false;

    Emboss::StyleManager m_style_manager;

    struct FaceName{
        wxString wx_name;
        std::string name_truncated = "";
        size_t texture_index = 0;
        // State for generation of texture
        // when start generate create share pointers
        std::shared_ptr<std::atomic<bool>> cancel = nullptr;
        // R/W only on main thread - finalize of job
        std::shared_ptr<bool> is_created = nullptr;
    };

    // Keep sorted list of loadable face names
    struct Facenames
    {
        // flag to keep need of enumeration fonts from OS
        // false .. wants new enumeration check by Hash
        // true  .. already enumerated(During opened combo box)
        bool is_init = false;

        // data of can_load() faces
        std::vector<FaceName> faces = {};
        // Sorter set of Non valid face names in OS
        std::vector<wxString> bad   = {};

        // Configuration of font encoding
        const wxFontEncoding encoding = wxFontEncoding::wxFONTENCODING_SYSTEM;

        // Identify if preview texture exists
        GLuint texture_id = 0;
                
        // protection for open too much font files together
        // Gtk:ERROR:../../../../gtk/gtkiconhelper.c:494:ensure_surface_for_gicon: assertion failed (error == NULL): Failed to load /usr/share/icons/Yaru/48x48/status/image-missing.png: Error opening file /usr/share/icons/Yaru/48x48/status/image-missing.png: Too many open files (g-io-error-quark, 31)
        unsigned int count_opened_font_files = 0; 

        // Configuration for texture height
        const int count_cached_textures = 32;

        // index for new generated texture index(must be lower than count_cached_textures)
        size_t texture_index = 0;

        // hash created from enumerated font from OS
        // check when new font was installed
        size_t hash = 0;
    } m_face_names;
    static bool store(const Facenames &facenames);
    static bool load(Facenames &facenames);


    // Text to emboss
    std::string m_text;

    // actual volume
    ModelVolume *m_volume;

    // state of volume when open EmbossGizmo
    struct EmbossVolume
    {
        TriangleMesh tm;
        TextConfiguration tc;
        Transform3d tr;
        std::string name;
    };
    std::optional<EmbossVolume> m_unmodified_volume;

    // True when m_text contain character unknown by selected font
    bool m_text_contain_unknown_glyph = false;

    // cancel for previous update of volume to cancel finalize part
    std::shared_ptr<std::atomic<bool>> m_update_job_cancel;

    // Rotation gizmo
    GLGizmoRotate m_rotate_gizmo;
    // Value is set only when dragging rotation to calculate actual angle
    std::optional<float> m_rotate_start_angle;

    // when draging with text object hold screen offset of cursor from object center
    std::optional<Vec2d> m_dragging_mouse_offset;

    // TODO: it should be accessible by other gizmo too.
    // May be move to plater?
    RaycastManager m_raycast_manager;

    // Only when drag text object it stores world position
    std::optional<Transform3d> m_temp_transformation;

    // drawing icons
    GLTexture m_icons_texture;
    void init_icons();
    enum class IconType : unsigned {
        rename = 0,
        erase,
        add,
        save,
        undo,
        italic,
        unitalic,
        bold,
        unbold,
        system_selector,
        open_file,
        revert_all,
        // VolumeType icons
        part,
        negative,
        modifier,
        // automatic calc of icon's count
        _count
    };
    enum class IconState: unsigned { activable = 0, hovered /*1*/, disabled /*2*/};
    void draw_icon(IconType icon, IconState state, ImVec2 size = ImVec2(0,0));
    void draw_transparent_icon();
    bool draw_clickable(IconType icon, IconState state, IconType hover_icon, IconState hover_state);
    bool draw_button(IconType icon, bool disable = false);

    // only temporary solution
    static const std::string M_ICON_FILENAME;

public:
    /// <summary>
    /// Check if text is last solid part of object
    /// TODO: move to emboss gui utils
    /// </summary>
    /// <param name="text">Model volume of Text</param>
    /// <returns>True when object otherwise False</returns>
    static bool is_text_object(const ModelVolume *text);

    // TODO: move to file utils
    static std::string get_file_name(const std::string &file_path);
};

} // namespace Slic3r::GUI

#endif // slic3r_GLGizmoEmboss_hpp_
