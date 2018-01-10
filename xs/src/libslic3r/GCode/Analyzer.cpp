#include <memory.h>
#include <string.h>
#include <float.h>

#include "../libslic3r.h"
#include "../PrintConfig.hpp"
//############################################################################################################
#if ENRICO_GCODE_PREVIEW
#include "Print.hpp"
#endif // ENRICO_GCODE_PREVIEW
//############################################################################################################

#include "Analyzer.hpp"

//############################################################################################################
#if ENRICO_GCODE_PREVIEW
static const std::string AXIS_STR = "XYZE";
static const float MMMIN_TO_MMSEC = 1.0f / 60.0f;
static const float INCHES_TO_MM = 25.4f;
static const float DEFAULT_FEEDRATE = 0.0f;
static const unsigned int DEFAULT_EXTRUDER_ID = 0;
static const Slic3r::Pointf3 DEFAULT_START_POSITION = Slic3r::Pointf3(0.0f, 0.0f, 0.0f);
static const float DEFAULT_START_EXTRUSION = 0.0f;
#endif // ENRICO_GCODE_PREVIEW
//############################################################################################################

namespace Slic3r {

//############################################################################################################
#if !ENRICO_GCODE_PREVIEW
//############################################################################################################
void GCodeMovesDB::reset()
{
    for (size_t i = 0; i < m_layers.size(); ++ i)
        delete m_layers[i];
    m_layers.clear();
}

GCodeAnalyzer::GCodeAnalyzer(const Slic3r::GCodeConfig *config) : 
    m_config(config)
{
    reset();
    m_moves = new GCodeMovesDB();
}

GCodeAnalyzer::~GCodeAnalyzer()
{
    delete m_moves;
}

void GCodeAnalyzer::reset()
{
    output_buffer.clear();
    output_buffer_length = 0;

    m_current_extruder = 0;
    // Zero the position of the XYZE axes + the current feed
    memset(m_current_pos, 0, sizeof(float) * 5);
    m_current_extrusion_role = erNone;
    m_current_extrusion_width = 0;
    m_current_extrusion_height = 0;
    // Expect the first command to fill the nozzle (deretract).
    m_retracted = true;
    m_moves->reset();
}

const char* GCodeAnalyzer::process(const char *szGCode, bool flush)
{
    // Reset length of the output_buffer.
    output_buffer_length = 0;

    if (szGCode != 0) {
        const char *p = szGCode;
        while (*p != 0) {
            // Find end of the line.
            const char *endl = p;
            // Slic3r always generates end of lines in a Unix style.
            for (; *endl != 0 && *endl != '\n'; ++ endl) ;
            // Process a G-code line, store it into the provided GCodeLine object.
            bool should_output = process_line(p, endl - p);
            if (*endl == '\n') 
                ++ endl;
            if (should_output)
                push_to_output(p, endl - p);
            p = endl;
        }
    }

    return output_buffer.data();
}

// Is a white space?
static inline bool is_ws(const char c) { return c == ' ' || c == '\t'; }
// Is it an end of line? Consider a comment to be an end of line as well.
static inline bool is_eol(const char c) { return c == 0 || c == '\r' || c == '\n' || c == ';'; };
// Is it a white space or end of line?
static inline bool is_ws_or_eol(const char c) { return is_ws(c) || is_eol(c); };

// Eat whitespaces.
static void eatws(const char *&line)
{
    while (is_ws(*line)) 
        ++ line;
}

// Parse an int starting at the current position of a line.
// If succeeded, the line pointer is advanced.
static inline int parse_int(const char *&line)
{
    char *endptr = NULL;
    long result = strtol(line, &endptr, 10);
    if (endptr == NULL || !is_ws_or_eol(*endptr))
        throw std::runtime_error("GCodeAnalyzer: Error parsing an int");
    line = endptr;
    return int(result);
};

// Parse an int starting at the current position of a line.
// If succeeded, the line pointer is advanced.
static inline float parse_float(const char *&line)
{
    char *endptr = NULL;
    float result = strtof(line, &endptr);
    if (endptr == NULL || !is_ws_or_eol(*endptr))
        throw std::runtime_error("GCodeAnalyzer: Error parsing a float");
    line = endptr;
    return result;
};

#define EXTRUSION_ROLE_TAG ";_EXTRUSION_ROLE:"
bool GCodeAnalyzer::process_line(const char *line, const size_t len)
{
    if (strncmp(line, EXTRUSION_ROLE_TAG, strlen(EXTRUSION_ROLE_TAG)) == 0) {
        line += strlen(EXTRUSION_ROLE_TAG);
        int role = atoi(line);
        this->m_current_extrusion_role = ExtrusionRole(role);
        return false;
    }

/*
    // Set the type, copy the line to the buffer.
    buf.type = GCODE_MOVE_TYPE_OTHER;
    buf.modified = false;
    if (buf.raw.size() < len + 1)
        buf.raw.assign(line, line + len + 1);
    else
        memcpy(buf.raw.data(), line, len);
    buf.raw[len] = 0;
    buf.raw_length = len;

    memcpy(buf.pos_start, m_current_pos, sizeof(float)*5);
    memcpy(buf.pos_end, m_current_pos, sizeof(float)*5);
    memset(buf.pos_provided, 0, 5);

    buf.volumetric_extrusion_rate = 0.f;
    buf.volumetric_extrusion_rate_start = 0.f;
    buf.volumetric_extrusion_rate_end = 0.f;
    buf.max_volumetric_extrusion_rate_slope_positive = 0.f;
    buf.max_volumetric_extrusion_rate_slope_negative = 0.f;
	buf.extrusion_role = m_current_extrusion_role;

    // Parse the G-code line, store the result into the buf.
    switch (toupper(*line ++)) {
    case 'G': {
        int gcode = parse_int(line);
        eatws(line);
        switch (gcode) {
        case 0:
        case 1:
        {
            // G0, G1: A FFF 3D printer does not make a difference between the two.
            float new_pos[5];
            memcpy(new_pos, m_current_pos, sizeof(float)*5);
            bool  changed[5] = { false, false, false, false, false };
            while (!is_eol(*line)) {
                char axis = toupper(*line++);
                int  i = -1;
                switch (axis) {
                case 'X':
                case 'Y':
                case 'Z':
                    i = axis - 'X';
                    break;
                case 'E':
                    i = 3;
                    break;
                case 'F':
                    i = 4;
                    break;
                default:
                    assert(false);
                }
                if (i == -1)
                    throw std::runtime_error(std::string("GCodeAnalyzer: Invalid axis for G0/G1: ") + axis);
                buf.pos_provided[i] = true;
                new_pos[i] = parse_float(line);
                if (i == 3 && m_config->use_relative_e_distances.value)
                    new_pos[i] += m_current_pos[i];
                changed[i] = new_pos[i] != m_current_pos[i];
                eatws(line);
            }
            if (changed[3]) {
                // Extrusion, retract or unretract.
                float diff = new_pos[3] - m_current_pos[3];
                if (diff < 0) {
                    buf.type = GCODE_MOVE_TYPE_RETRACT;
                    m_retracted = true;
                } else if (! changed[0] && ! changed[1] && ! changed[2]) {
                    // assert(m_retracted);
                    buf.type = GCODE_MOVE_TYPE_UNRETRACT;
                    m_retracted = false;
                } else {
                    assert(changed[0] || changed[1]);
                    // Moving in XY plane.
                    buf.type = GCODE_MOVE_TYPE_EXTRUDE;
                    // Calculate the volumetric extrusion rate.
                    float diff[4];
                    for (size_t i = 0; i < 4; ++ i)
                        diff[i] = new_pos[i] - m_current_pos[i];
                    // volumetric extrusion rate = A_filament * F_xyz * L_e / L_xyz [mm^3/min]
                    float len2 = diff[0]*diff[0]+diff[1]*diff[1]+diff[2]*diff[2];
                    float rate = m_filament_crossections[m_current_extruder] * new_pos[4] * sqrt((diff[3]*diff[3])/len2);
                    buf.volumetric_extrusion_rate       = rate;
                    buf.volumetric_extrusion_rate_start = rate;
                    buf.volumetric_extrusion_rate_end   = rate;
                    m_stat.update(rate, sqrt(len2));
                    if (rate < 10.f) {
                    	printf("Extremely low flow rate: %f\n", rate);
                    }
                }
            } else if (changed[0] || changed[1] || changed[2]) {
                // Moving without extrusion.
                buf.type = GCODE_MOVE_TYPE_MOVE;
            }
            memcpy(m_current_pos, new_pos, sizeof(float) * 5);
            break;
        }
        case 92:
        {
            // G92 : Set Position
            // Set a logical coordinate position to a new value without actually moving the machine motors.
            // Which axes to set?
            bool set = false;
            while (!is_eol(*line)) {
                char axis = toupper(*line++);
                switch (axis) {
                case 'X':
                case 'Y':
                case 'Z':
                    m_current_pos[axis - 'X'] = (!is_ws_or_eol(*line)) ? parse_float(line) : 0.f;
                    set = true;
                    break;
                case 'E':
                    m_current_pos[3] = (!is_ws_or_eol(*line)) ? parse_float(line) : 0.f;
                    set = true;
                    break;
                default:
                    throw std::runtime_error(std::string("GCodeAnalyzer: Incorrect axis in a G92 G-code: ") + axis);
                }
                eatws(line);
            }
            assert(set);
            break;
        }
        case 10:
        case 22:
            // Firmware retract.
            buf.type = GCODE_MOVE_TYPE_RETRACT;
            m_retracted = true;
            break;
        case 11:
        case 23:
            // Firmware unretract.
            buf.type = GCODE_MOVE_TYPE_UNRETRACT;
            m_retracted = false;
            break;
        default:
            // Ignore the rest.
        break;
        }
        break;
    }
    case 'M': {
        int mcode = parse_int(line);
        eatws(line);
        switch (mcode) {
        default:
            // Ignore the rest of the M-codes.
        break;
        }
        break;
    }
    case 'T':
    {
        // Activate an extruder head.
        int new_extruder = parse_int(line);
        if (new_extruder != m_current_extruder) {
            m_current_extruder = new_extruder;
            m_retracted = true;
            buf.type = GCODE_MOVE_TYPE_TOOL_CHANGE;
        } else {
            buf.type = GCODE_MOVE_TYPE_NOOP;
        }
        break;
    }
    }

    buf.extruder_id = m_current_extruder;
    memcpy(buf.pos_end, m_current_pos, sizeof(float)*5);
*/
	return true;
}

void GCodeAnalyzer::push_to_output(const char *text, const size_t len, bool add_eol)
{
    // New length of the output buffer content.
    size_t len_new = output_buffer_length + len + 1;
    if (add_eol)
        ++ len_new;

    // Resize the output buffer to a power of 2 higher than the required memory.
    if (output_buffer.size() < len_new) {
        size_t v = len_new;
        // Compute the next highest power of 2 of 32-bit v
        // http://graphics.stanford.edu/~seander/bithacks.html
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v++;
        output_buffer.resize(v);
    }

    // Copy the text to the output.
    if (len != 0) {
        memcpy(output_buffer.data() + output_buffer_length, text, len);
        output_buffer_length += len;
    }
    if (add_eol)
        output_buffer[output_buffer_length ++] = '\n';
    output_buffer[output_buffer_length] = 0;
}
//############################################################################################################
#endif // !ENRICO_GCODE_PREVIEW
//############################################################################################################

//############################################################################################################
#if ENRICO_GCODE_PREVIEW
const std::string GCodeAnalyzer::Extrusion_Role_Tag = "_ANALYZER_EXTR_ROLE:";
const std::string GCodeAnalyzer::Mm3_Per_Mm_Tag = "_ANALYZER_MM3_PER_MM:";
const std::string GCodeAnalyzer::Width_Tag = "_ANALYZER_WIDTH:";
const std::string GCodeAnalyzer::Height_Tag = "_ANALYZER_HEIGHT:";

const double GCodeAnalyzer::Default_mm3_per_mm = 0.0;
const float GCodeAnalyzer::Default_Width = 0.0f;
const float GCodeAnalyzer::Default_Height = 0.0f;

GCodeAnalyzer::Metadata::Metadata()
    : extrusion_role(erNone)
    , extruder_id(DEFAULT_EXTRUDER_ID)
    , mm3_per_mm(GCodeAnalyzer::Default_mm3_per_mm)
    , width(GCodeAnalyzer::Default_Width)
    , height(GCodeAnalyzer::Default_Height)
    , feedrate(DEFAULT_FEEDRATE)
{
}

GCodeAnalyzer::Metadata::Metadata(ExtrusionRole extrusion_role, unsigned int extruder_id, double mm3_per_mm, float width, float height, float feedrate)
    : extrusion_role(extrusion_role)
    , extruder_id(extruder_id)
    , mm3_per_mm(mm3_per_mm)
    , width(width)
    , height(height)
    , feedrate(feedrate)
{
}

bool GCodeAnalyzer::Metadata::operator != (const GCodeAnalyzer::Metadata& other) const
{
    if (extrusion_role != other.extrusion_role)
        return true;

    if (extruder_id != other.extruder_id)
        return true;

    if (mm3_per_mm != other.mm3_per_mm)
        return true;

    if (width != other.width)
        return true;

    if (height != other.height)
        return true;

    if (feedrate != other.feedrate)
        return true;

    return false;
}

GCodeAnalyzer::GCodeMove::GCodeMove(GCodeMove::EType type, ExtrusionRole extrusion_role, unsigned int extruder_id, double mm3_per_mm, float width, float height, float feedrate, const Pointf3& start_position, const Pointf3& end_position, float delta_extruder)
    : type(type)
    , data(extrusion_role, extruder_id, mm3_per_mm, width, height, feedrate)
    , start_position(start_position)
    , end_position(end_position)
    , delta_extruder(delta_extruder)
{
}

GCodeAnalyzer::GCodeMove::GCodeMove(GCodeMove::EType type, const GCodeAnalyzer::Metadata& data, const Pointf3& start_position, const Pointf3& end_position, float delta_extruder)
    : type(type)
    , data(data)
    , start_position(start_position)
    , end_position(end_position)
    , delta_extruder(delta_extruder)
{
}

const GCodeAnalyzer::PreviewData::Color GCodeAnalyzer::PreviewData::Color::Dummy(0.0f, 0.0f, 0.0f, 0.0f);

GCodeAnalyzer::PreviewData::Color::Color()
{
    rgba[0] = 1.0f;
    rgba[1] = 1.0f;
    rgba[2] = 1.0f;
    rgba[3] = 1.0f;
}

GCodeAnalyzer::PreviewData::Color::Color(float r, float g, float b, float a)
{
    rgba[0] = r;
    rgba[1] = g;
    rgba[2] = b;
    rgba[3] = a;
}

GCodeAnalyzer::PreviewData::Extrusion::Layer::Layer(float z, const ExtrusionPaths& paths)
    : z(z)
    , paths(paths)
{
}

GCodeAnalyzer::PreviewData::Travel::Polyline::Polyline(EType type, EDirection direction, const Polyline3& polyline)
    : type(type)
    , direction(direction)
    , polyline(polyline)
{
}

const GCodeAnalyzer::PreviewData::Color GCodeAnalyzer::PreviewData::Range::Default_Colors[Colors_Count] =
{
    Color(0.043f, 0.173f, 0.478f, 1.0f),
    Color(0.075f, 0.349f, 0.522f, 1.0f),
    Color(0.110f, 0.533f, 0.569f, 1.0f),
    Color(0.016f, 0.839f, 0.059f, 1.0f),
    Color(0.667f, 0.949f, 0.000f, 1.0f),
    Color(0.988f, 0.975f, 0.012f, 1.0f),
    Color(0.961f, 0.808f, 0.039f, 1.0f),
    Color(0.890f, 0.533f, 0.125f, 1.0f),
    Color(0.820f, 0.408f, 0.188f, 1.0f),
    Color(0.761f, 0.322f, 0.235f, 1.0f)
};

GCodeAnalyzer::PreviewData::Range::Range()
{
    reset();
}

void GCodeAnalyzer::PreviewData::Range::reset()
{
    min = FLT_MAX;
    max = -FLT_MAX;
}

bool GCodeAnalyzer::PreviewData::Range::empty() const
{
    return min == max;
}

void GCodeAnalyzer::PreviewData::Range::update_from(float value)
{
    min = std::min(min, value);
    max = std::max(max, value);
}

void GCodeAnalyzer::PreviewData::Range::set_from(const Range& other)
{
    min = other.min;
    max = other.max;
}

const GCodeAnalyzer::PreviewData::Color& GCodeAnalyzer::PreviewData::Range::get_color_at_max() const
{
    return colors[Colors_Count - 1];
}

const GCodeAnalyzer::PreviewData::Color& GCodeAnalyzer::PreviewData::Range::get_color_at(float value) const
{
    return empty() ? get_color_at_max() : colors[clamp((unsigned int)0, Colors_Count - 1, unsigned int((value - min) / _step()))];
}

float GCodeAnalyzer::PreviewData::Range::_step() const
{
    return (max - min) / (float)Colors_Count;
}

const GCodeAnalyzer::PreviewData::Color GCodeAnalyzer::PreviewData::Extrusion::Default_Extrusion_Role_Colors[Num_Extrusion_Roles] =
{
    Color(0.0f, 0.0f, 0.0f, 1.0f), // erNone
    Color(1.0f, 0.0f, 0.0f, 1.0f), // erPerimeter
    Color(0.0f, 1.0f, 0.0f, 1.0f), // erExternalPerimeter
    Color(0.0f, 0.0f, 1.0f, 1.0f), // erOverhangPerimeter
    Color(1.0f, 1.0f, 0.0f, 1.0f), // erInternalInfill
    Color(1.0f, 0.0f, 1.0f, 1.0f), // erSolidInfill
    Color(0.0f, 1.0f, 1.0f, 1.0f), // erTopSolidInfill
    Color(0.5f, 0.5f, 0.5f, 1.0f), // erBridgeInfill
    Color(1.0f, 1.0f, 1.0f, 1.0f), // erGapFill
    Color(0.5f, 0.0f, 0.0f, 1.0f), // erSkirt
    Color(0.0f, 0.5f, 0.0f, 1.0f), // erSupportMaterial
    Color(0.0f, 0.0f, 0.5f, 1.0f), // erSupportMaterialInterface
    Color(0.0f, 0.0f, 0.0f, 1.0f)  // erMixed
};

const GCodeAnalyzer::PreviewData::Extrusion::EViewType GCodeAnalyzer::PreviewData::Extrusion::Default_View_Type = GCodeAnalyzer::PreviewData::Extrusion::FeatureType;

void GCodeAnalyzer::PreviewData::Extrusion::set_default()
{
    view_type = Default_View_Type;

    ::memcpy((void*)role_colors, (const void*)Default_Extrusion_Role_Colors, Num_Extrusion_Roles * sizeof(Color));
    ::memcpy((void*)ranges.height.colors, (const void*)Range::Default_Colors, Range::Colors_Count * sizeof(Color));
    ::memcpy((void*)ranges.width.colors, (const void*)Range::Default_Colors, Range::Colors_Count * sizeof(Color));
    ::memcpy((void*)ranges.feedrate.colors, (const void*)Range::Default_Colors, Range::Colors_Count * sizeof(Color));

    role_flags = 0;
    for (unsigned int i = 0; i < Num_Extrusion_Roles; ++i)
    {
        role_flags += (unsigned int)::exp2((double)i);
    }
}

bool GCodeAnalyzer::PreviewData::Extrusion::is_role_flag_set(ExtrusionRole role) const
{
    if ((role < erPerimeter) || (erSupportMaterialInterface < role))
        return false;

    unsigned int flag = (unsigned int)::exp2((double)(role - erPerimeter));
    return (role_flags & flag) == flag;
}

const float GCodeAnalyzer::PreviewData::Travel::Default_Width = 0.075f;
const float GCodeAnalyzer::PreviewData::Travel::Default_Height = 0.075f;
const GCodeAnalyzer::PreviewData::Color GCodeAnalyzer::PreviewData::Travel::Default_Type_Colors[Num_Types] =
{
    Color(0.0f, 0.0f, 0.75f, 1.0f), // Move
    Color(0.0f, 0.75f, 0.0f, 1.0f), // Extrude
    Color(0.75f, 0.0f, 0.0f, 1.0f), // Retract
};

void GCodeAnalyzer::PreviewData::Travel::set_default()
{
    width = Default_Width;
    height = Default_Height;
    ::memcpy((void*)type_colors, (const void*)Default_Type_Colors, Num_Types * sizeof(Color));
    is_visible = false;
}

const GCodeAnalyzer::PreviewData::Color GCodeAnalyzer::PreviewData::Retraction::Default_Color = GCodeAnalyzer::PreviewData::Color(1.0f, 1.0f, 1.0f, 1.0f);

GCodeAnalyzer::PreviewData::Retraction::Position::Position(const Point3& position, float width, float height)
    : position(position)
    , width(width)
    , height(height)
{
}

void GCodeAnalyzer::PreviewData::Retraction::set_default()
{
    color = Default_Color;
    is_visible = false;
};

GCodeAnalyzer::PreviewData::PreviewData()
{
    set_default();
}

void GCodeAnalyzer::PreviewData::set_default()
{
    extrusion.set_default();
    travel.set_default();
    retraction.set_default();
    unretraction.set_default();
}

void GCodeAnalyzer::PreviewData::reset()
{
    extrusion.layers.clear();
    travel.polylines.clear();
    retraction.positions.clear();
    unretraction.positions.clear();
}

const GCodeAnalyzer::PreviewData::Color& GCodeAnalyzer::PreviewData::get_extrusion_role_color(ExtrusionRole role) const
{
    return extrusion.role_colors[role];
}

const GCodeAnalyzer::PreviewData::Color& GCodeAnalyzer::PreviewData::get_extrusion_height_color(float height) const
{
    return extrusion.ranges.height.get_color_at(height);
}

const GCodeAnalyzer::PreviewData::Color& GCodeAnalyzer::PreviewData::get_extrusion_width_color(float width) const
{
    return extrusion.ranges.width.get_color_at(width);
}

const GCodeAnalyzer::PreviewData::Color& GCodeAnalyzer::PreviewData::get_extrusion_feedrate_color(float feedrate) const
{
    return extrusion.ranges.feedrate.get_color_at(feedrate);
}

GCodeAnalyzer::GCodeAnalyzer()
{
    reset();
}

void GCodeAnalyzer::reset()
{
    _set_units(Millimeters);
    _set_positioning_xyz_type(Absolute);
    _set_positioning_e_type(Relative);
    _set_extrusion_role(erNone);
    _set_extruder_id(DEFAULT_EXTRUDER_ID);
    _set_mm3_per_mm(Default_mm3_per_mm);
    _set_width(Default_Width);
    _set_height(Default_Height);
    _set_feedrate(DEFAULT_FEEDRATE);
    _set_start_position(DEFAULT_START_POSITION);
    _set_start_extrusion(DEFAULT_START_EXTRUSION);
    _reset_axes_position();

    m_moves_map.clear();
}

const std::string& GCodeAnalyzer::process_gcode(const std::string& gcode)
{
    m_process_output = "";

    m_parser.parse_buffer(gcode,
        [this](GCodeReader& reader, const GCodeReader::GCodeLine& line)
    { this->_process_gcode_line(reader, line); });

    return m_process_output;
}

void GCodeAnalyzer::calc_gcode_preview_data(Print& print)
{
    // resets preview data
    print.gcode_preview.reset();

    // calculates extrusion layers
    _calc_gcode_preview_extrusion_layers(print);

    // calculates travel
    _calc_gcode_preview_travel(print);

    // calculates retractions
    _calc_gcode_preview_retractions(print);

    // calculates unretractions
    _calc_gcode_preview_unretractions(print);
}

void GCodeAnalyzer::_process_gcode_line(GCodeReader&, const GCodeReader::GCodeLine& line)
{
    // processes 'special' comments contained in line
    if (_process_tags(line))
        return;

    // sets new start position/extrusion
    _set_start_position(_get_end_position());
    _set_start_extrusion(_get_axis_position(E));

    // processes 'normal' gcode lines
    std::string cmd = line.cmd();
    if (cmd.length() > 1)
    {
        switch (::toupper(cmd[0]))
        {
        case 'G':
            {
                switch (::atoi(&cmd[1]))
                {
                case 1: // Move
                    {
                        _processG1(line);
                        break;
                    }
                case 22: // Firmware controlled Retract
                    {
                        _processG22(line);
                        break;
                    }
                case 23: // Firmware controlled Unretract
                    {
                        _processG23(line);
                        break;
                    }
                case 90: // Set to Absolute Positioning
                    {
                        _processG90(line);
                        break;
                    }
                case 91: // Set to Relative Positioning
                    {
                        _processG91(line);
                        break;
                    }
                case 92: // Set Position
                    {
                        _processG92(line);
                        break;
                    }
                }

                break;
            }
        case 'M':
            {
                switch (::atoi(&cmd[1]))
                {
                case 82: // Set extruder to absolute mode
                    {
                        _processM82(line);
                        break;
                    }
                case 83: // Set extruder to relative mode
                    {
                        _processM83(line);
                        break;
                    }
                }

                break;
            }
        case 'T': // Select Tools
            {
                _processT(line);
                break;
            }
        }
    }

    // puts the line back into the gcode
    m_process_output += line.raw() + "\n";
}

// Returns the new absolute position on the given axis in dependence of the given parameters
float axis_absolute_position_from_G1_line(GCodeAnalyzer::EAxis axis, const GCodeReader::GCodeLine& lineG1, GCodeAnalyzer::EUnits units, GCodeAnalyzer::EPositioningType type, float current_absolute_position)
{
    float lengthsScaleFactor = (units == GCodeAnalyzer::Inches) ? INCHES_TO_MM : 1.0f;
    if (lineG1.has(Slic3r::Axis(axis)))
    {
        float ret = lineG1.value(Slic3r::Axis(axis)) * lengthsScaleFactor;
        return (type == GCodeAnalyzer::Absolute) ? ret : current_absolute_position + ret;
    }
    else
        return current_absolute_position;
}

void GCodeAnalyzer::_processG1(const GCodeReader::GCodeLine& line)
{
    // updates axes positions from line
    EUnits units = _get_units();
    float new_pos[Num_Axis];
    for (unsigned char a = X; a < Num_Axis; ++a)
    {
        new_pos[a] = axis_absolute_position_from_G1_line((EAxis)a, line, units, (a == E) ? _get_positioning_e_type() : _get_positioning_xyz_type(), _get_axis_position((EAxis)a));
    }

    // updates feedrate from line, if present
    if (line.has_f())
        _set_feedrate(line.f() * MMMIN_TO_MMSEC);

    // calculates movement deltas
    float delta_pos[Num_Axis];
    for (unsigned char a = X; a < Num_Axis; ++a)
    {
        delta_pos[a] = new_pos[a] - _get_axis_position((EAxis)a);
    }

    // Detects move type
    GCodeMove::EType type = GCodeMove::Noop;

    if (delta_pos[E] < 0.0f)
    {
        if ((delta_pos[X] != 0.0f) || (delta_pos[Y] != 0.0f) || (delta_pos[Z] != 0.0f))
            type = GCodeMove::Move;
        else
            type = GCodeMove::Retract;
    }
    else if (delta_pos[E] > 0.0f)
    {
        if ((delta_pos[X] == 0.0f) && (delta_pos[Y] == 0.0f) && (delta_pos[Z] == 0.0f))
            type = GCodeMove::Unretract;
        else if ((delta_pos[X] != 0.0f) || (delta_pos[Y] != 0.0f))
            type = GCodeMove::Extrude;
    }
    else if ((delta_pos[X] != 0.0f) || (delta_pos[Y] != 0.0f) || (delta_pos[Z] != 0.0f))
        type = GCodeMove::Move;

    ExtrusionRole role = _get_extrusion_role();
    if ((type == GCodeMove::Extrude) && ((_get_width() == 0.0f) || (_get_height() == 0.0f) || (role < erPerimeter) || (erSupportMaterialInterface < role)))
        type = GCodeMove::Move;

    // updates axis positions
    for (unsigned char a = X; a < Num_Axis; ++a)
    {
        _set_axis_position((EAxis)a, new_pos[a]);
    }

    // stores the move
    if (type != GCodeMove::Noop)
        _store_move(type);
}

void GCodeAnalyzer::_processG22(const GCodeReader::GCodeLine& line)
{
    // stores retract move
    _store_move(GCodeMove::Retract);
}

void GCodeAnalyzer::_processG23(const GCodeReader::GCodeLine& line)
{
    // stores unretract move
    _store_move(GCodeMove::Unretract);
}

void GCodeAnalyzer::_processG90(const GCodeReader::GCodeLine& line)
{
    _set_positioning_xyz_type(Absolute);
}

void GCodeAnalyzer::_processG91(const GCodeReader::GCodeLine& line)
{
    _set_positioning_xyz_type(Relative);
}

void GCodeAnalyzer::_processG92(const GCodeReader::GCodeLine& line)
{
    float lengthsScaleFactor = (_get_units() == Inches) ? INCHES_TO_MM : 1.0f;
    bool anyFound = false;

    if (line.has_x())
    {
        _set_axis_position(X, line.x() * lengthsScaleFactor);
        anyFound = true;
    }

    if (line.has_y())
    {
        _set_axis_position(Y, line.y() * lengthsScaleFactor);
        anyFound = true;
    }

    if (line.has_z())
    {
        _set_axis_position(Z, line.z() * lengthsScaleFactor);
        anyFound = true;
    }

    if (line.has_e())
    {
        _set_axis_position(E, line.e() * lengthsScaleFactor);
        anyFound = true;
    }

    if (!anyFound)
    {
        for (unsigned char a = X; a < Num_Axis; ++a)
        {
            _set_axis_position((EAxis)a, 0.0f);
        }
    }
}

void GCodeAnalyzer::_processM82(const GCodeReader::GCodeLine& line)
{
    _set_positioning_e_type(Absolute);
}

void GCodeAnalyzer::_processM83(const GCodeReader::GCodeLine& line)
{
    _set_positioning_e_type(Relative);
}

void GCodeAnalyzer::_processT(const GCodeReader::GCodeLine& line)
{
    std::string cmd = line.cmd();
    if (cmd.length() > 1)
    {
        int id = (int)::strtol(cmd.substr(1).c_str(), nullptr, 10);
        // todo - add id validity check ?
        if (_get_extruder_id() != id)
        {
            _set_extruder_id(id);

            // stores tool change move
            _store_move(GCodeMove::Tool_change);
        }
    }
}

bool GCodeAnalyzer::_process_tags(const GCodeReader::GCodeLine& line)
{
    std::string comment = line.comment();

    // extrusion role tag
    size_t pos = comment.find(Extrusion_Role_Tag);
    if (pos != comment.npos)
    {
        _process_extrusion_role_tag(comment, pos);
        return true;
    }

    // mm3 per mm tag
    pos = comment.find(Mm3_Per_Mm_Tag);
    if (pos != comment.npos)
    {
        _process_mm3_per_mm_tag(comment, pos);
        return true;
    }

    // width tag
    pos = comment.find(Width_Tag);
    if (pos != comment.npos)
    {
        _process_width_tag(comment, pos);
        return true;
    }

    // height tag
    pos = comment.find(Height_Tag);
    if (pos != comment.npos)
    {
        _process_height_tag(comment, pos);
        return true;
    }

    return false;
}

void GCodeAnalyzer::_process_extrusion_role_tag(const std::string& comment, size_t pos)
{
    int role = (int)::strtol(comment.substr(pos + Extrusion_Role_Tag.length()).c_str(), nullptr, 10);
    if (_is_valid_extrusion_role(role))
        _set_extrusion_role((ExtrusionRole)role);
    else
    {
        // todo: show some error ?
    }
}

void GCodeAnalyzer::_process_mm3_per_mm_tag(const std::string& comment, size_t pos)
{
    _set_mm3_per_mm(::strtod(comment.substr(pos + Mm3_Per_Mm_Tag.length()).c_str(), nullptr));
}

void GCodeAnalyzer::_process_width_tag(const std::string& comment, size_t pos)
{
    _set_width((float)::strtod(comment.substr(pos + Width_Tag.length()).c_str(), nullptr));
}

void GCodeAnalyzer::_process_height_tag(const std::string& comment, size_t pos)
{
    _set_height((float)::strtod(comment.substr(pos + Height_Tag.length()).c_str(), nullptr));
}

void GCodeAnalyzer::_set_units(GCodeAnalyzer::EUnits units)
{
    m_state.units = units;
}

GCodeAnalyzer::EUnits GCodeAnalyzer::_get_units() const
{
    return m_state.units;
}

void GCodeAnalyzer::_set_positioning_xyz_type(GCodeAnalyzer::EPositioningType type)
{
    m_state.positioning_xyz_type = type;
}

GCodeAnalyzer::EPositioningType GCodeAnalyzer::_get_positioning_xyz_type() const
{
    return m_state.positioning_xyz_type;
}

void GCodeAnalyzer::_set_positioning_e_type(GCodeAnalyzer::EPositioningType type)
{
    m_state.positioning_e_type = type;
}

GCodeAnalyzer::EPositioningType GCodeAnalyzer::_get_positioning_e_type() const
{
    return m_state.positioning_e_type;
}

void GCodeAnalyzer::_set_extrusion_role(ExtrusionRole extrusion_role)
{
    m_state.data.extrusion_role = extrusion_role;
}

ExtrusionRole GCodeAnalyzer::_get_extrusion_role() const
{
    return m_state.data.extrusion_role;
}

void GCodeAnalyzer::_set_extruder_id(unsigned int id)
{
    m_state.data.extruder_id = id;
}

unsigned int GCodeAnalyzer::_get_extruder_id() const
{
    return m_state.data.extruder_id;
}

void GCodeAnalyzer::_set_mm3_per_mm(double value)
{
    m_state.data.mm3_per_mm = value;
}

double GCodeAnalyzer::_get_mm3_per_mm() const
{
    return m_state.data.mm3_per_mm;
}

void GCodeAnalyzer::_set_width(float width)
{
    m_state.data.width = width;
}

float GCodeAnalyzer::_get_width() const
{
    return m_state.data.width;
}

void GCodeAnalyzer::_set_height(float height)
{
    m_state.data.height = height;
}

float GCodeAnalyzer::_get_height() const
{
    return m_state.data.height;
}

void GCodeAnalyzer::_set_feedrate(float feedrate_mm_sec)
{
    m_state.data.feedrate = feedrate_mm_sec;
}

float GCodeAnalyzer::_get_feedrate() const
{
    return m_state.data.feedrate;
}

void GCodeAnalyzer::_set_axis_position(EAxis axis, float position)
{
    m_state.position[axis] = position;
}

float GCodeAnalyzer::_get_axis_position(EAxis axis) const
{
    return m_state.position[axis];
}

void GCodeAnalyzer::_reset_axes_position()
{
    ::memset((void*)m_state.position, 0, Num_Axis * sizeof(float));
}

void GCodeAnalyzer::_set_start_position(const Pointf3& position)
{
    m_state.start_position = position;
}

const Pointf3& GCodeAnalyzer::_get_start_position() const
{
    return m_state.start_position;
}

void GCodeAnalyzer::_set_start_extrusion(float extrusion)
{
    m_state.start_extrusion = extrusion;
}

float GCodeAnalyzer::_get_start_extrusion() const
{
    return m_state.start_extrusion;
}

float GCodeAnalyzer::_get_delta_extrusion() const
{
    return _get_axis_position(E) - m_state.start_extrusion;
}

Pointf3 GCodeAnalyzer::_get_end_position() const
{
    return Pointf3(m_state.position[X], m_state.position[Y], m_state.position[Z]);
}

void GCodeAnalyzer::_store_move(GCodeAnalyzer::GCodeMove::EType type)
{
    // if type non mapped yet, map it
    TypeToMovesMap::iterator it = m_moves_map.find(type);
    if (it == m_moves_map.end())
        it = m_moves_map.insert(TypeToMovesMap::value_type(type, GCodeMovesList())).first;

    // store move
    it->second.emplace_back(type, _get_extrusion_role(), _get_extruder_id(), _get_mm3_per_mm(), _get_width(), _get_height(), _get_feedrate(), _get_start_position(), _get_end_position(), _get_delta_extrusion());
}

bool GCodeAnalyzer::_is_valid_extrusion_role(int value) const
{
    return ((int)erNone <= value) && (value <= (int)erMixed);
}

void GCodeAnalyzer::_calc_gcode_preview_extrusion_layers(Print& print)
{
    struct Helper
    {
        static PreviewData::Extrusion::Layer& get_layer_at_z(PreviewData::Extrusion::LayersList& layers, float z)
        {
            for (PreviewData::Extrusion::Layer& layer : layers)
            {
                // if layer found, return it
                if (layer.z == z)
                    return layer;
            }

            // if layer not found, create and return it
            layers.emplace_back(z, ExtrusionPaths());
            return layers.back();
        }

        static void store_polyline(const Polyline& polyline, const Metadata& data, float z, Print& print)
        {
            // if the polyline is valid, create the extrusion path from it and store it
            if (polyline.is_valid())
            {
                ExtrusionPath path(data.extrusion_role, data.mm3_per_mm, data.width, data.height);
                path.polyline = polyline;
                path.feedrate = data.feedrate;

                get_layer_at_z(print.gcode_preview.extrusion.layers, z).paths.push_back(path);
            }
        }
    };

    TypeToMovesMap::iterator extrude_moves = m_moves_map.find(GCodeMove::Extrude);
    if (extrude_moves == m_moves_map.end())
        return;

    Metadata data;
    float z = FLT_MAX;
    Polyline polyline;
    Pointf3 position(FLT_MAX, FLT_MAX, FLT_MAX);
    PreviewData::Range height_range;
    PreviewData::Range width_range;
    PreviewData::Range feedrate_range;

    // constructs the polylines while traversing the moves
    for (const GCodeMove& move : extrude_moves->second)
    {
        if ((data != move.data) || (data.feedrate != move.data.feedrate) || (z != move.start_position.z) || (position != move.start_position))
        {
            // store current polyline
            polyline.remove_duplicate_points();
            Helper::store_polyline(polyline, data, z, print);

            // reset current polyline
            polyline = Polyline();

            // add both vertices of the move
            polyline.append(Point(scale_(move.start_position.x), scale_(move.start_position.y)));
            polyline.append(Point(scale_(move.end_position.x), scale_(move.end_position.y)));

            // update current values
            data = move.data;
            z = move.start_position.z;
            height_range.update_from(move.data.height);
            width_range.update_from(move.data.width);
            feedrate_range.update_from(move.data.feedrate);
        }
        else
            // append end vertex of the move to current polyline
            polyline.append(Point(scale_(move.end_position.x), scale_(move.end_position.y)));

        // update current values
        position = move.end_position;
    }

    // store last polyline
    polyline.remove_duplicate_points();
    Helper::store_polyline(polyline, data, z, print);

    // updates preview ranges data
    print.gcode_preview.extrusion.ranges.height.set_from(height_range);
    print.gcode_preview.extrusion.ranges.width.set_from(width_range);
    print.gcode_preview.extrusion.ranges.feedrate.set_from(feedrate_range);
}

void GCodeAnalyzer::_calc_gcode_preview_travel(Print& print)
{
    struct Helper
    {
        static void store_polyline(const Polyline3& polyline, PreviewData::Travel::EType type, PreviewData::Travel::Polyline::EDirection direction, Print& print)
        {
            // if the polyline is valid, store it
            if (polyline.is_valid())
                print.gcode_preview.travel.polylines.emplace_back(type, direction, polyline);
        }
    };

    TypeToMovesMap::iterator travel_moves = m_moves_map.find(GCodeMove::Move);
    if (travel_moves == m_moves_map.end())
        return;

    Polyline3 polyline;
    Pointf3 position(FLT_MAX, FLT_MAX, FLT_MAX);
    PreviewData::Travel::EType type = PreviewData::Travel::Num_Types;
    PreviewData::Travel::Polyline::EDirection direction = PreviewData::Travel::Polyline::Num_Directions;

    // constructs the polylines while traversing the moves
    for (const GCodeMove& move : travel_moves->second)
    {
        PreviewData::Travel::EType move_type = (move.delta_extruder < 0.0f) ? PreviewData::Travel::Retract : ((move.delta_extruder > 0.0f) ? PreviewData::Travel::Extrude : PreviewData::Travel::Move);
        PreviewData::Travel::Polyline::EDirection move_direction = ((move.start_position.x != move.end_position.x) || (move.start_position.y != move.end_position.y)) ? PreviewData::Travel::Polyline::Generic : PreviewData::Travel::Polyline::Vertical;

        if ((type != move_type) || (direction != move_direction) || (position != move.start_position))
        {
            // store current polyline
            polyline.remove_duplicate_points();
            Helper::store_polyline(polyline, type, direction, print);

            // reset current polyline
            polyline = Polyline3();

            // add both vertices of the move
            polyline.append(Point3(scale_(move.start_position.x), scale_(move.start_position.y), scale_(move.start_position.z)));
            polyline.append(Point3(scale_(move.end_position.x), scale_(move.end_position.y), scale_(move.end_position.z)));
        }
        else
            // append end vertex of the move to current polyline
            polyline.append(Point3(scale_(move.end_position.x), scale_(move.end_position.y), scale_(move.end_position.z)));

        // update current values
        position = move.end_position;
        type = move_type;
    }

    // store last polyline
    polyline.remove_duplicate_points();
    Helper::store_polyline(polyline, type, direction, print);
}

void GCodeAnalyzer::_calc_gcode_preview_retractions(Print& print)
{
    TypeToMovesMap::iterator retraction_moves = m_moves_map.find(GCodeMove::Retract);
    if (retraction_moves == m_moves_map.end())
        return;

    for (const GCodeMove& move : retraction_moves->second)
    {
        // store position
        Point3 position(scale_(move.start_position.x), scale_(move.start_position.y), scale_(move.start_position.z));
        print.gcode_preview.retraction.positions.emplace_back(position, move.data.width, move.data.height);
    }
}

void GCodeAnalyzer::_calc_gcode_preview_unretractions(Print& print)
{
    TypeToMovesMap::iterator unretraction_moves = m_moves_map.find(GCodeMove::Unretract);
    if (unretraction_moves == m_moves_map.end())
        return;

    for (const GCodeMove& move : unretraction_moves->second)
    {
        // store position
        Point3 position(scale_(move.start_position.x), scale_(move.start_position.y), scale_(move.start_position.z));
        print.gcode_preview.unretraction.positions.emplace_back(position, move.data.width, move.data.height);
    }
}

GCodeAnalyzer::PreviewData::Color operator + (const GCodeAnalyzer::PreviewData::Color& c1, const GCodeAnalyzer::PreviewData::Color& c2)
{
    return GCodeAnalyzer::PreviewData::Color(clamp(0.0f, 1.0f, c1.rgba[0] + c2.rgba[0]),
        clamp(0.0f, 1.0f, c1.rgba[1] + c2.rgba[1]),
        clamp(0.0f, 1.0f, c1.rgba[2] + c2.rgba[2]),
        clamp(0.0f, 1.0f, c1.rgba[3] + c2.rgba[3]));
}

GCodeAnalyzer::PreviewData::Color operator * (float f, const GCodeAnalyzer::PreviewData::Color& color)
{
    return GCodeAnalyzer::PreviewData::Color(clamp(0.0f, 1.0f, f * color.rgba[0]),
        clamp(0.0f, 1.0f, f * color.rgba[1]),
        clamp(0.0f, 1.0f, f * color.rgba[2]),
        clamp(0.0f, 1.0f, f * color.rgba[3]));
}
#endif // ENRICO_GCODE_PREVIEW
//############################################################################################################

} // namespace Slic3r
