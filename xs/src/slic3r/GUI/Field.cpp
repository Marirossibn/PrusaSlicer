#include "GUI.hpp"//"slic3r_gui.hpp"
#include "Field.hpp"

//#include <wx/event.h>
#include <regex>
#include <wx/numformatter.h>
#include "PrintConfig.hpp"

namespace Slic3r { namespace GUI {

    void Field::_on_kill_focus(wxFocusEvent& event) {
        // Without this, there will be nasty focus bugs on Windows.
        // Also, docs for wxEvent::Skip() say "In general, it is recommended to skip all 
        // non-command events to allow the default handling to take place."
        event.Skip(1);

        // call the registered function if it is available
        if (on_kill_focus) 
            on_kill_focus(m_opt_id);
    }
    void Field::on_change_field(wxCommandEvent& event) {
//        std::cerr << "calling Field::_on_change \n";
        if (m_on_change != nullptr  && !m_disable_change_event)
            m_on_change(m_opt_id, get_value());
    }


	bool Field::is_matched(std::string string, std::string pattern)
	{
		std::regex regex_pattern(pattern, std::regex_constants::icase); // use ::icase to make the matching case insensitive like /i in perl
		return std::regex_match(string, regex_pattern);
	}

	boost::any Field::get_value_by_opt_type(wxString str, ConfigOptionType type)
	{
		boost::any ret_val;
		switch (m_opt.type){
		case coInt:
			ret_val = wxAtoi(str);
			break;
		case coPercent:
		case coPercents:
		case coFloats:
		case coFloat:{
			if (m_opt.type == coPercent) str.RemoveLast();
			double val;
			str.ToCDouble(&val);
			ret_val = val;
			break; }
		case coString:
		case coStrings:
			ret_val = str.ToStdString();
			break;
		case coFloatOrPercent:{
			if (str.Last() == '%')
				str.RemoveLast();
			double val;
			str.ToCDouble(&val);
			ret_val = val;
			break;
		}
		default:
			break;
		}

		return ret_val;
	}

	void TextCtrl::BUILD() {
        auto size = wxSize(wxDefaultSize);
        if (m_opt.height >= 0) size.SetHeight(m_opt.height);
        if (m_opt.width >= 0) size.SetWidth(m_opt.width);

		wxString text_value = wxString(""); 

		switch (m_opt.type) {
		case coFloatOrPercent:
		{
			if (static_cast<const ConfigOptionFloatOrPercent*>(m_opt.default_value)->percent)
			{
				text_value = wxString::Format(_T("%i"), int(m_opt.default_value->getFloat()));
				text_value += "%";
			}
			else
				text_value = wxNumberFormatter::ToString(m_opt.default_value->getFloat(), 2);
			break;
		}
		case coPercent:
		{
			text_value = wxString::Format(_T("%i"), int(m_opt.default_value->getFloat()));
			text_value += "%";
			break;
		}	
		case coPercents:
		{
			const ConfigOptionPercents *vec = static_cast<const ConfigOptionPercents*>(m_opt.default_value);
			if (vec == nullptr || vec->empty()) break;
			if (vec->size() > 1)
				break;
			double val = vec->get_at(0);
			text_value = val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2);
			break;
		}			
		case coFloat:
		{
			double val = m_opt.default_value->getFloat();
			text_value = (val - int(val)) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2);
			break;
		}			
		case coFloats:
		{
			const ConfigOptionFloats *vec = static_cast<const ConfigOptionFloats*>(m_opt.default_value);
			if (vec == nullptr || vec->empty()) break;
			if (vec->size() > 1)
				break;
			double val = vec->get_at(0);
			text_value = val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2);
			break;
		}
		case coString:			
			text_value = static_cast<const ConfigOptionString*>(m_opt.default_value)->value;
			break;
		case coStrings:
		{
			const ConfigOptionStrings *vec = static_cast<const ConfigOptionStrings*>(m_opt.default_value);
			if (vec == nullptr || vec->empty()) break;
			if (vec->size() > 1)
				break;
			text_value = vec->values.at(0);
			break;
		}
		default:
			break; 
		}

		auto temp = new wxTextCtrl(m_parent, wxID_ANY, text_value, wxDefaultPosition, size, (m_opt.multiline ? wxTE_MULTILINE : 0));

        if (m_opt.tooltip.length() > 0) { temp->SetToolTip(m_opt.tooltip); }
        
        temp->Bind(wxEVT_TEXT, ([=](wxCommandEvent e) { on_change_field(e); }), temp->GetId());
        temp->Bind(wxEVT_KILL_FOCUS, ([this](wxFocusEvent e) { _on_kill_focus(e); }), temp->GetId());

        // recast as a wxWindow to fit the calling convention
        window = dynamic_cast<wxWindow*>(temp);
    }	

	boost::any TextCtrl::get_value()
	{
		wxString ret_str = static_cast<wxTextCtrl*>(window)->GetValue();
		boost::any ret_val = get_value_by_opt_type(ret_str, m_opt.type);

		return ret_val;
	}

	void TextCtrl::enable() { dynamic_cast<wxTextCtrl*>(window)->Enable(); dynamic_cast<wxTextCtrl*>(window)->SetEditable(true); }
    void TextCtrl::disable() { dynamic_cast<wxTextCtrl*>(window)->Disable(); dynamic_cast<wxTextCtrl*>(window)->SetEditable(false); }
    void TextCtrl::set_tooltip(const wxString& tip) { }

void CheckBox::BUILD() {
	auto size = wxSize(wxDefaultSize);
	if (m_opt.height >= 0) size.SetHeight(m_opt.height);
	if (m_opt.width >= 0) size.SetWidth(m_opt.width);

	bool check_value =	m_opt.type == coBool ? 
						m_opt.default_value->getBool() : m_opt.type == coBools ? 
						static_cast<ConfigOptionBools*>(m_opt.default_value)->values.at(0) : 
    					false;

	auto temp = new wxCheckBox(m_parent, wxID_ANY, wxString(""), wxDefaultPosition, size); 
	temp->SetValue(check_value);
	if (m_opt.readonly) temp->Disable();

	temp->Bind(wxEVT_CHECKBOX, ([this](wxCommandEvent e) { on_change_field(e); }), temp->GetId());

	if (m_opt.tooltip.length() > 0) { temp->SetToolTip(m_opt.tooltip); }

	// recast as a wxWindow to fit the calling convention
	window = dynamic_cast<wxWindow*>(temp);
}

int undef_spin_val = -9999;		//! Probably, It's not necessary

void SpinCtrl::BUILD() {
	auto size = wxSize(wxDefaultSize);
	if (m_opt.height >= 0) size.SetHeight(m_opt.height);
	if (m_opt.width >= 0) size.SetWidth(m_opt.width);

	wxString	text_value = wxString("");
	int			default_value = 0;

	switch (m_opt.type) {
	case coInt:
		default_value = m_opt.default_value->getInt();
		text_value = wxString::Format(_T("%i"), default_value);
		break;
	case coInts:
	{
		const ConfigOptionInts *vec = static_cast<const ConfigOptionInts*>(m_opt.default_value);
		if (vec == nullptr || vec->empty()) break;
		for (size_t id = 0; id < vec->size(); ++id)
		{
			default_value = vec->get_at(id);
			text_value += wxString::Format(_T("%i"), default_value);
		}
		break;
	}
	default:
		break;
	}

	auto temp = new wxSpinCtrl(m_parent, wxID_ANY, text_value, wxDefaultPosition, size,
		0, m_opt.min >0 ? m_opt.min : 0, m_opt.max < 2147483647 ? m_opt.max : 2147483647, default_value);

	temp->Bind(wxEVT_SPINCTRL, ([=](wxCommandEvent e) { tmp_value = undef_spin_val; on_change_field(e); }), temp->GetId());
	temp->Bind(wxEVT_KILL_FOCUS, ([this](wxFocusEvent e) { tmp_value = undef_spin_val; _on_kill_focus(e); }), temp->GetId());
	temp->Bind(wxEVT_TEXT, ([=](wxCommandEvent e)
	{
// 		# On OSX / Cocoa, wxSpinCtrl::GetValue() doesn't return the new value
// 		# when it was changed from the text control, so the on_change callback
// 		# gets the old one, and on_kill_focus resets the control to the old value.
// 		# As a workaround, we get the new value from $event->GetString and store
// 		# here temporarily so that we can return it from $self->get_value
		std::string value = e.GetString().utf8_str();
		if (is_matched(value, "^\\d+$"))
			tmp_value = std::stoi(value);
		on_change_field(e);
// 		# We don't reset tmp_value here because _on_change might put callbacks
// 		# in the CallAfter queue, and we want the tmp value to be available from
// 		# them as well.
	}), temp->GetId());


	if (m_opt.tooltip.length() > 0) { temp->SetToolTip(m_opt.tooltip); }

	// recast as a wxWindow to fit the calling convention
	window = dynamic_cast<wxWindow*>(temp);
}

void Choice::BUILD() {
	auto size = wxSize(wxDefaultSize);
	if (m_opt.height >= 0) size.SetHeight(m_opt.height);
	if (m_opt.width >= 0) size.SetWidth(m_opt.width);

	wxComboBox* temp;	
	if (!m_opt.gui_type.empty() && m_opt.gui_type.compare("select_open") != 0)
		temp = new wxComboBox(m_parent, wxID_ANY, wxString(""), wxDefaultPosition, size);
	else
		temp = new wxComboBox(m_parent, wxID_ANY, wxString(""), wxDefaultPosition, size, 0, NULL, wxCB_READONLY);

	// recast as a wxWindow to fit the calling convention
	window = dynamic_cast<wxWindow*>(temp);

	if (m_opt.enum_labels.empty() && m_opt.enum_values.empty()){
	}
	else{
		for (auto el : m_opt.enum_labels.empty() ? m_opt.enum_values : m_opt.enum_labels)
			temp->Append(wxString(el));
		set_selection();
	}
 	temp->Bind(wxEVT_TEXT, ([=](wxCommandEvent e) { on_change_field(e); }), temp->GetId());
 	temp->Bind(wxEVT_COMBOBOX, ([this](wxCommandEvent e) { on_change_field(e); }), temp->GetId());

	if (m_opt.tooltip.length() > 0) temp->SetToolTip(m_opt.tooltip);
}

void Choice::set_selection()
{
	wxString text_value = wxString("");
	switch (m_opt.type){
	case coFloat:
	case coPercent:	{
		double val = m_opt.default_value->getFloat();
		text_value = val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 1);
		auto idx = 0;
		for (auto el : m_opt.enum_values)
		{
			if (el.compare(text_value) == 0)
				break;
			++idx;
		}
		if (m_opt.type == coPercent) text_value += "%";
		idx == m_opt.enum_values.size() ?
			dynamic_cast<wxComboBox*>(window)->SetValue(text_value) :
			dynamic_cast<wxComboBox*>(window)->SetSelection(idx);
		break;
	}
	case coEnum:{
		int id_value = static_cast<const ConfigOptionEnum<SeamPosition>*>(m_opt.default_value)->value; //!!
		dynamic_cast<wxComboBox*>(window)->SetSelection(id_value);
		break;
	}
	case coInt:{
		int val = m_opt.default_value->getInt(); //!!
		text_value = wxString::Format(_T("%i"), int(val));
		auto idx = 0;
		for (auto el : m_opt.enum_values)
		{
			if (el.compare(text_value) == 0)
				break;
			++idx;
		}
		idx == m_opt.enum_values.size() ?
			dynamic_cast<wxComboBox*>(window)->SetValue(text_value) :
			dynamic_cast<wxComboBox*>(window)->SetSelection(idx);
		break;
	}
	case coStrings:{
		text_value = static_cast<const ConfigOptionStrings*>(m_opt.default_value)->values.at(0);

		auto idx = 0;
		for (auto el : m_opt.enum_values)
		{
			if (el.compare(text_value) == 0)
				break;
			++idx;
		}
		idx == m_opt.enum_values.size() ?
			dynamic_cast<wxComboBox*>(window)->SetValue(text_value) :
			dynamic_cast<wxComboBox*>(window)->SetSelection(idx);
		break;
	}
	}
}

void Choice::set_value(const std::string value)  //! Redundant?
{
	m_disable_change_event = true;

	auto idx=0;
	for (auto el : m_opt.enum_values)
	{
		if (el.compare(value) == 0)
			break;
		++idx;
	}

	idx == m_opt.enum_values.size() ? 
		dynamic_cast<wxComboBox*>(window)->SetValue(value) :
		dynamic_cast<wxComboBox*>(window)->SetSelection(idx);
	
	m_disable_change_event = false;
}

void Choice::set_value(boost::any value)
{
	switch (m_opt.type){
	case coInt:
	case coFloat:
	case coPercent:
	case coStrings:{
		wxString text_value;
		if (m_opt.type == coInt) 
			text_value = wxString::Format(_T("%i"), int(boost::any_cast<int>(value)));
		else
			text_value = boost::any_cast<wxString>(value);
		auto idx = 0;
		for (auto el : m_opt.enum_values)
		{
			if (el.compare(text_value) == 0)
				break;
			++idx;
		}
		if (m_opt.type == coPercent) text_value += "%";
		idx == m_opt.enum_values.size() ?
			dynamic_cast<wxComboBox*>(window)->SetValue(text_value) :
			dynamic_cast<wxComboBox*>(window)->SetSelection(idx);
		break;
	}
	case coEnum:{
		dynamic_cast<wxComboBox*>(window)->SetSelection(boost::any_cast<int>(value));
		break;
	}
	default:
		break;
	}
}

//! it's needed for _update_serial_ports()
void Choice::set_values(const std::vector<std::string> values)
{
	if (values.empty())
		return;
	m_disable_change_event = true;

// 	# it looks that Clear() also clears the text field in recent wxWidgets versions,
// 	# but we want to preserve it
	auto ww = dynamic_cast<wxComboBox*>(window);
	auto value = ww->GetValue();
	ww->Clear();
	for (auto el : values)
		ww->Append(wxString(el));
	ww->SetValue(value);

	m_disable_change_event = false;
}

boost::any Choice::get_value()
{
	boost::any ret_val;
	wxString ret_str = static_cast<wxComboBox*>(window)->GetValue();	

	if (m_opt.type != coEnum)
		ret_val = get_value_by_opt_type(ret_str, m_opt.type);
	else
	{
		int ret_enum = static_cast<wxComboBox*>(window)->GetSelection(); 
		if (m_opt_id.compare("external_fill_pattern") == 0 ||
			m_opt_id.compare("fill_pattern") == 0)
			ret_val = static_cast<InfillPattern>(ret_enum);
		else if (m_opt_id.compare("gcode_flavor") == 0)
			ret_val = static_cast<GCodeFlavor>(ret_enum);
		else if (m_opt_id.compare("support_material_pattern") == 0)
			ret_val = static_cast<SupportMaterialPattern>(ret_enum);
		else if (m_opt_id.compare("seam_position") == 0)
			ret_val = static_cast<SeamPosition>(ret_enum);
	}	

	return ret_val;
}

void ColourPicker::BUILD()
{
	auto size = wxSize(wxDefaultSize);
	if (m_opt.height >= 0) size.SetHeight(m_opt.height);
	if (m_opt.width >= 0) size.SetWidth(m_opt.width);

	wxString clr(static_cast<ConfigOptionStrings*>(m_opt.default_value)->values.at(0));
	auto temp = new wxColourPickerCtrl(m_parent, wxID_ANY, clr, wxDefaultPosition, size);
		
	// 	// recast as a wxWindow to fit the calling convention
	window = dynamic_cast<wxWindow*>(temp);

	temp->Bind(wxEVT_COLOURPICKER_CHANGED, ([=](wxCommandEvent e) { on_change_field(e); }), temp->GetId());

	if (m_opt.tooltip.length() > 0) temp->SetToolTip(m_opt.tooltip);

}

void Point::BUILD()
{
	auto size = wxSize(wxDefaultSize);
	if (m_opt.height >= 0) size.SetHeight(m_opt.height);
	if (m_opt.width >= 0) size.SetWidth(m_opt.width);

	auto temp = new wxBoxSizer(wxHORIZONTAL);
	// 	$self->wxSizer($sizer);
	// 
	wxSize field_size(40, -1);

	auto default_pt = static_cast<ConfigOptionPoints*>(m_opt.default_value)->values.at(0);
	double val = default_pt.x;
	wxString X = val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2);
	val = default_pt.y;
	wxString Y = val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2);

	x_textctrl = new wxTextCtrl(m_parent, wxID_ANY, X, wxDefaultPosition, field_size);
	y_textctrl = new wxTextCtrl(m_parent, wxID_ANY, Y, wxDefaultPosition, field_size);

	temp->Add(new wxStaticText(m_parent, wxID_ANY, "x:")/*, 0, wxALIGN_CENTER_VERTICAL, 0*/);
	temp->Add(x_textctrl);
	temp->Add(new wxStaticText(m_parent, wxID_ANY, "   y:")/*, 0, wxALIGN_CENTER_VERTICAL, 0*/);
	temp->Add(y_textctrl);

	x_textctrl->Bind(wxEVT_TEXT, ([=](wxCommandEvent e) { on_change_field(e/*$self->option->opt_id*/); }), x_textctrl->GetId());
	y_textctrl->Bind(wxEVT_TEXT, ([=](wxCommandEvent e) { on_change_field(e/*$self->option->opt_id*/); }), x_textctrl->GetId());

	// 	// recast as a wxWindow to fit the calling convention
	sizer = dynamic_cast<wxSizer*>(temp);

	if (m_opt.tooltip.length() > 0)
	{
		x_textctrl->SetToolTip(m_opt.tooltip);
		y_textctrl->SetToolTip(m_opt.tooltip);
	}
}

void Point::set_value(const Pointf value)
{
	m_disable_change_event = true;

	double val = value.x;
	x_textctrl->SetValue(val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2));
	val = value.y;
	y_textctrl->SetValue(val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2));

	m_disable_change_event = false;
}

boost::any Point::get_value()
{
	Pointf ret_point;
	double val;
	x_textctrl->GetValue().ToDouble(&val);
	ret_point.x = val;
	y_textctrl->GetValue().ToDouble(&val);
	ret_point.y = val;
	return ret_point;
}

} // GUI
} // Slic3r


