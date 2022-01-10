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

// TODO: remove include
#include "libslic3r/SVG.hpp"      // debug store
#include "libslic3r/Geometry.hpp" // covex hull 2d

#include "libslic3r/NSVGUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ClipperUtils.hpp" // union_ex
#include "libslic3r/AppConfig.hpp"    // store/load font list
#include "libslic3r/MapUtils.hpp"
#include "libslic3r/Format/OBJ.hpp" // load obj file for default object

#include "imgui/imgui_stdlib.h" // using std::string for inputs
#include "nanosvg/nanosvg.h"    // load SVG file

#include <wx/font.h>
#include <wx/fontutil.h>
#include <wx/fontdlg.h>

#include <GL/glew.h>

// uncomment for easier debug
//#define ALLOW_DEBUG_MODE



using namespace Slic3r;
using namespace Slic3r::GUI;

GLGizmoEmboss::GLGizmoEmboss(GLCanvas3D &parent)
    : GLGizmoBase(parent, M_ICON_FILENAME, -2)
    , m_font_selected(0)
    , m_font(nullptr)
    , m_volume(nullptr)
    , m_exist_notification(false)
    , m_is_initialized(false) // initialize on first opening gizmo
    , m_rotate_gizmo(parent, GLGizmoRotate::Axis::Z) // grab id = 2 (Z axis)
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

    // TODO: fix width - showing scroll bar during first show advanced
    ImVec2 windows_size = m_gui_cfg->draw_advanced ?
                              m_gui_cfg->minimal_window_size_with_advance :
                              m_gui_cfg->minimal_window_size;
    ImVec2 offset = ImGuiWrapper::suggest_location(windows_size, hull);
    m_gui_cfg->offset   = offset;
    return;

    Polygon rect({Point(offset.x, offset.y),
                  Point(offset.x + windows_size.x, offset.y),
                  Point(offset.x + windows_size.x, offset.y + windows_size.y),
                  Point(offset.x, offset.y + windows_size.y)});
    ImGuiWrapper::draw(hull);
    ImGuiWrapper::draw(rect);
}

#ifdef ALLOW_DEBUG_MODE
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
#endif // ALLOW_DEBUG_MODE

void GLGizmoEmboss::create_volume(ModelVolumeType volume_type, const Vec2d& mouse_pos)
{
    assert(volume_type == ModelVolumeType::MODEL_PART ||
           volume_type == ModelVolumeType::NEGATIVE_VOLUME ||
           volume_type == ModelVolumeType::PARAMETER_MODIFIER);

    if (!m_is_initialized) initialize();
    const Selection &selection = m_parent.get_selection();
    if(selection.is_empty()) return;

    set_default_configuration();
    
    // By position of cursor create transformation to put text on surface of model
    Transform3d transformation;        
    const ModelObjectPtrs &objects = wxGetApp().plater()->model().objects;
    m_raycast_manager.actualize(objects);
    auto hit = m_raycast_manager.unproject(mouse_pos);
    if (hit.has_value()) {
        transformation = Emboss::create_transformation_onto_surface(hit->position, hit->normal);
    } else {
        // there is no hit with object         
        // TODO: calculate X,Y offset position for lay on platter by mouse position
        transformation = Transform3d::Identity();
    }

    create_emboss_volume(create_mesh(), transformation, create_volume_name(),
                         create_configuration(), volume_type,
                         selection.get_object_idx());
}

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
        // TODO: move to job, for big scene it slow down
        m_raycast_manager.actualize(objects, &skip);
        return false;
    }

    // wxCoord == int --> wx/types.h
    Vec2i mouse_coord(mouse_event.GetX(), mouse_event.GetY());
    Vec2d mouse_pos = mouse_coord.cast<double>();
    auto hit = m_raycast_manager.unproject(mouse_pos, &skip);
    if (!hit.has_value()) { 
        // there is no hit
        // show common translation of object
        m_parent.toggle_model_objects_visibility(true);
        m_temp_transformation = {};
        return false; 
    }
        
    Transform3d object_trmat = m_raycast_manager.get_transformation(hit->tr_key);
    Transform3d trmat = Emboss::create_transformation_onto_surface(hit->position, hit->normal);
    if (mouse_event.Dragging()) {
        // hide common dragging of object
        m_parent.toggle_model_objects_visibility(false, m_volume->get_object(), gl_volume->instance_idx(), m_volume);

        // Show temporary position
        // TODO: store z-rotation and aply after transformation matrix
        m_temp_transformation = object_trmat * trmat;
    } else if (mouse_event.LeftUp()) {

        // TODO: Disable apply common transformation after draggig
        // Call after is used for apply transformation after common dragging to rewrite it
        ModelVolume *mv = m_volume;
        wxGetApp().plater()->CallAfter([trmat, mv]() {
            mv->set_transformation(trmat);
            mv->set_new_unique_id();
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
    std::array<float, 4> gray_color = {.6f, .6f, .6f, .3f};
    m_rotate_gizmo.set_highlight_color(gray_color);

    m_shortcut_key = WXK_CONTROL_T;
    return true;
}

std::string GLGizmoEmboss::on_get_name() const { return _u8L("Emboss"); }

void GLGizmoEmboss::on_render() {
    // no volume selected
    if (m_volume == nullptr) return;

    if (m_temp_transformation.has_value()) {
        // draw text volume on temporary position
        const Selection &selection = m_parent.get_selection();
        const GLVolume& gl_volume = *selection.get_volume(*selection.get_volume_idxs().begin());
        glsafe(::glPushMatrix());
        glsafe(::glMultMatrixd(m_temp_transformation->data()));                
        GLShaderProgram *shader = wxGetApp().get_shader("gouraud_light");
        shader->start_using();
        // dragging object must be selected so draw it with selected color
        shader->set_uniform("uniform_color", GLVolume::SELECTED_COLOR);

        glsafe(::glEnable(GL_DEPTH_TEST));
        gl_volume.indexed_vertex_array.render();
        glsafe(::glDisable(GL_DEPTH_TEST));

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

    ImVec2 min_window_size = m_gui_cfg->draw_advanced ?
                                 m_gui_cfg->minimal_window_size_with_advance :
                                 m_gui_cfg->minimal_window_size;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, min_window_size);
    // ImGui::SetNextWindowSize(ImVec2(0, min_window_size.y),
    // ImGuiCond_::ImGuiCond_Always);

#ifdef ALLOW_DEBUG_MODE
    // draw suggested position of window
    draw_fine_position(m_parent.get_selection());
#endif // ALLOW_DEBUG_MODE

    // check if is set window offset
    if (m_gui_cfg->offset.has_value()) {
        ImGui::SetNextWindowPos(*m_gui_cfg->offset, ImGuiCond_Always);
        // clear request on offset
        m_gui_cfg->offset = {};
    }

    int flag = // ImGuiWindowFlags_AlwaysAutoResize |
               // ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse;
    m_imgui->begin(on_get_name(), flag);

    ImGui::GetWindowSize();
    draw_window();
    m_imgui->end();
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

        const Selection &selection = m_parent.get_selection();
        bool create_new_object = selection.is_empty();
        // When add Text on empty plate, Create new object with volume
        if (create_new_object) {
            set_default_configuration();
            create_emboss_object(create_mesh(), create_volume_name(), create_configuration());

            // gizmo will open when successfuly create new object
            GLGizmoBase::m_state = GLGizmoBase::Off;
            return;
        }

        // Try(when exist) set configuration by volume
        load_configuration(get_selected_volume());

        // change position of just opened emboss window
        set_fine_position();

        // when open by hyperlink it needs to show up
        // or after key 'T' windows doesn't appear
        m_parent.reload_scene(true); 
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
    m_gui_cfg.emplace(GuiCfg());
    float space = ImGui::GetTextLineHeightWithSpacing() -
                  ImGui::GetTextLineHeight();
    m_gui_cfg->max_font_name_width = ImGui::CalcTextSize("Maximal font name").x;
    m_gui_cfg->icon_width = ImGui::GetTextLineHeight();
    m_gui_cfg->icon_width_with_spacing = m_gui_cfg->icon_width + space;

    float scroll_width          = m_gui_cfg->icon_width_with_spacing; // fix
    m_gui_cfg->combo_font_width = m_gui_cfg->max_font_name_width +
                                  2 * m_gui_cfg->icon_width_with_spacing +
                                  scroll_width;

    m_gui_cfg->rename_pos_x = m_gui_cfg->max_font_name_width + space;
    m_gui_cfg->delete_pos_x = m_gui_cfg->rename_pos_x +
                              m_gui_cfg->icon_width_with_spacing;

    m_gui_cfg->text_size = ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() *
                                                m_gui_cfg->count_line_of_text);

    ImVec2 letter_m_size = ImGui::CalcTextSize("M");

    m_gui_cfg->advanced_input_width = letter_m_size.x * 6;

    // calculate window size
    const ImGuiStyle &style = ImGui::GetStyle();
    float input_height = letter_m_size.y + style.FramePadding.y * 2.f;
    float window_width = m_gui_cfg->combo_font_width + style.WindowPadding.x  * 2.f;
    float window_height = input_height * 4.f + // header + combo font + advance + button
        style.ItemSpacing.y * 3.f + 
        m_gui_cfg->text_size.y +
        style.WindowPadding.y * 2.f;
    m_gui_cfg->minimal_window_size = ImVec2(window_width, window_height);
    float advance_height = (input_height + style.ItemSpacing.y) * 6.f;
    m_gui_cfg->minimal_window_size_with_advance =
        ImVec2(window_width, window_height + advance_height);

    // TODO: What to do when icon was NOT loaded?
    bool success = init_icons();
    assert(success);
    load_font_list_from_app_config();

    // try to load valid font
    m_font_selected = 0;
    bool is_font_loaded = load_font();
    while (!is_font_loaded && !m_font_list.empty()) {
        // can't load so erase it from list
        m_font_list.erase(m_font_list.begin());
        is_font_loaded = load_font();
    }
    set_default_configuration();
}

void GLGizmoEmboss::set_default_configuration()
{
    m_text = _u8L("Embossed text");
    //load_font(); // reload actual font - because of font size
}

Slic3r::TriangleMesh GLGizmoEmboss::create_default_mesh()
{
    // When cant load any font use default object loaded from file
    std::string  path = Slic3r::resources_dir() + "/data/embossed_text.stl";
    TriangleMesh triangle_mesh;
    if (!load_obj(path.c_str(), &triangle_mesh)) {
        // when can't load mesh use cube
        return TriangleMesh(its_make_cube(36., 4., 2.5));
    }
    return triangle_mesh;
}

Slic3r::TriangleMesh GLGizmoEmboss::create_mesh()
{
    // It is neccessary to create some shape
    // Emboss text window is opened by creation new embosstext object
    if (m_font == nullptr) return create_default_mesh();
    const FontItem &fi  = m_font_list[m_font_selected];
    TriangleMesh result = create_mesh(m_text.c_str(), *m_font, fi.prop);
    if (result.its.empty()) return create_default_mesh();
    return result;
}

Slic3r::TriangleMesh GLGizmoEmboss::create_mesh(const char *  text,
                                                Emboss::Font &font,
                                                const FontProp& font_prop)
{
    ExPolygons shapes   = Emboss::text2shapes(font, text, font_prop);
    float      scale    = font_prop.size_in_mm / font.ascent;
    float      depth    = font_prop.emboss / scale;
    auto       projectZ = std::make_unique<Emboss::ProjectZ>(depth);
    Emboss::ProjectScale project(std::move(projectZ), scale);
    return TriangleMesh(Emboss::polygons2model(shapes, project));
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
    set_default_configuration();
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
    if (m_font == nullptr) return false;
    auto data = std::make_unique<EmbossData>(
        m_font, create_configuration(), create_volume_name(), m_volume);
        
    auto &worker = wxGetApp().plater()->get_ui_job_worker();
    replace_job(worker, std::make_unique<EmbossJob>(std::move(data)));
    
    // notification is removed befor object is changed by job
    remove_notification_not_valid_font();
    return true;
}

void GLGizmoEmboss::close()
{
    // close gizmo == open it again
    m_parent.get_gizmos_manager().open_gizmo(GLGizmosManager::Emboss);
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
    if (m_font == nullptr) {
        ImGui::Text("%s",_u8L("Warning: No font is selected. Select correct one.").c_str());
    }
    draw_font_list();
    draw_text_input();

    bool &advanced = m_gui_cfg->draw_advanced;
    if (ImGui::Checkbox(_u8L("Advance").c_str(), &advanced)) {
        ImVec2 window_size =
            advanced ?
                ImVec2(0, m_gui_cfg->minimal_window_size_with_advance.y) :
                m_gui_cfg->minimal_window_size;
        ImGui::SetWindowSize(window_size, ImGuiCond_Always);
    }
    if (advanced) draw_advanced();

    if (ImGui::Button(_u8L("Close").c_str())) close();

    // Option to create text volume when reselecting volumes
    m_imgui->disabled_begin(m_font == nullptr);
    if (m_volume == nullptr) {
        ImGui::SameLine();
        if (ImGui::Button(_u8L("Generate preview").c_str())) { 
            const Selection &s = m_parent.get_selection();
            auto selected_indices = s.get_instance_idxs();
            if (selected_indices.empty()) { 
                create_emboss_object(create_mesh(), create_volume_name(), create_configuration());
            } else {
                create_volume(ModelVolumeType::MODEL_PART);
            }
        }
    }
    m_imgui->disabled_end();
}

void GLGizmoEmboss::draw_font_list()
{
    const float &         max_width = m_gui_cfg->max_font_name_width;
    std::optional<size_t> rename_index;
    const std::string& current_name = m_font_list[m_font_selected].name;
    std::string trunc_name = ImGuiWrapper::trunc(current_name, max_width);
    ImGui::SetNextItemWidth(m_gui_cfg->combo_font_width);
    if (ImGui::BeginCombo("##font_selector", trunc_name.c_str())) {
        // first line
        if (ImGui::Button(_u8L("Choose font").c_str())) {
            choose_font_by_wxdialog();
            store_font_list_to_app_config();
            ImGui::CloseCurrentPopup();
        } else if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s",
                _u8L("Choose from installed font inside dialog.").c_str());
                
#ifdef ALLOW_DEBUG_MODE
        ImGui::SameLine();
        // select font file by file browser
         if (ImGui::Button(_u8L("Add File").c_str())) {
            choose_true_type_file();
            store_font_list_to_app_config();
            ImGui::CloseCurrentPopup();
         } else if (ImGui::IsItemHovered())
             ImGui::SetTooltip("%s",_u8L("add file with font(.ttf, .ttc)").c_str());
#endif //  ALLOW_DEBUG_MODE

        ImGui::Separator();

        for (FontItem &f : m_font_list) {
            ImGui::PushID(f.name.c_str());
            std::string name        = ImGuiWrapper::trunc(f.name, max_width);
            size_t      index       = &f - &m_font_list.front();
            bool        is_selected = index == m_font_selected;
            auto        flags = ImGuiSelectableFlags_AllowItemOverlap; // allow click buttons
            if (ImGui::Selectable(name.c_str(), is_selected, flags)) {
                if (load_font(index)) process();
            } else if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", f.name.c_str());

            // draw buttons rename and delete
            ImGui::SameLine();
            ImGui::SetCursorPosX(m_gui_cfg->rename_pos_x);
            if (draw_button(IconType::rename)) rename_index = index;
            ImGui::SameLine();
            ImGui::SetCursorPosX(m_gui_cfg->delete_pos_x);
            if (draw_button(IconType::erase, is_selected)) {
                m_font_list.erase(m_font_list.begin() + index);
                // fix selected index
                if (index < m_font_selected) --m_font_selected;
                store_font_list_to_app_config();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    // rename modal window popup
    const char *rename_popup_id = "Rename_font";
    static FontItem* rename_item;
    static std::string new_name;
    if (rename_index.has_value() && !ImGui::IsPopupOpen(rename_popup_id)) {
        ImGui::OpenPopup(rename_popup_id);
        rename_item = &m_font_list[*rename_index];
        new_name    = rename_item->name; // initialize with original copy
    }

    if (ImGui::BeginPopupModal(rename_popup_id, 0, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::string &original_font_name = rename_item->name;
        std::string text_in_popup = GUI::format(_u8L("Change font name (%1%): "), original_font_name);
        text_in_popup += "\n" + _u8L("NOTE: Name has to be unique in font list.");
        ImGui::Text("%s", text_in_popup.c_str());
        ImGui::SetNextItemWidth(m_gui_cfg->combo_font_width);

        bool is_unique = true;
        for (const FontItem &fi : m_font_list) { 
            if (&fi == rename_item) continue; // could be same as original name
            if (fi.name == new_name) is_unique = false;
        }
        bool allow_change = is_unique && !new_name.empty();

        ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue;                
        if ((ImGui::InputText("##font name", &new_name, flags) && allow_change) ||
            m_imgui->button(_L("ok"), ImVec2(0.f, 0.f), allow_change)) {
            rename_item->name = new_name;
            ImGui::CloseCurrentPopup();
            store_font_list_to_app_config();
        }
        ImGui::EndPopup();
    }
}

void GLGizmoEmboss::draw_text_input()
{
    static const ImGuiInputTextFlags flags =
        ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_AutoSelectAll;

    ImVector<ImFont *> &fonts      = m_imgui_font_atlas.Fonts;
    ImFont *            imgui_font = fonts.empty() ? nullptr : fonts.front();
    bool exist_font = imgui_font != nullptr && imgui_font->IsLoaded();
    if (exist_font) ImGui::PushFont(imgui_font);

    bool exist_change = false;
    float window_height = ImGui::GetWindowHeight();
    float minimal_height = m_gui_cfg->draw_advanced ?
                               m_gui_cfg->minimal_window_size_with_advance.y :
                               m_gui_cfg->minimal_window_size.y;
    float extra_height   = window_height - minimal_height;
    ImVec2 text_size(m_gui_cfg->text_size.x,
                     m_gui_cfg->text_size.y + extra_height);
    if (ImGui::InputTextMultiline("##Text", &m_text, text_size, flags)) {
        process();
        exist_change = true;
    }

    if (exist_font) ImGui::PopFont();

    // imgui_font has to be unused
    if (exist_change) check_imgui_font_range();
}

void GLGizmoEmboss::draw_advanced()
{
    if (m_font != nullptr) { 
        ImGui::Text("%s", _u8L("Advanced font options could be change only for corect font.\nStart with select correct font."));
        return;
    }

    FontItem &fi = m_font_list[m_font_selected];
    FontProp &font_prop = fi.prop;
    bool      exist_change = false;

    ImGui::SetNextItemWidth(m_gui_cfg->advanced_input_width);
    if (ImGui::InputFloat(_u8L("Size[in mm]").c_str(),
                          &font_prop.size_in_mm)) {
        if (font_prop.size_in_mm < 0.1) font_prop.size_in_mm = 10;
        // store font size into path
        if (fi.type == WxFontUtils::get_actual_type()) {
            std::optional<wxFont> wx_font = WxFontUtils::load_wxFont(fi.path);
            if (wx_font.has_value()) {
                wx_font->SetPointSize(font_prop.size_in_mm);
                fi.path = WxFontUtils::store_wxFont(*wx_font);
            }
        }
        load_imgui_font();
        m_font->cache.clear();
        exist_change = true;
    }

    ImGui::SetNextItemWidth(m_gui_cfg->advanced_input_width);
    if (ImGui::InputFloat(_u8L("Emboss[in mm]").c_str(), &font_prop.emboss))
        exist_change = true;
    
    ImGui::SetNextItemWidth(2 * m_gui_cfg->advanced_input_width);
    if (ImGuiWrapper::input_optional_int(_u8L("CharGap[in font points]").c_str(), font_prop.char_gap)) {
        m_font->cache.clear();
        exist_change = true;
    }

    ImGui::SetNextItemWidth(2*m_gui_cfg->advanced_input_width);
    if (ImGuiWrapper::input_optional_int(_u8L("LineGap[in font points]").c_str(), font_prop.line_gap))
        exist_change = true;

    ImGui::SetNextItemWidth(2 * m_gui_cfg->advanced_input_width);
    if (m_imgui->slider_optional_float(_u8L("Boldness[in font points]").c_str(), font_prop.boldness, -200.f, 200.f, "%.0f", 1.f, false, _L("tiny / wide chars"))){
        m_font->cache.clear();
        exist_change = true;
    }

    ImGui::SetNextItemWidth(2 * m_gui_cfg->advanced_input_width);
    if (m_imgui->slider_optional_float(_u8L("Skew ratio").c_str(), font_prop.skew, -1.f, 1.f, "%.2f", 1.f, false, _L("italic strength"))){
        m_font->cache.clear();
        exist_change = true;
    }

    // when more collection add selector
    if (m_font != nullptr && m_font->count > 1) {
        ImGui::SetNextItemWidth(m_gui_cfg->advanced_input_width);
        if (ImGui::BeginCombo(_u8L("Font collection").c_str(),
                              std::to_string(m_font->index).c_str())) {
            for (unsigned int i = 0; i < m_font->count; ++i) {
                ImGui::PushID(1 << (10 + i));
                if (ImGui::Selectable(std::to_string(i).c_str(),
                                      i == m_font->index)) {
                    m_font->index = i;
                    m_font->cache.clear();
                    exist_change = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }

    if (exist_change) {
        store_font_item_to_app_config();
        process();
    }


#ifdef ALLOW_DEBUG_MODE
    std::string descriptor = m_font_list[m_font_selected].path;
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
    ImGui::Text("descriptor = %s", descriptor.c_str());
    ImGui::Image(m_imgui_font_atlas.TexID,
                 ImVec2(m_imgui_font_atlas.TexWidth,
                        m_imgui_font_atlas.TexHeight));
#endif // ALLOW_DEBUG_MODE
}

bool GLGizmoEmboss::load_font(size_t font_index)
{
    if (font_index >= m_font_list.size()) return false;
    std::swap(font_index, m_font_selected);
    bool is_loaded = load_font();
    if (!is_loaded) std::swap(font_index, m_font_selected);
    return is_loaded;
}

bool GLGizmoEmboss::load_font()
{
    if (m_font_selected >= m_font_list.size()) return false;
    FontItem &fi = m_font_list[m_font_selected];
    if (fi.type == FontItem::Type::file_path) {
        // fill font name after load from .3mf
        if (fi.name.empty())
            fi.name = Slic3r::GUI::GLGizmoEmboss::get_file_name(fi.path);
        std::unique_ptr<Emboss::Font> font_ptr = Emboss::load_font(fi.path.c_str());
        if (font_ptr == nullptr) return false;
        m_font = std::move(font_ptr);
        load_imgui_font();
        return true;
    }
    if (fi.type != WxFontUtils::get_actual_type()) return false;
    std::optional<wxFont> wx_font = WxFontUtils::load_wxFont(fi.path);
    if (!wx_font.has_value()) return false;

    // fill font name after load from .3mf
    if (fi.name.empty())
        fi.name = WxFontUtils::get_human_readable_name(*wx_font);
    return load_font(*wx_font);
}

bool GLGizmoEmboss::load_font(const wxFont &font)
{
    auto font_ptr = WxFontUtils::load_font(font);
    if (font_ptr == nullptr) return false;
    m_font = std::move(font_ptr);
    load_imgui_font();
    return true;
}

void GLGizmoEmboss::check_imgui_font_range()
{
    const char *text = m_text.c_str();

    const ImFont *font = m_imgui_font_atlas.Fonts.front();
    if (!font->IsLoaded()) {
        // when create font no one letter in text was inside font
        // check text again
        load_imgui_font();
        return;
    }
    if (font->ConfigData == nullptr) return;
    const ImWchar *ranges       = font->ConfigData->GlyphRanges;
    auto           is_in_ranges = [ranges](unsigned int letter) -> bool {
        for (const ImWchar *range = ranges; range[0] && range[1]; range += 2) {
            ImWchar from = range[0];
            ImWchar to   = range[1];
            if (from <= letter && letter <= to) return true;
            if (letter < to) return false; // ranges should be sorted
        }
        return false;
    };

    bool exist_unknown = false;
    while (*text) {
        unsigned int c     = 0;
        int          c_len = ImTextCharFromUtf8(&c, text, NULL);
        text += c_len;
        if (c_len == 0) break;
        if (!is_in_ranges(c)) {
            exist_unknown = true;
            break;
        }
    }
    if (exist_unknown) load_imgui_font();
}

void GLGizmoEmboss::load_imgui_font()
{
    if (m_font == nullptr) return;

    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(m_imgui->get_glyph_ranges());
    builder.AddText(m_text.c_str());

    m_imgui_font_ranges.clear();

    builder.BuildRanges(&m_imgui_font_ranges);
    const FontProp &font_prop = m_font_list[m_font_selected].prop;
    int font_size = static_cast<int>(
        std::round(std::abs(font_prop.size_in_mm / 0.3528)));
    if (font_size < m_gui_cfg->min_imgui_font_size)
        font_size = m_gui_cfg->min_imgui_font_size;
    if (font_size > m_gui_cfg->max_imgui_font_size)
        font_size = m_gui_cfg->max_imgui_font_size;

    ImFontConfig font_config;
    font_config.FontDataOwnedByAtlas = false;
    m_imgui_font_atlas.Flags |= ImFontAtlasFlags_NoMouseCursors |
                                ImFontAtlasFlags_NoPowerOfTwoHeight;
    m_imgui_font_atlas.Clear();
    m_imgui_font_atlas.AddFontFromMemoryTTF((void *) m_font->buffer.data(),
                                            m_font->buffer.size(), font_size,
                                            &font_config,
                                            m_imgui_font_ranges.Data);

    unsigned char *pixels;
    int            width, height;
    m_imgui_font_atlas.GetTexDataAsAlpha8(&pixels, &width, &height);

    // Upload texture to graphics system
    GLint last_texture;
    glsafe(::glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture));
    ScopeGuard sg([last_texture]() {
        glsafe(::glBindTexture(GL_TEXTURE_2D, last_texture));
    });

    GLuint font_texture;
    glsafe(::glGenTextures(1, &font_texture));
    glsafe(::glBindTexture(GL_TEXTURE_2D, font_texture));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    glsafe(::glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));
    glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, width, height, 0,
                          GL_ALPHA, GL_UNSIGNED_BYTE, pixels));

    // Store our identifier
    m_imgui_font_atlas.TexID = (ImTextureID) (intptr_t) font_texture;
}

static void make_unique_name(std::string &name, const FontList &list)
{
    auto is_unique = [&list](const std::string &name)->bool {
        for (const FontItem &fi : list) 
            if (fi.name == name) return false;
        return true;
    };

    if (name.empty()) name = "font";
    if (is_unique(name)) return;

    int order = 1; // start with value 2 to represents same font name
    std::string new_name;
    do {
        new_name = name + " (" + std::to_string(++order) + ")";
    } while (!is_unique(new_name));
    name = new_name;
}

bool GLGizmoEmboss::choose_font_by_wxdialog()
{
    wxFontData data;
    data.EnableEffects(false);
    data.RestrictSelection(wxFONTRESTRICT_SCALABLE);
    // set previous selected font
    FontItem &selected_font_item = m_font_list[m_font_selected];
    if (selected_font_item.type == WxFontUtils::get_actual_type()) {
        std::optional<wxFont> selected_font = WxFontUtils::load_wxFont(
            selected_font_item.path);
        if (selected_font.has_value()) data.SetInitialFont(*selected_font);
    }

    wxFontDialog font_dialog(wxGetApp().mainframe, data);
    if (font_dialog.ShowModal() != wxID_OK) return false;

    data                = font_dialog.GetFontData();
    wxFont   font       = data.GetChosenFont();
    size_t   font_index = m_font_list.size();
    FontItem font_item  = WxFontUtils::get_font_item(font);
    make_unique_name(font_item.name, m_font_list);
    m_font_list.emplace_back(font_item);

    // Check that deserialization NOT influence font
    // false - use direct selected wxFont in dialog
    // true - use font item (serialize and deserialize wxFont)
    bool use_deserialized_font = false;
    if (!use_deserialized_font) m_font_selected = font_index;
    // Try load and use new added font
    if ((!use_deserialized_font && !load_font(font)) ||
        (use_deserialized_font && !load_font(font_index)) || !process()) {
        // reverse index for font_selected
        std::swap(font_index, m_font_selected); // when not process
        // remove form font list
        m_font_list.pop_back();
        wxString message = GUI::format_wxstr(
            _L("Font '%1%' can't be used. Please select another."),
            font_item.name);
        wxString      title = _L("Selected font is NOT True-type.");
        MessageDialog not_loaded_font_message(nullptr, message, title, wxOK);
        not_loaded_font_message.ShowModal();
        return choose_font_by_wxdialog();
    }
    return true;
}

bool GLGizmoEmboss::choose_true_type_file()
{
    wxArrayString input_files;
    wxString      fontDir      = wxEmptyString;
    wxString      selectedFile = wxEmptyString;
    wxFileDialog  dialog(nullptr, _L("Choose one or more files (TTF, TTC):"),
                        fontDir, selectedFile, file_wildcards(FT_FONTS),
                        wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) dialog.GetPaths(input_files);
    if (input_files.IsEmpty()) return false;
    bool font_loaded = false;
    for (auto &input_file : input_files) {
        std::string path = std::string(input_file.c_str());
        std::string name = get_file_name(path);
        make_unique_name(name, m_font_list);
        m_font_list.emplace_back(name, path, FontItem::Type::file_path, FontProp());

        // set first valid added font as active
        if (!font_loaded) {
            if (!load_font(m_font_list.size() - 1))
                m_font_list.pop_back();
            else
                font_loaded = true;
        }
    }
    if (font_loaded) process();
    return font_loaded;
}

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
    const FontProp &fp = m_font_list[m_font_selected].prop;
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
    return TextConfiguration(m_font_list[m_font_selected], m_text);
}

bool GLGizmoEmboss::load_configuration(ModelVolume *volume)
{
    if (volume == nullptr) return false;
    if (!volume->text_configuration.has_value()) return false;

    TextConfiguration &configuration = *volume->text_configuration;
    FontItem &         c_font_item   = configuration.font_item;

    // try to find font in local font list
    auto is_config = [&c_font_item](const FontItem &font_item) -> bool {
        return font_item.path == c_font_item.path;
    };
    auto it = std::find_if(m_font_list.begin(), m_font_list.end(), is_config);

    size_t prev_font_selected = m_font_selected;

    if (it == m_font_list.end()) {
        // font is not in list
        // add font to list
        m_font_selected = m_font_list.size();
        make_unique_name(c_font_item.name, m_font_list);
        m_font_list.emplace_back(c_font_item);
    } else {
        // font is found in list
        m_font_selected = it - m_font_list.begin();
    }

    m_text      = configuration.text;
    m_volume    = volume;

    if (!load_font()) {
        // create similar font
        auto wx_font = WxFontUtils::create_wxFont(c_font_item, configuration.font_item.prop);
        if (wx_font.has_value()) {
            // fix not loadable font item
            FontItem &fi = m_font_list[m_font_selected];
            FontItem fi_new = WxFontUtils::get_font_item(*wx_font);
            fi_new.name = fi.name; // use previous name
            fi = fi_new; // rewrite font item
            fi.prop = configuration.font_item.prop;
            if (!load_font(*wx_font)) return false;
        } else {
            // can't create similar font use previous
            m_font_list.erase(m_font_list.begin() + m_font_selected);
            m_font_selected = prev_font_selected;
            if (!load_font()) return false;
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

    const auto &fi            = m_font_list[m_font_selected];
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
    std::string path = resources_dir() + "/icons/";

    // icon order has to match the enum IconType
    std::vector<std::string> filenames = {path + "wrench.svg",
                                          path + "delete.svg"};

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
    unsigned int icons_texture_id = m_icons_texture.get_id();
    int          tex_width        = m_icons_texture.get_width();
    int          tex_height       = m_icons_texture.get_height();

    // is icon loaded
    if ((icons_texture_id == 0) || (tex_width <= 1) || (tex_height <= 1))
        return;
    ImTextureID tex_id = (void *) (intptr_t) (GLuint) icons_texture_id;
    // ImVec2 image_size(tex_width, tex_height);

    size_t count_icons  = 2; // wrench | delete
    size_t count_states = 3; // activable | hovered | disabled
    ImVec2 icon_size(tex_width / count_states, tex_height / count_icons);

    ImVec2 start(static_cast<unsigned>(state) * icon_size.x,
                 static_cast<unsigned>(icon) * icon_size.y);

    ImVec2 uv0(start.x / tex_width, start.y / tex_height);

    ImVec2 uv1((start.x + icon_size.x) / tex_width,
               (start.y + icon_size.y) / tex_height);

    ImGui::Image(tex_id, icon_size, uv0, uv1);
}

bool GLGizmoEmboss::draw_button(IconType icon, bool disable)
{
    float line_spacing = ImGui::GetTextLineHeightWithSpacing() -
                         ImGui::GetTextLineHeight();
    float cursor_pos_y = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(cursor_pos_y - line_spacing / 2);
    ScopeGuard sg([cursor_pos_y]() {
        ImGui::SetCursorPosY(cursor_pos_y);
        ImGui::NewLine();
    });

    if (disable) {
        draw_icon(icon, IconState::disabled);
        if (ImGui::IsItemHovered() && icon == IconType::erase)
            ImGui::SetTooltip("%s",_u8L("Active font can't be removed").c_str());
        return false;
    }

    float cursor_x = ImGui::GetCursorPosX();

    draw_icon(icon, IconState::activable);
    if (ImGui::IsItemClicked()) return true;
    if (ImGui::IsItemHovered()) {
        switch (icon) {
        case IconType::rename:
            ImGui::SetTooltip("%s", _u8L("rename").c_str());
            break;
        case IconType::erase:
            ImGui::SetTooltip("%s", _u8L("delete").c_str());
            break;
        default: break;
        }
        // redraw image over previous
        ImGui::SameLine();
        ImGui::SetCursorPosX(cursor_x);

        draw_icon(icon, IconState::hovered);
        if (ImGui::IsItemClicked()) return true;
    }
    return false;
}

class FontListSerializable
{
    static const std::string APP_CONFIG_FONT_NAME;
    static const std::string APP_CONFIG_FONT_DESCRIPTOR;
    static const std::string APP_CONFIG_FONT_LINE_HEIGHT;
    static const std::string APP_CONFIG_FONT_DEPTH;
    static const std::string APP_CONFIG_FONT_BOLDNESS;
    static const std::string APP_CONFIG_FONT_SKEW;
    static const std::string APP_CONFIG_FONT_CHAR_GAP;
    static const std::string APP_CONFIG_FONT_LINE_GAP;
public:
    FontListSerializable() = delete;

    static FontList    create_default_font_list();
    static std::string create_section_name(unsigned index);
    static std::optional<FontItem> load_font_item(const std::map<std::string, std::string> &app_cfg_section);
    static void store_font_item(AppConfig &cfg, const FontItem &fi, unsigned index);

private:
    // TODO: move to app config like read from section
    static bool read(const std::map<std::string, std::string>& section, const std::string& key, float& value);
    static bool read(const std::map<std::string, std::string>& section, const std::string& key, std::optional<int>& value);
    static bool read(const std::map<std::string, std::string>& section, const std::string& key, std::optional<float>& value);
};

void GLGizmoEmboss::load_font_list_from_app_config()
{
    const AppConfig *cfg          = wxGetApp().app_config;
    unsigned         index        = 1;
    std::string      section_name = FontListSerializable::create_section_name(index++);
    while (cfg->has_section(section_name)) {
        std::optional<FontItem> fi = FontListSerializable::load_font_item(cfg->get_section(section_name));
        if (fi.has_value()) { 
            make_unique_name(fi->name, m_font_list);
            m_font_list.emplace_back(*fi);
        }
        section_name = FontListSerializable::create_section_name(index++);
    }
    if (m_font_list.empty())
        m_font_list = FontListSerializable::create_default_font_list();
}

void GLGizmoEmboss::store_font_list_to_app_config() const
{
    AppConfig *cfg   = wxGetApp().app_config;
    unsigned   index = 1;
    for (const FontItem &fi : m_font_list) {
        // skip file paths + fonts from other OS
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
    unsigned   index = m_font_selected + 1;
    FontListSerializable::store_font_item(
        *cfg, m_font_list[m_font_selected], index);
}

const std::string FontListSerializable::APP_CONFIG_FONT_NAME = "name";
const std::string FontListSerializable::APP_CONFIG_FONT_DESCRIPTOR = "descriptor";
const std::string FontListSerializable::APP_CONFIG_FONT_LINE_HEIGHT = "line_height";
const std::string FontListSerializable::APP_CONFIG_FONT_DEPTH       = "depth";
const std::string FontListSerializable::APP_CONFIG_FONT_BOLDNESS    = "boldness";
const std::string FontListSerializable::APP_CONFIG_FONT_SKEW        = "skew";
const std::string FontListSerializable::APP_CONFIG_FONT_CHAR_GAP    = "char_gap";
const std::string FontListSerializable::APP_CONFIG_FONT_LINE_GAP    = "line_gap";

FontList FontListSerializable::create_default_font_list()
{
    return {
        WxFontUtils::get_font_item(wxFont(5, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL))
        , WxFontUtils::get_font_item(wxFont(10, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD))
        , WxFontUtils::get_os_font()
    };
}

std::string FontListSerializable::create_section_name(unsigned index)
{
    return AppConfig::SECTION_FONT + ':' + std::to_string(index);
}

#include "fast_float/fast_float.h"
bool FontListSerializable::read(const std::map<std::string, std::string>& section, const std::string& key, float& value){
    auto item = section.find(key);
    if (item == section.end()) return false;
    const std::string &data = item->second;
    if (data.empty()) return false;
    float value_;
    fast_float::from_chars(data.c_str(), data.c_str() + data.length(), value_);
    // read only non zero value
    if (fabs(value_) <= std::numeric_limits<float>::epsilon()) return false;

    value = value_;
    return true;
}

bool FontListSerializable::read(const std::map<std::string, std::string>& section, const std::string& key, std::optional<int>& value){
    auto item = section.find(key);
    if (item == section.end()) return false;
    const std::string &data = item->second;
    if (data.empty()) return false;
    int value_ = std::atoi(data.c_str());
    if (value_ == 0) return false;

    value = value_;
    return true;
}

bool FontListSerializable::read(const std::map<std::string, std::string>& section, const std::string& key, std::optional<float>& value){
    auto item = section.find(key);
    if (item == section.end()) return false;
    const std::string &data = item->second;
    if (data.empty()) return false;
    float value_;
    fast_float::from_chars(data.c_str(), data.c_str() + data.length(), value_);
    // read only non zero value
    if (fabs(value_) <= std::numeric_limits<float>::epsilon()) return false;

    value = value_;
    return true;
}

std::optional<FontItem> FontListSerializable::load_font_item(
    const std::map<std::string, std::string> &app_cfg_section)
{
    auto path_it = app_cfg_section.find(APP_CONFIG_FONT_DESCRIPTOR);
    if (path_it == app_cfg_section.end()) return {};
    const std::string &path = path_it->second;

    auto name_it = app_cfg_section.find(APP_CONFIG_FONT_NAME);
    static const std::string default_name = "font_name";
    const std::string &name = 
        (name_it == app_cfg_section.end()) ?
        default_name : name_it->second;
        
    FontProp fp;
    read(app_cfg_section, APP_CONFIG_FONT_LINE_HEIGHT, fp.size_in_mm);
    read(app_cfg_section, APP_CONFIG_FONT_DEPTH, fp.emboss);
    read(app_cfg_section, APP_CONFIG_FONT_BOLDNESS, fp.boldness);
    read(app_cfg_section, APP_CONFIG_FONT_SKEW, fp.skew);
    read(app_cfg_section, APP_CONFIG_FONT_CHAR_GAP, fp.char_gap);
    read(app_cfg_section, APP_CONFIG_FONT_LINE_GAP, fp.line_gap);

    FontItem::Type type = WxFontUtils::get_actual_type();
    return FontItem(name, path, type, fp);
}

void FontListSerializable::store_font_item(AppConfig &     cfg,
                                           const FontItem &fi,
                                           unsigned        index)
{
    std::string section_name = create_section_name(index);
    cfg.clear_section(section_name);
    cfg.set(section_name, APP_CONFIG_FONT_NAME, fi.name);
    cfg.set(section_name, APP_CONFIG_FONT_DESCRIPTOR, fi.path);
    const FontProp &fp = fi.prop;
    cfg.set(section_name, APP_CONFIG_FONT_LINE_HEIGHT, std::to_string(fp.size_in_mm));
    cfg.set(section_name, APP_CONFIG_FONT_DEPTH, std::to_string(fp.emboss));
    if (fp.boldness.has_value())
        cfg.set(section_name, APP_CONFIG_FONT_BOLDNESS, std::to_string(*fp.boldness));
    if (fp.skew.has_value())
        cfg.set(section_name, APP_CONFIG_FONT_SKEW, std::to_string(*fp.skew));
    if (fp.char_gap.has_value())
        cfg.set(section_name, APP_CONFIG_FONT_CHAR_GAP, std::to_string(*fp.char_gap));
    if (fp.line_gap.has_value())
        cfg.set(section_name, APP_CONFIG_FONT_LINE_GAP, std::to_string(*fp.line_gap));
}

std::string GLGizmoEmboss::get_file_name(const std::string &file_path)
{
    size_t pos_last_delimiter = file_path.find_last_of('\\');
    size_t pos_point          = file_path.find_last_of('.');
    size_t offset             = pos_last_delimiter + 1;
    size_t count              = pos_point - pos_last_delimiter - 1;
    return file_path.substr(offset, count);
}

namespace Slic3r::GUI {

class Priv
{
public:
    Priv() = delete;
    struct EmbossObject
    {
        TriangleMesh      mesh;
        std::string       name;
        TextConfiguration cfg;

        EmbossObject(TriangleMesh &&   mesh,
                     std::string       name,
                     TextConfiguration cfg)
            : mesh(std::move(mesh)), name(name), cfg(cfg)
        {}
    };
    struct EmbossVolume : public EmbossObject
    {
        ModelVolumeType type;
        size_t          object_idx;
        Transform3d     transformation;
        EmbossVolume(TriangleMesh &&   mesh,
                     Transform3d       transformation,
                     std::string       name,
                     TextConfiguration cfg,
                     ModelVolumeType   type,
                     size_t            object_idx)
            : EmbossObject(std::move(mesh), name, cfg)
            , transformation(transformation)
            , type(type)
            , object_idx(object_idx)
        {}
    };
    static void create_emboss_object(EmbossObject &data);
    static void create_emboss_volume(EmbossVolume &data);    
};

} // namespace Slic3r

void GLGizmoEmboss::create_emboss_object(TriangleMesh &&   mesh,
                                         std::string       name,
                                         TextConfiguration cfg)
{
    // Move data to call after is not working
    // data are owen by lambda
    auto data = new Priv::EmbossObject(std::move(mesh), name, cfg);
    wxGetApp().plater()->CallAfter([data]() {
        ScopeGuard sg([data]() { delete data; });
        Priv::create_emboss_object(*data);
    });
}

void GLGizmoEmboss::create_emboss_volume(TriangleMesh &&   mesh,
                                         Transform3d       transformation,
                                         std::string       name,
                                         TextConfiguration cfg,
                                         ModelVolumeType   type,
                                         size_t            object_idx)
{
    // Move data to call after is not working
    // data are owen by lambda
    auto data = new Priv::EmbossVolume(std::move(mesh), transformation, name, cfg, type,
                                       object_idx);
    wxGetApp().plater()->CallAfter([data]() {
        ScopeGuard sg([data]() { delete data; });
        Priv::create_emboss_volume(*data);
    });
}

void Priv::create_emboss_object(EmbossObject &data)
{
    GUI_App &        app      = wxGetApp();
    Plater *         plater   = app.plater();
    ObjectList *     obj_list = app.obj_list();
    GLCanvas3D *     canvas   = plater->canvas3D();
    GLGizmosManager &manager  = canvas->get_gizmos_manager();

    plater->take_snapshot(_L("Add Emboss text object"));
    // Create new object and change selection
    bool center = true;
    obj_list->load_mesh_object(std::move(data.mesh), data.name, center,
                               &data.cfg);

    // new object successfuly added so open gizmo
    assert(manager.get_current_type() != GLGizmosManager::Emboss);
    manager.open_gizmo(GLGizmosManager::Emboss);

    // redraw scene
    canvas->reload_scene(true);

    // Gizmo is not open during time of creation object
    // When cursor move and no one object is selected than Manager::reset_all()
}

void Priv::create_emboss_volume(EmbossVolume &data)
{
    GUI_App &   app      = wxGetApp();
    Plater *    plater   = app.plater();
    ObjectList *obj_list = app.obj_list();
    GLCanvas3D *canvas   = plater->canvas3D();

    size_t          object_idx = data.object_idx;
    ModelVolumeType type       = data.type;

    // create new volume inside of object
    Model &model = plater->model();
    if (model.objects.size() <= object_idx) return;
    ModelObject *obj    = model.objects[object_idx];
    ModelVolume *volume = obj->add_volume(std::move(data.mesh));

    // set a default extruder value, since user can't add it manually
    volume->config.set_key_value("extruder", new ConfigOptionInt(0));

    // do not allow model reload from disk
    volume->source.is_from_builtin_objects = true;
    volume->set_type(type);
    volume->name               = data.name;
    volume->text_configuration = data.cfg;
    volume->set_transformation(data.transformation);

    // update volume name in object list
    // updata selection after new volume added
    // change name of volume in right panel
    // select only actual volume
    // when new volume is created change selection to this volume
    auto add_to_selection = [volume](const ModelVolume *vol) {
        return vol == volume;
    };
    wxDataViewItemArray sel =
        obj_list->reorder_volumes_and_get_selection((int) object_idx,
                                                    add_to_selection);
    if (!sel.IsEmpty()) obj_list->select_item(sel.front());

    // update printable state on canvas
    if (type == ModelVolumeType::MODEL_PART)
        canvas->update_instance_printable_state_for_object(object_idx);

    obj_list->selection_changed();

    // WHY selection_changed set manipulation to world ???
    // so I set it back to local --> RotationGizmo need it
    ObjectManipulation *manipul = wxGetApp().obj_manipul();
    manipul->set_coordinates_type(ECoordinatesType::Local);


    // redraw scene
    canvas->reload_scene(true);
}

void GLGizmoEmboss::update_emboss_volume(TriangleMesh &&   mesh,
                                         const std::string& name,
                                         const TextConfiguration& cfg,
                                         ModelVolume *     volume)
{
    // for sure that some object is created from shape
    if (mesh.its.indices.empty()) return;

    GUI_App &        app      = wxGetApp(); // may be move to input
    Plater *         plater   = app.plater();
    ObjectList *     obj_list = app.obj_list();
    GLCanvas3D *     canvas   = plater->canvas3D();
    GLGizmosManager &manager  = canvas->get_gizmos_manager();

    // Check emboss gizmo is still open
    if (manager.get_current_type() != GLGizmosManager::Emboss) return;

    plater->take_snapshot(_L("Emboss text") + ": " + name);

    // find volume by object id - NOT WORK
    // -> edit text change volume id so could apper not found volume
    // ModelVolume *volume = nullptr;
    // Model &model = plater->model();
    // for (auto obj : model.objects)
    //    for (auto vol : obj->volumes)
    //        if (vol->id() == volume_id) {
    //            volume = vol;
    //            break;
    //        }
    // if (volume == nullptr) return;
    assert(volume != nullptr);

    // update volume
    volume->set_mesh(std::move(mesh));
    volume->set_new_unique_id();
    volume->calculate_convex_hull();
    volume->get_object()->invalidate_bounding_box();
    volume->name               = name;
    volume->text_configuration = cfg;

    // update volume in right panel( volume / object name)
    const Selection &selection = canvas->get_selection();
    const GLVolume * gl_volume = selection.get_volume(
        *selection.get_volume_idxs().begin());
    int object_idx = gl_volume->object_idx();
    int volume_idx = gl_volume->volume_idx();
    obj_list->update_name_in_list(object_idx, volume_idx);

    // update printable state on canvas
    if (volume->type() == ModelVolumeType::MODEL_PART)
        canvas->update_instance_printable_state_for_object(
            (size_t) object_idx);

    // redraw scene
    canvas->reload_scene(true);
}

// any existing icon filename to not influence GUI
const std::string GLGizmoEmboss::M_ICON_FILENAME = "cut.svg";
