#ifndef slic3r_TextConfiguration_hpp_
#define slic3r_TextConfiguration_hpp_

#include <vector>
#include <string>
#include <optional>
#include <cereal/cereal.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/string.hpp>
#include <cereal/archives/binary.hpp>
#include "Point.hpp" // Transform3d

namespace Slic3r {

/// <summary>
/// User modifiable property of text style
/// NOTE: OnEdit fix serializations: EmbossStylesSerializable, TextConfigurationSerialization
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

    // Flag that text should use surface cutted from object
    // FontProp::distance should without value
    // FontProp::emboss should be positive number
    // Note: default value is false
    bool use_surface;

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

    // Parameter for True Type Font collections
    // Select index of font in collection
    std::optional<unsigned int> collection_number;

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
        : emboss(depth), size_in_mm(line_height), use_surface(false)
    {}

    bool operator==(const FontProp& other) const {
        return 
            char_gap == other.char_gap && 
            line_gap == other.line_gap &&
            use_surface == other.use_surface &&
            is_approx(emboss, other.emboss) &&
            is_approx(size_in_mm, other.size_in_mm) && 
            is_approx(boldness, other.boldness) &&
            is_approx(skew, other.skew) &&
            is_approx(distance, other.distance) &&
            is_approx(angle, other.angle);
    }

    // undo / redo stack recovery
    template<class Archive> void save(Archive &ar) const
    {
        ar(emboss, use_surface, size_in_mm);
        cereal::save(ar, char_gap);
        cereal::save(ar, line_gap);
        cereal::save(ar, boldness);
        cereal::save(ar, skew);
        cereal::save(ar, distance);
        cereal::save(ar, angle);
        cereal::save(ar, collection_number);
        cereal::save(ar, family);
        cereal::save(ar, face_name);
        cereal::save(ar, style);
        cereal::save(ar, weight);        
    }
    template<class Archive> void load(Archive &ar)
    {
        ar(emboss, use_surface, size_in_mm);
        cereal::load(ar, char_gap);
        cereal::load(ar, line_gap);
        cereal::load(ar, boldness);
        cereal::load(ar, skew);
        cereal::load(ar, distance);
        cereal::load(ar, angle);
        cereal::load(ar, collection_number);
        cereal::load(ar, family);
        cereal::load(ar, face_name);
        cereal::load(ar, style);
        cereal::load(ar, weight);
    }
};

/// <summary>
/// Style of embossed text
/// (Path + Type) must define how to open font for using on different OS
/// NOTE: OnEdit fix serializations: EmbossStylesSerializable, TextConfigurationSerialization
/// </summary>
struct EmbossStyle
{
    // Human readable name of style it is shown in GUI
    std::string name;

    // Define how to open font
    // Meaning depend on type
    std::string path;

    enum class Type;
    // Define what is stored in path
    Type type { Type::undefined };

    // User modification of font style
    FontProp prop;

    // when name is empty than Font item was loaded from .3mf file 
    // and potentionaly it is not reproducable
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

    bool operator==(const EmbossStyle &other) const
    {
        return 
            type == other.type &&
            prop == other.prop &&
            name == other.name &&
            path == other.path
            ;
    }

    // undo / redo stack recovery
    template<class Archive> void serialize(Archive &ar){
        ar(name, path, type, prop);
    }
};

// Emboss style name inside vector is unique
// It is not map beacuse items has own order (view inside of slect)
// It is stored into AppConfig by EmbossStylesSerializable
using EmbossStyles = std::vector<EmbossStyle>;

/// <summary>
/// Define how to create 'Text volume'
/// It is stored into .3mf by TextConfigurationSerialization
/// It is part of ModelVolume optional data
/// </summary>
struct TextConfiguration
{
    // Style of embossed text
    EmbossStyle style;

    // Embossed text value
    std::string text = "None";

    // !!! Volume stored in .3mf has transformed vertices.
    // (baked transformation into vertices position)
    // Only place for fill this is when load from .3mf 
    // This is correct volume transformation
    std::optional<Transform3d> fix_3mf_tr;

    // undo / redo stack recovery
    template<class Archive> void save(Archive &ar) const{
        ar(text, style); 
        cereal::save(ar, fix_3mf_tr);
    }
    template<class Archive> void load(Archive &ar){
        ar(text, style); 
        cereal::load(ar, fix_3mf_tr);
    }
};    

} // namespace Slic3r

#endif // slic3r_TextConfiguration_hpp_
