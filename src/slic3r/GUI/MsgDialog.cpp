#include "MsgDialog.hpp"

#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/statbmp.h>
#include <wx/scrolwin.h>
#include <wx/clipbrd.h>
#include <wx/checkbox.h>
#include <wx/html/htmlwin.h>

#include <boost/algorithm/string/replace.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Utils.hpp"
#include "GUI.hpp"
#include "format.hpp"
#include "I18N.hpp"
#include "ConfigWizard.hpp"
#include "wxExtensions.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "GUI_App.hpp"

namespace Slic3r {
namespace GUI {

MsgDialog::MsgDialog(wxWindow *parent, const wxString &title, const wxString &headline, long style, wxBitmap bitmap)
	: wxDialog(parent ? parent : dynamic_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, boldfont(wxGetApp().normal_font())
	, content_sizer(new wxBoxSizer(wxVERTICAL))
	, btn_sizer(new wxBoxSizer(wxHORIZONTAL))
{
	boldfont.SetWeight(wxFONTWEIGHT_BOLD);

    this->SetFont(wxGetApp().normal_font());
    this->CenterOnParent();

    auto *main_sizer = new wxBoxSizer(wxVERTICAL);
	auto *topsizer = new wxBoxSizer(wxHORIZONTAL);
	auto *rightsizer = new wxBoxSizer(wxVERTICAL);

	auto *headtext = new wxStaticText(this, wxID_ANY, headline);
	headtext->SetFont(boldfont);
    headtext->Wrap(CONTENT_WIDTH*wxGetApp().em_unit());
	rightsizer->Add(headtext);
	rightsizer->AddSpacer(VERT_SPACING);

	rightsizer->Add(content_sizer, 1, wxEXPAND);
    btn_sizer->AddStretchSpacer();

	logo = new wxStaticBitmap(this, wxID_ANY, bitmap.IsOk() ? bitmap : wxNullBitmap);

	topsizer->Add(logo, 0, wxALL, BORDER);
	topsizer->Add(rightsizer, 1, wxTOP | wxBOTTOM | wxRIGHT | wxEXPAND, BORDER);

    main_sizer->Add(topsizer, 1, wxEXPAND);
    main_sizer->Add(new StaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, HORIZ_SPACING);
    main_sizer->Add(btn_sizer, 0, wxALL | wxEXPAND, VERT_SPACING);

    apply_style(style);

	SetSizerAndFit(main_sizer);
}

void MsgDialog::SetButtonLabel(wxWindowID btn_id, const wxString& label, bool set_focus/* = false*/) 
{
    if (wxButton* btn = get_button(btn_id)) {
        btn->SetLabel(label);
        if (set_focus)
            btn->SetFocus();
    }
}

wxButton* MsgDialog::add_button(wxWindowID btn_id, bool set_focus /*= false*/, const wxString& label/* = wxString()*/)
{
    wxButton* btn = new wxButton(this, btn_id, label);
    if (set_focus) {
        btn->SetFocus();
        // For non-MSW platforms SetFocus is not enought to use it as default, when the dialog is closed by ENTER
        // We have to set this button as the (permanently) default one in its dialog
        // See https://twitter.com/ZMelmed/status/1472678454168539146
        btn->SetDefault();
    }
    btn_sizer->Add(btn, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, HORIZ_SPACING);
    btn->Bind(wxEVT_BUTTON, [this, btn_id](wxCommandEvent&) { this->EndModal(btn_id); });
    return btn;
};

wxButton* MsgDialog::get_button(wxWindowID btn_id){
    return static_cast<wxButton*>(FindWindowById(btn_id, this));
}

void MsgDialog::apply_style(long style)
{
    if (style & wxOK)       add_button(wxID_OK, true);
    if (style & wxYES)      add_button(wxID_YES, true);
    if (style & wxNO)       add_button(wxID_NO);
    if (style & wxCANCEL)   add_button(wxID_CANCEL);

    logo->SetBitmap( create_scaled_bitmap(style & wxICON_WARNING        ? "exclamation" :
                                          style & wxICON_INFORMATION    ? "info"        :
                                          style & wxICON_QUESTION       ? "question"    : "PrusaSlicer", this, 64, style & wxICON_ERROR));
}

void MsgDialog::finalize()
{
    wxGetApp().UpdateDlgDarkUI(this);
    Fit();
    this->CenterOnParent();
}


// Text shown as HTML, so that mouse selection and Ctrl-V to copy will work.
static void add_msg_content(wxWindow* parent, wxBoxSizer* content_sizer, wxString msg, bool monospaced_font = false, bool is_marked_msg = false)
{
    wxHtmlWindow* html = new wxHtmlWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_AUTO);

    // count lines in the message
    int msg_lines = 0;
    if (!monospaced_font) {
        int line_len = 55;// count of symbols in one line
        int start_line = 0;
        for (auto i = msg.begin(); i != msg.end(); ++i) {
            if (*i == '\n') {
                int cur_line_len = i - msg.begin() - start_line;
                start_line = i - msg.begin();
                if (cur_line_len == 0 || line_len > cur_line_len)
                    msg_lines++;
                else
                    msg_lines += std::lround((double)(cur_line_len) / line_len);
            }
        }
        msg_lines++;
    }

    wxFont      font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    wxFont      monospace = wxGetApp().code_font();
    wxColour    text_clr = wxGetApp().get_label_clr_default();
    wxColour    bgr_clr = parent->GetBackgroundColour(); //wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    auto        text_clr_str = wxString::Format(wxT("#%02X%02X%02X"), text_clr.Red(), text_clr.Green(), text_clr.Blue());
    auto        bgr_clr_str = wxString::Format(wxT("#%02X%02X%02X"), bgr_clr.Red(), bgr_clr.Green(), bgr_clr.Blue());
    const int   font_size = font.GetPointSize();
    int         size[] = { font_size, font_size, font_size, font_size, font_size, font_size, font_size };
    html->SetFonts(font.GetFaceName(), monospace.GetFaceName(), size);
    html->SetBorders(2);

    // calculate html page size from text
    wxSize page_size;
    int em = wxGetApp().em_unit();
    if (!wxGetApp().mainframe) {
        // If mainframe is nullptr, it means that GUI_App::on_init_inner() isn't completed 
        // (We just show information dialog about configuration version now)
        // And as a result the em_unit value wasn't created yet
        // So, calculate it from the scale factor of Dialog
#if defined(__WXGTK__)
        // Linux specific issue : get_dpi_for_window(this) still doesn't responce to the Display's scale in new wxWidgets(3.1.3).
        // So, initialize default width_unit according to the width of the one symbol ("m") of the currently active font of this window.
        em = std::max<size_t>(10, parent->GetTextExtent("m").x - 1);
#else
        double scale_factor = (double)get_dpi_for_window(parent) / (double)DPI_DEFAULT;
        em = std::max<size_t>(10, 10.0f * scale_factor);
#endif // __WXGTK__
    }

    // if message containes the table
    if (msg.Contains("<tr>")) {
        int lines = msg.Freq('\n') + 1;
        int pos = 0;
        while (pos < (int)msg.Len() && pos != wxNOT_FOUND) {
            pos = msg.find("<tr>", pos + 1);
            lines += 2;
        }
        int page_height = std::min(int(font.GetPixelSize().y+2) * lines, 68 * em);
        page_size = wxSize(68 * em, page_height);
    }
    else {
        wxClientDC dc(parent);
        wxSize msg_sz = dc.GetMultiLineTextExtent(msg);
        page_size = wxSize(std::min(msg_sz.GetX() + 2 * em, 68 * em),
                           std::min(msg_sz.GetY() + 2 * em, 68 * em));
    }
    html->SetMinSize(page_size);

    std::string msg_escaped = xml_escape(into_u8(msg), is_marked_msg);
    boost::replace_all(msg_escaped, "\r\n", "<br>");
    boost::replace_all(msg_escaped, "\n", "<br>");
    if (monospaced_font)
        // Code formatting will be preserved. This is useful for reporting errors from the placeholder parser.
        msg_escaped = std::string("<pre><code>") + msg_escaped + "</code></pre>";
    html->SetPage(format_wxstr("<html>"
                                    "<body bgcolor=%1% link=%2%>"
                                        "<font color=%2%>"
                                            "%3%"
                                        "</font>"
                                    "</body>"
                               "</html>", 
                    bgr_clr_str, text_clr_str, from_u8(msg_escaped)));

    html->Bind(wxEVT_HTML_LINK_CLICKED, [parent](wxHtmlLinkEvent& event) {
        wxGetApp().open_browser_with_warning_dialog(event.GetLinkInfo().GetHref(), parent, false);
        event.Skip(false);
    });

    content_sizer->Add(html, 1, wxEXPAND);
    wxGetApp().UpdateDarkUI(html);
}

// ErrorDialog

ErrorDialog::ErrorDialog(wxWindow *parent, const wxString &msg, bool monospaced_font)
    : MsgDialog(parent, wxString::Format(_(L("%s error")), SLIC3R_APP_NAME), 
                        wxString::Format(_(L("%s has encountered an error")), SLIC3R_APP_NAME), wxOK)
	, msg(msg)
{
    add_msg_content(this, content_sizer, msg, monospaced_font);

	// Use a small bitmap with monospaced font, as the error text will not be wrapped.
	logo->SetBitmap(create_scaled_bitmap("PrusaSlicer_192px_grayscale.png", this, monospaced_font ? 48 : /*1*/84));

    SetMaxSize(wxSize(-1, CONTENT_MAX_HEIGHT*wxGetApp().em_unit()));

    finalize();
}

// WarningDialog

WarningDialog::WarningDialog(wxWindow *parent,
                             const wxString& message,
                             const wxString& caption/* = wxEmptyString*/,
                             long style/* = wxOK*/)
    : MsgDialog(parent, caption.IsEmpty() ? wxString::Format(_L("%s warning"), SLIC3R_APP_NAME) : caption, 
                        wxString::Format(_L("%s has a warning")+":", SLIC3R_APP_NAME), style)
{
    add_msg_content(this, content_sizer, message);
    finalize();
}

#ifdef _WIN32
// MessageDialog

MessageDialog::MessageDialog(wxWindow* parent,
    const wxString& message,
    const wxString& caption/* = wxEmptyString*/,
    long style/* = wxOK*/)
    : MsgDialog(parent, caption.IsEmpty() ? wxString::Format(_L("%s info"), SLIC3R_APP_NAME) : caption, wxEmptyString, style)
{
    add_msg_content(this, content_sizer, get_wraped_wxString(message));
    finalize();
}


// RichMessageDialog

RichMessageDialog::RichMessageDialog(wxWindow* parent,
    const wxString& message,
    const wxString& caption/* = wxEmptyString*/,
    long style/* = wxOK*/)
    : MsgDialog(parent, caption.IsEmpty() ? wxString::Format(_L("%s info"), SLIC3R_APP_NAME) : caption, wxEmptyString, style)
{
    add_msg_content(this, content_sizer, get_wraped_wxString(message));

    m_checkBox = new wxCheckBox(this, wxID_ANY, m_checkBoxText);
    wxGetApp().UpdateDarkUI(m_checkBox);
    m_checkBox->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { m_checkBoxValue = m_checkBox->GetValue(); });

    btn_sizer->Insert(0, m_checkBox, wxALIGN_CENTER_VERTICAL);

    finalize();
}

int RichMessageDialog::ShowModal()
{
    if (m_checkBoxText.IsEmpty())
        m_checkBox->Hide();
    else
        m_checkBox->SetLabelText(m_checkBoxText);
    Layout();

    return wxDialog::ShowModal();
}
#endif

// InfoDialog

InfoDialog::InfoDialog(wxWindow* parent, const wxString &title, const wxString& msg, bool is_marked_msg/* = false*/, long style/* = wxOK | wxICON_INFORMATION*/)
	: MsgDialog(parent, wxString::Format(_L("%s information"), SLIC3R_APP_NAME), title, style)
	, msg(msg)
{
    add_msg_content(this, content_sizer, msg, false, is_marked_msg);
    finalize();
}

wxString get_wraped_wxString(const wxString& text_in, size_t line_len /*=80*/)
{
#ifdef __WXMSW__
    static constexpr const char slash = '\\';
#else
    static constexpr const char slash = '/';
#endif
    static constexpr const char space = ' ';
    static constexpr const char new_line = '\n';

    wxString text = text_in;

    int idx = -1;
    size_t cur_len = 0;
    size_t text_len = text.size();

    for (size_t i = 0; i < text_len; ++ i) {
        if (text[i] == new_line) {
            idx = -1;
            cur_len = 0;
        } else {
            ++ cur_len;
            if (text[i] == space || text[i] == slash)
                idx = i;
            if (cur_len >= line_len && idx >= 0) {
                if (text[idx] == slash) {
                    text.insert(static_cast<size_t>(++ idx), 1, new_line);
                    ++ text_len;
                    ++ i;
                }
                else // space
                    text[idx] = new_line;
                cur_len = i - static_cast<size_t>(idx);
                idx = -1;
            }
        }
    }
    return text;
}

}
}
