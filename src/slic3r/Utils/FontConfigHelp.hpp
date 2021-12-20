#ifndef slic3r_FontConfigHelp_hpp_
#define slic3r_FontConfigHelp_hpp_

#ifdef __linux__
#define EXIST_FONT_CONFIG_INCLUDE
#endif

#ifdef EXIST_FONT_CONFIG_INCLUDE
#include <wx/font.h>
namespace Slic3r::GUI {

/// <summary>
/// helper object for RAII access to font config
/// initialize & finalize FontConfig
/// </summary>
class FontConfigHelp
{
public:
    /// <summary>
    /// initialize font config
    /// </summary>
    FontConfigHelp();
        
    /// <summary>
    /// free font config resources
    /// </summary>
    ~FontConfigHelp();
    
    /// <summary>
    /// initialize font config
    /// Convert wx widget font to file path
    /// inspired by wxpdfdoc -
    /// https://github.com/utelle/wxpdfdoc/blob/5bdcdb9953327d06dc50ec312685ccd9bc8400e0/src/pdffontmanager.cpp
    /// </summary>
    std::string get_font_path(const wxFont &font);
};

} // namespace Slic3r
#endif // EXIST_FONT_CONFIG_INCLUDE
#endif // slic3r_FontConfigHelp_hpp_
