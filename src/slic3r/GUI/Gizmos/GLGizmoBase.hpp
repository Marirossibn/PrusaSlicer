#ifndef slic3r_GLGizmoBase_hpp_
#define slic3r_GLGizmoBase_hpp_

#include "libslic3r/Point.hpp"
#include "libslic3r/Color.hpp"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/GLModel.hpp"
#if ENABLE_RAYCAST_PICKING
#include "slic3r/GUI/MeshUtils.hpp"
#include "slic3r/GUI/SceneRaycaster.hpp"
#endif // ENABLE_RAYCAST_PICKING

#include <cereal/archives/binary.hpp>

class wxWindow;
class wxMouseEvent;

namespace Slic3r {

class BoundingBoxf3;
class Linef3;
class ModelObject;

namespace GUI {

static const ColorRGBA DEFAULT_BASE_COLOR        = { 0.625f, 0.625f, 0.625f, 1.0f };
static const ColorRGBA DEFAULT_DRAG_COLOR        = ColorRGBA::WHITE();
static const ColorRGBA DEFAULT_HIGHLIGHT_COLOR   = ColorRGBA::ORANGE();
static const std::array<ColorRGBA, 3> AXES_COLOR = {{ ColorRGBA::X(), ColorRGBA::Y(), ColorRGBA::Z() }};
static const ColorRGBA CONSTRAINED_COLOR         = ColorRGBA::GRAY();

class ImGuiWrapper;
class GLCanvas3D;
enum class CommonGizmosDataID;
class CommonGizmosDataPool;

class GLGizmoBase
{
public:
    // Starting value for ids to avoid clashing with ids used by GLVolumes
    // (254 is choosen to leave some space for forward compatibility)
    static const unsigned int BASE_ID = 255 * 255 * 254;

    enum class EGrabberExtension
    {
        None = 0,
        PosX = 1 << 0,
        NegX = 1 << 1,
        PosY = 1 << 2,
        NegY = 1 << 3,
        PosZ = 1 << 4,
        NegZ = 1 << 5,
    };

protected:
    struct Grabber
    {
        static const float SizeFactor;
        static const float MinHalfSize;
        static const float DraggingScaleFactor;

        bool enabled{ true };
        bool dragging{ false };
        Vec3d center{ Vec3d::Zero() };
        Vec3d angles{ Vec3d::Zero() };
#if ENABLE_LEGACY_OPENGL_REMOVAL
        Transform3d matrix{ Transform3d::Identity() };
#endif // ENABLE_LEGACY_OPENGL_REMOVAL
        ColorRGBA color{ ColorRGBA::WHITE() };
        EGrabberExtension extensions{ EGrabberExtension::None };
#if ENABLE_RAYCAST_PICKING
        // the picking id shared by all the elements
        int picking_id{ -1 };
        bool elements_registered_for_picking{ false };
#endif // ENABLE_RAYCAST_PICKING

        Grabber() = default;
        ~Grabber();

#if ENABLE_RAYCAST_PICKING
        void render(bool hover, float size) { render(size, hover ? complementary(color) : color); }
#else
        void render(bool hover, float size) { render(size, hover ? complementary(color) : color, false); }
        void render_for_picking(float size) { render(size, color, true); }
#endif // ENABLE_RAYCAST_PICKING

        float get_half_size(float size) const;
        float get_dragging_half_size(float size) const;

#if ENABLE_RAYCAST_PICKING
        void register_raycasters_for_picking(int id);
        void unregister_raycasters_for_picking();
#endif // ENABLE_RAYCAST_PICKING

    private:
#if ENABLE_RAYCAST_PICKING
        void render(float size, const ColorRGBA& render_color);
#else
        void render(float size, const ColorRGBA& render_color, bool picking);
#endif // ENABLE_RAYCAST_PICKING

#if ENABLE_RAYCAST_PICKING
        static PickingModel s_cube;
        static PickingModel s_cone;
#else
        static GLModel s_cube;
        static GLModel s_cone;
#endif // ENABLE_RAYCAST_PICKING
    };

public:
    enum EState
    {
        Off,
        On,
        Num_States
    };

    struct UpdateData
    {
        const Linef3& mouse_ray;
        const Point& mouse_pos;

        UpdateData(const Linef3& mouse_ray, const Point& mouse_pos)
            : mouse_ray(mouse_ray), mouse_pos(mouse_pos)
        {}
    };

protected:
    GLCanvas3D& m_parent;

    int m_group_id{ -1 }; // TODO: remove only for rotate
    EState m_state{ Off };
    int m_shortcut_key{ 0 };
    std::string m_icon_filename;
    unsigned int m_sprite_id;
    int m_hover_id{ -1 };
    bool m_dragging{ false };
    mutable std::vector<Grabber> m_grabbers;
    ImGuiWrapper* m_imgui;
    bool m_first_input_window_render{ true };
    CommonGizmosDataPool* m_c{ nullptr };

public:
    GLGizmoBase(GLCanvas3D& parent,
                const std::string& icon_filename,
                unsigned int sprite_id);
    virtual ~GLGizmoBase() = default;

    bool init() { return on_init(); }

    void load(cereal::BinaryInputArchive& ar) { m_state = On; on_load(ar); }
    void save(cereal::BinaryOutputArchive& ar) const { on_save(ar); }

    std::string get_name(bool include_shortcut = true) const;

    EState get_state() const { return m_state; }
    void set_state(EState state) { m_state = state; on_set_state(); }

    int get_shortcut_key() const { return m_shortcut_key; }

    const std::string& get_icon_filename() const { return m_icon_filename; }

    bool is_activable() const { return on_is_activable(); }
    bool is_selectable() const { return on_is_selectable(); }
    CommonGizmosDataID get_requirements() const { return on_get_requirements(); }
    virtual bool wants_enter_leave_snapshots() const { return false; }
    virtual std::string get_gizmo_entering_text() const { assert(false); return ""; }
    virtual std::string get_gizmo_leaving_text() const { assert(false); return ""; }
    virtual std::string get_action_snapshot_name() { return _u8L("Gizmo action"); }
    void set_common_data_pool(CommonGizmosDataPool* ptr) { m_c = ptr; }

    unsigned int get_sprite_id() const { return m_sprite_id; }

    int get_hover_id() const { return m_hover_id; }
    void set_hover_id(int id);
    
    bool is_dragging() const { return m_dragging; }

    // returns True when Gizmo changed its state
    bool update_items_state();

    void render() { on_render(); }
#if !ENABLE_RAYCAST_PICKING
    void render_for_picking() { on_render_for_picking(); }
#endif // !ENABLE_RAYCAST_PICKING
    void render_input_window(float x, float y, float bottom_limit);

    /// <summary>
    /// Mouse tooltip text
    /// </summary>
    /// <returns>Text to be visible in mouse tooltip</returns>
    virtual std::string get_tooltip() const { return ""; }

    /// <summary>
    /// Is called when data (Selection) is changed
    /// </summary>
    virtual void data_changed(){};

    /// <summary>
    /// Implement when want to process mouse events in gizmo
    /// Click, Right click, move, drag, ...
    /// </summary>
    /// <param name="mouse_event">Keep information about mouse click</param>
    /// <returns>Return True when use the information and don't want to propagate it otherwise False.</returns>
    virtual bool on_mouse(const wxMouseEvent &mouse_event) { return false; }

#if ENABLE_RAYCAST_PICKING
    void register_raycasters_for_picking(bool use_group_id = false) { register_grabbers_for_picking(use_group_id); on_register_raycasters_for_picking(); }
    void unregister_raycasters_for_picking() { unregister_grabbers_for_picking(); on_unregister_raycasters_for_picking(); }
#endif // ENABLE_RAYCAST_PICKING

protected:
    virtual bool on_init() = 0;
    virtual void on_load(cereal::BinaryInputArchive& ar) {}
    virtual void on_save(cereal::BinaryOutputArchive& ar) const {}
    virtual std::string on_get_name() const = 0;
    virtual void on_set_state() {}
    virtual void on_set_hover_id() {}
    virtual bool on_is_activable() const { return true; }
    virtual bool on_is_selectable() const { return true; }
    virtual CommonGizmosDataID on_get_requirements() const { return CommonGizmosDataID(0); }
    virtual void on_enable_grabber(unsigned int id) {}
    virtual void on_disable_grabber(unsigned int id) {}
       
    // called inside use_grabbers
    virtual void on_start_dragging() {}
    virtual void on_stop_dragging() {}
    virtual void on_dragging(const UpdateData& data) {}

    virtual void on_render() = 0;
#if !ENABLE_RAYCAST_PICKING
    virtual void on_render_for_picking() = 0;
#endif // !ENABLE_RAYCAST_PICKING
    virtual void on_render_input_window(float x, float y, float bottom_limit) {}

#if ENABLE_RAYCAST_PICKING
    void register_grabbers_for_picking(bool use_group_id = false);
    void unregister_grabbers_for_picking();
    virtual void on_register_raycasters_for_picking() {}
    virtual void on_unregister_raycasters_for_picking() {}
#endif // ENABLE_RAYCAST_PICKING

    // Returns the picking color for the given id, based on the BASE_ID constant
    // No check is made for clashing with other picking color (i.e. GLVolumes)
    ColorRGBA picking_color_component(unsigned int id) const;

    void render_grabbers(const BoundingBoxf3& box) const;
    void render_grabbers(float size) const;
#if !ENABLE_RAYCAST_PICKING
    void render_grabbers_for_picking(const BoundingBoxf3& box) const;
#endif // !ENABLE_RAYCAST_PICKING

    std::string format(float value, unsigned int decimals) const;

    // Mark gizmo as dirty to Re-Render when idle()
    void set_dirty();

    /// <summary>
    /// function which 
    /// Set up m_dragging and call functions
    /// on_start_dragging / on_dragging / on_stop_dragging
    /// </summary>
    /// <param name="mouse_event">Keep information about mouse click</param>
    /// <returns>same as on_mouse</returns>
    bool use_grabbers(const wxMouseEvent &mouse_event);

#if ENABLE_WORLD_COORDINATE
    void do_stop_dragging(bool perform_mouse_cleanup);
#endif // ENABLE_WORLD_COORDINATE

private:
    // Flag for dirty visible state of Gizmo
    // When True then need new rendering
    bool m_dirty{ false };
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoBase_hpp_
