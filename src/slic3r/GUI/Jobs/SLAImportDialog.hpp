#ifndef SLAIMPORTDIALOG_HPP
#define SLAIMPORTDIALOG_HPP

#include "SLAImportJob.hpp"

#include <wx/dialog.h>
#include <wx/stattext.h>
#include <wx/combobox.h>
#include <wx/filename.h>
#include <wx/filepicker.h>

#include "libslic3r/AppConfig.hpp"
#include "slic3r/GUI/I18N.hpp"

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"

//#include "libslic3r/Model.hpp"
//#include "libslic3r/PresetBundle.hpp"

namespace Slic3r { namespace GUI {

class SLAImportDialog: public wxDialog, public SLAImportJobView {
    wxFilePickerCtrl *m_filepicker;
    wxComboBox *m_import_dropdown, *m_quality_dropdown;

public:
    SLAImportDialog(Plater *plater)
        : wxDialog{plater, wxID_ANY, "Import SLA archive"}
    {
        auto szvert = new wxBoxSizer{wxVERTICAL};
        auto szfilepck = new wxBoxSizer{wxHORIZONTAL};

        m_filepicker = new wxFilePickerCtrl(this, wxID_ANY,
                                            from_u8(wxGetApp().app_config->get_last_dir()), _(L("Choose SLA archive:")),
                                            "SL1 / SL1S archive files (*.sl1, *.sl1s, *.zip)|*.sl1;*.SL1;*.sl1s;*.SL1S;*.zip;*.ZIP|SL2 archive files (*.sl2)|*.sl2",
                                            wxDefaultPosition, wxDefaultSize, wxFLP_DEFAULT_STYLE | wxFD_OPEN | wxFD_FILE_MUST_EXIST);

        szfilepck->Add(new wxStaticText(this, wxID_ANY, _L("Import file") + ": "), 0, wxALIGN_CENTER);
        szfilepck->Add(m_filepicker, 1);
        szvert->Add(szfilepck, 0, wxALL | wxEXPAND, 5);

        auto szchoices = new wxBoxSizer{wxHORIZONTAL};

        static const std::vector<wxString> inp_choices = {
            _(L("Import model and profile")),
            _(L("Import profile only")),
            _(L("Import model only"))
        };

        m_import_dropdown = new wxComboBox(
            this, wxID_ANY, inp_choices[0], wxDefaultPosition, wxDefaultSize,
            inp_choices.size(), inp_choices.data(), wxCB_READONLY | wxCB_DROPDOWN);

        szchoices->Add(m_import_dropdown);
        szchoices->AddStretchSpacer(1);
        szchoices->Add(new wxStaticText(this, wxID_ANY, _L("Quality") + ": "), 0, wxALIGN_CENTER | wxALL, 5);

        static const std::vector<wxString> qual_choices = {
            _(L("Accurate")),
            _(L("Balanced")),
            _(L("Quick"))
        };

        m_quality_dropdown = new wxComboBox(
            this, wxID_ANY, qual_choices[1], wxDefaultPosition, wxDefaultSize,
            qual_choices.size(), qual_choices.data(), wxCB_READONLY | wxCB_DROPDOWN);
        szchoices->Add(m_quality_dropdown, 1);

        m_import_dropdown->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &) {
            if (get_selection() == Sel::profileOnly)
                m_quality_dropdown->Disable();
            else m_quality_dropdown->Enable();
        });

        szvert->Add(szchoices, 1, wxEXPAND | wxALL, 5);
        auto szbtn = new wxBoxSizer(wxHORIZONTAL);
        szbtn->Add(new wxButton{this, wxID_CANCEL}, 0, wxRIGHT, 5);
        szbtn->Add(new wxButton{this, wxID_OK});
        szvert->Add(szbtn, 0, wxALIGN_RIGHT | wxALL, 5);

        SetSizerAndFit(szvert);
        wxGetApp().UpdateDlgDarkUI(this);
    }

    int ShowModal() override
    {
        CenterOnParent();
        return wxDialog::ShowModal();
    }

    Sel get_selection() const override
    {
        int sel = m_import_dropdown->GetSelection();
        return Sel(std::min(int(Sel::modelOnly), std::max(0, sel)));
    }

    SLAImportQuality get_quality() const override
    {
        switch(m_quality_dropdown->GetSelection())
        {
        case 2: return SLAImportQuality::Fast;
        case 1: return SLAImportQuality::Balanced;
        case 0: return SLAImportQuality::Accurate;
        default:
            return SLAImportQuality::Balanced;
        }
    }

    std::string get_path() const override
    {
        return m_filepicker->GetPath().ToUTF8().data();
    }
};

}} // namespace Slic3r::GUI

#endif // SLAIMPORTDIALOG_HPP
