#ifndef slic3r_TextConfiguration_hpp_
#define slic3r_TextConfiguration_hpp_

#include <vector>
#include <string>
#include <optional>

namespace Slic3r {

/// <summary>
/// User modifiable property of text style
/// </summary>
struct FontProp
{
    // define extra space between letters, negative mean closer letter
    // When not set value is zero and is not stored
    std::optional<int> char_gap; // [in font point]

    // define extra space between lines, negative mean closer lines
    // When not set value is zero and is not stored
    std::optional<int> line_gap; // [in font point]

    // Z depth of text 
    float emboss; // [in mm]

    // positive value mean wider character shape
    // negative value mean tiner character shape
    // When not set value is zero and is not stored
    std::optional<float> boldness; // [in mm]

    // positive value mean italic of character (CW)
    // negative value mean CCW skew (unItalic)
    // When not set value is zero and is not stored
    std::optional<float> skew; // [ration x:y]

    // distance from surface point
    // used for move over model surface
    // When not set value is zero and is not stored
    std::optional<float> distance; // [in mm]

    // change up vector direction of font
    // When not set value is zero and is not stored
    std::optional<float> angle; // [in radians]

    //enum class Align {
    //    left,
    //    right,
    //    center,
    //    top_left,
    //    top_right,
    //    top_center,
    //    bottom_left,
    //    bottom_right,
    //    bottom_center
    //};
    //// change pivot of text
    //// When not set, center is used and is not stored
    //std::optional<Align> align;

    //////
    // Duplicit data to wxFontDescriptor
    // used for store/load .3mf file
    //////

    // Height of text line (letters)
    // duplicit to wxFont::PointSize
    float size_in_mm; // [in mm]

    // Additional data about font to be able to find substitution,
    // when same font is not installed
    std::optional<std::string> family;
    std::optional<std::string> face_name;
    std::optional<std::string> style;
    std::optional<std::string> weight;

    /// <summary>
    /// Only constructor with restricted values
    /// </summary>
    /// <param name="line_height">Y size of text [in mm]</param>
    /// <param name="depth">Z size of text [in mm]</param>
    FontProp(float line_height = 10.f, float depth = 2.f)
        : emboss(depth), size_in_mm(line_height)
    {}

    bool operator==(const FontProp& other) const {
        return 
            char_gap == other.char_gap && 
            line_gap == other.line_gap &&
            is_approx(emboss, other.emboss) &&
            is_approx(size_in_mm, other.size_in_mm) && 
            is_approx(boldness, other.boldness) &&
            is_approx(skew, other.skew) &&
            is_approx(distance, other.distance) &&
            is_approx(angle, other.angle);
    }

    // undo / redo stack recovery
    //template<class Archive> void serialize(Archive &ar)
    //{
    //    ar(char_gap, line_gap, emboss, boldness, skew, size_in_mm, family, face_name, style, weight);
    //}
};

/// <summary>
/// Style of embossed text
/// (Path + Type) must define how to open font for using on different OS
/// NOTE: OnEdit fix serializations: FontListSerializable, TextConfigurationSerialization
/// </summary>
struct FontItem
{
    // Human readable name of style it is shown in GUI
    std::string name;

    // Define how to open font
    // Meaning depend on type
    std::string path;

    enum class Type;
    // Define what is stored in path
    Type type;

    // User modification of font style
    FontProp prop;

    FontItem() : type(Type::undefined){} // set undefined type

    // when name is empty than Font item was loaded from .3mf file 
    // and potentionaly it is not reproducable
    FontItem(const std::string &name,
             const std::string &path,
             Type               type,
             const FontProp &   prop)
        : name(name), path(path), type(type), prop(prop) // copy values
    {}

    // define data stored in path
    // when wx change way of storing add new descriptor Type
    enum class Type { 
        undefined = 0,

        // wx font descriptors are platform dependent
        // path is font descriptor generated by wxWidgets
        wx_win_font_descr, // on Windows 
        wx_lin_font_descr, // on Linux
        wx_mac_font_descr, // on Max OS

        // TrueTypeFont file loacation on computer
        // for privacy: only filename is stored into .3mf
        file_path
    };

    bool operator==(const FontItem &other) const {
        return 
            type == other.type &&
            prop == other.prop &&
            name == other.name &&
            path == other.path
            ;
    }

    //// undo / redo stack recovery
    //template<class Archive> void serialize(Archive &ar)
    //{
    //    ar(name, path, (int) type, prop);
    //}
};

// Font item name inside list is unique
// FontList is not map beacuse items order matters (view of list)
// It is stored into AppConfig by FontListSerializable
using FontList = std::vector<FontItem>;

/// <summary>
/// Define how to create 'Text volume'
/// It is stored into .3mf by TextConfigurationSerialization
/// It is part of ModelVolume optional data
/// </summary>
struct TextConfiguration
{
    // Style of embossed text
    FontItem font_item;

    // Embossed text value
    std::string text = "None";

    TextConfiguration() = default; // optional needs empty constructor
    TextConfiguration(const FontItem &font_item, const std::string &text)
        : font_item(font_item), text(text)
    {}

    // undo / redo stack recovery
    //template<class Archive> void serialize(Archive &ar){ ar(text, font_item); }
};    

} // namespace Slic3r

#endif // slic3r_TextConfiguration_hpp_
