#include "GUI_ObjectManipulation.hpp"
#include "GUI_ObjectList.hpp"
#include "I18N.hpp"

#include "OptionsGroup.hpp"
#include "wxExtensions.hpp"
#include "PresetBundle.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Geometry.hpp"
#include "Selection.hpp"

#include <boost/algorithm/string.hpp>
#include "slic3r/Utils/FixModelByWin10.hpp"

namespace Slic3r
{
namespace GUI
{


// Helper function to be used by drop to bed button. Returns lowest point of this
// volume in world coordinate system.
static double get_volume_min_z(const GLVolume* volume)
{
    const Transform3f& world_matrix = volume->world_matrix().cast<float>();

    // need to get the ModelVolume pointer
    const ModelObject* mo = wxGetApp().model().objects[volume->composite_id.object_id];
    const ModelVolume* mv = mo->volumes[volume->composite_id.volume_id];
    const TriangleMesh& hull = mv->get_convex_hull();

    float min_z = std::numeric_limits<float>::max();
    for (const stl_facet& facet : hull.stl.facet_start) {
        for (int i = 0; i < 3; ++ i)
            min_z = std::min(min_z, Vec3f::UnitZ().dot(world_matrix * facet.vertex[i]));
    }
    return min_z;
}



static wxBitmapComboBox* create_word_local_combo(wxWindow *parent)
{
    wxSize size(15 * wxGetApp().em_unit(), -1);

    wxBitmapComboBox *temp = nullptr;
#ifdef __WXOSX__
    /* wxBitmapComboBox with wxCB_READONLY style return NULL for GetTextCtrl(),
     * so ToolTip doesn't shown.
     * Next workaround helps to solve this problem
     */
    temp = new wxBitmapComboBox();
    temp->SetTextCtrlStyle(wxTE_READONLY);
	temp->Create(parent, wxID_ANY, wxString(""), wxDefaultPosition, size, 0, nullptr);
#else
	temp = new wxBitmapComboBox(parent, wxID_ANY, wxString(""), wxDefaultPosition, size, 0, nullptr, wxCB_READONLY);
#endif //__WXOSX__

    temp->SetFont(Slic3r::GUI::wxGetApp().normal_font());
    temp->SetBackgroundStyle(wxBG_STYLE_PAINT);

    temp->Append(_(L("World coordinates")));
    temp->Append(_(L("Local coordinates")));
    temp->SetSelection(0);
    temp->SetValue(temp->GetString(0));

#ifndef __WXGTK__
    /* Workaround for a correct rendering of the control without Bitmap (under MSW and OSX):
     * 
     * 1. We should create small Bitmap to fill Bitmaps RefData,
     *    ! in this case wxBitmap.IsOK() return true.
     * 2. But then set width to 0 value for no using of bitmap left and right spacing 
     * 3. Set this empty bitmap to the at list one item and BitmapCombobox will be recreated correct
     * 
     * Note: Set bitmap height to the Font size because of OSX rendering.
     */
    wxBitmap empty_bmp(1, temp->GetFont().GetPixelSize().y + 2);
    empty_bmp.SetWidth(0);
    temp->SetItemBitmap(0, empty_bmp);
#endif

    temp->SetToolTip(_(L("Select coordinate space, in which the transformation will be performed.")));
	return temp;
}

void msw_rescale_word_local_combo(wxBitmapComboBox* combo)
{
    const wxString selection = combo->GetString(combo->GetSelection());

    /* To correct scaling (set new controll size) of a wxBitmapCombobox
     * we need to refill control with new bitmaps. So, in our case :
     * 1. clear control
     * 2. add content
     * 3. add scaled "empty" bitmap to the at least one item
     */
    combo->Clear();
    wxSize size(wxDefaultSize);
    size.SetWidth(15 * wxGetApp().em_unit());

    // Set rescaled min height to correct layout
    combo->SetMinSize(wxSize(-1, int(1.5f*combo->GetFont().GetPixelSize().y + 0.5f)));
    // Set rescaled size
    combo->SetSize(size);
    
    combo->Append(_(L("World coordinates")));
    combo->Append(_(L("Local coordinates")));

    wxBitmap empty_bmp(1, combo->GetFont().GetPixelSize().y + 2);
    empty_bmp.SetWidth(0);
    combo->SetItemBitmap(0, empty_bmp);

    combo->SetValue(selection);
}

static void set_font_and_background_style(wxWindow* win, const wxFont& font)
{
    win->SetFont(font);
    win->SetBackgroundStyle(wxBG_STYLE_PAINT);
}

ObjectManipulation::ObjectManipulation(wxWindow* parent) :
    OG_Settings(parent, true)
#ifndef __APPLE__
    , m_focused_option("")
#endif // __APPLE__
{
    const int border = wxOSX ? 0 : 4;
    const int em = wxGetApp().em_unit();
    m_main_grid_sizer = new wxFlexGridSizer(2, 3, 3); // "Name/label", "String name / Editors"
    m_main_grid_sizer->SetFlexibleDirection(wxBOTH);

    // Add "Name" label with warning icon
    auto sizer = new wxBoxSizer(wxHORIZONTAL);

    m_fix_throught_netfab_bitmap = new wxStaticBitmap(parent, wxID_ANY, wxNullBitmap);
    if (is_windows10())
        m_fix_throught_netfab_bitmap->Bind(wxEVT_CONTEXT_MENU, [this](wxCommandEvent& e)
            {
                // if object/sub-object has no errors
                if (m_fix_throught_netfab_bitmap->GetBitmap().GetRefData() == wxNullBitmap.GetRefData())
                    return;

                wxGetApp().obj_list()->fix_through_netfabb();
                update_warning_icon_state(wxGetApp().obj_list()->get_mesh_errors_list());
            });

    sizer->Add(m_fix_throught_netfab_bitmap);

    auto name_label = new wxStaticText(m_parent, wxID_ANY, _(L("Name"))+":");
    set_font_and_background_style(name_label, wxGetApp().normal_font());
    name_label->SetToolTip(_(L("Object name")));
    sizer->Add(name_label);

    m_main_grid_sizer->Add(sizer);

    // Add name of the item
    const wxSize name_size = wxSize(20 * em, wxDefaultCoord);
    m_item_name = new wxStaticText(m_parent, wxID_ANY, "", wxDefaultPosition, name_size, wxST_ELLIPSIZE_MIDDLE);
    set_font_and_background_style(m_item_name, wxGetApp().bold_font());

    m_main_grid_sizer->Add(m_item_name, 0, wxEXPAND);

    // Add labels grid sizer
    m_labels_grid_sizer = new wxFlexGridSizer(1, 3, 3); // "Name/label", "String name / Editors"
    m_labels_grid_sizer->SetFlexibleDirection(wxBOTH);

    // Add world local combobox
    m_word_local_combo = create_word_local_combo(parent);
    m_word_local_combo->Bind(wxEVT_COMBOBOX, ([this](wxCommandEvent& evt) {
        this->set_world_coordinates(evt.GetSelection() != 1);
    }), m_word_local_combo->GetId());

    // Small trick to correct layouting in different view_mode :
    // Show empty string of a same height as a m_word_local_combo, when m_word_local_combo is hidden
    sizer = new wxBoxSizer(wxHORIZONTAL);
    m_empty_str = new wxStaticText(parent, wxID_ANY, "");
    sizer->Add(m_word_local_combo);
    sizer->Add(m_empty_str);
    sizer->SetMinSize(wxSize(-1, m_word_local_combo->GetBestHeight(-1)));
    m_labels_grid_sizer->Add(sizer);

    // Text trick to grid sizer layout:
    // Height of labels should be equivalent to the edit boxes
    int height = wxTextCtrl(parent, wxID_ANY, "Br").GetBestHeight(-1);

    auto add_label = [this, height](wxStaticText** label, std::string name, wxSizer* resive_sizer = nullptr)
    {
        *label = new wxStaticText(m_parent, wxID_ANY, _(name) + ":");
        set_font_and_background_style(m_move_Label, wxGetApp().normal_font());

        wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->SetMinSize(wxSize(-1, height));
        sizer->Add(*label, 0, wxALIGN_CENTER_VERTICAL);
      
        if (resive_sizer)
            resive_sizer->Add(sizer);
        else
            m_labels_grid_sizer->Add(sizer);
    };

    // Add labels
    add_label(&m_move_Label,    L("Position"));
    add_label(&m_rotate_Label,  L("Rotation"));

    // additional sizer for lock and labels "Scale" & "Size"
    sizer = new wxBoxSizer(wxHORIZONTAL);

    m_lock_bnt = new LockButton(parent, wxID_ANY);
    m_lock_bnt->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event) {
        event.Skip();
        wxTheApp->CallAfter([this]() { set_uniform_scaling(m_lock_bnt->IsLocked()); });
    });
    sizer->Add(m_lock_bnt, 0, wxALIGN_CENTER_VERTICAL);

    auto v_sizer = new wxGridSizer(1, 3, 3);

    add_label(&m_scale_Label,   L("Scale"), v_sizer);
    wxStaticText* size_Label {nullptr};
    add_label(&size_Label,      L("Size"), v_sizer);

    sizer->Add(v_sizer, 0, wxLEFT, border);
    m_labels_grid_sizer->Add(sizer);
    m_main_grid_sizer->Add(m_labels_grid_sizer, 0, wxEXPAND);


    // Add editors grid sizer
    m_editors_grid_sizer = new wxFlexGridSizer(5, 3, 3); // "Name/label", "String name / Editors"
    m_editors_grid_sizer->SetFlexibleDirection(wxBOTH);

    // Add Axes labels with icons
    static const char axes[] = { 'X', 'Y', 'Z' };
    for (size_t axis_idx = 0; axis_idx < sizeof(axes); axis_idx++) {
        const char label = axes[axis_idx];

        wxStaticText* axis_name = new wxStaticText(m_parent, wxID_ANY, wxString(label));
        set_font_and_background_style(axis_name, wxGetApp().bold_font());

        sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(axis_name, 0, wxALIGN_CENTER_VERTICAL/* | wxLEFT*/ | wxRIGHT, border);

        // We will add a button to toggle mirroring to each axis:
        auto btn = new ScalableButton(parent, wxID_ANY, "mirroring_off", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER | wxTRANSPARENT_WINDOW);
        btn->SetToolTip(wxString::Format(_(L("Toggle %c axis mirroring")), (int)label));
        btn->SetBitmapDisabled_(m_mirror_bitmap_hidden);

        m_mirror_buttons[axis_idx].first = btn;
        m_mirror_buttons[axis_idx].second = mbShown;

        sizer->AddStretchSpacer(2);
        sizer->Add(btn, 0, wxALIGN_CENTER_VERTICAL);

        btn->Bind(wxEVT_BUTTON, [this, axis_idx](wxCommandEvent&) {
            Axis axis = (Axis)(axis_idx + X);
            if (m_mirror_buttons[axis_idx].second == mbHidden)
                return;

            GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
            Selection& selection = canvas->get_selection();

            if (selection.is_single_volume() || selection.is_single_modifier()) {
                GLVolume* volume = const_cast<GLVolume*>(selection.get_volume(*selection.get_volume_idxs().begin()));
                volume->set_volume_mirror(axis, -volume->get_volume_mirror(axis));
            }
            else if (selection.is_single_full_instance()) {
                for (unsigned int idx : selection.get_volume_idxs()) {
                    GLVolume* volume = const_cast<GLVolume*>(selection.get_volume(idx));
                    volume->set_instance_mirror(axis, -volume->get_instance_mirror(axis));
                }
            }
            else
                return;

            // Update mirroring at the GLVolumes.
            selection.synchronize_unselected_instances(Selection::SYNC_ROTATION_GENERAL);
            selection.synchronize_unselected_volumes();
            // Copy mirroring values from GLVolumes into Model (ModelInstance / ModelVolume), trigger background processing.
            canvas->do_mirror(L("Set Mirror"));
            UpdateAndShow(true);
        });

        m_editors_grid_sizer->Add(sizer, 0, wxALIGN_CENTER_HORIZONTAL);        
    }

    m_editors_grid_sizer->AddStretchSpacer(1);
    m_editors_grid_sizer->AddStretchSpacer(1);

    // add EditBoxes 
    auto add_edit_boxes = [this, em, parent](std::string opt_key, int axis)
    {
        wxTextCtrl* editor = new wxTextCtrl(parent, wxID_ANY, wxEmptyString, 
                                            wxDefaultPosition, wxSize(5 * em, wxDefaultCoord),
                                            wxTE_PROCESS_ENTER);

        set_font_and_background_style(editor, wxGetApp().normal_font());
#ifdef __WXOSX__
        editor->OSXDisableAllSmartSubstitutions();
#endif // __WXOSX__
        m_editors_grid_sizer->Add(editor, 1, wxEXPAND);
    };
    
    // add Units 
    auto add_unit_text = [this, parent](std::string unit)
    {
        wxStaticText* unit_text = new wxStaticText(parent, wxID_ANY, _(unit));

        set_font_and_background_style(unit_text, wxGetApp().normal_font());
        m_editors_grid_sizer->Add(unit_text, 0, wxALIGN_CENTER_VERTICAL);
    };

    for (size_t axis_idx = 0; axis_idx < sizeof(axes); axis_idx++)
        add_edit_boxes("position", axis_idx);
    add_unit_text(L("mm"));

    // Add drop to bed button
    m_drop_to_bed_button = new ScalableButton(parent, wxID_ANY, ScalableBitmap(parent, "drop_to_bed"));
    m_drop_to_bed_button->SetToolTip(_(L("Drop to bed")));
    m_drop_to_bed_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent& e) {
        // ???
        GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
        Selection& selection = canvas->get_selection();

        if (selection.is_single_volume() || selection.is_single_modifier()) {
            const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());

            const Geometry::Transformation& instance_trafo = volume->get_instance_transformation();
            Vec3d diff = m_cache.position - instance_trafo.get_matrix(true).inverse() * Vec3d(0., 0., get_volume_min_z(volume));

            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("Drop to bed")));
            change_position_value(0, diff.x());
            change_position_value(1, diff.y());
            change_position_value(2, diff.z());
        }
        });
    m_editors_grid_sizer->Add(m_drop_to_bed_button);

    for (size_t axis_idx = 0; axis_idx < sizeof(axes); axis_idx++)
        add_edit_boxes("rotation", axis_idx);
    add_unit_text("°");

    // Add reset rotation button
    m_reset_rotation_button = new ScalableButton(parent, wxID_ANY, ScalableBitmap(parent, "undo"));
    m_reset_rotation_button->SetToolTip(_(L("Reset rotation")));
    m_reset_rotation_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
        GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
        Selection& selection = canvas->get_selection();

        if (selection.is_single_volume() || selection.is_single_modifier()) {
            GLVolume* volume = const_cast<GLVolume*>(selection.get_volume(*selection.get_volume_idxs().begin()));
            volume->set_volume_rotation(Vec3d::Zero());
        }
        else if (selection.is_single_full_instance()) {
            for (unsigned int idx : selection.get_volume_idxs()) {
                GLVolume* volume = const_cast<GLVolume*>(selection.get_volume(idx));
                volume->set_instance_rotation(Vec3d::Zero());
            }
        }
        else
            return;

        // Update rotation at the GLVolumes.
        selection.synchronize_unselected_instances(Selection::SYNC_ROTATION_GENERAL);
        selection.synchronize_unselected_volumes();
        // Copy rotation values from GLVolumes into Model (ModelInstance / ModelVolume), trigger background processing.
        canvas->do_rotate(L("Reset Rotation"));

        UpdateAndShow(true);
    });
    m_editors_grid_sizer->Add(m_reset_rotation_button);

    for (size_t axis_idx = 0; axis_idx < sizeof(axes); axis_idx++)
        add_edit_boxes("scale", axis_idx);
    add_unit_text("%");

    // Add reset scale button
    m_reset_scale_button = new ScalableButton(parent, wxID_ANY, ScalableBitmap(parent, "undo"));
    m_reset_scale_button->SetToolTip(_(L("Reset scale")));
    m_reset_scale_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("Reset scale")));
        change_scale_value(0, 100.);
        change_scale_value(1, 100.);
        change_scale_value(2, 100.);
    });
    m_editors_grid_sizer->Add(m_reset_scale_button);

    for (size_t axis_idx = 0; axis_idx < sizeof(axes); axis_idx++)
        add_edit_boxes("size", axis_idx);
    add_unit_text("mm");
    m_editors_grid_sizer->AddStretchSpacer(1);

    m_main_grid_sizer->Add(m_editors_grid_sizer, 1, wxEXPAND);

    m_og->sizer->Clear(true);
    m_og->sizer->Add(m_main_grid_sizer, 1, wxEXPAND | wxALL, border);

    m_manifold_warning_bmp = ScalableBitmap(parent, "exclamation");

    // Load bitmaps to be used for the mirroring buttons:
    m_mirror_bitmap_on = ScalableBitmap(parent, "mirroring_on");
    m_mirror_bitmap_off = ScalableBitmap(parent, "mirroring_off");
    m_mirror_bitmap_hidden = ScalableBitmap(parent, "mirroring_transparent.png");
}

/*
ObjectManipulation::ObjectManipulation(wxWindow* parent) :
    OG_Settings(parent, true)
#ifndef __APPLE__
    , m_focused_option("")
#endif // __APPLE__
{
    m_manifold_warning_bmp = ScalableBitmap(parent, "exclamation");
    m_og->set_name(_(L("Object Manipulation")));
    m_og->label_width = 12;//125;
    m_og->set_grid_vgap(5);
    
    m_og->m_on_change = std::bind(&ObjectManipulation::on_change, this, std::placeholders::_1, std::placeholders::_2);
    m_og->m_fill_empty_value = std::bind(&ObjectManipulation::on_fill_empty_value, this, std::placeholders::_1);

    m_og->m_set_focus = [this](const std::string& opt_key)
    {
#ifndef __APPLE__
        m_focused_option = opt_key;
#endif // __APPLE__

        // needed to show the visual hints in 3D scene
        wxGetApp().plater()->canvas3D()->handle_sidebar_focus_event(opt_key, true);
    };

    ConfigOptionDef def;

    Line line = Line{ "Name", "Object name" };

    auto manifold_warning_icon = [this](wxWindow* parent) {
        m_fix_throught_netfab_bitmap = new wxStaticBitmap(parent, wxID_ANY, wxNullBitmap);

        if (is_windows10())
            m_fix_throught_netfab_bitmap->Bind(wxEVT_CONTEXT_MENU, [this](wxCommandEvent &e)
            {
                // if object/sub-object has no errors
                if (m_fix_throught_netfab_bitmap->GetBitmap().GetRefData() == wxNullBitmap.GetRefData())
                    return;

                wxGetApp().obj_list()->fix_through_netfabb();
                update_warning_icon_state(wxGetApp().obj_list()->get_mesh_errors_list());
            });

        return m_fix_throught_netfab_bitmap;
    };

    line.near_label_widget = manifold_warning_icon;
    def.label = "";
    def.gui_type = "legend";
    def.tooltip = L("Object name");
#ifdef __APPLE__
    def.width = 20;
#else
    def.width = 22;
#endif
    def.set_default_value(new ConfigOptionString{ " " });
    line.append_option(Option(def, "object_name"));
    m_og->append_line(line);

    const int field_width = 5;

    // Mirror button size:
    const int mirror_btn_width = 3;

    // Legend for object modification
    line = Line{ "", "" };
    def.label = "";
    def.type = coString;
    def.width = field_width - mirror_btn_width;

    // Load bitmaps to be used for the mirroring buttons:
    m_mirror_bitmap_on  = ScalableBitmap(parent, "mirroring_on");
    m_mirror_bitmap_off = ScalableBitmap(parent, "mirroring_off");
    m_mirror_bitmap_hidden = ScalableBitmap(parent, "mirroring_transparent.png");

    static const char axes[] = { 'X', 'Y', 'Z' };
    for (size_t axis_idx = 0; axis_idx < sizeof(axes); axis_idx++) {
        const char label = axes[axis_idx];
        def.set_default_value(new ConfigOptionString{ std::string("   ") + label });
        Option option(def, std::string() + label + "_axis_legend");

        // We will add a button to toggle mirroring to each axis:
        auto mirror_button = [this, mirror_btn_width, axis_idx, label](wxWindow* parent) {
            wxSize btn_size(em_unit(parent) * mirror_btn_width, em_unit(parent) * mirror_btn_width);
            auto btn = new ScalableButton(parent, wxID_ANY, "mirroring_off", wxEmptyString, btn_size, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER | wxTRANSPARENT_WINDOW);
            btn->SetToolTip(wxString::Format(_(L("Toggle %c axis mirroring")), (int)label));
            btn->SetBitmapDisabled_(m_mirror_bitmap_hidden);

            m_mirror_buttons[axis_idx].first = btn;
            m_mirror_buttons[axis_idx].second = mbShown;
            auto sizer = new wxBoxSizer(wxHORIZONTAL);
            sizer->Add(btn);

            btn->Bind(wxEVT_BUTTON, [this, axis_idx](wxCommandEvent &e) {
                Axis axis = (Axis)(axis_idx + X);
                if (m_mirror_buttons[axis_idx].second == mbHidden)
                    return;

                GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
                Selection& selection = canvas->get_selection();

                if (selection.is_single_volume() || selection.is_single_modifier()) {
                    GLVolume* volume = const_cast<GLVolume*>(selection.get_volume(*selection.get_volume_idxs().begin()));
                    volume->set_volume_mirror(axis, -volume->get_volume_mirror(axis));
                }
                else if (selection.is_single_full_instance()) {
                    for (unsigned int idx : selection.get_volume_idxs()){
                        GLVolume* volume = const_cast<GLVolume*>(selection.get_volume(idx));
                        volume->set_instance_mirror(axis, -volume->get_instance_mirror(axis));
                    }
                }
                else
                    return;

                // Update mirroring at the GLVolumes.
                selection.synchronize_unselected_instances(Selection::SYNC_ROTATION_GENERAL);
                selection.synchronize_unselected_volumes();
                // Copy mirroring values from GLVolumes into Model (ModelInstance / ModelVolume), trigger background processing.
                canvas->do_mirror(L("Set Mirror"));
                UpdateAndShow(true);
            });

            return sizer;
        };

        option.side_widget = mirror_button;
        line.append_option(option);
    }
    line.near_label_widget = [this](wxWindow* parent) {
        wxBitmapComboBox *combo = create_word_local_combo(parent);
		combo->Bind(wxEVT_COMBOBOX, ([this](wxCommandEvent &evt) { this->set_world_coordinates(evt.GetSelection() != 1); }), combo->GetId());
        m_word_local_combo = combo;
        return combo;
    };
    m_og->append_line(line);

    auto add_og_to_object_settings = [this, field_width](const std::string& option_name, const std::string& sidetext)
    {
        Line line = { _(option_name), "" };
        ConfigOptionDef def;
        def.type = coFloat;
        def.set_default_value(new ConfigOptionFloat(0.0));
        def.width = field_width;

        if (option_name == "Scale") {
            // Add "uniform scaling" button in front of "Scale" option
            line.near_label_widget = [this](wxWindow* parent) {
                auto btn = new LockButton(parent, wxID_ANY);
                btn->Bind(wxEVT_BUTTON, [btn, this](wxCommandEvent &event){
                    event.Skip();
                    wxTheApp->CallAfter([btn, this]() { set_uniform_scaling(btn->IsLocked()); });
                });
                m_lock_bnt = btn;
                return btn;
            };
            // Add reset scale button
            auto reset_scale_button = [this](wxWindow* parent) {
                auto btn = new ScalableButton(parent, wxID_ANY, ScalableBitmap(parent, "undo"));
                btn->SetToolTip(_(L("Reset scale")));
                m_reset_scale_button = btn;
                auto sizer = new wxBoxSizer(wxHORIZONTAL);
                sizer->Add(btn, wxBU_EXACTFIT);
                btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &e) {
                    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("Reset scale")));
                    change_scale_value(0, 100.);
                    change_scale_value(1, 100.);
                    change_scale_value(2, 100.);
                });
            return sizer;
            };
            line.append_widget(reset_scale_button);
        }
        else if (option_name == "Rotation") {
            // Add reset rotation button
            auto reset_rotation_button = [this](wxWindow* parent) {
                auto btn = new ScalableButton(parent, wxID_ANY, ScalableBitmap(parent, "undo"));
                btn->SetToolTip(_(L("Reset rotation")));
                m_reset_rotation_button = btn;
                auto sizer = new wxBoxSizer(wxHORIZONTAL);
                sizer->Add(btn, wxBU_EXACTFIT);
                btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &e) {
                    GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
                    Selection& selection = canvas->get_selection();

                    if (selection.is_single_volume() || selection.is_single_modifier()) {
                        GLVolume* volume = const_cast<GLVolume*>(selection.get_volume(*selection.get_volume_idxs().begin()));
                        volume->set_volume_rotation(Vec3d::Zero());
                    }
                    else if (selection.is_single_full_instance()) {
                        for (unsigned int idx : selection.get_volume_idxs()){
                            GLVolume* volume = const_cast<GLVolume*>(selection.get_volume(idx));
                            volume->set_instance_rotation(Vec3d::Zero());
                        }
                    }
                    else
                        return;

                    // Update rotation at the GLVolumes.
                    selection.synchronize_unselected_instances(Selection::SYNC_ROTATION_GENERAL);
                    selection.synchronize_unselected_volumes();
                    // Copy rotation values from GLVolumes into Model (ModelInstance / ModelVolume), trigger background processing.
                    canvas->do_rotate(L("Reset Rotation"));

                    UpdateAndShow(true);
                });
                return sizer;
            };
            line.append_widget(reset_rotation_button);
        }
        else if (option_name == "Position") {
            // Add drop to bed button
            auto drop_to_bed_button = [=](wxWindow* parent) {
                auto btn = new ScalableButton(parent, wxID_ANY, ScalableBitmap(parent, "drop_to_bed"));
                btn->SetToolTip(_(L("Drop to bed")));
                m_drop_to_bed_button = btn;
                auto sizer = new wxBoxSizer(wxHORIZONTAL);
                sizer->Add(btn, wxBU_EXACTFIT);
                btn->Bind(wxEVT_BUTTON, [=](wxCommandEvent &e) {
                    // ???
                    GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
                    Selection& selection = canvas->get_selection();

                    if (selection.is_single_volume() || selection.is_single_modifier()) {
                        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());

                        const Geometry::Transformation& instance_trafo = volume->get_instance_transformation();
                        Vec3d diff = m_cache.position - instance_trafo.get_matrix(true).inverse() * Vec3d(0., 0., get_volume_min_z(volume));

                        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("Drop to bed")));
                        change_position_value(0, diff.x());
                        change_position_value(1, diff.y());
                        change_position_value(2, diff.z());
                    }
                });
            return sizer;
            };
            line.append_widget(drop_to_bed_button);
        }
        // Add empty bmp (Its size have to be equal to PrusaLockButton) in front of "Size" option to label alignment
        else if (option_name == "Size") {
            line.near_label_widget = [this](wxWindow* parent) {
                return new wxStaticBitmap(parent, wxID_ANY, wxNullBitmap, wxDefaultPosition,
                                          create_scaled_bitmap(m_parent, "one_layer_lock_on.png").GetSize());
            };
        }

        const std::string lower_name = boost::algorithm::to_lower_copy(option_name);

        for (const char *axis : { "_x", "_y", "_z" }) {
            if (axis[1] == 'z')
                def.sidetext = sidetext;
            Option option = Option(def, lower_name + axis);
            option.opt.full_width = true;
            line.append_option(option);
        }

        return line;
    };

    // Settings table
    m_og->sidetext_width = 3;
    m_og->append_line(add_og_to_object_settings(L("Position"), L("mm")), &m_move_Label);
    m_og->append_line(add_og_to_object_settings(L("Rotation"), "°"), &m_rotate_Label);
    m_og->append_line(add_og_to_object_settings(L("Scale"), "%"), &m_scale_Label);
    m_og->append_line(add_og_to_object_settings(L("Size"), "mm"));

    // call back for a rescale of button "Set uniform scale"
    m_og->rescale_near_label_widget = [this](wxWindow* win) {
        // rescale lock icon
        auto *ctrl = dynamic_cast<LockButton*>(win);
        if (ctrl != nullptr) {
            ctrl->msw_rescale();
            return;
        }

        if (win == m_fix_throught_netfab_bitmap)
            return;

        // rescale "place" of the empty icon (to correct layout of the "Size" and "Scale")
        if (dynamic_cast<wxStaticBitmap*>(win) != nullptr)
            win->SetMinSize(create_scaled_bitmap(m_parent, "one_layer_lock_on.png").GetSize());
    };
}
*/ 
 

void ObjectManipulation::Show(const bool show)
{
	if (show != IsShown()) {
		// Show all lines of the panel. Some of these lines will be hidden in the lines below.
		m_og->Show(show);

        if (show && wxGetApp().get_mode() != comSimple) {
            // Show the label and the name of the STL in simple mode only.
            // Label "Name: "
            /*m_og->get_grid_sizer()*/m_main_grid_sizer->Show(size_t(0), false);
            // The actual name of the STL.
            /*m_og->get_grid_sizer()*/m_main_grid_sizer->Show(size_t(1), false);
        }
    }

	if (show) {
		// Show the "World Coordinates" / "Local Coordintes" Combo in Advanced / Expert mode only.
		bool show_world_local_combo = wxGetApp().plater()->canvas3D()->get_selection().is_single_full_instance() && wxGetApp().get_mode() != comSimple;
		m_word_local_combo->Show(show_world_local_combo);
        m_empty_str->Show(!show_world_local_combo);
	}
}

bool ObjectManipulation::IsShown()
{
	return dynamic_cast<const wxStaticBoxSizer*>(m_og->sizer)->GetStaticBox()->IsShown(); //  m_og->get_grid_sizer()->IsShown(2);
}

void ObjectManipulation::UpdateAndShow(const bool show)
{
	if (show) {
        this->set_dirty();
		this->update_if_dirty();
	}

    OG_Settings::UpdateAndShow(show);
}

void ObjectManipulation::update_settings_value(const Selection& selection)
{
	m_new_move_label_string   = L("Position");
    m_new_rotate_label_string = L("Rotation");
    m_new_scale_label_string  = L("Scale factors");

    if (wxGetApp().get_mode() == comSimple)
        m_world_coordinates = true;

    ObjectList* obj_list = wxGetApp().obj_list();
    if (selection.is_single_full_instance())
    {
        // all volumes in the selection belongs to the same instance, any of them contains the needed instance data, so we take the first one
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        m_new_position = volume->get_instance_offset();

        // Verify whether the instance rotation is multiples of 90 degrees, so that the scaling in world coordinates is possible.
		if (m_world_coordinates && ! m_uniform_scale && 
            ! Geometry::is_rotation_ninety_degrees(volume->get_instance_rotation())) {
			// Manipulating an instance in the world coordinate system, rotation is not multiples of ninety degrees, therefore enforce uniform scaling.
			m_uniform_scale = true;
			m_lock_bnt->SetLock(true);
		}

        if (m_world_coordinates) {
			m_new_rotate_label_string = L("Rotate");
			m_new_rotation = Vec3d::Zero();
			m_new_size     = selection.get_scaled_instance_bounding_box().size();
			m_new_scale    = m_new_size.cwiseProduct(selection.get_unscaled_instance_bounding_box().size().cwiseInverse()) * 100.;
		} else {
			m_new_rotation = volume->get_instance_rotation() * (180. / M_PI);
			m_new_size     = volume->get_instance_transformation().get_scaling_factor().cwiseProduct(wxGetApp().model().objects[volume->object_idx()]->raw_mesh_bounding_box().size());
			m_new_scale    = volume->get_instance_scaling_factor() * 100.;
		}

        m_new_enabled  = true;
    }
    else if (selection.is_single_full_object() && obj_list->is_selected(itObject))
    {
        const BoundingBoxf3& box = selection.get_bounding_box();
        m_new_position = box.center();
        m_new_rotation = Vec3d::Zero();
        m_new_scale    = Vec3d(100., 100., 100.);
        m_new_size     = box.size();
        m_new_rotate_label_string = L("Rotate");
		m_new_scale_label_string  = L("Scale");
        m_new_enabled  = true;
    }
    else if (selection.is_single_modifier() || selection.is_single_volume())
    {
        // the selection contains a single volume
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        m_new_position = volume->get_volume_offset();
        m_new_rotation = volume->get_volume_rotation() * (180. / M_PI);
        m_new_scale    = volume->get_volume_scaling_factor() * 100.;
        m_new_size = volume->get_volume_transformation().get_scaling_factor().cwiseProduct(volume->bounding_box().size());
        m_new_enabled = true;
    }
    else if (obj_list->multiple_selection() || obj_list->is_selected(itInstanceRoot))
    {
        reset_settings_value();
		m_new_move_label_string   = L("Translate");
		m_new_rotate_label_string = L("Rotate");
		m_new_scale_label_string  = L("Scale");
        m_new_size = selection.get_bounding_box().size();
        m_new_enabled  = true;
    }
	else {
        // No selection, reset the cache.
//		assert(selection.is_empty());
		reset_settings_value();
	}
}

void ObjectManipulation::update_if_dirty()
{
    if (! m_dirty)
        return;

    const Selection &selection = wxGetApp().plater()->canvas3D()->get_selection();
    this->update_settings_value(selection);

    auto update_label = [](wxString &label_cache, const std::string &new_label, wxStaticText *widget) {
        wxString new_label_localized = _(new_label) + ":";
        if (label_cache != new_label_localized) {
            label_cache = new_label_localized;
            widget->SetLabel(new_label_localized);
        }
    };
    update_label(m_cache.move_label_string,   m_new_move_label_string,   m_move_Label);
    update_label(m_cache.rotate_label_string, m_new_rotate_label_string, m_rotate_Label);
    update_label(m_cache.scale_label_string,  m_new_scale_label_string,  m_scale_Label);

    char axis[2] = "x";
    for (int i = 0; i < 3; ++ i, ++ axis[0]) {
        auto update = [this, i, &axis](Vec3d &cached, Vec3d &cached_rounded, const char *key, const Vec3d &new_value) {
			wxString new_text = double_to_string(new_value(i), 2);
			double new_rounded;
			new_text.ToDouble(&new_rounded);
			if (std::abs(cached_rounded(i) - new_rounded) > EPSILON) {
				cached_rounded(i) = new_rounded;
                m_og->set_value(std::string(key) + axis, new_text);
            }
			cached(i) = new_value(i);
		};
        update(m_cache.position, m_cache.position_rounded, "position_", m_new_position);
        update(m_cache.scale,    m_cache.scale_rounded,    "scale_",    m_new_scale);
        update(m_cache.size,     m_cache.size_rounded,     "size_",     m_new_size);
        update(m_cache.rotation, m_cache.rotation_rounded, "rotation_", m_new_rotation);
    }

    if (selection.requires_uniform_scale()) {
        m_lock_bnt->SetLock(true);
        m_lock_bnt->SetToolTip(_(L("You cannot use non-uniform scaling mode for multiple objects/parts selection")));
        m_lock_bnt->disable();
    }
    else {
        m_lock_bnt->SetLock(m_uniform_scale);
        m_lock_bnt->SetToolTip(wxEmptyString);
        m_lock_bnt->enable();
    }

    { 
        int new_selection = m_world_coordinates ? 0 : 1; 
        if (m_word_local_combo->GetSelection() != new_selection)
            m_word_local_combo->SetSelection(new_selection);
    }

    if (m_new_enabled)
        m_og->enable();
    else
        m_og->disable();

    update_reset_buttons_visibility();
    update_mirror_buttons_visibility();

    m_dirty = false;
}



void ObjectManipulation::update_reset_buttons_visibility()
{
    GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
    if (!canvas)
        return;
    const Selection& selection = canvas->get_selection();

    bool show_rotation = false;
    bool show_scale = false;
    bool show_drop_to_bed = false;

    if (selection.is_single_full_instance() || selection.is_single_modifier() || selection.is_single_volume()) {
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        Vec3d rotation;
        Vec3d scale;
        double min_z = 0.;

        if (selection.is_single_full_instance()) {
            rotation = volume->get_instance_rotation();
            scale = volume->get_instance_scaling_factor();
        }
        else {
            rotation = volume->get_volume_rotation();
            scale = volume->get_volume_scaling_factor();
            min_z = get_volume_min_z(volume);
        }
        show_rotation = !rotation.isApprox(Vec3d::Zero());
        show_scale = !scale.isApprox(Vec3d::Ones());
        show_drop_to_bed = (std::abs(min_z) > EPSILON);
    }

    wxGetApp().CallAfter([this, show_rotation, show_scale, show_drop_to_bed]{
        m_reset_rotation_button->Show(show_rotation);
        m_reset_scale_button->Show(show_scale);
        m_drop_to_bed_button->Show(show_drop_to_bed);

        // Because of CallAfter we need to layout sidebar after Show/hide of reset buttons one more time
        Sidebar& panel = wxGetApp().sidebar();
        if (!panel.IsFrozen()) {
            panel.Freeze();
            panel.Layout();
            panel.Thaw();
        }
    });
}



void ObjectManipulation::update_mirror_buttons_visibility()
{
    GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
    Selection& selection = canvas->get_selection();
    std::array<MirrorButtonState, 3> new_states = {mbHidden, mbHidden, mbHidden};

    if (!m_world_coordinates) {
        if (selection.is_single_full_instance() || selection.is_single_modifier() || selection.is_single_volume()) {
            const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
            Vec3d mirror;

            if (selection.is_single_full_instance())
                mirror = volume->get_instance_mirror();
            else
                mirror = volume->get_volume_mirror();

            for (unsigned char i=0; i<3; ++i)
                new_states[i] = (mirror[i] < 0. ? mbActive : mbShown);
        }
    }
    else {
        // the mirroring buttons should be hidden in world coordinates,
        // unless we make it actually mirror in world coords.
    }

    // Hiding the buttons through Hide() always messed up the sizers. As a workaround, the button
    // is assigned a transparent bitmap. We must of course remember the actual state.
    wxGetApp().CallAfter([this, new_states]{
        for (int i=0; i<3; ++i) {
            if (new_states[i] != m_mirror_buttons[i].second) {
                const ScalableBitmap* bmp = nullptr;
                switch (new_states[i]) {
                    case mbHidden : bmp = &m_mirror_bitmap_hidden; m_mirror_buttons[i].first->Enable(false); break;
                    case mbShown  : bmp = &m_mirror_bitmap_off; m_mirror_buttons[i].first->Enable(true); break;
                    case mbActive : bmp = &m_mirror_bitmap_on; m_mirror_buttons[i].first->Enable(true); break;
                }
                m_mirror_buttons[i].first->SetBitmap_(*bmp);
                m_mirror_buttons[i].second = new_states[i];
            }
        }
    });
}




#ifndef __APPLE__
void ObjectManipulation::emulate_kill_focus()
{
    if (m_focused_option.empty())
        return;

    // we need to use a copy because the value of m_focused_option is modified inside on_change() and on_fill_empty_value()
    std::string option = m_focused_option;

    // see TextCtrl::propagate_value()
    if (static_cast<wxTextCtrl*>(m_og->get_fieldc(option, 0)->getWindow())->GetValue().empty())
        on_fill_empty_value(option);
    else
        on_change(option, 0);
}
#endif // __APPLE__

void ObjectManipulation::update_item_name(const wxString& item_name)
{
    m_item_name->SetLabel(item_name);
}

void ObjectManipulation::update_warning_icon_state(const wxString& tooltip)
{
    m_fix_throught_netfab_bitmap->SetBitmap(tooltip.IsEmpty() ? wxNullBitmap : m_manifold_warning_bmp.bmp());
    m_fix_throught_netfab_bitmap->SetMinSize(tooltip.IsEmpty() ? wxSize(0,0) : m_manifold_warning_bmp.bmp().GetSize());
    m_fix_throught_netfab_bitmap->SetToolTip(tooltip);
}

void ObjectManipulation::reset_settings_value()
{
    m_new_position = Vec3d::Zero();
    m_new_rotation = Vec3d::Zero();
    m_new_scale = Vec3d::Ones() * 100.;
    m_new_size = Vec3d::Zero();
    m_new_enabled = false;
    // no need to set the dirty flag here as this method is called from update_settings_value(),
    // which is called from update_if_dirty(), which resets the dirty flag anyways.
//    m_dirty = true;
}

void ObjectManipulation::change_position_value(int axis, double value)
{
    if (std::abs(m_cache.position_rounded(axis) - value) < EPSILON)
        return;

    Vec3d position = m_cache.position;
    position(axis) = value;

    auto canvas = wxGetApp().plater()->canvas3D();
    Selection& selection = canvas->get_selection();
    selection.start_dragging();
    selection.translate(position - m_cache.position, selection.requires_local_axes());
    canvas->do_move(L("Set Position"));

    m_cache.position = position;
	m_cache.position_rounded(axis) = DBL_MAX;
    this->UpdateAndShow(true);
}

void ObjectManipulation::change_rotation_value(int axis, double value)
{
    if (std::abs(m_cache.rotation_rounded(axis) - value) < EPSILON)
        return;

    Vec3d rotation = m_cache.rotation;
    rotation(axis) = value;

    GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
    Selection& selection = canvas->get_selection();

    TransformationType transformation_type(TransformationType::World_Relative_Joint);
    if (selection.is_single_full_instance() || selection.requires_local_axes())
		transformation_type.set_independent();
	if (selection.is_single_full_instance() && ! m_world_coordinates) {
        //FIXME Selection::rotate() does not process absoulte rotations correctly: It does not recognize the axis index, which was changed.
		// transformation_type.set_absolute();
		transformation_type.set_local();
	}

    selection.start_dragging();
	selection.rotate(
		(M_PI / 180.0) * (transformation_type.absolute() ? rotation : rotation - m_cache.rotation), 
		transformation_type);
    canvas->do_rotate(L("Set Orientation"));

    m_cache.rotation = rotation;
	m_cache.rotation_rounded(axis) = DBL_MAX;
    this->UpdateAndShow(true);
}

void ObjectManipulation::change_scale_value(int axis, double value)
{
    if (std::abs(m_cache.scale_rounded(axis) - value) < EPSILON)
        return;

    Vec3d scale = m_cache.scale;
	scale(axis) = value;

    this->do_scale(axis, scale);

    m_cache.scale = scale;
	m_cache.scale_rounded(axis) = DBL_MAX;
	this->UpdateAndShow(true);
}


void ObjectManipulation::change_size_value(int axis, double value)
{
    if (std::abs(m_cache.size_rounded(axis) - value) < EPSILON)
        return;

    Vec3d size = m_cache.size;
    size(axis) = value;

    const Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();

    Vec3d ref_size = m_cache.size;
	if (selection.is_single_volume() || selection.is_single_modifier())
        ref_size = selection.get_volume(*selection.get_volume_idxs().begin())->bounding_box().size();
    else if (selection.is_single_full_instance())
		ref_size = m_world_coordinates ? 
            selection.get_unscaled_instance_bounding_box().size() :
            wxGetApp().model().objects[selection.get_volume(*selection.get_volume_idxs().begin())->object_idx()]->raw_mesh_bounding_box().size();

    this->do_scale(axis, 100. * Vec3d(size(0) / ref_size(0), size(1) / ref_size(1), size(2) / ref_size(2)));

    m_cache.size = size;
	m_cache.size_rounded(axis) = DBL_MAX;
	this->UpdateAndShow(true);
}

void ObjectManipulation::do_scale(int axis, const Vec3d &scale) const
{
    Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();
    Vec3d scaling_factor = scale;

    TransformationType transformation_type(TransformationType::World_Relative_Joint);
    if (selection.is_single_full_instance()) {
        transformation_type.set_absolute();
        if (! m_world_coordinates)
            transformation_type.set_local();
    }

    if (m_uniform_scale || selection.requires_uniform_scale())
        scaling_factor = scale(axis) * Vec3d::Ones();

    selection.start_dragging();
    selection.scale(scaling_factor * 0.01, transformation_type);
    wxGetApp().plater()->canvas3D()->do_scale(L("Set Scale"));
}

void ObjectManipulation::on_change(t_config_option_key opt_key, const boost::any& value)
{
    Field* field = m_og->get_field(opt_key);
    bool enter_pressed = (field != nullptr) && field->get_enter_pressed();
    if (!enter_pressed)
    {
        // if the change does not come from the user pressing the ENTER key
        // we need to hide the visual hints in 3D scene
        wxGetApp().plater()->canvas3D()->handle_sidebar_focus_event(opt_key, false);

#ifndef __APPLE__
        m_focused_option = "";
#endif // __APPLE__
    }
    else
        // if the change comes from the user pressing the ENTER key, restore the key state
        field->set_enter_pressed(false);

    if (!m_cache.is_valid())
        return;

    int    axis      = opt_key.back() - 'x';
    double new_value = boost::any_cast<double>(m_og->get_value(opt_key));

    if (boost::starts_with(opt_key, "position_"))
        change_position_value(axis, new_value);
    else if (boost::starts_with(opt_key, "rotation_"))
        change_rotation_value(axis, new_value);
    else if (boost::starts_with(opt_key, "scale_"))
        change_scale_value(axis, new_value);
    else if (boost::starts_with(opt_key, "size_"))
        change_size_value(axis, new_value);
}

void ObjectManipulation::on_fill_empty_value(const std::string& opt_key)
{
    // needed to hide the visual hints in 3D scene
    wxGetApp().plater()->canvas3D()->handle_sidebar_focus_event(opt_key, false);
#ifndef __APPLE__
    m_focused_option = "";
#endif // __APPLE__

    if (!m_cache.is_valid())
        return;

    const Vec3d *vec = nullptr;
    Vec3d       *rounded = nullptr;
	if (boost::starts_with(opt_key, "position_")) {
		vec = &m_cache.position;
        rounded = &m_cache.position_rounded;
    } else if (boost::starts_with(opt_key, "rotation_")) {
		vec = &m_cache.rotation;
        rounded = &m_cache.rotation_rounded;
    } else if (boost::starts_with(opt_key, "scale_")) {
		vec = &m_cache.scale;
        rounded = &m_cache.scale_rounded;
    } else if (boost::starts_with(opt_key, "size_")) {
		vec = &m_cache.size;
        rounded = &m_cache.size_rounded;
    } else
		assert(false);

	if (vec != nullptr) {
        int axis = opt_key.back() - 'x';
        wxString new_text = double_to_string((*vec)(axis));
		m_og->set_value(opt_key, new_text);
		new_text.ToDouble(&(*rounded)(axis));
    }
}

void ObjectManipulation::set_uniform_scaling(const bool new_value)
{ 
    const Selection &selection = wxGetApp().plater()->canvas3D()->get_selection();
	if (selection.is_single_full_instance() && m_world_coordinates && !new_value) {
        // Verify whether the instance rotation is multiples of 90 degrees, so that the scaling in world coordinates is possible.
        // all volumes in the selection belongs to the same instance, any of them contains the needed instance data, so we take the first one
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        // Is the angle close to a multiple of 90 degrees?
		if (! Geometry::is_rotation_ninety_degrees(volume->get_instance_rotation())) {
            // Cannot apply scaling in the world coordinate system.
			wxMessageDialog dlg(GUI::wxGetApp().mainframe,
                _(L("The currently manipulated object is tilted (rotation angles are not multiples of 90°).\n"
                    "Non-uniform scaling of tilted objects is only possible in the World coordinate system,\n"
                    "once the rotation is embedded into the object coordinates.")) + "\n" +
                _(L("This operation is irreversible.\n"
                    "Do you want to proceed?")),
                SLIC3R_APP_NAME,
				wxYES_NO | wxCANCEL | wxCANCEL_DEFAULT | wxICON_QUESTION);
            if (dlg.ShowModal() != wxID_YES) {
                // Enforce uniform scaling.
                m_lock_bnt->SetLock(true);
                return;
            }
            // Bake the rotation into the meshes of the object.
            wxGetApp().model().objects[volume->composite_id.object_id]->bake_xy_rotation_into_meshes(volume->composite_id.instance_id);
            // Update the 3D scene, selections etc.
            wxGetApp().plater()->update();
            // Recalculate cached values at this panel, refresh the screen.
            this->UpdateAndShow(true);
        }
    }
    m_uniform_scale = new_value;
}

void ObjectManipulation::msw_rescale()
{
    const int em = wxGetApp().em_unit();
    m_item_name->SetMinSize(wxSize(20*em, wxDefaultCoord));
    msw_rescale_word_local_combo(m_word_local_combo);
    m_manifold_warning_bmp.msw_rescale();

    const wxString& tooltip = m_fix_throught_netfab_bitmap->GetToolTipText();
    m_fix_throught_netfab_bitmap->SetBitmap(tooltip.IsEmpty() ? wxNullBitmap : m_manifold_warning_bmp.bmp());
    m_fix_throught_netfab_bitmap->SetMinSize(tooltip.IsEmpty() ? wxSize(0, 0) : m_manifold_warning_bmp.bmp().GetSize());

    m_mirror_bitmap_on.msw_rescale();
    m_mirror_bitmap_off.msw_rescale();
    m_mirror_bitmap_hidden.msw_rescale();
    m_reset_scale_button->msw_rescale();
    m_reset_rotation_button->msw_rescale();
    m_drop_to_bed_button->msw_rescale();
    m_lock_bnt->msw_rescale();

    for (int id = 0; id < 3; ++id)
        m_mirror_buttons[id].first->msw_rescale();

    // rescale label-heights
    // Text trick to grid sizer layout:
    // Height of labels should be equivalent to the edit boxes
    const int height = wxTextCtrl(parent(), wxID_ANY, "Br").GetBestHeight(-1);
    int cells_cnt = m_labels_grid_sizer->GetEffectiveRowsCount();
    for (int i = 0; i < cells_cnt; i++)
    {
        const wxSizerItem* item = m_labels_grid_sizer->GetItem(i);
        if (item->IsSizer())
        {
            const wxSizerItem* label_item = item->GetSizer()->GetItem(size_t(0));
            if (label_item->IsWindow()) 
            {
                if (dynamic_cast<wxStaticText*>(label_item->GetWindow()))
                    item->GetSizer()->SetMinSize(wxSize(-1, height));
                if (dynamic_cast<wxBitmapComboBox*>(label_item->GetWindow()))
                    item->GetSizer()->SetMinSize(wxSize(-1, m_word_local_combo->GetBestHeight(-1)));
                else if (dynamic_cast<LockButton*>(label_item->GetWindow())) // case when we have lock_btn and labels "Scale" and "Size"
                {
                    const wxSizerItem* l_item = item->GetSizer()->GetItem(1); // sizer with labels "Scale" and "Size"
                    if (l_item->IsSizer()) {
                        for (int id : {0,1}) {
                            l_item->GetSizer()->GetItem(id)->GetSizer()->SetMinSize(wxSize(-1, height));
                        }
                    }
                }
            }
        }
    }

    // rescale edit-boxes
    cells_cnt = m_editors_grid_sizer->GetCols() * m_editors_grid_sizer->GetEffectiveRowsCount();
    for (int i = 0; i < cells_cnt; i++)
    {
        const wxSizerItem* item = m_editors_grid_sizer->GetItem(i);
        if (item->IsWindow() && dynamic_cast<wxTextCtrl*>(item->GetWindow()))
            item->GetWindow()->SetMinSize(wxSize(5 * em, -1));
    }

    get_og()->msw_rescale();
}

} //namespace GUI
} //namespace Slic3r 
