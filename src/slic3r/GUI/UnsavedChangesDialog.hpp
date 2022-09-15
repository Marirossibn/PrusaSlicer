#ifndef slic3r_UnsavedChangesDialog_hpp_
#define slic3r_UnsavedChangesDialog_hpp_

#include <wx/dataview.h>
#include <map>
#include <vector>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include "libslic3r/PresetBundle.hpp"

class ScalableButton;
class wxStaticText;

namespace Slic3r {
namespace GUI{

wxDECLARE_EVENT(EVT_DIFF_DIALOG_TRANSFER, SimpleEvent);

// ----------------------------------------------------------------------------
//                  ModelNode: a node inside DiffModel
// ----------------------------------------------------------------------------

class ModelNode;
class PresetComboBox;
class MainFrame;
using ModelNodePtrArray = std::vector<std::unique_ptr<ModelNode>>;

// On all of 3 different platforms Bitmap+Text icon column looks different 
// because of Markup text is missed or not implemented.
// As a temporary workaround, we will use:
// MSW - DataViewBitmapText (our custom renderer wxBitmap + wxString, supported Markup text)
// OSX - -//-, but Markup text is not implemented right now
// GTK - wxDataViewIconText (wxWidgets for GTK renderer wxIcon + wxString, supported Markup text)
class ModelNode
{
    wxWindow*           m_parent_win{ nullptr };

    ModelNode*          m_parent;
    ModelNodePtrArray   m_children;
    wxBitmap            m_empty_bmp;
    Preset::Type        m_preset_type {Preset::TYPE_INVALID};

    std::string         m_icon_name;
    // saved values for colors if they exist
    wxString            m_old_color;
    wxString            m_mod_color;
    wxString            m_new_color;

#ifdef __linux__
    wxIcon              get_bitmap(const wxString& color);
#else
    wxBitmap            get_bitmap(const wxString& color);
#endif //__linux__

public:

    bool        m_toggle {true};
#ifdef __linux__
    wxIcon      m_icon;
    wxIcon      m_old_color_bmp;
    wxIcon      m_mod_color_bmp;
    wxIcon      m_new_color_bmp;
#else
    wxBitmap    m_icon;
    wxBitmap    m_old_color_bmp;
    wxBitmap    m_mod_color_bmp;
    wxBitmap    m_new_color_bmp;
#endif //__linux__
    wxString    m_text;
    wxString    m_old_value;
    wxString    m_mod_value;
    wxString    m_new_value;

    // TODO/FIXME:
    // the GTK version of wxDVC (in particular wxDataViewCtrlInternal::ItemAdded)
    // needs to know in advance if a node is or _will be_ a container.
    // Thus implementing:
    //   bool IsContainer() const
    //    { return m_children.size()>0; }
    // doesn't work with wxGTK when DiffModel::AddToClassical is called
    // AND the classical node was removed (a new node temporary without children
    // would be added to the control)
    bool                m_container {true};

    // preset(root) node
    ModelNode(Preset::Type preset_type, wxWindow* parent_win, const wxString& text, const std::string& icon_name, const wxString& new_val_column_text);

    // category node
    ModelNode(ModelNode* parent, const wxString& text, const std::string& icon_name);

    // group node
    ModelNode(ModelNode* parent, const wxString& text);

    // option node
    ModelNode(ModelNode* parent, const wxString& text, const wxString& old_value, const wxString& mod_value, const wxString& new_value);

    bool                IsContainer() const         { return m_container; }
    bool                IsToggled() const           { return m_toggle; }
    void                Toggle(bool toggle = true)  { m_toggle = toggle; }
    bool                IsRoot() const              { return m_parent == nullptr; }
    Preset::Type        type() const                { return m_preset_type; }
    const wxString&     text() const                { return m_text; }

    ModelNode*          GetParent()                 { return m_parent; }
    ModelNodePtrArray&  GetChildren()               { return m_children; }
    ModelNode*          GetNthChild(unsigned int n) { return m_children[n].get(); }
    unsigned int        GetChildCount() const       { return (unsigned int)(m_children.size()); }

    void Append(std::unique_ptr<ModelNode> child)   { m_children.emplace_back(std::move(child)); }

    void UpdateEnabling();
    void UpdateIcons();
};


// ----------------------------------------------------------------------------
//                  DiffModel
// ----------------------------------------------------------------------------

class DiffModel : public wxDataViewModel
{
    wxWindow*               m_parent_win { nullptr };
    ModelNodePtrArray       m_preset_nodes;

    wxDataViewCtrl*         m_ctrl{ nullptr };

    ModelNode *AddOption(ModelNode *group_node,
                         wxString   option_name,
                         wxString   old_value,
                         wxString   mod_value,
                         wxString   new_value);
    ModelNode *AddOptionWithGroup(ModelNode *category_node,
                                  wxString   group_name,
                                  wxString   option_name,
                                  wxString   old_value,
                                  wxString   mod_value,
                                  wxString   new_value);
    ModelNode *AddOptionWithGroupAndCategory(ModelNode *preset_node,
                                             wxString   category_name,
                                             wxString   group_name,
                                             wxString   option_name,
                                             wxString   old_value,
                                             wxString   mod_value,
                                             wxString   new_value,
                                             const std::string category_icon_name);

public:
    enum {
        colToggle,
        colIconText,
        colOldValue,
        colModValue,
        colNewValue,
        colMax
    };

    DiffModel(wxWindow* parent);
    ~DiffModel() override = default;

    void            SetAssociatedControl(wxDataViewCtrl* ctrl) { m_ctrl = ctrl; }

    wxDataViewItem  AddPreset(Preset::Type type, wxString preset_name, PrinterTechnology pt, wxString new_preset_name = wxString());
    wxDataViewItem  AddOption(Preset::Type type, wxString category_name, wxString group_name, wxString option_name,
                              wxString old_value, wxString mod_value, wxString new_value, const std::string category_icon_name);

    void            UpdateItemEnabling(wxDataViewItem item);
    bool            IsEnabledItem(const wxDataViewItem& item);

    unsigned int    GetColumnCount() const override { return colMax; }
    wxString        GetColumnType(unsigned int col) const override;
    void            Rescale();

    wxDataViewItem  Delete(const wxDataViewItem& item);
    void            Clear();

    wxDataViewItem  GetParent(const wxDataViewItem& item) const override;
    unsigned int    GetChildren(const wxDataViewItem& parent, wxDataViewItemArray& array) const override;

    void GetValue(wxVariant& variant, const wxDataViewItem& item, unsigned int col) const override;
    bool SetValue(const wxVariant& variant, const wxDataViewItem& item, unsigned int col) override;

    bool IsEnabled(const wxDataViewItem& item, unsigned int col) const override;
    bool IsContainer(const wxDataViewItem& item) const override;
    // Is the container just a header or an item with all columns
    // In our case it is an item with all columns
    bool HasContainerColumns(const wxDataViewItem& WXUNUSED(item)) const override { return true; }
};


// ----------------------------------------------------------------------------
//                  DiffViewCtrl
// ----------------------------------------------------------------------------

class DiffViewCtrl : public wxDataViewCtrl
{
    bool                    m_has_long_strings{ false };
    bool                    m_empty_selection { false };
    int                     m_em_unit;

    struct ItemData
    {
        std::string     opt_key;
        wxString        opt_name;
        wxString        old_val;
        wxString        mod_val;
        wxString        new_val;
        Preset::Type    type;
        bool            is_long{ false };
    };

    // tree items related to the options
    std::map<wxDataViewItem, ItemData> m_items_map;
    std::map<unsigned int, int>        m_columns_width;

public:
    DiffViewCtrl(wxWindow* parent, wxSize size);
    ~DiffViewCtrl() override {
        if (model) {
            Clear();
            model->DecRef();
        }
    }

    DiffModel* model{ nullptr };

    void    AppendBmpTextColumn(const wxString& label, unsigned model_column, int width, bool set_expander = false);
    void    AppendToggleColumn_(const wxString& label, unsigned model_column, int width);
    void    Rescale(int em = 0);
    void    Append(const std::string& opt_key, Preset::Type type, wxString category_name, wxString group_name, wxString option_name,
                   wxString old_value, wxString mod_value, wxString new_value, const std::string category_icon_name);
    void    Clear();

    wxString    get_short_string(wxString full_string);
    bool        has_selection() { return !m_empty_selection; }
    void        context_menu(wxDataViewEvent& event);
    void        item_value_changed(wxDataViewEvent& event);
    void        set_em_unit(int em) { m_em_unit = em; }
    bool        has_unselected_options();
    bool        has_long_strings() { return m_has_long_strings; }
    bool        has_new_value_column() { return this->GetColumnCount() == DiffModel::colMax; }

    std::vector<std::string> options(Preset::Type type, bool selected);
    std::vector<std::string> selected_options();
};

// Discard and Cancel buttons are always but next buttons are optional
enum ActionButtons {
    TRANSFER = 1,
    KEEP = 2,
    SAVE = 4,
    DONT_SAVE = 8,
};

enum class Action {
    Undef,
    Transfer,
    Discard,
    Save
};

//------------------------------------------
//          UnsavedChangesDialog
//------------------------------------------
class UnsavedChangesDialog : public DPIDialog
{
    DiffViewCtrl*           m_tree          { nullptr };
    ScalableButton*         m_save_btn      { nullptr };
    ScalableButton*         m_transfer_btn  { nullptr };
    ScalableButton*         m_discard_btn   { nullptr };
    wxStaticText*           m_action_line   { nullptr };
    wxStaticText*           m_info_line     { nullptr };
    wxCheckBox*             m_remember_choice   { nullptr };

    int                     m_save_btn_id       { wxID_ANY };
    int                     m_move_btn_id       { wxID_ANY };
    int                     m_continue_btn_id   { wxID_ANY };

    std::string             m_app_config_key;

    static constexpr char ActTransfer[] = "transfer";
    static constexpr char ActDiscard[]  = "discard";
    static constexpr char ActSave[]     = "save";

    // selected action after Dialog closing
    Action m_exit_action {Action::Undef};
    // preset names which are modified in SavePresetDialog and related types
    std::vector<std::pair<std::string, Preset::Type>>  names_and_types;
    // additional action buttons used in dialog
    int m_buttons { ActionButtons::TRANSFER | ActionButtons::SAVE };

public:

    // show unsaved changes when preset is switching
    UnsavedChangesDialog(Preset::Type type, PresetCollection* dependent_presets, const std::string& new_selected_preset = std::string());
    // show unsaved changes for all another cases
    UnsavedChangesDialog(const wxString& caption, const wxString& header, const std::string& app_config_key, int act_buttons);
    ~UnsavedChangesDialog() override = default;

    void build(Preset::Type type, PresetCollection* dependent_presets, const std::string& new_selected_preset, const wxString& header = "");
    void update(Preset::Type type, PresetCollection* dependent_presets, const std::string& new_selected_preset, const wxString& header);
    void update_tree(Preset::Type type, PresetCollection *presets, const std::string& new_selected_preset);
    void show_info_line(Action action, std::string preset_name = "");
    void update_config(Action action);
    void close(Action action);
    // save information about saved presets and their types to names_and_types and show SavePresetDialog to set the names for new presets
    bool save(PresetCollection* dependent_presets, bool show_save_preset_dialog = true);

    bool save_preset() const        { return m_exit_action == Action::Save;     }
    bool transfer_changes() const   { return m_exit_action == Action::Transfer; }
    bool discard() const            { return m_exit_action == Action::Discard;  }

    // get full bundle of preset names and types for saving
    const std::vector<std::pair<std::string, Preset::Type>>& get_names_and_types() { return names_and_types; }
    // short version of the previous function, for the case, when just one preset is modified
    std::string get_preset_name() { return names_and_types[0].first; }

    std::vector<std::string> get_unselected_options(Preset::Type type)  { return m_tree->options(type, false); }
    std::vector<std::string> get_selected_options  (Preset::Type type)  { return m_tree->options(type, true); }
    std::vector<std::string> get_selected_options()                     { return m_tree->selected_options(); }
    bool                     has_unselected_options()                   { return m_tree->has_unselected_options(); }

    static wxString msg_success_saved_modifications(size_t saved_presets_cnt);

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override;
};


//------------------------------------------
//          FullCompareDialog
//------------------------------------------
class FullCompareDialog : public wxDialog
{
public:
    FullCompareDialog(const wxString& option_name, const wxString& old_value, const wxString& mod_value, const wxString& new_value,
                      const wxString& old_value_header, const wxString& mod_value_header, const wxString& new_value_header);
    ~FullCompareDialog() override = default;
};


//------------------------------------------
//          DiffPresetDialog
//------------------------------------------
class DiffPresetDialog : public DPIDialog
{
    DiffViewCtrl*           m_tree              { nullptr };
    wxBoxSizer*             m_presets_sizer     { nullptr };
    wxStaticText*           m_top_info_line     { nullptr };
    wxStaticText*           m_bottom_info_line  { nullptr };
    wxCheckBox*             m_show_all_presets  { nullptr };
    wxCheckBox*             m_use_for_transfer  { nullptr };
    ScalableButton*         m_transfer_btn      { nullptr };
    ScalableButton*         m_save_btn          { nullptr };
    ScalableButton*         m_cancel_btn        { nullptr };
    wxBoxSizer*             m_buttons           { nullptr };
    wxBoxSizer*             m_edit_sizer        { nullptr };

    Preset::Type            m_view_type         { Preset::TYPE_INVALID };
    PrinterTechnology       m_pr_technology;
    std::unique_ptr<PresetBundle>   m_preset_bundle_left;
    std::unique_ptr<PresetBundle>   m_preset_bundle_right;

    void create_buttons();
    void create_edit_sizer();
    void complete_dialog_creation();
    void create_presets_sizer();
    void create_info_lines();
    void create_tree();
    void create_show_all_presets_chb();

    void update_bottom_info(wxString bottom_info = "");
    void update_tree();
    void update_bundles_from_app();
    void update_controls_visibility(Preset::Type type = Preset::TYPE_INVALID);
    void update_compatibility(const std::string& preset_name, Preset::Type type, PresetBundle* preset_bundle);
         
    void button_event(Action act);
    bool save();

    struct DiffPresets
    {
        PresetComboBox* presets_left    { nullptr };
        ScalableButton* equal_bmp       { nullptr };
        PresetComboBox* presets_right   { nullptr };
    };

    std::vector<DiffPresets> m_preset_combos;

    // attributes witch are used for save preset
    struct PresetToSave
    {
        Preset::Type    type;
        std::string     from_name;
        std::string     to_name;
        std::string     new_name;
    };

    std::vector<PresetToSave>  presets_to_saves;

public:
    DiffPresetDialog(MainFrame*mainframe);
    ~DiffPresetDialog() override = default;

    void show(Preset::Type type = Preset::TYPE_INVALID);
    void update_presets(Preset::Type type = Preset::TYPE_INVALID);

    Preset::Type        view_type() const           { return m_view_type; }
    PrinterTechnology   printer_technology() const  { return m_pr_technology; }

    std::string get_left_preset_name(Preset::Type type);
    std::string get_right_preset_name(Preset::Type type);

    std::vector<std::string> get_selected_options(Preset::Type type) const { return std::move(m_tree->options(type, true)); }

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override;
};

} 
}

#endif //slic3r_UnsavedChangesDialog_hpp_
