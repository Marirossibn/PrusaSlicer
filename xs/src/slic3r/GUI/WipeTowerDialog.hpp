#ifndef _WIPE_TOWER_DIALOG_H_
#define _WIPE_TOWER_DIALOG_H_

#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>
#include <wx/choicdlg.h>
#include <wx/notebook.h>
#include <wx/msgdlg.h>

#include "../../libslic3r/GCode/WipeTowerPrusaMM.hpp"
#include "RammingChart.hpp"


// Human-readable output of Parameters structure
std::ostream& operator<<(std::ostream& str,Slic3r::WipeTowerParameters& par);
    

class RammingPanel : public wxPanel {
public:
    RammingPanel(wxWindow* parent,const Slic3r::WipeTowerParameters& p);
    void fill_parameters(Slic3r::WipeTowerParameters& p);

private:
    Chart* m_chart = nullptr;
    wxSpinCtrl* m_widget_volume = nullptr;
    wxSpinCtrl* m_widget_ramming_line_width_multiplicator = nullptr;
    wxSpinCtrl* m_widget_ramming_step_multiplicator = nullptr;
    wxSpinCtrlDouble* m_widget_time = nullptr;
    wxChoice* m_widget_extruder = nullptr;
    std::vector<int> m_ramming_step_multiplicators;    
    std::vector<int> m_ramming_line_width_multiplicators;
    int m_current_extruder = 0;     // zero-based index
    
    void extruder_selection_changed();
    
    void line_parameters_changed();
};






class CoolingPanel : public wxPanel {
public:
    CoolingPanel(wxWindow* parent,const Slic3r::WipeTowerParameters& p);
    void fill_parameters(Slic3r::WipeTowerParameters& p);
    
private:
    std::vector<wxSpinCtrl*> m_widget_edits;
};






class WipingPanel : public wxPanel {
public:
    WipingPanel(wxWindow* parent,const Slic3r::WipeTowerParameters& p);
    void fill_parameters(Slic3r::WipeTowerParameters& p);
        
private:
    void fill_in_matrix();
        
    std::vector<wxSpinCtrl*> m_old;
    std::vector<wxSpinCtrl*> m_new;
    std::vector<std::vector<wxTextCtrl*>> edit_boxes;
    wxButton* m_widget_button=nullptr;    
};




class GeneralPanel : public wxPanel {
public:
    GeneralPanel(wxWindow* parent,const Slic3r::WipeTowerParameters& p);
    void fill_parameters(Slic3r::WipeTowerParameters& p);
    
private:
    wxSpinCtrl* m_widget_bridge;
    wxCheckBox* m_widget_adhesion;
};




class WipeTowerDialog : public wxDialog {
public:
    WipeTowerDialog(wxWindow* parent,const std::string& init_data);
    
    std::string GetValue() const { return m_output_data; }
    
    
private:
    std::string m_file_name="config_wipe_tower";
    GeneralPanel* m_panel_general = nullptr;
    RammingPanel* m_panel_ramming = nullptr;
    CoolingPanel* m_panel_cooling = nullptr;
    WipingPanel*  m_panel_wiping  = nullptr;
    std::string m_output_data = "";
            
    std::string read_dialog_values() {
        Slic3r::WipeTowerParameters p;
        m_panel_general->fill_parameters(p);
        m_panel_ramming->fill_parameters(p);
        m_panel_cooling->fill_parameters(p);
        m_panel_wiping ->fill_parameters(p);
        return p.to_string();
    }
};

#endif  // _WIPE_TOWER_DIALOG_H_