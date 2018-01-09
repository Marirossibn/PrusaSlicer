﻿#include "OptionsGroup.hpp"
#include "ConfigExceptions.hpp"

#include <utility>
#include <wx/tooltip.h>
#include <wx/numformatter.h>

namespace Slic3r { namespace GUI {

const t_field& OptionsGroup::build_field(const Option& opt) {
    return build_field(opt.opt_id, opt.opt);
}
const t_field& OptionsGroup::build_field(const t_config_option_key& id) {
	const ConfigOptionDef& opt = m_options.at(id);
    return build_field(id, opt);
}

const t_field& OptionsGroup::build_field(const t_config_option_key& id, const ConfigOptionDef& opt) {
    // Check the gui_type field first, fall through
    // is the normal type.
    if (opt.gui_type.compare("select") == 0) {
    } else if (opt.gui_type.compare("select_open") == 0) {
		m_fields.emplace(id, STDMOVE(Choice::Create<Choice>(m_parent, opt, id)));
    } else if (opt.gui_type.compare("color") == 0) {
		m_fields.emplace(id, STDMOVE(ColourPicker::Create<ColourPicker>(m_parent, opt, id)));
    } else if (opt.gui_type.compare("f_enum_open") == 0 || 
                opt.gui_type.compare("i_enum_open") == 0 ||
                opt.gui_type.compare("i_enum_closed") == 0) {
		m_fields.emplace(id, STDMOVE(Choice::Create<Choice>(m_parent, opt, id)));
    } else if (opt.gui_type.compare("slider") == 0) {
    } else if (opt.gui_type.compare("i_spin") == 0) { // Spinctrl
    } else { 
        switch (opt.type) {
            case coFloatOrPercent:
            case coFloat:
            case coFloats:
			case coPercent:
			case coPercents:
			case coString:
			case coStrings:
				m_fields.emplace(id, STDMOVE(TextCtrl::Create<TextCtrl>(m_parent, opt, id)));
                break;
			case coBool:
			case coBools:
				m_fields.emplace(id, STDMOVE(CheckBox::Create<CheckBox>(m_parent, opt, id)));
				break;
			case coInt:
			case coInts:
				m_fields.emplace(id, STDMOVE(SpinCtrl::Create<SpinCtrl>(m_parent, opt, id)));
				break;
            case coEnum:
				m_fields.emplace(id, STDMOVE(Choice::Create<Choice>(m_parent, opt, id)));
				break;
            case coPoints:
				m_fields.emplace(id, STDMOVE(Point::Create<Point>(m_parent, opt, id)));
				break;
            case coNone:   break;
            default:
				throw /*//!ConfigGUITypeError("")*/std::logic_error("This control doesn't exist till now"); break;
        }
    }
    // Grab a reference to fields for convenience
    const t_field& field = m_fields[id];
	field->m_on_change = [this](std::string opt_id, boost::any value){
			//! This function will be called from Field.					
			//! Call OptionGroup._on_change(...)
			if (!this->m_disabled) 
				this->on_change_OG(opt_id, value);
	};
    field->m_parent = parent();
    // assign function objects for callbacks, etc.
    return field;
}

void OptionsGroup::append_line(const Line& line) {
//!    if (line.sizer != nullptr || (line.widget != nullptr && line.full_width > 0)){
	if ( (line.sizer != nullptr || line.widget != nullptr) && line.full_width){
		if (line.sizer != nullptr) {
            sizer->Add(line.sizer, 0, wxEXPAND | wxALL, wxOSX ? 0 : 15);
            return;
        }
        if (line.widget != nullptr) {
            sizer->Add(line.widget(m_parent), 0, wxEXPAND | wxALL, wxOSX ? 0 : 15);
            return;
        }
    }

	auto option_set = line.get_options();

	// if we have a single option with no label, no sidetext just add it directly to sizer
	if (option_set.size() == 1 && label_width == 0 && option_set.front().opt.full_width &&
		option_set.front().opt.sidetext.size() == 0 && option_set.front().side_widget == nullptr && 
		line.get_extra_widgets().size() == 0) {
		const auto& option = option_set.front();
		const auto& field = build_field(option);

		if (is_window_field(field))
			sizer->Add(field->getWindow(), 0, wxEXPAND | wxALL, wxOSX ? 0 : 5);
		if (is_sizer_field(field))
			sizer->Add(field->getSizer(), 0, wxEXPAND | wxALL, wxOSX ? 0 : 5);
		return;
	}

    auto grid_sizer = m_grid_sizer;

    // Build a label if we have it
    if (label_width != 0) {
		auto label = new wxStaticText(parent(), wxID_ANY, line.label + (line.label.IsEmpty() ? "" : ":" ), wxDefaultPosition, wxSize(label_width, -1));
        label->SetFont(label_font);
        label->Wrap(label_width); // avoid a Linux/GTK bug
        grid_sizer->Add(label, 0, wxALIGN_CENTER_VERTICAL,0);
        if (line.label_tooltip.compare("") != 0)
            label->SetToolTip(line.label_tooltip);
    }

    // If there's a widget, build it and add the result to the sizer.
    if (line.widget != nullptr) {
        auto wgt = line.widget(parent());
        grid_sizer->Add(wgt, 0, wxEXPAND | wxALL, wxOSX ? 0 : 15);
        return;
    }

    
    // if we have a single option with no sidetext just add it directly to the grid sizer
//!    auto option_set = line.get_options();
    if (option_set.size() == 1 && option_set.front().opt.sidetext.size() == 0 &&
        option_set.front().side_widget == nullptr && line.get_extra_widgets().size() == 0) {
        const auto& option = option_set.front();
        const auto& field = build_field(option);
//!         std::cerr << "single option, no sidetext.\n";
//!         std::cerr << "field parent is not null?: " << (field->parent != nullptr) << "\n";

        if (is_window_field(field)) 
            grid_sizer->Add(field->getWindow(), 0, (option.opt.full_width ? wxEXPAND : 0) | wxALIGN_CENTER_VERTICAL, 0);
        if (is_sizer_field(field)) 
            grid_sizer->Add(field->getSizer(), 0, (option.opt.full_width ? wxEXPAND : 0) | wxALIGN_CENTER_VERTICAL, 0);
        return;
    }

    // if we're here, we have more than one option or a single option with sidetext
    // so we need a horizontal sizer to arrange these things
    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    grid_sizer->Add(sizer, 0, 0, 0);
	for (auto opt : option_set) {
		ConfigOptionDef option = opt.opt;
		// add label if any
		if (option.label != "") {
			auto field_label = new wxStaticText(parent(), wxID_ANY, wxString(option.label) + ":", wxDefaultPosition, wxDefaultSize);
			field_label->SetFont(sidetext_font);
			sizer->Add(field_label, 0, wxALIGN_CENTER_VERTICAL, 0);
		}

		// add field
		const Option& opt_ref = opt;
		auto& field = build_field(opt_ref);
		is_sizer_field(field) ? 
			sizer->Add(field->getSizer(), 0, wxALIGN_CENTER_VERTICAL, 0) :
			sizer->Add(field->getWindow(), 0, wxALIGN_CENTER_VERTICAL, 0);
		
		// add sidetext if any
		if (option.sidetext != "") {
			auto sidetext = new wxStaticText(parent(), wxID_ANY, wxString::FromUTF8(option.sidetext.c_str()), wxDefaultPosition, wxDefaultSize);
			sidetext->SetFont(sidetext_font);
			sizer->Add(sidetext, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 4);
		}

		// add side widget if any
		if (opt.side_widget != nullptr) {
			sizer->Add(opt.side_widget(parent())/*!.target<wxWindow>()*/, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 1);	//! requires verification
		}

		if (opt.opt_id != option_set.back().opt_id) //! istead of (opt != option_set.back())
		{
			sizer->AddSpacer(4);
	    }
	}
	// add extra sizers if any
	for (auto extra_widget : line.get_extra_widgets()) {
		sizer->Add(extra_widget(parent())/*!.target<wxWindow>()*/, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 4);		//! requires verification
	}
}

Line OptionsGroup::create_single_option_line(const Option& option) const {
    Line retval {option.opt.label, option.opt.tooltip};	
    Option tmp(option);
    tmp.opt.label = std::string("");
    retval.append_option(tmp);
    return retval;
}

void OptionsGroup::on_change_OG(t_config_option_key id, /*config_value*/boost::any value) {
	if (m_on_change != nullptr)
		m_on_change(id, value);
}

void OptionsGroup::_on_kill_focus (t_config_option_key id) { 
    // do nothing.
}

Option ConfigOptionsGroup::get_option(const std::string opt_key, int opt_index /*= -1*/)
{
	if (!m_config->has(opt_key)) {
		//! exception  ("No $opt_key in ConfigOptionsGroup config");
	}

	std::string opt_id = opt_index == -1 ? opt_key : opt_key + "#" + std::to_string(opt_index);
	std::pair<std::string, int> pair(opt_key, opt_index);
	m_opt_map.emplace(opt_id, pair);

	return Option(*m_config->def()->get(opt_key), opt_id);
}

void ConfigOptionsGroup::on_change_OG(t_config_option_key opt_id, boost::any value)
{
	if (!m_opt_map.empty())
	{
		std::string opt_key = m_opt_map.at(opt_id).first;
		int opt_index = m_opt_map.at(opt_id).second;
		auto option = m_options.at(opt_id);

		// get value
		auto field_value = get_value(opt_id);
		if (option.gui_flags.compare("serialized")==0) {
			if (opt_index != -1){
				// 		die "Can't set serialized option indexed value" ;
			}
			// 		# Split a string to multiple strings by a semi - colon.This is the old way of storing multi - string values.
			// 		# Currently used for the post_process config value only.
			// 		my @values = split / ; / , $field_value;
			// 		$self->config->set($opt_key, \@values);
		}
		else {
			if (opt_index == -1) {
				// change_opt_value(*m_config, opt_key, field_value);
				//!? why field_value?? in this case changed value will be lose! No?
				change_opt_value(*m_config, opt_key, value);
			}
			else {
// 				auto value = m_config->get($opt_key);
// 				$value->[$opt_index] = $field_value;
// 				$self->config->set($opt_key, $value);
			}
		}
	}

	OptionsGroup::on_change_OG(opt_id, value);
}

void ConfigOptionsGroup::reload_config(){
	for (std::map< std::string, std::pair<std::string, int> >::iterator it = m_opt_map.begin(); it != m_opt_map.end(); ++it) {
		auto opt_id = it->first;
		std::string opt_key = m_opt_map.at(opt_id).first;
		int opt_index = m_opt_map.at(opt_id).second;
		auto option = m_options.at(opt_id);
		set_value(opt_id, config_value(opt_key, opt_index, option.gui_flags.compare("serialized") == 0 ));
	}
}

boost::any ConfigOptionsGroup::config_value(std::string opt_key, int opt_index, bool deserialize){

	if (deserialize) {
		// Want to edit a vector value(currently only multi - strings) in a single edit box.
		// Aggregate the strings the old way.
		// Currently used for the post_process config value only.
		if (opt_index != -1)
			throw std::out_of_range("Can't deserialize option indexed value");
// 		return join(';', m_config->get(opt_key)});
		return get_config_value(*m_config, opt_key);
	}
	else {
//		return opt_index == -1 ? m_config->get(opt_key) : m_config->get_at(opt_key, opt_index);
		return get_config_value(*m_config, opt_key, opt_index);
	}
}

boost::any ConfigOptionsGroup::get_config_value(DynamicPrintConfig& config, std::string opt_key, int opt_index/* = -1*/)
{
	boost::any ret;
	wxString text_value = wxString("");
	const ConfigOptionDef* opt = config.def()->get(opt_key);
	switch (opt->type){
	case coFloatOrPercent:{
		const auto &value = *config.option<ConfigOptionFloatOrPercent>(opt_key);
		if (value.percent)
		{
			text_value = wxString::Format(_T("%i"), int(value.value));
			text_value += "%";
		}
		else
			text_value = wxNumberFormatter::ToString(value.value, 2);
		ret = text_value;
		break;
	}
	case coPercent:{
		double val = config.option<ConfigOptionPercent>(opt_key)->value;
		text_value = wxString::Format(_T("%i"), int(val));
		ret = text_value;// += "%";
	}
		break;
	case coPercents:
	case coFloats:{
		double val = config.opt_float(opt_key, 0/*opt_index*/);
		ret = val - int(val) == 0 ? 
			wxString::Format(_T("%i"), int(val)) : 
			wxNumberFormatter::ToString(val, 2);
		}
		break;
	case coFloat:
		ret = wxNumberFormatter::ToString(config.opt_float(opt_key), 2);
		break;
	case coString:
		ret = static_cast<wxString>(config.opt_string(opt_key));
		break;
	case coStrings:
		if (config.option<ConfigOptionStrings>(opt_key)->values.empty())
			ret = text_value;
		else
			ret = static_cast<wxString>(config.opt_string(opt_key, static_cast<unsigned int>(0)/*opt_index*/));
		break;
	case coBool:
		ret = config.opt_bool(opt_key);
		break;
	case coBools:
		ret = config.opt_bool(opt_key, 0/*opt_index*/);
		break;
	case coInt:
		ret = config.opt_int(opt_key);
		break;
	case coInts:
		ret = config.opt_int(opt_key, 0/*opt_index*/);
		break;
	case coEnum:{
		if (opt_key.compare("external_fill_pattern") == 0 ||
			opt_key.compare("fill_pattern") == 0 ||
			opt_key.compare("external_fill_pattern") == 0 ){
			ret = static_cast<int>(config.option<ConfigOptionEnum<InfillPattern>>(opt_key)->value);
		}
		else if (opt_key.compare("gcode_flavor") == 0 ){
			ret = static_cast<int>(config.option<ConfigOptionEnum<GCodeFlavor>>(opt_key)->value);
		}
		else if (opt_key.compare("support_material_pattern") == 0){
			ret = static_cast<int>(config.option<ConfigOptionEnum<SupportMaterialPattern>>(opt_key)->value);
		}
		else if (opt_key.compare("seam_position") == 0)
			ret = static_cast<int>(config.option<ConfigOptionEnum<SeamPosition>>(opt_key)->value);
	}
		break;
	case coPoints:{
		const auto &value = *config.option<ConfigOptionPoints>(opt_key);
		ret = value.values.at(0);
		}
		break;
	case coNone:
	default:
		break;
	}
	return ret;
}

} // GUI
} // Slic3r
