#include "GLGizmoEmboss.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectManipulation.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/MainFrame.hpp" // to update title when add text
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/CameraUtils.hpp"
#include "slic3r/GUI/Jobs/EmbossJob.hpp"
#include "slic3r/GUI/Jobs/NotificationProgressIndicator.hpp"
#include "slic3r/Utils/WxFontUtils.hpp"
#include "slic3r/Utils/FontListSerializable.hpp"

// TODO: remove include
#include "libslic3r/SVG.hpp"      // debug store
#include "libslic3r/Geometry.hpp" // covex hull 2d

#include "libslic3r/NSVGUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ClipperUtils.hpp" // union_ex
#include "libslic3r/AppConfig.hpp"    // store/load font list
#include "libslic3r/MapUtils.hpp"
#include "libslic3r/Format/OBJ.hpp" // load obj file for default object
#include "libslic3r/BuildVolume.hpp"

#include "imgui/imgui_stdlib.h" // using std::string for inputs
#include "nanosvg/nanosvg.h"    // load SVG file

#include <wx/font.h>
#include <wx/fontutil.h>
#include <wx/fontdlg.h>
#include <wx/fontenum.h>

#include <GL/glew.h>

// uncomment for easier debug
//#define ALLOW_DEBUG_MODE
#ifdef ALLOW_DEBUG_MODE
#define ALLOW_ADD_FONT_BY_FILE
#define ALLOW_ADD_FONT_BY_OS_SELECTOR
#define SHOW_WX_FONT_DESCRIPTOR
#define SHOW_IMGUI_ATLAS
#define SHOW_ICONS_TEXTURE
#define SHOW_FINE_POSITION
#define SHOW_WX_WEIGHT_INPUT
#define DRAW_PLACE_TO_ADD_TEXT
#define ALLOW_REVERT_ALL_STYLES
#endif // ALLOW_DEBUG_MODE

#define SHOW_WX_FONT_DESCRIPTOR
#define ALLOW_ADD_FONT_BY_FILE
#define ALLOW_ADD_FONT_BY_OS_SELECTOR
#define ALLOW_REVERT_ALL_STYLES

using namespace Slic3r;
using namespace Slic3r::GUI;

GLGizmoEmboss::GLGizmoEmboss(GLCanvas3D &parent)
    : GLGizmoBase(parent, M_ICON_FILENAME, -2)
    , m_volume(nullptr)
    , m_exist_notification(false)
    , m_is_initialized(false) // initialize on first opening gizmo
    , m_rotate_gizmo(parent, GLGizmoRotate::Axis::Z) // grab id = 2 (Z axis)
    , m_font_manager(m_imgui->get_glyph_ranges())
{
    m_rotate_gizmo.set_group_id(0);
    // TODO: add suggestion to use https://fontawesome.com/
    // (copy & paste) unicode symbols from web    
}

void GLGizmoEmboss::set_fine_position()
{
    const Selection &            selection = m_parent.get_selection();
    const Selection::IndicesList indices   = selection.get_volume_idxs();
    // no selected volume
    if (indices.empty()) return;
    const GLVolume *volume = selection.get_volume(*indices.begin());
    // bad volume selected (e.g. deleted one)
    if (volume == nullptr) return;

    const Camera &camera = wxGetApp().plater()->get_camera();
    Polygon hull = CameraUtils::create_hull2d(camera, *volume);

    const ImVec2 &windows_size = get_minimal_window_size();
    ImVec2 offset = ImGuiWrapper::suggest_location(windows_size, hull);
    m_set_window_offset = offset;
    return;

    Polygon rect({Point(offset.x, offset.y),
                  Point(offset.x + windows_size.x, offset.y),
                  Point(offset.x + windows_size.x, offset.y + windows_size.y),
                  Point(offset.x, offset.y + windows_size.y)});
    ImGuiWrapper::draw(hull);
    ImGuiWrapper::draw(rect);
}

#ifdef SHOW_FINE_POSITION
static void draw_fine_position(const Selection &selection)
{
    const Selection::IndicesList indices = selection.get_volume_idxs();
    // no selected volume
    if (indices.empty()) return;
    const GLVolume *volume = selection.get_volume(*indices.begin());
    // bad volume selected (e.g. deleted one)
    if (volume == nullptr) return;

    const Camera &camera = wxGetApp().plater()->get_camera();
    Slic3r::Polygon hull   = CameraUtils::create_hull2d(camera, *volume);

    ImVec2 windows_size(174, 202);
    ImVec2 offset       = ImGuiWrapper::suggest_location(windows_size, hull);
    Slic3r::Polygon rect(
        {Point(offset.x, offset.y), Point(offset.x + windows_size.x, offset.y),
         Point(offset.x + windows_size.x, offset.y + windows_size.y),
         Point(offset.x, offset.y + windows_size.y)});
    ImGuiWrapper::draw(hull);
    ImGuiWrapper::draw(rect);
}
#endif // SHOW_FINE_POSITION

void GLGizmoEmboss::create_volume(ModelVolumeType volume_type, const Vec2d& mouse_pos)
{
    assert(volume_type == ModelVolumeType::MODEL_PART ||
           volume_type == ModelVolumeType::NEGATIVE_VOLUME ||
           volume_type == ModelVolumeType::PARAMETER_MODIFIER);
    if (!m_is_initialized) initialize();    
    set_default_text();

    Vec2d screen_coor = mouse_pos;
    if (mouse_pos.x() < 0 || mouse_pos.y() < 0) {
        // use center of screen
        auto screen_size = m_parent.get_canvas_size();
        screen_coor.x()  = screen_size.get_width() / 2.;
        screen_coor.y()  = screen_size.get_height() / 2.;        
    }

    std::optional<int> object_idx;

    std::optional<Transform3d> hit_vol_tr;
    const Selection &selection = m_parent.get_selection();
    if (!selection.is_empty()) { 
        object_idx = selection.get_object_idx(); 
        int hovered_id = m_parent.get_first_hover_volume_idx();
        if (hovered_id >= 0) {
            GLVolume *gl_volume = m_parent.get_volumes().volumes[hovered_id];
            hit_vol_tr = gl_volume->get_instance_transformation().get_matrix();
        }
    }
    Plater* plater = wxGetApp().plater();
    const Camera &camera = plater->get_camera();
    auto data = std::make_unique<EmbossDataCreate>(
        m_font_manager.get_font_file(), 
        create_configuration(),
        create_volume_name(), volume_type, screen_coor, object_idx,
        hit_vol_tr, camera,
        plater->build_volume().bed_shape(),
        &m_raycast_manager);

    auto &worker = plater->get_ui_job_worker();    
    queue_job(worker, std::make_unique<EmbossCreateJob>(std::move(data)));
}

#ifdef DRAW_PLACE_TO_ADD_TEXT
static void draw_place_to_add_text() {
    ImVec2 mp = ImGui::GetMousePos();
    Vec2d mouse_pos(mp.x, mp.y);
    const Camera &camera = wxGetApp().plater()->get_camera();
    Vec3d  p1 = CameraUtils::get_z0_position(camera, mouse_pos);
    std::vector<Vec3d> rect3d{p1 + Vec3d(5, 5, 0), p1 + Vec3d(-5, 5, 0),
                              p1 + Vec3d(-5, -5, 0), p1 + Vec3d(5, -5, 0)};
    Points rect2d = CameraUtils::project(camera, rect3d);
    ImGuiWrapper::draw(Slic3r::Polygon(rect2d));
}
#endif // DRAW_PLACE_TO_ADD_TEXT


bool GLGizmoEmboss::on_mouse_for_rotation(const wxMouseEvent &mouse_event)
{
    if (mouse_event.Dragging()) {
        if (m_dragging) {
            // temporary rotation
            TransformationType transformation_type(
                TransformationType::Local_Relative_Independent);
            Vec3d rotation(0., 0., m_rotate_gizmo.get_angle());
            m_parent.get_selection().rotate(rotation, transformation_type);
        }
    } else if (mouse_event.LeftUp()) {
        if (m_dragging) {
            // apply rotation
            m_parent.do_rotate(L("Text-Rotate"));
        }
    }
    return false;
}

bool GLGizmoEmboss::on_mouse_for_translate(const wxMouseEvent &mouse_event)
{
    // filter events
    if (!mouse_event.Dragging() && 
        !mouse_event.LeftUp() &&
        !mouse_event.LeftDown())
        return false;

    // text volume must be selected
    if (m_volume == nullptr) return false;

    // must exist hover object
    int hovered_id = m_parent.get_first_hover_volume_idx();
    if (hovered_id < 0) return false;

    GLVolume *gl_volume = m_parent.get_volumes().volumes[hovered_id];
    const ModelObjectPtrs &objects = wxGetApp().plater()->model().objects;
    ModelVolume *act_model_volume = get_model_volume(gl_volume, objects);

    // hovered object must be actual text volume
    if (m_volume != act_model_volume) return false;

    RaycastManager::SkipVolume skip(m_volume->id().id);
    // detect start text dragging
    if (mouse_event.LeftDown()) {
        // initialize raycasters
        // IMPROVE: move to job, for big scene it slows down 
        ModelObject *act_model_object = act_model_volume->get_object();
        m_raycast_manager.actualize(act_model_object, &skip);
        return false;
    }

    // wxCoord == int --> wx/types.h
    Vec2i mouse_coord(mouse_event.GetX(), mouse_event.GetY());
    Vec2d mouse_pos = mouse_coord.cast<double>();
    const Camera &camera = wxGetApp().plater()->get_camera();
    auto hit = m_raycast_manager.unproject(mouse_pos, camera, &skip);
    if (!hit.has_value()) { 
        // there is no hit
        // show common translation of object
        m_parent.toggle_model_objects_visibility(true);
        m_temp_transformation = {};
        return false; 
    }
        
    if (mouse_event.Dragging()) {
        // hide common dragging of object
        m_parent.toggle_model_objects_visibility(false, m_volume->get_object(), gl_volume->instance_idx(), m_volume);

        // Show temporary position
        // TODO: store z-rotation and aply after transformation matrix        
        Transform3d object_trmat = m_raycast_manager.get_transformation(hit->tr_key);
        Transform3d trmat = Emboss::create_transformation_onto_surface(hit->position, hit->normal);
        m_temp_transformation = object_trmat * trmat;
    } else if (mouse_event.LeftUp()) {
        // TODO: Disable apply common transformation after draggig
        // Call after is used for apply transformation after common dragging to rewrite it
        Transform3d volume_trmat =
            gl_volume->get_instance_transformation().get_matrix().inverse() *
            *m_temp_transformation;
        wxGetApp().plater()->CallAfter([volume_trmat, mv = m_volume]() {
            mv->set_transformation(volume_trmat);
        });

        m_parent.toggle_model_objects_visibility(true);
        // Apply temporary position
        m_temp_transformation = {};     
    }
    return false;
}

bool GLGizmoEmboss::on_mouse(const wxMouseEvent &mouse_event)
{
    // not selected volume
    if (m_volume == nullptr) return false;

    // do not process moving event
    if (mouse_event.Moving()) return false;

    if (on_mouse_for_rotation(mouse_event)) return true;
    if (on_mouse_for_translate(mouse_event)) return true;
    return false;
}

bool GLGizmoEmboss::on_init()
{
    m_rotate_gizmo.init();
    ColorRGBA gray_color(.6f, .6f, .6f, .3f);
    m_rotate_gizmo.set_highlight_color(gray_color);

    m_shortcut_key = WXK_CONTROL_T;
    return true;
}

std::string GLGizmoEmboss::on_get_name() const { return _u8L("Emboss"); }

void GLGizmoEmboss::on_render() {
    // no volume selected
    if (m_volume == nullptr) return;
    Selection &selection = m_parent.get_selection();
    if (selection.is_empty()) return;

    if (m_temp_transformation.has_value()) {
        // draw text volume on temporary position
        const GLVolume& gl_volume = *selection.get_volume(*selection.get_volume_idxs().begin());
        glsafe(::glPushMatrix());
        glsafe(::glMultMatrixd(m_temp_transformation->data()));                
        GLShaderProgram *shader = wxGetApp().get_shader("gouraud_light");
        shader->start_using();

        // dragging object must be selected so draw it with correct color
        //auto color = gl_volume.color;
        //auto color = gl_volume.render_color;
        auto color = GLVolume::SELECTED_COLOR;
        // Set transparent color for NEGATIVE_VOLUME & PARAMETER_MODIFIER
        bool is_transparent = m_volume->type() != ModelVolumeType::MODEL_PART;        
        if (is_transparent) {
            color = ColorRGBA(color.r(), color.g(), color.b(), 0.5f);            
            glsafe(::glEnable(GL_BLEND));
            glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
        }
        shader->set_uniform("uniform_color", color);

        glsafe(::glEnable(GL_DEPTH_TEST));
        gl_volume.indexed_vertex_array.render();
        glsafe(::glDisable(GL_DEPTH_TEST));

        if (is_transparent) glsafe(::glDisable(GL_BLEND));

        shader->stop_using();
        glsafe(::glPopMatrix());
    }    

    // Do NOT render rotation grabbers when dragging object
    bool is_rotate_by_grabbers = m_dragging;
    if (!m_parent.is_dragging() || is_rotate_by_grabbers) {
        glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
        m_rotate_gizmo.render();
    }
}

void GLGizmoEmboss::on_render_for_picking() {
    m_rotate_gizmo.render_for_picking();
}

void GLGizmoEmboss::on_render_input_window(float x, float y, float bottom_limit)
{
    initialize();
    check_selection(); 

    // TODO: fix width - showing scroll in first draw of advanced.
    const ImVec2 &min_window_size = get_minimal_window_size();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, min_window_size);

#ifdef SHOW_FINE_POSITION
    // draw suggested position of window
    draw_fine_position(m_parent.get_selection());
#endif // SHOW_FINE_POSITION
#ifdef DRAW_PLACE_TO_ADD_TEXT
    draw_place_to_add_text();
#endif // DRAW_PLACE_TO_ADD_TEXT


    // check if is set window offset
    if (m_set_window_offset.has_value()) {
        ImGui::SetNextWindowPos(*m_set_window_offset, ImGuiCond_Always);
        m_set_window_offset.reset();
    }

    ImGuiWindowFlags flag = //ImGuiWindowFlags_AlwaysAutoResize 
               //ImGuiWindowFlags_NoResize         
               ImGuiWindowFlags_NoCollapse
        ;
    bool is_open = true;
    if (ImGui::Begin(on_get_name().c_str(), &is_open, flag))
        draw_window();
    ImGui::End();

    // close button in header was hit
    if (!is_open) { 
        close();     
    }

    ImGui::PopStyleVar(); // WindowMinSize
}

void GLGizmoEmboss::on_set_state()
{
    // set manipulator to be able to rotate with text
    ObjectManipulation *manipul = wxGetApp().obj_manipul();
    static ECoordinatesType prev_coordinate_type = ECoordinatesType::World;
    if (GLGizmoBase::m_state == GLGizmoBase::Off)
        manipul->set_coordinates_type(prev_coordinate_type); // set previous state
    else if (GLGizmoBase::m_state == GLGizmoBase::On) {
        prev_coordinate_type = manipul->get_coordinates_type();
        manipul->set_coordinates_type(ECoordinatesType::Local);
    }

    m_rotate_gizmo.set_state(GLGizmoBase::m_state);

    // Closing gizmo. e.g. selecting another one
    if (GLGizmoBase::m_state == GLGizmoBase::Off) {
        // refuse outgoing during text preview
        if (false) {
            GLGizmoBase::m_state = GLGizmoBase::On;
            auto notification_manager = wxGetApp().plater()->get_notification_manager();
            notification_manager->push_notification(
                NotificationType::CustomNotification,
                NotificationManager::NotificationLevel::RegularNotificationLevel,
                _u8L("ERROR: Wait until ends or Cancel process."));
            return;
        }
        m_volume = nullptr;
        remove_notification_not_valid_font();
    } else if (GLGizmoBase::m_state == GLGizmoBase::On) {
        if (!m_is_initialized) initialize();

        // to reload fonts from system, when install new one
        wxFontEnumerator::InvalidateCache();

        // Try(when exist) set configuration by volume
        load_configuration(get_selected_volume());

        // change position of just opened emboss window
        set_fine_position();

        // when open by hyperlink it needs to show up
        // or after key 'T' windows doesn't appear
        m_parent.set_as_dirty();
    }
}

void GLGizmoEmboss::on_start_dragging() { m_rotate_gizmo.start_dragging(); }
void GLGizmoEmboss::on_stop_dragging()
{
    m_rotate_gizmo.stop_dragging();

    // TODO: when start second rotatiton previous rotation rotate draggers
    // This is fast fix for second try to rotate
    // When fixing, move grabber above text (not on side)
    m_rotate_gizmo.set_angle(0);
}

void GLGizmoEmboss::initialize()
{
    if (m_is_initialized) return;
    m_is_initialized = true;

    GuiCfg cfg; // initialize by default values;
    
    float line_height = ImGui::GetTextLineHeight();
    float line_height_with_spacing = ImGui::GetTextLineHeightWithSpacing();
    float space = line_height_with_spacing - line_height;

    cfg.max_font_name_width = ImGui::CalcTextSize("Maximal font name").x;

    cfg.icon_width = std::ceil(line_height);
    // make size pair number
    if (cfg.icon_width % 2 != 0) ++cfg.icon_width;

    float icon_width_with_spacing = cfg.icon_width + space;
    float scroll_width = icon_width_with_spacing; // TODO: fix it
    cfg.combo_font_width = cfg.max_font_name_width + space
                                  + icon_width_with_spacing
                                  + scroll_width;
    cfg.delete_pos_x = cfg.max_font_name_width + space;
    int count_line_of_text = 3;
    cfg.text_size = ImVec2(-FLT_MIN, line_height_with_spacing * count_line_of_text);
    ImVec2 letter_m_size = ImGui::CalcTextSize("M");
    int count_letter_M_in_input = 6;
    cfg.advanced_input_width = letter_m_size.x * count_letter_M_in_input;
    GuiCfg::Translations &tr = cfg.translations;
    tr.font                  = _u8L("Font");
    tr.size                  = _u8L("Height");
    tr.depth                 = _u8L("Depth");
    cfg.style_edit_text_width =
        3 * space + ImGui::GetTreeNodeToLabelSpacing() +
        std::max(ImGui::CalcTextSize(tr.font.c_str()).x,
                 std::max(ImGui::CalcTextSize(tr.size.c_str()).x,
                          ImGui::CalcTextSize(tr.depth.c_str()).x));

    // calculate window size
    const ImGuiStyle &style = ImGui::GetStyle();
    float window_title = line_height + 2*style.FramePadding.y;
    float input_height = line_height_with_spacing + 2*style.FramePadding.y;
    float tree_header  = line_height_with_spacing;
    float window_height = 
        window_title + // window title
        cfg.text_size.y +  // text field
        input_height * 3 + // type Radios + style selector + close button
        tree_header +      // Edit style
        2 * style.WindowPadding.y;
    float window_width = cfg.combo_font_width + style.WindowPadding.x * 2;
    cfg.minimal_window_size = ImVec2(window_width, window_height);

    float addition_edit_height        = input_height * 3 + tree_header;
    cfg.minimal_window_size_with_edit = ImVec2(cfg.minimal_window_size.x,
                                               cfg.minimal_window_size.y +
                                                   addition_edit_height);

    float advance_height = input_height * 4;
    cfg.minimal_window_size_with_advance =
        ImVec2(cfg.minimal_window_size_with_edit.x,
               cfg.minimal_window_size_with_edit.y + advance_height);

    cfg.min_style_image_height = line_height_with_spacing;
    cfg.max_style_image_width  = cfg.max_font_name_width -
                                2 * style.FramePadding.x;

    // initialize default font
    FontList default_font_list = create_default_font_list();
    for (const FontItem &fi : default_font_list) {
        assert(cfg.default_styles.find(fi.name) == cfg.default_styles.end());
        cfg.default_styles[fi.name] = fi; // copy
    }
    m_gui_cfg.emplace(cfg);

    // TODO: What to do when icon was NOT loaded? Generate them?
    bool success = init_icons();
    assert(success);

    const AppConfig *app_cfg = wxGetApp().app_config;
    FontList font_list = load_font_list_from_app_config(app_cfg);
    m_font_manager.add_fonts(font_list);
    // TODO: select last session sellected font index

    if (!m_font_manager.load_first_valid_font()) {
        m_font_manager.add_fonts(default_font_list);
        // TODO: What to do when default fonts are not loadable?
        bool success = m_font_manager.load_first_valid_font();
        assert(success);
    }
    set_default_text();
    select_stored_font_item();
}

FontList GLGizmoEmboss::create_default_font_list()
{
    // https://docs.wxwidgets.org/3.0/classwx_font.html
    // Predefined objects/pointers: wxNullFont, wxNORMAL_FONT, wxSMALL_FONT, wxITALIC_FONT, wxSWISS_FONT
    return {
        WxFontUtils::get_font_item(*wxNORMAL_FONT, _u8L("NORMAL")), // wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT)
        WxFontUtils::get_font_item(*wxSMALL_FONT, _u8L("SMALL")),  // A font using the wxFONTFAMILY_SWISS family and 2 points smaller than wxNORMAL_FONT.
        WxFontUtils::get_font_item(*wxITALIC_FONT, _u8L("ITALIC")), // A font using the wxFONTFAMILY_ROMAN family and wxFONTSTYLE_ITALIC style and of the same size of wxNORMAL_FONT.
        WxFontUtils::get_font_item(*wxSWISS_FONT, _u8L("SWISS")),  // A font identic to wxNORMAL_FONT except for the family used which is wxFONTFAMILY_SWISS.
        WxFontUtils::get_font_item(wxFont(10, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD), _u8L("MODERN"))
        //, WxFontUtils::get_os_font() == wxNORMAL_FONT
    };
}

void GLGizmoEmboss::set_default_text()
{
    m_text = _u8L("Embossed text");
}

#include "imgui/imgui_internal.h" // to unfocus input --> ClearActiveID
void GLGizmoEmboss::check_selection()
{
    ModelVolume *vol = get_selected_volume();
    // is same volume selected?
    if (vol != nullptr && m_volume == vol) return;

    // for changed volume notification is NOT valid
    remove_notification_not_valid_font();

    // Do not use focused input value when switch volume(it must swith value)
    if (m_volume != nullptr) ImGui::ClearActiveID();

    // is select embossed volume?
    if (load_configuration(vol)) 
        // successfull load volume for editing
        return;
    
    // behave like adding new text
    m_volume = nullptr;
    set_default_text();
}

ModelVolume *GLGizmoEmboss::get_selected_volume()
{
    return get_selected_volume(m_parent.get_selection(),
                               wxGetApp().plater()->model().objects);
}

ModelVolume *GLGizmoEmboss::get_model_volume(const GLVolume *      gl_volume,
                                             const ModelObjectPtrs& objects)
{
    const GLVolume::CompositeID &id = gl_volume->composite_id;

    if (id.object_id < 0 ||
        static_cast<size_t>(id.object_id) >= objects.size())
        return nullptr;
    ModelObject *object = objects[id.object_id];

    if (id.volume_id < 0 ||
        static_cast<size_t>(id.volume_id) >= object->volumes.size())
        return nullptr;
    return object->volumes[id.volume_id];
}

ModelVolume *GLGizmoEmboss::get_selected_volume(const Selection &selection,
                                                const ModelObjectPtrs& objects)
{
    int object_idx = selection.get_object_idx();
    // is more object selected?
    if (object_idx == -1) return nullptr;

    auto volume_idxs = selection.get_volume_idxs();
    // is more volumes selected?
    if (volume_idxs.size() != 1) return nullptr;
    unsigned int                 vol_id_gl = *volume_idxs.begin();
    const GLVolume *             vol_gl    = selection.get_volume(vol_id_gl);
    return get_model_volume(vol_gl, objects);
}

bool GLGizmoEmboss::process()
{
    // no volume is selected -> selection from right panel
    if (m_volume == nullptr) return false;

    // exist loaded font?
    std::shared_ptr<Emboss::FontFile>& font_file = m_font_manager.get_font_file();
    if (font_file == nullptr) return false;
    auto data = std::make_unique<EmbossDataUpdate>(font_file,
                                             create_configuration(),
                                             create_volume_name(), m_volume);
        
    auto &worker = wxGetApp().plater()->get_ui_job_worker();
    replace_job(worker, std::make_unique<EmbossUpdateJob>(std::move(data)));
    
    // notification is removed befor object is changed by job
    remove_notification_not_valid_font();
    return true;
}

void GLGizmoEmboss::close()
{
    // close gizmo == open it again
    m_parent.get_gizmos_manager().open_gizmo(GLGizmosManager::Emboss);
}

void GLGizmoEmboss::select_stored_font_item()
{    
    const std::string &name = m_font_manager.get_font_item().name;
    const auto &styles = m_gui_cfg->default_styles;
    const auto &it = styles.find(name);
    if (it == styles.end()) { 
        m_stored_font_item.reset();
        return;
    }
    m_stored_font_item = it->second;
}

void GLGizmoEmboss::draw_window()
{
#ifdef ALLOW_DEBUG_MODE
    if (ImGui::Button("re-process")) process();
    if (ImGui::Button("add svg")) choose_svg_file();
    if (ImGui::Button("use system font")) {
        size_t font_index = m_font_list.size();
        m_font_list.emplace_back(WxFontUtils::get_os_font());
        bool loaded = load_font(font_index);
    }
#endif //  ALLOW_DEBUG_MODE
    bool exist_font_file = m_font_manager.get_font_file() != nullptr;
    if (!exist_font_file) {
        m_imgui->text_colored(
            ImGuiWrapper::COL_ORANGE_LIGHT,
            _L("Warning: No font is selected. Select correct one."));
    }
    draw_text_input();
    draw_model_type();
    draw_style_list();
    if (ImGui::TreeNode(_u8L("Edit style").c_str())) {
#ifdef SHOW_WX_FONT_DESCRIPTOR
        ImGui::SameLine();
        m_imgui->text_colored(ImGuiWrapper::COL_GREY_DARK, m_font_manager.get_font_item().path);
#endif // SHOW_WX_FONT_DESCRIPTOR
        draw_style_edit();
        ImGui::TreePop();
        if (!m_is_edit_style)
            set_minimal_window_size(true, m_is_advanced_edit_style);
    } else if (m_is_edit_style)
        set_minimal_window_size(false, m_is_advanced_edit_style);

    if (ImGui::Button(_u8L("Close").c_str())) close();

    // Option to create text volume when reselecting volumes
    m_imgui->disabled_begin(!exist_font_file);
    if (m_volume == nullptr) {
        ImGui::SameLine();
        if (ImGui::Button(_u8L("Generate object").c_str()))
            create_volume(ModelVolumeType::MODEL_PART);
    }
    m_imgui->disabled_end();

#ifdef SHOW_ICONS_TEXTURE    
    auto &t = m_icons_texture;
    ImGui::Image((void *) t.get_id(), ImVec2(t.get_width(), t.get_height()));
#endif //SHOW_ICONS_TEXTURE
#ifdef SHOW_IMGUI_ATLAS
    auto &atlas = m_font_manager.m_imgui_font_atlas;
    ImGui::Image(atlas.TexID, ImVec2(atlas.TexWidth, atlas.TexHeight));
#endif // SHOW_IMGUI_ATLAS
}

void GLGizmoEmboss::draw_text_input()
{
    static const ImGuiInputTextFlags flags =
        ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_AutoSelectAll;

    ImFont *imgui_font = m_font_manager.get_imgui_font(m_text);
    bool    exist_font = imgui_font != nullptr && imgui_font->IsLoaded();
    if (exist_font) ImGui::PushFont(imgui_font);

    bool   exist_change   = false;
    float  window_height  = ImGui::GetWindowHeight();
    float  minimal_height = get_minimal_window_size().y;
    float  extra_height   = window_height - minimal_height;
    ImVec2 text_size(m_gui_cfg->text_size.x,
                     m_gui_cfg->text_size.y + extra_height);
    if (ImGui::InputTextMultiline("##Text", &m_text, text_size, flags)) {
        process();
        exist_change = true;
    }

    if (exist_font) ImGui::PopFont();

    // show warning about incorrectness view of font
    // TODO: add char gap and line gap
    std::string warning;
    const FontProp& prop = m_font_manager.get_font_prop();
    if (!exist_font) {
        warning = _u8L("Can't write text by selected font.");
    }else if (prop.skew.has_value())
        warning = prop.boldness.has_value() ?
                      _u8L("Italic & Bold is NOT shown") :
                      _u8L("Italic is NOT shown");
    else if (prop.boldness.has_value())
        warning = _u8L("Boldness is NOT shown");

    if (!warning.empty()) {
        ImVec2 cursor = ImGui::GetCursorPos();
        float width = ImGui::GetContentRegionAvailWidth();
        ImVec2 size = ImGui::CalcTextSize(warning.c_str());
        ImVec2 padding = ImGui::GetStyle().FramePadding;
        ImGui::SetCursorPos(ImVec2(width - size.x + padding.x,
                                   cursor.y - size.y - padding.y));
        m_imgui->text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, warning);
        ImGui::SetCursorPos(cursor);
    }

    // Extend font ranges
    // imgui_font has to be unused
    if (exist_change) m_font_manager.clear_imgui_font();
}

void GLGizmoEmboss::draw_font_list()
{
    class MyFontEnumerator : public wxFontEnumerator
    {
        wxArrayString m_facenames;
        wxFontEncoding m_encoding;
        bool m_fixed_width_only;
        bool m_is_init;
    public: 
        MyFontEnumerator(wxFontEncoding encoding, bool fixed_width_only)
            : m_encoding(encoding)
            , m_fixed_width_only(fixed_width_only)
            , m_is_init(false)
        {}
        const wxArrayString& get_facenames() const{ return m_facenames; }
        bool is_init() const { return m_is_init; }
        bool init() {
            if (m_is_init) return false;
            m_is_init = true;
            if (!wxFontEnumerator::EnumerateFacenames(m_encoding, m_fixed_width_only)) return false;
            if (m_facenames.empty()) return false;
            std::sort(m_facenames.begin(), m_facenames.end());
            return true;
        }

        std::vector<std::string> m_efacenames;
    protected:
        virtual bool OnFacename(const wxString& facename) wxOVERRIDE {
            // vertical font start with @, we will filter it out
            if (facename.empty() || facename[0] == '@') return true;
            wxFont wx_font(wxFontInfo().FaceName(facename).Encoding(m_encoding));
            //*
            if (!WxFontUtils::can_load(wx_font)) return true; // can't load
            /*/
            auto ff = WxFontUtils::create_font_file(wx_font);
            if (ff == nullptr) {
                m_efacenames.emplace_back(facename.c_str());
                return true; // can't create font file
            } // */
            m_facenames.Add(facename);
            return true;
        }
    };
    wxFontEncoding encoding = wxFontEncoding::wxFONTENCODING_SYSTEM;
    bool fixed_width_only = false;
    static MyFontEnumerator fontEnumerator(encoding, fixed_width_only);

    std::optional<wxFont> &wx_font_opt = m_font_manager.get_wx_font();
    wxString actual_face_name = wx_font_opt.has_value() ?
        wx_font_opt->GetFaceName() : "";
    const char * selected = (!actual_face_name.empty()) ?
        actual_face_name.ToUTF8().data() : " --- ";
    if (ImGui::BeginCombo("##font_selector", selected)) {
        if(!fontEnumerator.is_init()) fontEnumerator.init();
        const wxArrayString &face_names = fontEnumerator.get_facenames();
        //const wxArrayString &face_names = wxFontEnumerator::GetFacenames(encoding, fixed_width_only);
        ImGui::TextColored(ImGuiWrapper::COL_GREY_LIGHT, "count %d", static_cast<int>(face_names.size()));
        for (const wxString &face_name : face_names) {
            size_t index = &face_name - &face_names.front();
            ImGui::PushID(index);
            bool is_selected = (actual_face_name == face_name);
            if (ImGui::Selectable(face_name.ToUTF8().data(), is_selected) &&
                wxFontEnumerator::IsValidFacename(face_name)) {
                // Select font
                wxFont wx_font(wxFontInfo().FaceName(face_name).Encoding(encoding));
                if(m_font_manager.set_wx_font(wx_font))
                    process();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

#ifdef ALLOW_ADD_FONT_BY_FILE
    ImGui::SameLine();
    // select font file by file browser
    if (draw_button(IconType::open_file)) {
        if (choose_true_type_file()) { 
            m_font_manager.free_style_images();
            process();
        }
    } else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", _u8L("add file with font(.ttf, .ttc)").c_str());
#endif //  ALLOW_ADD_FONT_BY_FILE

#ifdef ALLOW_ADD_FONT_BY_OS_SELECTOR
    ImGui::SameLine();
    if (draw_button(IconType::system_selector)) {
        if (choose_font_by_wxdialog()) {           
            m_font_manager.free_style_images();
            process();
        }
    } else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", _u8L("Open dialog for choose from fonts.").c_str());
#endif //  ALLOW_ADD_FONT_BY_OS_SELECTOR

}

void GLGizmoEmboss::draw_model_type()
{
    std::optional<ModelVolumeType> new_type;
    ModelVolumeType modifier = ModelVolumeType::PARAMETER_MODIFIER;
    ModelVolumeType negative = ModelVolumeType::NEGATIVE_VOLUME;
    ModelVolumeType part = ModelVolumeType::MODEL_PART;
    ModelVolumeType type = (m_volume != nullptr) ? m_volume->type() :
                                                   ModelVolumeType::INVALID;
    bool is_last_solid_part = false;
    if (type == part) {
        is_last_solid_part = true;
        for (const ModelVolume* vol : m_volume->get_object()->volumes) {
            if (vol == m_volume) continue;
            if (vol->type() == part) {
                is_last_solid_part = false;
                break;
            }
        }
    }
    if (ImGui::RadioButton("modifier", type == modifier) && !is_last_solid_part)
        new_type = modifier;
    if(is_last_solid_part && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", _u8L("You can't change a type of the last solid part of the object.").c_str());

    ImGui::SameLine();
    if (ImGui::RadioButton("negative", type == negative) && !is_last_solid_part)
        new_type = negative;
    if(is_last_solid_part && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", _u8L("You can't change a type of the last solid part of the object.").c_str());

    ImGui::SameLine();
    if (ImGui::RadioButton("part", type == part))
        new_type = part;

    ImGui::SameLine();
    m_imgui->disabled_begin(true);
    ImGui::RadioButton("baked in", false);
    m_imgui->disabled_end();

    if (m_volume != nullptr && new_type.has_value() && !is_last_solid_part) {
        GUI_App &app    = wxGetApp();
        Plater * plater = app.plater();
        plater->take_snapshot(_L("Change Part Type"));         
        m_volume->set_type(*new_type);

        // inspiration in ObjectList::change_part_type()
        // how to view correct side panel with objects
        ObjectList *obj_list = app.obj_list();
        ModelVolume * volume = m_volume;
        wxDataViewItemArray sel = obj_list->reorder_volumes_and_get_selection(
            obj_list->get_selected_obj_idx(),
            [volume](const ModelVolume *vol) { return vol == volume; });
        if (!sel.IsEmpty()) obj_list->select_item(sel.front());

        // TODO: fix way of view - color after change from volume to negative
    }
}

void GLGizmoEmboss::draw_rename_style(bool start_rename)
{
    std::string title = _u8L("Rename style");
    const char * popup_id = title.c_str();
    static FontItem *  rename_item;
    static std::string new_name;
    if (start_rename && !ImGui::IsPopupOpen(popup_id)) {
        ImGui::OpenPopup(popup_id);
        rename_item = &m_font_manager.get_font_item();
        new_name    = rename_item->name; // initialize with original copy
    }

    if (ImGui::BeginPopupModal(popup_id, 0, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::string &original_style_name = rename_item->name;
        std::string text_in_popup =
            GUI::format(_u8L("Rename style(%1%) for embossing text: "), original_style_name);
        ImGui::Text("%s", text_in_popup.c_str());

        bool is_unique = true;
        for (const auto &item : m_font_manager.get_fonts()) {
            const FontItem &fi = item.font_item;
            if (&fi == rename_item)
                continue; // could be same as original name
            if (fi.name == new_name) is_unique = false;
        }
        bool allow_change = false;
        if (new_name.empty()) {
            m_imgui->text_colored(ImGuiWrapper::COL_ORANGE_DARK, _u8L("Name can't be empty."));
        }else if (!is_unique) { 
            m_imgui->text_colored(ImGuiWrapper::COL_ORANGE_DARK, _u8L("Name has to be unique."));
        } else {
            ImGui::NewLine();
            allow_change = true;
        }

        is_unique && !new_name.empty();

        ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue;
        ImGui::SetNextItemWidth(m_gui_cfg->combo_font_width);
        if ((ImGui::InputText("##font name", &new_name, flags) && allow_change) ||
            m_imgui->button(_L("ok"), ImVec2(0.f, 0.f), allow_change)) {
            rename_item->name = new_name;
            m_font_manager.get_truncated_name() = "";
            m_font_manager.free_style_images();
            ImGui::CloseCurrentPopup();
            select_stored_font_item();
        }
        ImGui::EndPopup();
    }
}

void GLGizmoEmboss::draw_style_list() {
    const float &max_width = m_gui_cfg->max_font_name_width;
    std::optional<size_t> delete_index;
    const FontItem &actual_font_item = m_font_manager.get_font_item();
    std::string &trunc_name = m_font_manager.get_truncated_name();
    if (trunc_name.empty()) {
        // generate trunc name
        const std::string &current_name  = actual_font_item.name;
        trunc_name = ImGuiWrapper::trunc(current_name, max_width);
    }

    ImGui::Text("%s", _u8L("Style").c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(m_gui_cfg->combo_font_width);
    if (ImGui::BeginCombo("##style_selector", trunc_name.c_str())) {
        m_font_manager.init_style_images(m_gui_cfg->max_style_image_width);
        const auto &fonts = m_font_manager.get_fonts();
        for (const auto &item : fonts) {
            size_t             index             = &item - &fonts.front();
            const FontItem &   fi                = item.font_item;
            const std::string &actual_style_name = fi.name;
            ImGui::PushID(actual_style_name.c_str());
            std::string name_truncated = 
                ImGuiWrapper::trunc(actual_style_name, max_width);

            bool is_selected = (&fi == &actual_font_item);
            ImGuiSelectableFlags_ flags =
                ImGuiSelectableFlags_AllowItemOverlap; // allow click buttons

            const FontManager::StyleImage &img = *item.image;
            ImVec2 select_size(0.f, std::max(img.tex_size.y, m_gui_cfg->min_style_image_height));
            if (ImGui::Selectable("##style_select", is_selected, flags, select_size)) {
                if (m_font_manager.load_font(index)) { 
                    process();
                    select_stored_font_item();
                }
            } else if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", actual_style_name.c_str());

            // reorder items
            if (ImGui::IsItemActive() && !ImGui::IsItemHovered()) {
                std::optional<size_t> other_index;
                if (ImGui::GetMouseDragDelta(0).y < 0.f) {
                    if (index > 0) other_index = index - 1;
                } else if ((index + 1) < fonts.size())
                    other_index = index + 1;

                if (other_index.has_value()) {
                    m_font_manager.swap(index, *other_index);
                    ImGui::ResetMouseDragDelta();
                }
            }

            // draw style name
            ImGui::SameLine();
            ImGui::Image(img.texture_id, img.tex_size, img.uv0, img.uv1);

            // delete button
            ImGui::SameLine(m_gui_cfg->delete_pos_x);
            if (draw_button(IconType::erase, is_selected) && !is_selected)
                delete_index = index;
            if (ImGui::IsItemHovered()) {
                std::string tooltip = (is_selected)?
                    GUI::format(_L("Active style \"%1%\" can't be deleted."), actual_style_name) :
                    GUI::format(_L("Delete \"%1%\" style."), actual_style_name);
                ImGui::SetTooltip("%s", tooltip.c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    // delete font item
    if (delete_index.has_value())
        m_font_manager.erase(*delete_index);
    
    ImGui::SameLine();
    bool start_rename = false;
    if (draw_button(IconType::rename)) start_rename = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", _u8L("Rename actual style.").c_str());
    draw_rename_style(start_rename);

    ImGui::SameLine();
    if (draw_button(IconType::duplicate))
        m_font_manager.duplicate();
    
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", _u8L("Duplicate style.").c_str());
    
    // TODO: Is style changed against stored one
    bool is_stored  = m_stored_font_item.has_value();
    bool is_changed = (is_stored) ?
        !(*m_stored_font_item == m_font_manager.get_font_item()) : true;

    ImGui::SameLine();
    if (draw_button(IconType::save, !is_changed)) {
        // TODO: make save style
        store_font_list_to_app_config();
        //store_font_item_to_app_config();
    }
    if (ImGui::IsItemHovered()) { 
        if (is_changed) {
            ImGui::SetTooltip("%s", _u8L("Save current settings to selected style").c_str());
        } else {
            ImGui::SetTooltip("%s", _u8L("No changes to save into style").c_str());
        }
    }

    if (is_changed) {
        ImGui::SameLine();
        if (draw_button(IconType::undo)) {
           // TODO: make undo changes in style
        }    
        if (ImGui::IsItemHovered()) 
            ImGui::SetTooltip("%s", _u8L("Reload original value of selected style").c_str());
    }
    
#ifdef ALLOW_REVERT_ALL_STYLES
    ImGui::SameLine();
    if (draw_button(IconType::revert_all)) {
        m_font_manager     = FontManager(m_imgui->get_glyph_ranges());
        FontList font_list = create_default_font_list();
        m_font_manager.add_fonts(font_list);
        // TODO: What to do when default fonts are not loadable?
        bool success = m_font_manager.load_first_valid_font();
        select_stored_font_item();
    }
    if (ImGui::IsItemHovered()) 
        ImGui::SetTooltip("%s", _u8L("Revert all styles").c_str());
#endif // ALLOW_REVERT_ALL_STYLES
}

bool GLGizmoEmboss::italic_button()
{
    std::optional<wxFont> &wx_font = m_font_manager.get_wx_font(); 
    std::shared_ptr<Emboss::FontFile> &font_file = m_font_manager.get_font_file();
    if (!wx_font.has_value() || font_file == nullptr) { 
        draw_icon(IconType::italic, IconState::disabled);
        return false;
    }

    std::optional<float> &skew = m_font_manager.get_font_prop().skew;
    bool is_font_italic = skew.has_value() || WxFontUtils::is_italic(*wx_font);
    if (is_font_italic) {
        // unset italic
        if (draw_clickable(IconType::italic, IconState::hovered,
                           IconType::unitalic, IconState::hovered)) {
            skew.reset();
            if (wx_font->GetStyle() != wxFontStyle::wxFONTSTYLE_NORMAL) {
                wx_font->SetStyle(wxFontStyle::wxFONTSTYLE_NORMAL);
                m_font_manager.wx_font_changed();
            }
            return true;
        }
        if (ImGui::IsItemHovered()) 
            ImGui::SetTooltip("%s", _u8L("Unset italic").c_str());
    } else {
        // set italic
        if (draw_button(IconType::italic)) {
            auto new_ff = WxFontUtils::set_italic(*wx_font, *font_file);
            if (new_ff != nullptr) {
                m_font_manager.wx_font_changed(std::move(new_ff));
            } else {
                // italic font doesn't exist 
                // add skew when wxFont can't set it
                skew = 0.2f;
            }            
            return true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", _u8L("Set italic").c_str());
    }
    return false;
}

bool GLGizmoEmboss::bold_button() {
    std::optional<wxFont> &wx_font = m_font_manager.get_wx_font();
    std::shared_ptr<Emboss::FontFile> &font_file = m_font_manager.get_font_file();
    if (!wx_font.has_value() || font_file==nullptr) {
        draw_icon(IconType::bold, IconState::disabled);
        return false;
    }
    
    std::optional<float> &boldness = m_font_manager.get_font_prop().boldness;
    bool is_font_bold = boldness.has_value() || WxFontUtils::is_bold(*wx_font);
    if (is_font_bold) {
        // unset bold
        if (draw_clickable(IconType::bold, IconState::hovered,
                           IconType::unbold, IconState::hovered)) {
            boldness.reset();
            if (wx_font->GetWeight() != wxFontWeight::wxFONTWEIGHT_NORMAL) {
                wx_font->SetWeight(wxFontWeight::wxFONTWEIGHT_NORMAL);
                m_font_manager.wx_font_changed();
            }
            return true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", _u8L("Unset bold").c_str());
    } else {
        // set bold
        if (draw_button(IconType::bold)) {
            auto new_ff = WxFontUtils::set_bold(*wx_font, *font_file);
            if (new_ff != nullptr) {
                m_font_manager.wx_font_changed(std::move(new_ff));
            } else {
                // bold font can't be loaded
                // set up boldness
                boldness = 20.f;
                m_font_manager.free_style_images();
                //font_file->cache.empty();
            }
            return true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", _u8L("Set bold").c_str());
    }
    return false;
}

void GLGizmoEmboss::draw_style_edit() {
    const GuiCfg::Translations &tr = m_gui_cfg->translations;
    ImGui::Text("%s", tr.font.c_str());
    ImGui::SameLine(m_gui_cfg->style_edit_text_width);
    ImGui::SetNextItemWidth(m_gui_cfg->combo_font_width);
    draw_font_list();
    ImGui::SameLine();
    bool exist_change = false;
    exist_change |= italic_button();
    ImGui::SameLine();
    exist_change |= bold_button();

    FontItem &fi = m_font_manager.get_font_item();
    std::optional<wxFont> &wx_font = m_font_manager.get_wx_font();

    // TODO: should not be there
    // when actual font not loaded try to load
    if (!wx_font.has_value() && fi.type == WxFontUtils::get_actual_type())
        wx_font = WxFontUtils::load_wxFont(fi.path);

    FontProp &font_prop = fi.prop;

    ImGui::Text("%s", tr.size.c_str());
    ImGui::SameLine(m_gui_cfg->style_edit_text_width);
    ImGui::SetNextItemWidth(m_gui_cfg->combo_font_width);
    if (ImGui::InputFloat("##line height", &font_prop.size_in_mm, 0.1f, 1.f, "%.1f mm")) {
        // size can't be zero or negative
        if (font_prop.size_in_mm < std::numeric_limits<float>::epsilon()) 
            font_prop.size_in_mm = 10.f;

        // store font size into path
        if (fi.type == WxFontUtils::get_actual_type()) {
            if (wx_font.has_value()) {
                wx_font->SetPointSize(font_prop.size_in_mm);
                m_font_manager.wx_font_changed();
            }
        }
        exist_change = true;
    }

#ifdef SHOW_WX_WEIGHT_INPUT
    if (wx_font.has_value()) {
        ImGui::Text("%s", "weight");
        ImGui::SameLine(m_gui_cfg->style_edit_text_width);
        ImGui::SetNextItemWidth(m_gui_cfg->combo_font_width);
        int weight     = wx_font->GetNumericWeight();
        int min_weight = 1, max_weight = 1000;
        if (ImGui::SliderInt("##weight", &weight, min_weight, max_weight)) {
            wx_font->SetNumericWeight(weight);
            m_font_manager.wx_font_changed();
            exist_change = true;
        }

        wxFont f       = wx_font->Bold();
        bool   disable = f == *wx_font;
        ImGui::SameLine();
        if (draw_button(IconType::bold, disable)) {
            *wx_font = f;
            m_font_manager.wx_font_changed();
            exist_change = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", _u8L("wx Make bold").c_str());
    }
#endif // SHOW_WX_WEIGHT_INPUT

    if (exist_change)
        process();    

    ImGui::Text("%s", tr.depth.c_str());
    ImGui::SameLine(m_gui_cfg->style_edit_text_width);
    ImGui::SetNextItemWidth(m_gui_cfg->combo_font_width);
    if (ImGui::InputFloat("##size in Z", &font_prop.emboss, 0.1f, 0.25, "%.2f mm")) {
        process();
    }

    if (ImGui::TreeNode(_u8L("advanced").c_str())) {
        draw_advanced();
        ImGui::TreePop();
        if (!m_is_advanced_edit_style)
            set_minimal_window_size(true, true);
    } else if (m_is_advanced_edit_style) 
        set_minimal_window_size(true, false);    
}

void GLGizmoEmboss::draw_advanced()
{
    std::shared_ptr<Emboss::FontFile>& font_file = m_font_manager.get_font_file();
    if (font_file == nullptr) { 
        ImGui::Text("%s", _u8L("Advanced font options could be change only for corect font.\nStart with select correct font.").c_str());
        return;
    }

    FontProp &font_prop = m_font_manager.get_font_item().prop;
    bool   exist_change = false;    
    
    float item_width = 2 * m_gui_cfg->advanced_input_width;
    ImGui::SetNextItemWidth(item_width);
    if (ImGuiWrapper::input_optional_int(_u8L("CharGap[in font points]").c_str(), font_prop.char_gap)) {
        // char gap is stored inside of imgui font atlas
        m_font_manager.clear_imgui_font();
        exist_change = true;
    }

    ImGui::SetNextItemWidth(item_width);
    if (ImGuiWrapper::input_optional_int(_u8L("LineGap [in font points]").c_str(), font_prop.line_gap))
        exist_change = true;

    ImGui::SetNextItemWidth(item_width);
    if (m_imgui->slider_optional_float(_u8L("Boldness [in font points]").c_str(), font_prop.boldness, -200.f, 200.f, "%.0f", 1.f, false, _L("Tiny / Wide glyphs")))
        exist_change = true;
    

    ImGui::SetNextItemWidth(item_width);
    if (m_imgui->slider_optional_float(_u8L("Italic [Skew ratio]").c_str(), font_prop.skew, -1.f, 1.f, "%.2f", 1.f, false, _L("Italic strength ratio")))
        exist_change = true;
    
    float prev_distance = font_prop.distance.has_value() ?
                         *font_prop.distance : .0f;
    ImGui::SetNextItemWidth(item_width);
    if (m_imgui->slider_optional_float(_u8L("Surface distance").c_str(), font_prop.distance,
        -font_prop.emboss, font_prop.emboss, "%.2f mm", 1.f, false, _L("Distance from model surface"))) {
        float act_distance = font_prop.distance.has_value() ?
                            *font_prop.distance : .0f;
        float diff         = prev_distance - act_distance;

        // move with volume by diff size in volume z
        Vec3d r = m_volume->get_rotation();
        Eigen::Matrix3d rot_mat =
            (Eigen::AngleAxisd(r.z(), Vec3d::UnitZ()) *
            Eigen::AngleAxisd(r.y(), Vec3d::UnitY()) *
            Eigen::AngleAxisd(r.x(), Vec3d::UnitX())).toRotationMatrix();
        Vec3d displacement_rot = rot_mat * (Vec3d::UnitZ() * diff);
        m_volume->translate(displacement_rot);
        m_volume->set_new_unique_id();
        /*Selection &s = m_parent.get_selection();
        const GLVolume *v = s.get_volume(*s.get_volume_idxs().begin());
        s.translate(v->get_volume_offset() + displacement_rot*diff, ECoordinatesType::Local);*/
    }

    // when more collection add selector
    if (font_file->count > 1) {
        ImGui::SetNextItemWidth(item_width);
        if (ImGui::BeginCombo(_u8L("Font collection").c_str(),
                              std::to_string(font_file->index).c_str())) {
            for (unsigned int i = 0; i < font_file->count; ++i) {
                ImGui::PushID(1 << (10 + i));
                bool is_selected = i == font_file->index;
                if (ImGui::Selectable(std::to_string(i).c_str(), is_selected)) {
                    font_file->index = i;
                    exist_change = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }

    if (exist_change) {
        m_font_manager.free_style_images();
        process();
    }
#ifdef ALLOW_DEBUG_MODE
    ImGui::Text("family = %s", (font_prop.family.has_value() ?
                                    font_prop.family->c_str() :
                                    " --- "));
    ImGui::Text("face name = %s", (font_prop.face_name.has_value() ?
                                       font_prop.face_name->c_str() :
                                       " --- "));
    ImGui::Text("style = %s",
                (font_prop.style.has_value() ? font_prop.style->c_str() :
                                                 " --- "));
    ImGui::Text("weight = %s", (font_prop.weight.has_value() ?
                                    font_prop.weight->c_str() :
                                    " --- "));

    std::string descriptor = fi.path;
    ImGui::Text("descriptor = %s", descriptor.c_str());
#endif // ALLOW_DEBUG_MODE
}

void GLGizmoEmboss::set_minimal_window_size(bool is_edit_style,
                                            bool is_advance_edit_style)
{
    ImVec2 window_size = ImGui::GetWindowSize();
    const ImVec2& min_win_size_prev = get_minimal_window_size();
    //ImVec2 diff(window_size.x - min_win_size_prev.x,
    //            window_size.y - min_win_size_prev.y);
    float diff_y = window_size.y - min_win_size_prev.y;
    m_is_edit_style = is_edit_style;
    m_is_advanced_edit_style = is_advance_edit_style;
    const ImVec2 &min_win_size = get_minimal_window_size();
    ImGui::SetWindowSize(ImVec2(0.f, min_win_size.y + diff_y),
                         ImGuiCond_Always);
}

const ImVec2 &GLGizmoEmboss::get_minimal_window_size() const
{
    return (m_is_edit_style) ?
               ((m_is_advanced_edit_style) ?
                    m_gui_cfg->minimal_window_size_with_advance :
                    m_gui_cfg->minimal_window_size_with_edit) :
               m_gui_cfg->minimal_window_size;
}

#ifdef ALLOW_ADD_FONT_BY_OS_SELECTOR
bool GLGizmoEmboss::choose_font_by_wxdialog()
{
    wxFontData data;
    data.EnableEffects(false);
    data.RestrictSelection(wxFONTRESTRICT_SCALABLE);
    // set previous selected font
    FontItem &selected_font_item = m_font_manager.get_font_item();
    if (selected_font_item.type == WxFontUtils::get_actual_type()) {
        std::optional<wxFont> selected_font = WxFontUtils::load_wxFont(
            selected_font_item.path);
        if (selected_font.has_value()) data.SetInitialFont(*selected_font);
    }

    wxFontDialog font_dialog(wxGetApp().mainframe, data);
    if (font_dialog.ShowModal() != wxID_OK) return false;

    data                = font_dialog.GetFontData();
    wxFont   wx_font       = data.GetChosenFont();
    size_t   font_index = m_font_manager.get_fonts().size();
    FontItem font_item  = WxFontUtils::get_font_item(wx_font);
    m_font_manager.add_font(font_item);

    // Check that deserialization NOT influence font
    // false - use direct selected wxFont in dialog
    // true - use font item (serialize and deserialize wxFont)
    bool use_deserialized_font = false;

    // Try load and use new added font
    if ((use_deserialized_font && !m_font_manager.load_font(font_index)) ||
        (!use_deserialized_font && !m_font_manager.load_font(font_index, wx_font))) {
        m_font_manager.erase(font_index);
        wxString message = GUI::format_wxstr(
            _L("Font '%1%' can't be used. Please select another."),
            font_item.name);
        wxString      title = _L("Selected font is NOT True-type.");
        MessageDialog not_loaded_font_message(nullptr, message, title, wxOK);
        not_loaded_font_message.ShowModal();
        return choose_font_by_wxdialog();
    }

    // fix dynamic creation of italic font
    if (WxFontUtils::is_italic(wx_font) &&
        !Emboss::is_italic(*m_font_manager.get_font_file())) {
        m_font_manager.get_font_item().prop.skew = 0.2;
    }
    return true;
}
#endif // ALLOW_ADD_FONT_BY_OS_SELECTOR

#ifdef ALLOW_ADD_FONT_BY_FILE
bool GLGizmoEmboss::choose_true_type_file()
{
    wxArrayString input_files;
    wxString      fontDir      = wxEmptyString;
    wxString      selectedFile = wxEmptyString;
    wxFileDialog  dialog(nullptr, _L("Choose one or more files (TTF, TTC):"),
                        fontDir, selectedFile, file_wildcards(FT_FONTS),
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) dialog.GetPaths(input_files);
    if (input_files.IsEmpty()) return false;
    size_t index = m_font_manager.get_fonts().size();
    // use first valid font
    for (auto &input_file : input_files) {
        std::string path = std::string(input_file.c_str());
        std::string name = get_file_name(path);
        //make_unique_name(name, m_font_list);
        const FontProp& prop = m_font_manager.get_font_prop();
        FontItem fi(name, path, FontItem::Type::file_path, prop);
        m_font_manager.add_font(fi);
        // set first valid added font as active
        if (m_font_manager.load_font(index)) return true;
        m_font_manager.erase(index);       
    }
    return false;
}
#endif // ALLOW_ADD_FONT_BY_FILE

bool GLGizmoEmboss::choose_svg_file()
{
    wxArrayString input_files;
    wxString      fontDir      = wxEmptyString;
    wxString      selectedFile = wxEmptyString;
    wxFileDialog  dialog(nullptr, _L("Choose SVG file:"), fontDir,
                        selectedFile, file_wildcards(FT_SVG),
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) dialog.GetPaths(input_files);
    if (input_files.IsEmpty()) return false;
    if (input_files.size() != 1) return false;
    auto &      input_file = input_files.front();
    std::string path       = std::string(input_file.c_str());
    std::string name       = get_file_name(path);

    NSVGimage *image = nsvgParseFromFile(path.c_str(), "mm", 96.0f);
    ExPolygons polys = NSVGUtils::to_ExPolygons(image);
    nsvgDelete(image);

    BoundingBox bb;
    for (const auto &p : polys) bb.merge(p.contour.points);
    const FontProp &fp = m_font_manager.get_font_item().prop;
    float scale   = fp.size_in_mm / std::max(bb.max.x(), bb.max.y());
    auto  project = std::make_unique<Emboss::ProjectScale>(
        std::make_unique<Emboss::ProjectZ>(fp.emboss / scale), scale);
    indexed_triangle_set its = Emboss::polygons2model(polys, *project);
    return false;
    // test store:
    // for (auto &poly : polys) poly.scale(1e5);
    // SVG svg("converted.svg", BoundingBox(polys.front().contour.points));
    // svg.draw(polys);
    //return add_volume(name, its);
}

TextConfiguration GLGizmoEmboss::create_configuration()
{
    FontItem &fi = m_font_manager.get_font_item();
    // actualize font path
    if (fi.type == WxFontUtils::get_actual_type()) {
        std::optional<wxFont> &wx_font = m_font_manager.get_wx_font();
        if (wx_font.has_value())
            fi.path = WxFontUtils::store_wxFont(*wx_font);
    }
    return TextConfiguration(m_font_manager.get_font_item(), m_text);
}

bool GLGizmoEmboss::load_configuration(ModelVolume *volume)
{
    if (volume == nullptr) return false;
    if (!volume->text_configuration.has_value()) return false;

    TextConfiguration &configuration = *volume->text_configuration;
    FontItem &         c_font_item   = configuration.font_item;

    // try to find font in local font list
    auto is_config = [&c_font_item](const FontManager::Item &font_item) -> bool {
        const FontItem &fi = font_item.font_item;
        return fi.path == c_font_item.path && fi.prop == c_font_item.prop;
    };
    const auto& fonts = m_font_manager.get_fonts();
    auto it = std::find_if(fonts.begin(), fonts.end(), is_config);
    bool found_font = it != fonts.end();
    size_t font_index;
    if (!found_font) {
        // font is not in list
        // add font to list
        font_index = fonts.size();
        m_font_manager.add_font(c_font_item);        
    } else {
        // font is found in list
        font_index = it - fonts.begin();
    }

    m_text      = configuration.text;
    m_volume    = volume;

    if (!m_font_manager.load_font(font_index)) {
        // create similar font
        auto wx_font = WxFontUtils::create_wxFont(c_font_item, configuration.font_item.prop);
        if (wx_font.has_value()) {
            // fix not loadable font item
            FontItem &fi = m_font_manager.get_font_item();
            FontItem fi_new = WxFontUtils::get_font_item(*wx_font);
            fi_new.name = fi.name; // use previous name
            fi = fi_new; // rewrite font item
            fi.prop = configuration.font_item.prop;
            if (!m_font_manager.load_font(font_index, *wx_font)) return false;
        } else {
            // can't create similar font use previous
            m_font_manager.erase(font_index);
        }
        create_notification_not_valid_font(configuration);
    }
    return true;
}

void GLGizmoEmboss::create_notification_not_valid_font(
    const TextConfiguration &tc)
{
    // not neccessary, but for sure that old notification doesnt exist
    if (m_exist_notification) remove_notification_not_valid_font();
    m_exist_notification = true;

    auto type = NotificationType::UnknownFont;
    auto level =
        NotificationManager::NotificationLevel::WarningNotificationLevel;

    const auto &fi            = m_font_manager.get_font_item();
    const auto &origin_family = tc.font_item.prop.face_name;
    const auto &actual_family = fi.prop.face_name;

    const std::string &origin_font_name = origin_family.has_value() ?
                                              *origin_family :
                                              tc.font_item.path;
    const std::string &actual_font_name = actual_family.has_value() ?
                                              *actual_family :
                                              fi.name;

    std::string text =
        GUI::format(_L("Can't load exactly same font(\"%1%\"), "
                       "Aplication select similar one(\"%2%\"). "
                       "When you edit text, similar font will be applied."),
                    origin_font_name, actual_font_name);
    auto notification_manager = wxGetApp().plater()->get_notification_manager();
    notification_manager->push_notification(type, level, text);
}

void GLGizmoEmboss::remove_notification_not_valid_font()
{
    if (!m_exist_notification) return;
    m_exist_notification      = false;
    auto type                 = NotificationType::UnknownFont;
    auto notification_manager = wxGetApp().plater()->get_notification_manager();
    notification_manager->close_notification_of_type(type);
}

std::string GLGizmoEmboss::create_volume_name()
{
    const size_t &max_len = m_gui_cfg->max_count_char_in_volume_name;
    return _u8L("Text") + " - " +
           ((m_text.size() > max_len) ?
                (m_text.substr(0, max_len - 3) + " ..") :
                m_text);
}

bool GLGizmoEmboss::init_icons()
{
    // icon order has to match the enum IconType
    std::vector<std::string> filenames{
        "edit_button.svg",
        "delete.svg",
        "add_copies.svg", 
        "save.svg", 
        "undo.svg",    
        "make_italic.svg",
        "make_unitalic.svg",
        "make_bold.svg",
        "make_unbold.svg",   
        "search.svg",
        "open.svg",          
        "revert_all_.svg"
    };
    assert(filenames.size() == static_cast<size_t>(IconType::_count));
    std::string path = resources_dir() + "/icons/";
    for (std::string &filename : filenames) filename = path + filename;

    // state order has to match the enum IconState
    std::vector<std::pair<int, bool>> states;
    states.push_back(std::make_pair(1, false)); // Activable
    states.push_back(std::make_pair(0, true));  // Hovered
    states.push_back(std::make_pair(2, false)); // Disabled

    unsigned int sprite_size_px = std::ceil(m_gui_cfg->icon_width);
    // make size pair number
    if (sprite_size_px % 2 != 0) ++sprite_size_px;
    bool compress = false;
    return m_icons_texture.load_from_svg_files_as_sprites_array(filenames,
                                                                states,
                                                                sprite_size_px,
                                                                compress);
}

void GLGizmoEmboss::draw_icon(IconType icon, IconState state)
{
    // canot draw count
    assert(icon != IconType::_count);
    if (icon == IconType::_count) return; 

    unsigned int icons_texture_id = m_icons_texture.get_id();
    int          tex_width        = m_icons_texture.get_width();
    int          tex_height       = m_icons_texture.get_height();
    int          icon_width       = m_gui_cfg->icon_width;
    // is icon loaded
    if ((icons_texture_id == 0) || (tex_width <= 1) || (tex_height <= 1))
        return;
    ImTextureID tex_id = (void *) (intptr_t) (GLuint) icons_texture_id;

    size_t count_icons  = static_cast<size_t>(IconType::_count);
    size_t count_states = 3; // activable | hovered | disabled
    int start_x = static_cast<unsigned>(state) * (icon_width + 1) + 1,
        start_y = static_cast<unsigned>(icon) * (icon_width + 1) + 1;

    ImVec2 uv0(start_x / (float) tex_width, 
               start_y / (float) tex_height);
    ImVec2 uv1((start_x + icon_width) / (float) tex_width,
               (start_y + icon_width) / (float) tex_height);

    ImGui::Image(tex_id, ImVec2(icon_width, icon_width), uv0, uv1);
}

void GLGizmoEmboss::draw_transparent_icon()
{
    // zero pixel is transparent in texture
    ImGui::Image((void *) (intptr_t) (GLuint) m_icons_texture.get_id(),
                 ImVec2(m_gui_cfg->icon_width, m_gui_cfg->icon_width),
                 ImVec2(0, 0),
                 ImVec2(1.f / m_icons_texture.get_width(),
                        1.f / m_icons_texture.get_height()));
}

bool GLGizmoEmboss::draw_clickable(
    IconType icon, IconState state, 
    IconType hover_icon, IconState hover_state)
{
    // check of hover
    float cursor_x = ImGui::GetCursorPosX();
    draw_transparent_icon();
    ImGui::SameLine(cursor_x);

    if (ImGui::IsItemHovered()) {
        // redraw image
        draw_icon(hover_icon, hover_state);
    } else {
        // redraw normal image
        draw_icon(icon, state);
    }
    return ImGui::IsItemClicked();
}

bool GLGizmoEmboss::draw_button(IconType icon, bool disable)
{
    if (disable) {
        draw_icon(icon, IconState::disabled);
        return false;
    }
    return draw_clickable(
        icon, IconState::activable,
        icon, IconState::hovered
    );
}

FontList GLGizmoEmboss::load_font_list_from_app_config(const AppConfig *cfg)
{
    FontList    result;
    unsigned    index        = 1;
    std::string section_name = FontListSerializable::create_section_name(index++);
    while (cfg->has_section(section_name)) {
        std::optional<FontItem> fi = FontListSerializable::load_font_item(cfg->get_section(section_name));
        if (fi.has_value()) result.emplace_back(*fi);
        section_name = FontListSerializable::create_section_name(index++);
    }
    if (result.empty())
        return create_default_font_list();
    return result;
}

void GLGizmoEmboss::store_font_list_to_app_config() const
{
    AppConfig *cfg   = wxGetApp().app_config;
    unsigned   index = 1;
    for (const auto& item : m_font_manager.get_fonts()) {
        const FontItem &fi = item.font_item;
        // skip file paths + fonts from other OS(loaded from .3mf)
        if (fi.type != WxFontUtils::get_actual_type()) continue;
        FontListSerializable::store_font_item(*cfg, fi, index++);
    }

    // remove rest of font sections
    std::string section_name = FontListSerializable::create_section_name(index);
    while (cfg->has_section(section_name)) {
        cfg->clear_section(section_name);
        section_name = FontListSerializable::create_section_name(++index);
    }
}

void GLGizmoEmboss::store_font_item_to_app_config() const
{
    AppConfig *cfg = wxGetApp().app_config;
    // index of section start from 1
    const auto &act_item = m_font_manager.get_font();
    const FontItem &fi = act_item.font_item;

    //size_t index = &m_font_manager.get_font() -
    //               &m_font_manager.get_fonts().front();
    // fix index when, not serialized font is in list
    size_t index = 0;
    for (const auto &item : m_font_manager.get_fonts()) {
        if (fi.type != WxFontUtils::get_actual_type()) continue;
        if (&item == &act_item) break;
        ++index;
    }
    
    FontListSerializable::store_font_item(*cfg, fi, index);
}

std::string GLGizmoEmboss::get_file_name(const std::string &file_path)
{
    size_t pos_last_delimiter = file_path.find_last_of('\\');
    size_t pos_point          = file_path.find_last_of('.');
    size_t offset             = pos_last_delimiter + 1;
    size_t count              = pos_point - pos_last_delimiter - 1;
    return file_path.substr(offset, count);
}

// any existing icon filename to not influence GUI
const std::string GLGizmoEmboss::M_ICON_FILENAME = "cut.svg";
