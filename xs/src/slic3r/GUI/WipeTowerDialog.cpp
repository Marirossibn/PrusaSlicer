#include <algorithm>
#include "WipeTowerDialog.hpp"

// Human-readable output of Parameters structure
std::ostream& operator<<(std::ostream& str,Slic3r::WipeTowerParameters& par) {
    str << "bridging: " << par.bridging << "\n";
    str << "adhesion: " << par.adhesion << "\n";
    str << "sampling: " << par.sampling << "\n"; 
   
    str << "cooling times: ";
    for (const auto& a : par.cooling_time) str << a << " ";
    
    str << "line widths: ";
    for (const auto& a : par.ramming_line_width_multiplicator) str << a << " ";
    
    str << "line spacing: ";
    for (const auto& a : par.ramming_step_multiplicator) str << a << " ";
    
    str<<"\n\nramming_speeds:\n";
    for (const auto& a : par.ramming_speed) {
        for (const auto& b : a)
            str << b << " ";
        str<<"\n";
    }
    str<<"\n\nramming_buttons:\n";
    for (const auto& a : par.ramming_buttons) {
        for (const auto& b : a) {
            Slic3r::operator <<(str,b); // temporary hack (this << is in the namespace Slic3r)
            str << " | ";               // the function will be deleted after everything is debugged, anyway
        }
        str<<"\n";
    }
    str<<"\n\nwipe volumes:\n";
    for (const auto& a : par.wipe_volumes) {
        for (const auto& b : a)
            str << b << " ";
        str<<"\n";
    }
    str<<"\n\nfilament wipe volumes:\n";
    for (const auto& a : par.filament_wipe_volumes) {
        Slic3r::operator <<(str,a);
        str << " ";
    }
    str<<"\n";
        
    return str;
}
    



RammingPanel::RammingPanel(wxWindow* parent,const Slic3r::WipeTowerParameters& p)
: wxPanel(parent,wxID_ANY,wxPoint(0,0),wxSize(0,0),wxBORDER_RAISED)
{
    new wxStaticText(this,wxID_ANY,wxString("Total ramming time (s):"),     wxPoint(500,105),      wxSize(200,25),wxALIGN_LEFT);
    m_widget_time = new wxSpinCtrlDouble(this,wxID_ANY,wxEmptyString,       wxPoint(700,100),      wxSize(75,25),wxSP_ARROW_KEYS|wxALIGN_RIGHT,0.,5.0,3.,0.5);        
    new wxStaticText(this,wxID_ANY,wxString("Total rammed volume (mm3):"),  wxPoint(500,135),      wxSize(200,25),wxALIGN_LEFT);
    m_widget_volume = new wxSpinCtrl(this,wxID_ANY,wxEmptyString,           wxPoint(700,130),      wxSize(75,25),wxSP_ARROW_KEYS|wxALIGN_RIGHT,0,10000,0);        
    new wxStaticText(this,wxID_ANY,wxString("Ramming line width (%):"),     wxPoint(500,205),      wxSize(200,25),wxALIGN_LEFT);
    m_widget_ramming_line_width_multiplicator = new wxSpinCtrl(this,wxID_ANY,wxEmptyString,       wxPoint(700,200),      wxSize(75,25),wxSP_ARROW_KEYS|wxALIGN_RIGHT,10,200,100);        
    new wxStaticText(this,wxID_ANY,wxString("Ramming line spacing (%):"),   wxPoint(500,235),      wxSize(200,25),wxALIGN_LEFT);
    m_widget_ramming_step_multiplicator = new wxSpinCtrl(this,wxID_ANY,wxEmptyString,     wxPoint(700,230),      wxSize(75,25),wxSP_ARROW_KEYS|wxALIGN_RIGHT,10,200,100);        
    new wxStaticText(this,wxID_ANY,wxString("Extruder #:"),                 wxPoint(500,12),       wxSize(200,25),wxALIGN_LEFT);
    
    wxArrayString choices;
    for (unsigned int i=0;i<p.ramming_line_width_multiplicator.size();++i) {            // for all extruders
        choices.Add(wxString("")<<i+1);
        m_ramming_line_width_multiplicators.push_back(p.ramming_line_width_multiplicator[i]*100);
        m_ramming_step_multiplicators.push_back(p.ramming_step_multiplicator[i]*100);
    }
    m_widget_extruder = new wxChoice(this,wxID_ANY,wxPoint(580,5),wxSize(50,27),choices);
    
    m_chart = new Chart(this,wxRect(10,10,480,360),p.ramming_buttons,p.ramming_speed,p.sampling);
    
    m_chart->set_extruder(0);
    m_widget_time->SetValue(m_chart->get_time());
    m_widget_time->SetDigits(2);
    m_widget_volume->SetValue(m_chart->get_volume());
    m_widget_volume->Disable();
    m_widget_extruder->SetSelection(0);
    extruder_selection_changed(); // tell everyone to redraw
    
    m_widget_ramming_step_multiplicator->Bind(wxEVT_TEXT,[this](wxCommandEvent&) { line_parameters_changed(); });
    m_widget_ramming_line_width_multiplicator->Bind(wxEVT_TEXT,[this](wxCommandEvent&) { line_parameters_changed(); });
    m_widget_extruder->Bind(wxEVT_CHOICE,[this](wxCommandEvent&) { extruder_selection_changed(); });
    m_widget_time->Bind(wxEVT_TEXT,[this](wxCommandEvent&) {m_chart->set_xy_range(m_widget_time->GetValue(),-1);});
    m_widget_time->Bind(wxEVT_CHAR,[](wxKeyEvent&){});      // do nothing - prevents the user to change the value
    m_widget_volume->Bind(wxEVT_CHAR,[](wxKeyEvent&){});    // do nothing - prevents the user to change the value   
    Bind(EVT_WIPE_TOWER_CHART_CHANGED,[this](wxCommandEvent&) {m_widget_volume->SetValue(m_chart->get_volume()); m_widget_time->SetValue(m_chart->get_time());} );        
}

void RammingPanel::fill_parameters(Slic3r::WipeTowerParameters& p)
{
    if (!m_chart) return;
    p.ramming_buttons = m_chart->get_buttons();
    p.ramming_speed   = m_chart->get_ramming_speeds(p.sampling);
    for (unsigned int i=0;i<m_ramming_line_width_multiplicators.size();++i) {  // we assume m_ramming_line_width_multiplicators.size() == m_ramming_step_multiplicators.size()         
        p.ramming_line_width_multiplicator.push_back(m_ramming_line_width_multiplicators[i]/100.f);
        p.ramming_step_multiplicator.push_back(m_ramming_step_multiplicators[i]/100.f);
    }
}
    
void RammingPanel::extruder_selection_changed() {
    m_current_extruder = m_widget_extruder->GetSelection();
    m_chart->set_extruder(m_current_extruder);  // tell our chart to redraw
    m_widget_ramming_line_width_multiplicator  ->SetValue(m_ramming_line_width_multiplicators[m_current_extruder]);
    m_widget_ramming_step_multiplicator->SetValue(m_ramming_step_multiplicators[m_current_extruder]);        
}

void RammingPanel::line_parameters_changed() {
    m_ramming_line_width_multiplicators[m_current_extruder]=m_widget_ramming_line_width_multiplicator->GetValue();
    m_ramming_step_multiplicators[m_current_extruder]=m_widget_ramming_step_multiplicator->GetValue();
}






CoolingPanel::CoolingPanel(wxWindow* parent,const Slic3r::WipeTowerParameters& p)
: wxPanel(parent,wxID_ANY,wxPoint(0,0),wxSize(0,0),wxBORDER_RAISED)
{
    new wxStaticText(this,wxID_ANY,wxString("Time (in seconds) reserved for cooling after unload:"),wxPoint(220,50) ,wxSize(400,25),wxALIGN_LEFT);
    for (int i=0;i<4;++i) {
        new wxStaticText(this,wxID_ANY,wxString("Filament #")<<i+1<<": ",wxPoint(300,105+30*i) ,wxSize(150,25),wxALIGN_LEFT);
        m_widget_edits.push_back(new wxSpinCtrl(this,wxID_ANY,wxEmptyString,wxPoint(400,100+30*i),wxSize(75,25),wxSP_ARROW_KEYS|wxALIGN_RIGHT,0,30,15));
    }        
    for (unsigned int i=0;i<p.cooling_time.size();++i) {
        if (i>=m_widget_edits.size())
            break;  // so we don't initialize non-existent widget
        m_widget_edits[i]->SetValue(p.cooling_time[i]);
    }
}

void CoolingPanel::fill_parameters(Slic3r::WipeTowerParameters& p) {
    p.cooling_time.clear();
    for (int i=0;i<4;++i)
        p.cooling_time.push_back(m_widget_edits[i]->GetValue());
}



WipingPanel::WipingPanel(wxWindow* parent,const Slic3r::WipeTowerParameters& p)
: wxPanel(parent,wxID_ANY,wxPoint(0,0),wxSize(0,0),wxBORDER_RAISED)
{
    const int N = 4; // number of extruders
    new wxStaticText(this,wxID_ANY,wxString("Volume to wipe when the filament is being"),wxPoint(40,55) ,wxSize(500,25));
    new wxStaticText(this,wxID_ANY,wxString("unloaded"),wxPoint(110,75) ,wxSize(500,25));
    new wxStaticText(this,wxID_ANY,wxString("loaded"),wxPoint(195,75) ,wxSize(500,25));        
    m_widget_button = new wxButton(this,wxID_ANY,"-> Fill in the matrix ->",wxPoint(300,130),wxSize(175,50));
    for (int i=0;i<N;++i) {
        new wxStaticText(this,wxID_ANY,wxString("Filament #")<<i+1<<": ",wxPoint(20,105+30*i) ,wxSize(150,25),wxALIGN_LEFT);
        m_old.push_back(new wxSpinCtrl(this,wxID_ANY,wxEmptyString,wxPoint(120,100+30*i),wxSize(50,25),wxSP_ARROW_KEYS|wxALIGN_RIGHT,0,100,p.filament_wipe_volumes[i].first));
        m_new.push_back(new wxSpinCtrl(this,wxID_ANY,wxEmptyString,wxPoint(195,100+30*i),wxSize(50,25),wxSP_ARROW_KEYS|wxALIGN_RIGHT,0,100,p.filament_wipe_volumes[i].second));
    }
    
    wxPoint origin(515,55);
    for (int i=0;i<N;++i) {
        edit_boxes.push_back(std::vector<wxTextCtrl*>(0));
        new wxStaticText(this,wxID_ANY,wxString("")<<i+1,origin+wxPoint(45+60*i,25) ,wxSize(20,25));
        new wxStaticText(this,wxID_ANY,wxString("")<<i+1,origin+wxPoint(0,50+30*i) ,wxSize(500,25));
        for (int j=0;j<N;++j) {
            edit_boxes.back().push_back(new wxTextCtrl(this,wxID_ANY,wxEmptyString,origin+wxPoint(25+60*i,45+30*j),wxSize(50,25)));
            if (i==j)
                edit_boxes[i][j]->Disable();
            else
                edit_boxes[i][j]->SetValue(wxString("")<<int(p.wipe_volumes[j][i]));
        }
        new wxStaticText(this,wxID_ANY,wxString("Filament changed to"),origin+wxPoint(75,0) ,wxSize(500,25));            
    }
    
    m_widget_button->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){fill_in_matrix();});
}

void WipingPanel::fill_parameters(Slic3r::WipeTowerParameters& p) {
    p.wipe_volumes.clear();
    p.filament_wipe_volumes.clear();
    for (int i=0;i<4;++i) {
        // first go through the full matrix:
        p.wipe_volumes.push_back(std::vector<float>());
        for (int j=0;j<4;++j) {
            double val = 0.;
            edit_boxes[j][i]->GetValue().ToDouble(&val);
            p.wipe_volumes[i].push_back((float)val);  
        }
        
        // now the filament volumes:
        p.filament_wipe_volumes.push_back(std::make_pair(m_old[i]->GetValue(),m_new[i]->GetValue()));
    }
}

void WipingPanel::fill_in_matrix() {
    wxArrayString choices;
    choices.Add("sum");
    choices.Add("maximum");
    wxSingleChoiceDialog dialog(this,"How shall I calculate volume for any given pair?\n\nI can either sum volumes for old and new filament, or just use the higher value.","DEBUGGING",choices);
    if (dialog.ShowModal() == wxID_CANCEL)
        return;        
    for (unsigned i=0;i<4;++i) {
        for (unsigned j=0;j<4;++j) {
            if (i==j) continue;
            if (!dialog.GetSelection()) edit_boxes[j][i]->SetValue(wxString("")<< (m_old[i]->GetValue() + m_new[j]->GetValue()));
            else
              edit_boxes[j][i]->SetValue(wxString("")<< (std::max(m_old[i]->GetValue(), m_new[j]->GetValue())));
        }
    }
}



GeneralPanel::GeneralPanel(wxWindow* parent,const Slic3r::WipeTowerParameters& p) : wxPanel(parent,wxID_ANY,wxPoint(0,0),wxSize(0,0),wxBORDER_RAISED) {
    new wxStaticText(this,wxID_ANY,wxString("Maximum bridging over sparse infill (mm):"),wxPoint(100,105) ,wxSize(280,25),wxALIGN_LEFT);
    m_widget_bridge = new wxSpinCtrl(this,wxID_ANY,wxEmptyString,wxPoint(380,100),wxSize(50,25),wxALIGN_RIGHT|wxSP_ARROW_KEYS,1,50,10);
    m_widget_adhesion = new wxCheckBox(this,wxID_ANY,"Increased adhesion of first layer",wxPoint(100,150),wxSize(330,25),wxALIGN_RIGHT);
    m_widget_bridge->SetValue(p.bridging);
    m_widget_adhesion->SetValue(p.adhesion);
}

void GeneralPanel::fill_parameters(Slic3r::WipeTowerParameters& p) {
    p.bridging = m_widget_bridge->GetValue();
    p.adhesion = m_widget_adhesion->GetValue();        
}
  




WipeTowerDialog::WipeTowerDialog(wxWindow* parent,const std::string& init_data)
: wxDialog(parent, -1,  wxT("Wipe tower advanced settings"), wxPoint(50,50), wxSize(800,550), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    this->Centre();
            
    Slic3r::WipeTowerParameters parameters(init_data);
    if (!parameters.validate()) {
        wxMessageDialog(this,"Wipe tower parameters not parsed correctly!\nRestoring default settings.","Error",wxICON_ERROR);
        parameters.set_defaults();
    }
            
    wxNotebook* notebook = new wxNotebook(this,wxID_ANY,wxPoint(0,0),wxSize(800,450));
    
    m_panel_general = new GeneralPanel(notebook,parameters);
    m_panel_ramming = new RammingPanel(notebook,parameters);
    m_panel_cooling = new CoolingPanel(notebook,parameters);
    m_panel_wiping  = new WipingPanel(notebook,parameters);
    notebook->AddPage(m_panel_general,"General");
    notebook->AddPage(m_panel_ramming,"Ramming");
    notebook->AddPage(m_panel_cooling,"Cooling");
    notebook->AddPage(m_panel_wiping,"Wiping");
    this->Show();

    auto main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(notebook, 1, wxEXPAND);
    main_sizer->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);
    SetSizer(main_sizer);
    SetMinSize(GetSize());
    main_sizer->SetSizeHints(this);
    
    this->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) { EndModal(wxCANCEL); });
    
    this->Bind(wxEVT_BUTTON,[this](wxCommandEvent&) {
        m_output_data=read_dialog_values();
        EndModal(wxID_OK);
        },wxID_OK);
}

