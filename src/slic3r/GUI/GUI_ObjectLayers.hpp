#ifndef slic3r_GUI_ObjectLayers_hpp_
#define slic3r_GUI_ObjectLayers_hpp_

#include "GUI_ObjectSettings.hpp"
#include "wxExtensions.hpp"

#ifdef __WXOSX__
#include "../libslic3r/PrintConfig.hpp"
#endif

class wxBoxSizer;

namespace Slic3r {
class ModelObject;

namespace GUI {
class ConfigOptionsGroup;

typedef double                          coordf_t;
typedef std::pair<coordf_t, coordf_t>   t_layer_height_range;

enum EditorType
{
    etUndef         = 0,
    etMinZ          = 1,
    etMaxZ          = 2,
    etLayerHeight   = 4,
};

class LayerRangeEditor : public wxTextCtrl
{
    bool                m_enter_pressed     { false };
    bool                m_call_kill_focus   { false };
    wxString            m_valid_value;
    EditorType          m_type;

public:
    LayerRangeEditor(   wxWindow* parent,
                        const wxString& value = wxEmptyString,
                        EditorType type = etUndef,
                        std::function<void(EditorType)>     set_focus_fn = [](EditorType)      {;},
                        std::function<bool(coordf_t, bool)> edit_fn      = [](coordf_t, bool) {return false; }
                        );
    ~LayerRangeEditor() {}

    EditorType          type() const {return m_type;}

private:
    coordf_t            get_value();
};

class ObjectLayers : public OG_Settings
{
    ScalableBitmap  m_bmp_delete;
    ScalableBitmap  m_bmp_add;
    ModelObject*    m_object {nullptr};

    wxFlexGridSizer*       m_grid_sizer;
    t_layer_height_range   m_last_edited_range;
    EditorType             m_selection_type {etUndef};

public:
    ObjectLayers(wxWindow* parent);
    ~ObjectLayers() {}

    void        select_editor(LayerRangeEditor* editor, const bool is_last_edited_range);
    wxSizer*    create_layer(const t_layer_height_range& range);    // without_buttons
    void        create_layers_list();
    void        update_layers_list();

    void        UpdateAndShow(const bool show) override;
    void        msw_rescale();
};

}}

#endif // slic3r_GUI_ObjectLayers_hpp_
