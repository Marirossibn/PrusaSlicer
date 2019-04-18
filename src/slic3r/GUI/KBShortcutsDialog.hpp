#ifndef slic3r_GUI_KBShortcutsDialog_hpp_
#define slic3r_GUI_KBShortcutsDialog_hpp_

#include <wx/wx.h>
#include <map>
#include <vector>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"

namespace Slic3r { 
namespace GUI {

class KBShortcutsDialog : public DPIDialog/*wxDialog*/
{
    enum PLACED_SIZER_ID
    {
        szLeft = 0,
        szRight
    };

    typedef std::pair<std::string, std::string> Shortcut;
    typedef std::vector< Shortcut >             Shortcuts;
    typedef std::vector< std::pair<wxString, std::pair<Shortcuts, PLACED_SIZER_ID>> >   ShortcutsVec;

    wxString text_info {wxEmptyString};

    ShortcutsVec    m_full_shortcuts;
    wxSizer*        main_sizer;
    PrusaBitmap     m_logo_bmp;
    std::vector<wxStaticBitmap*>    m_head_bitmaps;

public:
    KBShortcutsDialog();
    
    void fill_shortcuts();

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    void onCloseDialog(wxEvent &);
};

} // namespace GUI
} // namespace Slic3r

#endif
