#include "PresetComboBoxes.hpp"

#include <cstddef>
#include <vector>
#include <string>
#include <boost/algorithm/string.hpp>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/statbox.h>
#include <wx/colordlg.h>
#include <wx/wupdlock.h>
#include <wx/menu.h>
#include <wx/odcombo.h>
#include <wx/listbook.h>

#ifdef _WIN32
#include <wx/msw/dcclient.h>
#include <wx/msw/private.h>
#endif

#include "libslic3r/libslic3r.h"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Color.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "Tab.hpp"
#include "ConfigWizard.hpp"
#include "../Utils/ASCIIFolding.hpp"
#include "../Utils/FixModelByWin10.hpp"
#include "../Utils/UndoRedo.hpp"
#include "BitmapCache.hpp"
#include "PhysicalPrinterDialog.hpp"
#include "MsgDialog.hpp"

// A workaround for a set of issues related to text fitting into gtk widgets:
// See e.g.: https://github.com/prusa3d/PrusaSlicer/issues/4584
#if defined(__WXGTK20__) || defined(__WXGTK3__)
    #include <glib-2.0/glib-object.h>
    #include <pango-1.0/pango/pango-layout.h>
    #include <gtk/gtk.h>
#endif

using Slic3r::GUI::format_wxstr;

namespace Slic3r {
namespace GUI {

#define BORDER_W 10

// ---------------------------------
// ***  PresetComboBox  ***
// ---------------------------------

/* For PresetComboBox we use bitmaps that are created from images that are already scaled appropriately for Retina
 * (Contrary to the intuition, the `scale` argument for Bitmap's constructor doesn't mean
 * "please scale this to such and such" but rather
 * "the wxImage is already sized for backing scale such and such". )
 * Unfortunately, the constructor changes the size of wxBitmap too.
 * Thus We need to use unscaled size value for bitmaps that we use
 * to avoid scaled size of control items.
 * For this purpose control drawing methods and
 * control size calculation methods (virtual) are overridden.
 **/

PresetComboBox::PresetComboBox(wxWindow* parent, Preset::Type preset_type, const wxSize& size, PresetBundle* preset_bundle/* = nullptr*/) :
    BitmapComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, size, 0, nullptr, wxCB_READONLY),
    m_type(preset_type),
    m_last_selected(wxNOT_FOUND),
    m_em_unit(em_unit(this)),
    m_preset_bundle(preset_bundle ? preset_bundle : wxGetApp().preset_bundle)
{
    switch (m_type)
    {
    case Preset::TYPE_PRINT: {
        m_collection = &m_preset_bundle->prints;
        m_main_bitmap_name = "cog";
        break;
    }
    case Preset::TYPE_FILAMENT: {
        m_collection = &m_preset_bundle->filaments;
        m_main_bitmap_name = "spool";
        break;
    }
    case Preset::TYPE_SLA_PRINT: {
        m_collection = &m_preset_bundle->sla_prints;
        m_main_bitmap_name = "cog";
        break;
    }
    case Preset::TYPE_SLA_MATERIAL: {
        m_collection = &m_preset_bundle->sla_materials;
        m_main_bitmap_name = "resin";
        break;
    }
    case Preset::TYPE_PRINTER: {
        m_collection = &m_preset_bundle->printers;
        m_main_bitmap_name = "printer";
        break;
    }
    default: break;
    }

    m_bitmapCompatible   = get_bmp_bundle("flag_green");
    m_bitmapIncompatible = get_bmp_bundle("flag_red");

    // parameters for an icon's drawing
    fill_width_height();

    Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent& e) {
        if (m_suppress_change)
            e.StopPropagation();
        else
            e.Skip();
    });
    Bind(wxEVT_COMBOBOX_DROPDOWN, [this](wxCommandEvent&) { m_suppress_change = false; });
    Bind(wxEVT_COMBOBOX_CLOSEUP,  [this](wxCommandEvent&) { m_suppress_change = true;  });

    Bind(wxEVT_COMBOBOX, &PresetComboBox::OnSelect, this);
}

void PresetComboBox::OnSelect(wxCommandEvent& evt)
{
    // see https://github.com/prusa3d/PrusaSlicer/issues/3889
    // Under OSX: in case of use of a same names written in different case (like "ENDER" and "Ender")
    // m_presets_choice->GetSelection() will return first item, because search in PopupListCtrl is case-insensitive.
    // So, use GetSelection() from event parameter 
    auto selected_item = evt.GetSelection();

    auto marker = reinterpret_cast<Marker>(this->GetClientData(selected_item));
    if (marker >= LABEL_ITEM_DISABLED && marker < LABEL_ITEM_MAX)
        this->SetSelection(m_last_selected);
    else if (on_selection_changed && (m_last_selected != selected_item || m_collection->current_is_dirty())) {
        m_last_selected = selected_item;
        on_selection_changed(selected_item);
        evt.StopPropagation();
    }
    evt.Skip();
}

PresetComboBox::~PresetComboBox()
{
}

BitmapCache& PresetComboBox::bitmap_cache()
{
    static BitmapCache bmps;
    return bmps;
}

void PresetComboBox::set_label_marker(int item, LabelItemType label_item_type)
{
    this->SetClientData(item, (void*)label_item_type);
}

bool PresetComboBox::set_printer_technology(PrinterTechnology pt)
{
    if (printer_technology != pt) {
        printer_technology = pt;
        return true;
    }
    return false;
}

void PresetComboBox::invalidate_selection()
{
    m_last_selected = INT_MAX; // this value means that no one item is selected
}

void PresetComboBox::validate_selection(bool predicate/*=false*/)
{
    if (predicate ||
        // just in case: mark m_last_selected as a first added element
        m_last_selected == INT_MAX)
        m_last_selected = GetCount() - 1;
}

void PresetComboBox::update_selection()
{
    /* If selected_preset_item is still equal to INT_MAX, it means that
     * there is no presets added to the list.
     * So, select last combobox item ("Add/Remove preset")
     */
    validate_selection();

    SetSelection(m_last_selected);
#ifdef __WXMSW__
    // From the Windows 2004 the tooltip for preset combobox doesn't work after next call of SetTooltip()
    // (There was an issue, when tooltip doesn't appears after changing of the preset selection)
    // But this workaround seems to work: We should to kill tooltip and than set new tooltip value
    SetToolTip(NULL);
#endif
    SetToolTip(GetString(m_last_selected));

// A workaround for a set of issues related to text fitting into gtk widgets:
// See e.g.: https://github.com/prusa3d/PrusaSlicer/issues/4584
#if defined(__WXGTK20__) || defined(__WXGTK3__)
    GList* cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(m_widget));

    // 'cells' contains the GtkCellRendererPixBuf for the icon,
    // 'cells->next' contains GtkCellRendererText for the text we need to ellipsize
    if (!cells || !cells->next) return;

    auto cell = static_cast<GtkCellRendererText *>(cells->next->data);

    if (!cell) return;

    g_object_set(G_OBJECT(cell), "ellipsize", PANGO_ELLIPSIZE_END, (char*)NULL);

    // Only the list of cells must be freed, the renderer isn't ours to free
    g_list_free(cells);
#endif
}

static std::string suffix(const Preset& preset)
{
    return (preset.is_dirty ? Preset::suffix_modified() : "");
}

static std::string suffix(Preset* preset)
{
    return (preset->is_dirty ? Preset::suffix_modified() : "");
}

wxString PresetComboBox::get_preset_name(const Preset & preset)
{
    return from_u8(preset.name);
}

static wxString get_preset_name_with_suffix(const Preset & preset)
{
    return from_u8(preset.name + Preset::suffix_modified());
}

void PresetComboBox::update(std::string select_preset_name)
{
    Freeze();
    Clear();
    invalidate_selection();

    const std::deque<Preset>& presets = m_collection->get_presets();

    std::map<wxString, std::pair<wxBitmapBundle*, bool>>  nonsys_presets;
    std::map<wxString, wxBitmapBundle*>                   incomp_presets;

    wxString selected = "";
    if (!presets.front().is_visible)
        set_label_marker(Append(separator(L("System presets")), NullBitmapBndl()));

    for (size_t i = presets.front().is_visible ? 0 : m_collection->num_default_presets(); i < presets.size(); ++i)
    {
        const Preset& preset = presets[i];
        if (!m_show_all && (!preset.is_visible || !preset.is_compatible))
            continue;

        // marker used for disable incompatible printer models for the selected physical printer
        bool is_enabled = m_type == Preset::TYPE_PRINTER && printer_technology != ptAny ? preset.printer_technology() == printer_technology : true;
        if (select_preset_name.empty() && is_enabled)
            select_preset_name = preset.name;

        std::string   bitmap_key = "cb";
        if (m_type == Preset::TYPE_PRINTER) {
            bitmap_key += "_printer";
            if (preset.printer_technology() == ptSLA)
                bitmap_key += "_sla";
        }
        std::string main_icon_name = m_type == Preset::TYPE_PRINTER && preset.printer_technology() == ptSLA ? "sla_printer" : m_main_bitmap_name;

        auto bmp = get_bmp(bitmap_key, main_icon_name, "lock_closed", is_enabled, preset.is_compatible, preset.is_system || preset.is_default);
        assert(bmp);

        if (!is_enabled) {
            incomp_presets.emplace(get_preset_name(preset), bmp);
            if (preset.is_dirty && m_show_modif_preset_separately)
                incomp_presets.emplace(get_preset_name_with_suffix(preset), bmp);
        }
        else if (preset.is_default || preset.is_system)
        {
            Append(get_preset_name(preset), *bmp);
            validate_selection(preset.name == select_preset_name);
            if (preset.is_dirty && m_show_modif_preset_separately) {
                wxString preset_name = get_preset_name_with_suffix(preset);
                Append(preset_name, *bmp);
                validate_selection(into_u8(preset_name) == select_preset_name);
            }
        }
        else
        {
            nonsys_presets.emplace(get_preset_name(preset), std::pair<wxBitmapBundle*, bool>(bmp, is_enabled));
            if (preset.name == select_preset_name || (select_preset_name.empty() && is_enabled))
                selected = get_preset_name(preset);
            if (preset.is_dirty && m_show_modif_preset_separately) {
                wxString preset_name = get_preset_name_with_suffix(preset);
                nonsys_presets.emplace(preset_name, std::pair<wxBitmapBundle*, bool>(bmp, is_enabled));
                if (preset_name == select_preset_name || (select_preset_name.empty() && is_enabled))
                    selected = preset_name;
            }
        }
        if (i + 1 == m_collection->num_default_presets())
            set_label_marker(Append(separator(L("System presets")), NullBitmapBndl()));
    }
    if (!nonsys_presets.empty())
    {
        set_label_marker(Append(separator(L("User presets")), NullBitmapBndl()));
        for (std::map<wxString, std::pair<wxBitmapBundle*, bool>>::iterator it = nonsys_presets.begin(); it != nonsys_presets.end(); ++it) {
            int item_id = Append(it->first, *it->second.first);
            bool is_enabled = it->second.second;
            if (!is_enabled)
                set_label_marker(item_id, LABEL_ITEM_DISABLED);
            validate_selection(it->first == selected);
        }
    }
    if (!incomp_presets.empty())
    {
        set_label_marker(Append(separator(L("Incompatible presets")), NullBitmapBndl()));
        for (std::map<wxString, wxBitmapBundle*>::iterator it = incomp_presets.begin(); it != incomp_presets.end(); ++it) {
            set_label_marker(Append(it->first, *it->second), LABEL_ITEM_DISABLED);
        }
    }

    update_selection();
    Thaw();
}

void PresetComboBox::edit_physical_printer()
{
    if (!m_preset_bundle->physical_printers.has_selection())
        return;

    PhysicalPrinterDialog dlg(this->GetParent(),this->GetString(this->GetSelection()));
    if (dlg.ShowModal() == wxID_OK)
        update();
}

void PresetComboBox::add_physical_printer()
{
    if (PhysicalPrinterDialog(this->GetParent(), wxEmptyString).ShowModal() == wxID_OK)
        update();
}

void PresetComboBox::open_physical_printer_url()
{
    const PhysicalPrinter& pp = m_preset_bundle->physical_printers.get_selected_printer();
    std::string host = pp.config.opt_string("print_host");
    assert(!host.empty());
    wxGetApp().open_browser_with_warning_dialog(host);
}

bool PresetComboBox::del_physical_printer(const wxString& note_string/* = wxEmptyString*/)
{
    const std::string& printer_name = m_preset_bundle->physical_printers.get_selected_full_printer_name();
    if (printer_name.empty())
        return false;

    wxString msg;
    if (!note_string.IsEmpty())
        msg += note_string + "\n";
    msg += format_wxstr(_L("Are you sure you want to delete \"%1%\" printer?"), printer_name);

    if (MessageDialog(this, msg, _L("Delete Physical Printer"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION).ShowModal() != wxID_YES)
        return false;

    m_preset_bundle->physical_printers.delete_selected_printer();

    this->update();

    if (dynamic_cast<PlaterPresetComboBox*>(this) != nullptr)
        wxGetApp().get_tab(m_type)->update_preset_choice();
    else if (dynamic_cast<TabPresetComboBox*>(this) != nullptr)
    {
        wxGetApp().get_tab(m_type)->update_btns_enabling();
        wxGetApp().plater()->sidebar().update_presets(m_type);
    }

    return true;
}

void PresetComboBox::show_all(bool show_all)
{
    m_show_all = show_all;
    update();
}

void PresetComboBox::update()
{
    int n = this->GetSelection();
    this->update(n < 0 ? "" : into_u8(this->GetString(n)));
}

void PresetComboBox::update_from_bundle()
{
    this->update(m_collection->get_selected_preset().name);
}

void PresetComboBox::msw_rescale()
{
    m_em_unit = em_unit(this);
}

void PresetComboBox::sys_color_changed()
{
    m_bitmapCompatible = get_bmp_bundle("flag_green");
    m_bitmapIncompatible = get_bmp_bundle("flag_red");
    wxGetApp().UpdateDarkUI(this);

    // update the control to redraw the icons
    update();
}

void PresetComboBox::fill_width_height()
{
    icon_height     = 16;
    norm_icon_width = 16;

    thin_icon_width = 8;
    wide_icon_width = norm_icon_width + thin_icon_width;

    null_icon_width = 2 * norm_icon_width;

    space_icon_width      = 2;
    thin_space_icon_width = 4;
    wide_space_icon_width = 6;
}

wxString PresetComboBox::separator(const std::string& label)
{
    return wxString::FromUTF8(separator_head()) + _(label) + wxString::FromUTF8(separator_tail());
}


wxBitmapBundle* PresetComboBox::get_bmp(  std::string bitmap_key, bool wide_icons, const std::string& main_icon_name,
                                    bool is_compatible/* = true*/, bool is_system/* = false*/, bool is_single_bar/* = false*/,
                                    const std::string& filament_rgb/* = ""*/, const std::string& extruder_rgb/* = ""*/, const std::string& material_rgb/* = ""*/)
{
    // If the filament preset is not compatible and there is a "red flag" icon loaded, show it left
    // to the filament color image.
    if (wide_icons)
        bitmap_key += is_compatible ? ",cmpt" : ",ncmpt";

    bitmap_key += is_system ? ",syst" : ",nsyst";
    bitmap_key += ",h" + std::to_string(icon_height);
    bool dark_mode = wxGetApp().dark_mode();
    if (dark_mode)
        bitmap_key += ",dark";
    bitmap_key += material_rgb;

    wxBitmapBundle* bmp_bndl = bitmap_cache().find_bndl(bitmap_key);
    if (bmp_bndl == nullptr) {
        // Create the bitmap with color bars.
        std::vector<wxBitmapBundle*> bmps;
        if (wide_icons)
            // Paint a red flag for incompatible presets.
            bmps.emplace_back(is_compatible ? get_empty_bmp_bundle(norm_icon_width, icon_height) : m_bitmapIncompatible);

        if (m_type == Preset::TYPE_FILAMENT && !filament_rgb.empty()) {
            // Paint the color bars.
            bmps.emplace_back(get_solid_bmp_bundle(is_single_bar ? wide_icon_width : norm_icon_width, icon_height, filament_rgb));
            if (!is_single_bar)
                bmps.emplace_back(get_solid_bmp_bundle(thin_icon_width, icon_height, extruder_rgb));
            // Paint a lock at the system presets.
            bmps.emplace_back(get_empty_bmp_bundle(space_icon_width, icon_height));
        }
        else
        {
            // Paint the color bars.
            bmps.emplace_back(get_empty_bmp_bundle(thin_space_icon_width, icon_height));
            if (m_type == Preset::TYPE_SLA_MATERIAL)
                bmps.emplace_back(bitmap_cache().from_svg(main_icon_name, 16, 16, dark_mode, material_rgb));
            else
                bmps.emplace_back(get_bmp_bundle(main_icon_name));
            // Paint a lock at the system presets.
            bmps.emplace_back(get_empty_bmp_bundle(wide_space_icon_width, icon_height));
        }
        bmps.emplace_back(is_system ? get_bmp_bundle("lock_closed") : get_empty_bmp_bundle(norm_icon_width, icon_height));
        bmp_bndl = bitmap_cache().insert_bndl(bitmap_key, bmps);
    }

    return bmp_bndl;
}

wxBitmapBundle* PresetComboBox::get_bmp(  std::string bitmap_key, const std::string& main_icon_name, const std::string& next_icon_name,
                                    bool is_enabled/* = true*/, bool is_compatible/* = true*/, bool is_system/* = false*/)
{
    bitmap_key += !is_enabled ? "_disabled" : "";
    bitmap_key += is_compatible ? ",cmpt" : ",ncmpt";
    bitmap_key += is_system ? ",syst" : ",nsyst";
    bitmap_key += ",h" + std::to_string(icon_height);
    if (wxGetApp().dark_mode())
        bitmap_key += ",dark";

    wxBitmapBundle* bmp = bitmap_cache().find_bndl(bitmap_key);
    if (bmp == nullptr) {
        // Create the bitmap with color bars.
        std::vector<wxBitmapBundle*> bmps;
        bmps.emplace_back(m_type == Preset::TYPE_PRINTER ? get_bmp_bundle(main_icon_name) :
                          is_compatible ? m_bitmapCompatible : m_bitmapIncompatible);
        // Paint a lock at the system presets.
        bmps.emplace_back(is_system ? get_bmp_bundle(next_icon_name) : get_empty_bmp_bundle(norm_icon_width, icon_height));
        bmp = bitmap_cache().insert_bndl(bitmap_key, bmps);
    }

    return bmp;
}

wxBitmapBundle PresetComboBox::NullBitmapBndl()
{
    assert(null_icon_width > 0);
    return *get_empty_bmp_bundle(null_icon_width, icon_height);
}

bool PresetComboBox::is_selected_physical_printer()
{
    auto selected_item = this->GetSelection();
    auto marker = reinterpret_cast<Marker>(this->GetClientData(selected_item));
    return marker == LABEL_ITEM_PHYSICAL_PRINTER;
}

bool PresetComboBox::selection_is_changed_according_to_physical_printers()
{
    if (m_type != Preset::TYPE_PRINTER)
        return false;

    const std::string           selected_string     = into_u8(this->GetString(this->GetSelection()));
    PhysicalPrinterCollection&  physical_printers   = m_preset_bundle->physical_printers;
    Tab*                        tab                 = wxGetApp().get_tab(Preset::TYPE_PRINTER);

    if (!is_selected_physical_printer()) {
        if (!physical_printers.has_selection())
            return false;

        const bool is_changed = selected_string == physical_printers.get_selected_printer_preset_name();
        physical_printers.unselect_printer();
        if (is_changed)
            tab->select_preset(selected_string);
        return is_changed;
    }

    std::string old_printer_full_name, old_printer_preset;
    if (physical_printers.has_selection()) {
        old_printer_full_name = physical_printers.get_selected_full_printer_name();
        old_printer_preset = physical_printers.get_selected_printer_preset_name();
    }
    else
        old_printer_preset = m_collection->get_edited_preset().name;
    // Select related printer preset on the Printer Settings Tab 
    physical_printers.select_printer(selected_string);
    std::string preset_name = physical_printers.get_selected_printer_preset_name();

    // if new preset wasn't selected, there is no need to call update preset selection
    if (old_printer_preset == preset_name) {
        tab->update_preset_choice();
        // update action buttons to show/hide "Send to" button
        wxGetApp().plater()->show_action_buttons();

        // we need just to update according Plater<->Tab PresetComboBox 
        if (dynamic_cast<PlaterPresetComboBox*>(this)!=nullptr) {
            // Synchronize config.ini with the current selections.
            m_preset_bundle->export_selections(*wxGetApp().app_config);
            this->update();
        }
        else if (dynamic_cast<TabPresetComboBox*>(this)!=nullptr)
            wxGetApp().sidebar().update_presets(m_type);

        return true;
    }

    if (tab)
        tab->select_preset(preset_name, false, old_printer_full_name);
    return true;
}

// ---------------------------------
// ***  PlaterPresetComboBox  ***
// ---------------------------------

PlaterPresetComboBox::PlaterPresetComboBox(wxWindow *parent, Preset::Type preset_type) :
    PresetComboBox(parent, preset_type, wxSize(15 * wxGetApp().em_unit(), -1))
{
    if (m_type == Preset::TYPE_FILAMENT)
    {
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &event) {
            const Filament* selected_filament = m_preset_bundle->extruders_filaments[m_extruder_idx].get_selected_filament();
            // Wide icons are shown if the currently selected preset is not compatible with the current printer,
            // and red flag is drown in front of the selected preset.
            const bool wide_icons = selected_filament && !selected_filament->is_compatible;
            float scale = m_em_unit*0.1f;

            int shifl_Left = wide_icons ? int(scale * 16 + 0.5) : 0;
#if defined(wxBITMAPCOMBOBOX_OWNERDRAWN_BASED)
            shifl_Left  += int(scale * 4 + 0.5f); // IMAGE_SPACING_RIGHT = 4 for wxBitmapComboBox -> Space left of image
#endif
            int icon_right_pos = shifl_Left + int(scale * (24+4) + 0.5);
            int mouse_pos = event.GetLogicalPosition(wxClientDC(this)).x;
            if (mouse_pos < shifl_Left || mouse_pos > icon_right_pos ) {
                // Let the combo box process the mouse click.
                event.Skip();
                return;
            }

            // Swallow the mouse click and open the color picker.
            change_extruder_color();
        });
    }

    edit_btn = new ScalableButton(parent, wxID_ANY, "cog");
    edit_btn->SetToolTip(_L("Click to edit preset"));

    edit_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent)
    {
        // In a case of a physical printer, for its editing open PhysicalPrinterDialog
        if (m_type == Preset::TYPE_PRINTER
#ifdef __linux__
            // To edit extruder color from the sidebar
            || m_type == Preset::TYPE_FILAMENT
#endif //__linux__
            )
            show_edit_menu();
        else
            switch_to_tab();
    });
}

PlaterPresetComboBox::~PlaterPresetComboBox()
{
    if (edit_btn)
        edit_btn->Destroy();
}

static void run_wizard(ConfigWizard::StartPage sp)
{
    wxGetApp().run_wizard(ConfigWizard::RR_USER, sp);
}

void PlaterPresetComboBox::OnSelect(wxCommandEvent &evt)
{
    auto selected_item = evt.GetSelection();

    auto marker = reinterpret_cast<Marker>(this->GetClientData(selected_item));
    if (marker >= LABEL_ITEM_MARKER && marker < LABEL_ITEM_MAX) {
        this->SetSelection(m_last_selected);
        evt.StopPropagation();
        if (marker == LABEL_ITEM_MARKER)
            return;
        if (marker == LABEL_ITEM_WIZARD_PRINTERS)
            show_add_menu();
        else {
            ConfigWizard::StartPage sp = ConfigWizard::SP_WELCOME;
            switch (marker) {
            case LABEL_ITEM_WIZARD_FILAMENTS: sp = ConfigWizard::SP_FILAMENTS; break;
            case LABEL_ITEM_WIZARD_MATERIALS: sp = ConfigWizard::SP_MATERIALS; break;
            default: break;
            }
            wxTheApp->CallAfter([sp]() { run_wizard(sp); });
        }
        return;
    }
    else if (marker == LABEL_ITEM_PHYSICAL_PRINTER || m_last_selected != selected_item || m_collection->current_is_dirty())
        m_last_selected = selected_item;
        
    evt.Skip();
}

std::string PlaterPresetComboBox::get_selected_ph_printer_name() const
{
    if (m_type != Preset::TYPE_PRINTER)
        return {};

    const PhysicalPrinterCollection& physical_printers = m_preset_bundle->physical_printers;
    if (physical_printers.has_selection())
        return physical_printers.get_selected_full_printer_name();

    return {};
}

void PlaterPresetComboBox::switch_to_tab()
{
    Tab* tab = wxGetApp().get_tab(m_type);
    if (!tab)
        return;

    if (int page_id = wxGetApp().tab_panel()->FindPage(tab); page_id != wxNOT_FOUND)
    {
        //In a case of a multi-material printing, for editing another Filament Preset
        //it's needed to select this preset for the "Filament settings" Tab
        if (m_type == Preset::TYPE_FILAMENT && wxGetApp().extruders_edited_cnt() > 1 && 
            !dynamic_cast<TabFilament*>(wxGetApp().get_tab(m_type))->set_active_extruder(m_extruder_idx))
            // do nothing, if we can't set new extruder and select new preset
            return;

        wxGetApp().tab_panel()->SetSelection(page_id);
        // Switch to Settings NotePad
        wxGetApp().mainframe->select_tab();
    }
}

void PlaterPresetComboBox::change_extruder_color()
{
    // get current color
    DynamicPrintConfig* cfg = wxGetApp().get_tab(Preset::TYPE_PRINTER)->get_config();
    auto colors = static_cast<ConfigOptionStrings*>(cfg->option("extruder_colour")->clone());
    wxColour clr(colors->values[m_extruder_idx]);
    if (!clr.IsOk())
        clr = wxColour(0, 0, 0); // Don't set alfa to transparence

    auto data = new wxColourData();
    data->SetChooseFull(1);
    data->SetColour(clr);

    wxColourDialog dialog(this, data);
    dialog.CenterOnParent();
    if (dialog.ShowModal() == wxID_OK)
    {
        colors->values[m_extruder_idx] = dialog.GetColourData().GetColour().GetAsString(wxC2S_HTML_SYNTAX).ToStdString();

        DynamicPrintConfig cfg_new = *cfg;
        cfg_new.set_key_value("extruder_colour", colors);

        wxGetApp().get_tab(Preset::TYPE_PRINTER)->load_config(cfg_new);
        this->update();
        wxGetApp().plater()->on_config_change(cfg_new);
    }
}

void PlaterPresetComboBox::show_add_menu()
{
    wxMenu* menu = new wxMenu();

    append_menu_item(menu, wxID_ANY, _L("Add/Remove presets"), "",
        [](wxCommandEvent&) {
            wxTheApp->CallAfter([]() { run_wizard(ConfigWizard::SP_PRINTERS); });
        }, "edit_uni", menu, []() { return true; }, wxGetApp().plater());

    append_menu_item(menu, wxID_ANY, _L("Add physical printer"), "",
        [this](wxCommandEvent&) {
            PhysicalPrinterDialog dlg(this->GetParent(), wxEmptyString);
            if (dlg.ShowModal() == wxID_OK)
                update();
        }, "edit_uni", menu, []() { return true; }, wxGetApp().plater());

    wxGetApp().plater()->PopupMenu(menu);
}

void PlaterPresetComboBox::show_edit_menu()
{
    wxMenu* menu = new wxMenu();

    append_menu_item(menu, wxID_ANY, _L("Edit preset"), "",
        [this](wxCommandEvent&) { this->switch_to_tab(); }, "cog", menu, []() { return true; }, wxGetApp().plater());

#ifdef __linux__
    // To edit extruder color from the sidebar
    if (m_type == Preset::TYPE_FILAMENT) {
        append_menu_item(menu, wxID_ANY, _L("Change extruder color"), "",
            [this](wxCommandEvent&) { this->change_extruder_color(); }, "funnel", menu, []() { return true; }, wxGetApp().plater());
        wxGetApp().plater()->PopupMenu(menu);
        return;
    }
#endif //__linux__

    if (this->is_selected_physical_printer()) {
        append_menu_item(menu, wxID_ANY, _L("Edit physical printer"), "",
            [this](wxCommandEvent&) { this->edit_physical_printer(); }, "cog", menu, []() { return true; }, wxGetApp().plater());

        const PhysicalPrinter& pp = m_preset_bundle->physical_printers.get_selected_printer();
        std::string host = pp.config.opt_string("print_host");
        if (!host.empty()) {
            append_menu_item(menu, wxID_ANY, _L("Open the physical printer URL"), "",
                [this](wxCommandEvent&) { this->open_physical_printer_url(); }, "open_browser", menu, []() { return true; }, wxGetApp().plater());
        }
        

        append_menu_item(menu, wxID_ANY, _L("Delete physical printer"), "",
            [this](wxCommandEvent&) { this->del_physical_printer(); }, "cross", menu, []() { return true; }, wxGetApp().plater());
    }
    else
        append_menu_item(menu, wxID_ANY, _L("Add/Remove presets"), "",
            [](wxCommandEvent&) {
                wxTheApp->CallAfter([]() { run_wizard(ConfigWizard::SP_PRINTERS); });
            }, "edit_uni", menu, []() { return true; }, wxGetApp().plater());

    append_menu_item(menu, wxID_ANY, _L("Add physical printer"), "",
        [this](wxCommandEvent&) { this->add_physical_printer(); }, "edit_uni", menu, []() { return true; }, wxGetApp().plater());

    wxGetApp().plater()->PopupMenu(menu);
}

wxString PlaterPresetComboBox::get_preset_name(const Preset& preset)
{
    std::string name = preset.alias.empty() ? preset.name : (preset.vendor && preset.vendor->templates_profile ? preset.name : preset.alias);
    return from_u8(name + suffix(preset));
}

// Only the compatible presets are shown.
// If an incompatible preset is selected, it is shown as well.
void PlaterPresetComboBox::update()
{
    if (m_type == Preset::TYPE_FILAMENT &&
        (m_preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA ||
        m_preset_bundle->extruders_filaments.size() <= (size_t)m_extruder_idx) )
        return;

    // Otherwise fill in the list from scratch.
    this->Freeze();
    this->Clear();
    invalidate_selection();

    const ExtruderFilaments& extruder_filaments  = m_preset_bundle->extruders_filaments[m_extruder_idx >= 0 ? m_extruder_idx : 0];

    const Preset* selected_filament_preset = nullptr;
    std::string extruder_color;
    if (m_type == Preset::TYPE_FILAMENT) {
        extruder_color = m_preset_bundle->printers.get_edited_preset().config.opt_string("extruder_colour", (unsigned int)m_extruder_idx);
        if (!can_decode_color(extruder_color))
            // Extruder color is not defined.
            extruder_color.clear();
        selected_filament_preset = extruder_filaments.get_selected_preset();
        assert(selected_filament_preset);
    }

    // Show wide icons if the currently selected preset is not compatible with the current printer,
    // and draw a red flag in front of the selected preset.
    bool wide_icons = m_type == Preset::TYPE_FILAMENT ? 
                      extruder_filaments.get_selected_filament()     && !extruder_filaments.get_selected_filament()->is_compatible :
                      m_collection->get_selected_idx() != size_t(-1) && !m_collection->get_selected_preset().is_compatible;

    null_icon_width = (wide_icons ? 3 : 2) * norm_icon_width + thin_space_icon_width + wide_space_icon_width;

    std::map<wxString, wxBitmapBundle*> nonsys_presets;
    std::map<wxString, wxBitmapBundle*> template_presets;

    const bool allow_templates = !wxGetApp().app_config->get_bool("no_templates");

    wxString selected_user_preset;
    wxString tooltip;
    const std::deque<Preset>& presets = m_collection->get_presets();

    if (!presets.front().is_visible)
        this->set_label_marker(this->Append(separator(L("System presets")), NullBitmapBndl()));

    for (size_t i = presets.front().is_visible ? 0 : m_collection->num_default_presets(); i < presets.size(); ++i) 
    {
        const Preset& preset = presets[i];
        const bool is_selected =  m_type == Preset::TYPE_FILAMENT ?
                            selected_filament_preset->name == preset.name :
                            // The case, when some physical printer is selected
                            m_type == Preset::TYPE_PRINTER && m_preset_bundle->physical_printers.has_selection() ? false :
                            i == m_collection->get_selected_idx();

        const bool is_compatible = m_type == Preset::TYPE_FILAMENT ? extruder_filaments.filament(i).is_compatible : preset.is_compatible;

        if (!preset.is_visible || (!is_compatible && !is_selected))
            continue;

        std::string bitmap_key, filament_rgb, extruder_rgb, material_rgb;
        std::string bitmap_type_name = bitmap_key = m_type == Preset::TYPE_PRINTER && preset.printer_technology() == ptSLA ? "sla_printer" : m_main_bitmap_name;

        bool single_bar = false;
        if (m_type == Preset::TYPE_FILAMENT)
        {
            // Assign an extruder color to the selected item if the extruder color is defined.
            filament_rgb = is_selected ? selected_filament_preset->config.opt_string("filament_colour", 0) : 
                                         preset.config.opt_string("filament_colour", 0);
            extruder_rgb = (is_selected && !extruder_color.empty()) ? extruder_color : filament_rgb;
            single_bar = filament_rgb == extruder_rgb;

            bitmap_key += single_bar ? filament_rgb : filament_rgb + extruder_rgb;
        }
        else if (m_type == Preset::TYPE_SLA_MATERIAL) {
            material_rgb = is_selected ? m_preset_bundle->sla_materials.get_edited_preset().config.opt_string("material_colour") : preset.config.opt_string("material_colour");
            if (material_rgb.empty())
                material_rgb = print_config_def.get("material_colour")->get_default_value<ConfigOptionString>()->value;
        }

        auto bmp = get_bmp(bitmap_key, wide_icons, bitmap_type_name,
                                is_compatible, preset.is_system || preset.is_default, 
                                single_bar, filament_rgb, extruder_rgb, material_rgb);
        assert(bmp);

        const std::string name = preset.alias.empty() ? preset.name : preset.alias;
        if (preset.is_default || preset.is_system) {
            if (preset.vendor && preset.vendor->templates_profile) {
                if (allow_templates) {
                    template_presets.emplace(get_preset_name(preset), bmp);
                    if (is_selected) {
                        selected_user_preset = get_preset_name(preset);
                        tooltip = from_u8(preset.name);
                    }
                }
            } else {
                Append(get_preset_name(preset), *bmp);
                validate_selection(is_selected);
                if (is_selected)
                    tooltip = from_u8(preset.name);
            }
        }
        else
        {
            nonsys_presets.emplace(get_preset_name(preset), bmp);
            if (is_selected) {
                selected_user_preset = get_preset_name(preset);
                tooltip = from_u8(preset.name);
            }
        }
        if (i + 1 == m_collection->num_default_presets())
            set_label_marker(Append(separator(L("System presets")), NullBitmapBndl()));
    }

    
    if (!nonsys_presets.empty())
    {
        set_label_marker(Append(separator(L("User presets")), NullBitmapBndl()));
        for (std::map<wxString, wxBitmapBundle*>::iterator it = nonsys_presets.begin(); it != nonsys_presets.end(); ++it) {
            Append(it->first, *it->second);
            validate_selection(it->first == selected_user_preset);
        }
    }

    if (!template_presets.empty()) {
        set_label_marker(Append(separator(L("Template presets")), wxNullBitmap));
        for (std::map<wxString, wxBitmapBundle*>::iterator it = template_presets.begin(); it != template_presets.end(); ++it) {
            Append(it->first, *it->second);
            validate_selection(it->first == selected_user_preset);
        }
    }

    if (m_type == Preset::TYPE_PRINTER)
    {
        // add Physical printers, if any exists
        if (!m_preset_bundle->physical_printers.empty()) {
            set_label_marker(Append(separator(L("Physical printers")), NullBitmapBndl()));
            const PhysicalPrinterCollection& ph_printers = m_preset_bundle->physical_printers;

            for (PhysicalPrinterCollection::ConstIterator it = ph_printers.begin(); it != ph_printers.end(); ++it) {
                for (const std::string& preset_name : it->get_preset_names()) {
                    Preset* preset = m_collection->find_preset(preset_name);
                    if (!preset || !preset->is_visible)
                        continue;
                    std::string main_icon_name, bitmap_key = main_icon_name = preset->printer_technology() == ptSLA ? "sla_printer" : m_main_bitmap_name;
                    auto bmp = get_bmp(main_icon_name, wide_icons, main_icon_name);
                    assert(bmp);

                    set_label_marker(Append(from_u8(it->get_full_name(preset_name) + suffix(preset)), *bmp), LABEL_ITEM_PHYSICAL_PRINTER);
                    validate_selection(ph_printers.is_selected(it, preset_name));
                }
            }
        }
    }

    if (m_type == Preset::TYPE_PRINTER || m_type == Preset::TYPE_FILAMENT || m_type == Preset::TYPE_SLA_MATERIAL) {
        auto bmp = get_bmp("edit_preset_list", wide_icons, "edit_uni");
        assert(bmp);

        if (m_type == Preset::TYPE_FILAMENT)
            set_label_marker(Append(separator(L("Add/Remove filaments")), *bmp), LABEL_ITEM_WIZARD_FILAMENTS);
        else if (m_type == Preset::TYPE_SLA_MATERIAL)
            set_label_marker(Append(separator(L("Add/Remove materials")), *bmp), LABEL_ITEM_WIZARD_MATERIALS);
        else
            set_label_marker(Append(separator(L("Add/Remove printers")), *bmp), LABEL_ITEM_WIZARD_PRINTERS);
    }

    update_selection();
    Thaw();

    if (!tooltip.IsEmpty()) {
#ifdef __WXMSW__
        // From the Windows 2004 the tooltip for preset combobox doesn't work after next call of SetTooltip()
        // (There was an issue, when tooltip doesn't appears after changing of the preset selection)
        // But this workaround seems to work: We should to kill tooltip and than set new tooltip value
        // See, https://groups.google.com/g/wx-users/c/mOEe3fgHrzk
        SetToolTip(NULL);
#endif
        SetToolTip(tooltip);
    }

#ifdef __WXMSW__
    // Use this part of code just on Windows to avoid of some layout issues on Linux
    // see https://github.com/prusa3d/PrusaSlicer/issues/5163 and https://github.com/prusa3d/PrusaSlicer/issues/5505
    // Update control min size after rescale (changed Display DPI under MSW)
    if (GetMinWidth() != 20 * m_em_unit)
        SetMinSize(wxSize(20 * m_em_unit, GetSize().GetHeight()));
#endif //__WXMSW__
}

void PlaterPresetComboBox::msw_rescale()
{
    PresetComboBox::msw_rescale();
#ifdef __WXMSW__
    // Use this part of code just on Windows to avoid of some layout issues on Linux
    // see https://github.com/prusa3d/PrusaSlicer/issues/5163 and https://github.com/prusa3d/PrusaSlicer/issues/5505
    // Update control min size after rescale (changed Display DPI under MSW)
    if (GetMinWidth() != 20 * m_em_unit)
        SetMinSize(wxSize(20 * m_em_unit, GetSize().GetHeight()));
#endif //__WXMSW__
}

void PlaterPresetComboBox::sys_color_changed()
{
    PresetComboBox::sys_color_changed();
    edit_btn->sys_color_changed();
}

// ---------------------------------
// ***  TabPresetComboBox  ***
// ---------------------------------

TabPresetComboBox::TabPresetComboBox(wxWindow* parent, Preset::Type preset_type) :
    PresetComboBox(parent, preset_type, wxSize(35 * wxGetApp().em_unit(), -1))
{
}

void TabPresetComboBox::OnSelect(wxCommandEvent &evt)
{
    // see https://github.com/prusa3d/PrusaSlicer/issues/3889
    // Under OSX: in case of use of a same names written in different case (like "ENDER" and "Ender")
    // m_presets_choice->GetSelection() will return first item, because search in PopupListCtrl is case-insensitive.
    // So, use GetSelection() from event parameter 
    auto selected_item = evt.GetSelection();

    auto marker = reinterpret_cast<Marker>(this->GetClientData(selected_item));
    if (marker >= LABEL_ITEM_DISABLED && marker < LABEL_ITEM_MAX) {
        this->SetSelection(m_last_selected);
        if (marker == LABEL_ITEM_WIZARD_PRINTERS)
            wxTheApp->CallAfter([this]() {
            run_wizard(ConfigWizard::SP_PRINTERS);

            // update combobox if its parent is a PhysicalPrinterDialog
            PhysicalPrinterDialog* parent = dynamic_cast<PhysicalPrinterDialog*>(this->GetParent());
            if (parent != nullptr)
                update();
        });
    }
    else if (on_selection_changed && (m_last_selected != selected_item || m_collection->current_is_dirty())) {
        m_last_selected = selected_item;
        on_selection_changed(selected_item);
    }

    evt.StopPropagation();
#ifdef __WXMSW__
    // From the Win 2004 preset combobox lose a focus after change the preset selection
    // and that is why the up/down arrow doesn't work properly
    // (see https://github.com/prusa3d/PrusaSlicer/issues/5531 ).
    // So, set the focus to the combobox explicitly
    this->SetFocus();
#endif
}

wxString TabPresetComboBox::get_preset_name(const Preset& preset)
{
    return from_u8(preset.name + suffix(preset));
}

// Update the choice UI from the list of presets.
// If show_incompatible, all presets are shown, otherwise only the compatible presets are shown.
// If an incompatible preset is selected, it is shown as well.
void TabPresetComboBox::update()
{
    Freeze();
    Clear();
    invalidate_selection();

    const ExtruderFilaments& extruder_filaments = m_preset_bundle->extruders_filaments[m_active_extruder_idx];

    const std::deque<Preset>& presets = m_collection->get_presets();

    std::map<wxString, std::pair<wxBitmapBundle*, bool>> nonsys_presets;
    std::map<wxString, std::pair<wxBitmapBundle*, bool>> template_presets;

    const bool allow_templates = !wxGetApp().app_config->get_bool("no_templates");

    wxString selected = "";
    if (!presets.front().is_visible)
        set_label_marker(Append(separator(L("System presets")), NullBitmapBndl()));
    size_t idx_selected = m_type == Preset::TYPE_FILAMENT ? extruder_filaments.get_selected_idx() : m_collection->get_selected_idx();

    if (m_type == Preset::TYPE_PRINTER && m_preset_bundle->physical_printers.has_selection()) {
        std::string sel_preset_name = m_preset_bundle->physical_printers.get_selected_printer_preset_name();
        Preset* preset = m_collection->find_preset(sel_preset_name);
        if (!preset)
            m_preset_bundle->physical_printers.unselect_printer();
    }

    for (size_t i = presets.front().is_visible ? 0 : m_collection->num_default_presets(); i < presets.size(); ++i)
    {
        const Preset& preset = presets[i];

        const bool is_compatible = m_type == Preset::TYPE_FILAMENT ? extruder_filaments.filament(i).is_compatible : preset.is_compatible;

        if (!preset.is_visible || (!show_incompatible && !is_compatible && i != idx_selected))
            continue;
        
        // marker used for disable incompatible printer models for the selected physical printer
        bool is_enabled = true;

        std::string bitmap_key = "tab";
        if (m_type == Preset::TYPE_PRINTER) {
            bitmap_key += "_printer";
            if (preset.printer_technology() == ptSLA)
                bitmap_key += "_sla";
        }
        std::string main_icon_name = m_type == Preset::TYPE_PRINTER && preset.printer_technology() == ptSLA ? "sla_printer" : m_main_bitmap_name;

        auto bmp = get_bmp(bitmap_key, main_icon_name, "lock_closed", is_enabled, is_compatible, preset.is_system || preset.is_default);
        assert(bmp);

        if (preset.is_default || preset.is_system) {
            if (preset.vendor && preset.vendor->templates_profile) {
                if (allow_templates) {
                    template_presets.emplace(get_preset_name(preset), std::pair<wxBitmapBundle*, bool>(bmp, is_enabled));
                    if (i == idx_selected)
                        selected = get_preset_name(preset);
                }
            } else {
                int item_id = Append(get_preset_name(preset), *bmp);
                if (!is_enabled)
                    set_label_marker(item_id, LABEL_ITEM_DISABLED);
                validate_selection(i == idx_selected);
            }

            
        }
        else
        {
            std::pair<wxBitmapBundle*, bool> pair(bmp, is_enabled);
            nonsys_presets.emplace(get_preset_name(preset), std::pair<wxBitmapBundle*, bool>(bmp, is_enabled));
            if (i == idx_selected)
                selected = get_preset_name(preset);
        }
        if (i + 1 == m_collection->num_default_presets())
            set_label_marker(Append(separator(L("System presets")), NullBitmapBndl()));
    }
   
    if (!nonsys_presets.empty())
    {
        set_label_marker(Append(separator(L("User presets")), NullBitmapBndl()));
        for (std::map<wxString, std::pair<wxBitmapBundle*, bool>>::iterator it = nonsys_presets.begin(); it != nonsys_presets.end(); ++it) {
            int item_id = Append(it->first, *it->second.first);
            bool is_enabled = it->second.second;
            if (!is_enabled)
                set_label_marker(item_id, LABEL_ITEM_DISABLED);
            validate_selection(it->first == selected);
        }
    }

    if (!template_presets.empty()) {
        set_label_marker(Append(separator(L("Template presets")), wxNullBitmap));
        for (std::map<wxString, std::pair<wxBitmapBundle*, bool>>::iterator it = template_presets.begin(); it != template_presets.end(); ++it) {
            int item_id = Append(it->first, *it->second.first);
            bool is_enabled = it->second.second;
            if (!is_enabled)
                set_label_marker(item_id, LABEL_ITEM_DISABLED);
            validate_selection(it->first == selected);
        }
    }
    
    if (m_type == Preset::TYPE_PRINTER)
    {
        // add Physical printers, if any exists
        if (!m_preset_bundle->physical_printers.empty()) {
            set_label_marker(Append(separator(L("Physical printers")), NullBitmapBndl()));
            const PhysicalPrinterCollection& ph_printers = m_preset_bundle->physical_printers;

            for (PhysicalPrinterCollection::ConstIterator it = ph_printers.begin(); it != ph_printers.end(); ++it) {
                for (const std::string& preset_name : it->get_preset_names()) {
                    Preset* preset = m_collection->find_preset(preset_name);
                    if (!preset || !preset->is_visible)
                        continue;
                    std::string main_icon_name = preset->printer_technology() == ptSLA ? "sla_printer" : m_main_bitmap_name;

                    auto bmp = get_bmp(main_icon_name, main_icon_name, "", true, true, false);
                    assert(bmp);

                    set_label_marker(Append(from_u8(it->get_full_name(preset_name) + suffix(preset)), *bmp), LABEL_ITEM_PHYSICAL_PRINTER);
                    validate_selection(ph_printers.is_selected(it, preset_name));
                }
            }
        }

        // add "Add/Remove printers" item
        std::string icon_name = "edit_uni";
        auto bmp = get_bmp("edit_preset_list, tab,", icon_name, "");
        assert(bmp);

        set_label_marker(Append(separator(L("Add/Remove printers")), *bmp), LABEL_ITEM_WIZARD_PRINTERS);
    }

    update_selection();
    Thaw();
}

void TabPresetComboBox::msw_rescale()
{
    PresetComboBox::msw_rescale();
    wxSize sz = wxSize(35 * m_em_unit, -1);
    SetMinSize(sz);
    SetSize(sz);
}

void TabPresetComboBox::update_dirty()
{
    // 1) Update the dirty flag of the current preset.
    m_collection->update_dirty();

    // 2) Update the labels.
    wxWindowUpdateLocker noUpdates(this);
    for (unsigned int ui_id = 0; ui_id < GetCount(); ++ui_id) {
        auto marker = reinterpret_cast<Marker>(this->GetClientData(ui_id));
        if (marker >= LABEL_ITEM_MARKER)
            continue;

        std::string   old_label = GetString(ui_id).utf8_str().data();
        std::string   preset_name = Preset::remove_suffix_modified(old_label);
        std::string   ph_printer_name;

        if (marker == LABEL_ITEM_PHYSICAL_PRINTER) {
            ph_printer_name = PhysicalPrinter::get_short_name(preset_name);
            preset_name = PhysicalPrinter::get_preset_name(preset_name);
        }
            
        Preset* preset = m_collection->find_preset(preset_name, false);
        if (preset) {
            std::string new_label = preset->name + suffix(preset);

            if (marker == LABEL_ITEM_PHYSICAL_PRINTER)
                new_label = ph_printer_name + PhysicalPrinter::separator() + new_label;

            if (old_label != new_label)
                SetString(ui_id, from_u8(new_label));
        }
    }
#ifdef __APPLE__
    // wxWidgets on OSX do not upload the text of the combo box line automatically.
    // Force it to update by re-selecting.
    SetSelection(GetSelection());
#endif /* __APPLE __ */
}

}}    // namespace Slic3r::GUI
