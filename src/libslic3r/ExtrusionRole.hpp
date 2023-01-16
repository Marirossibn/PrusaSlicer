#ifndef slic3r_ExtrusionRole_hpp_
#define slic3r_ExtrusionRole_hpp_

#include "enum_bitmask.hpp"

#include <string>
#include <string_view>

namespace Slic3r {

enum class ExtrusionRoleModifier : uint16_t {
// 1) Extrusion types
    // Perimeter (external, inner, ...)
    Perimeter,
    // Infill (top / bottom / solid inner / sparse inner / bridging inner ...)
    Infill,
    // Variable width extrusion
    Thin,
    // Support material extrusion
    Support,
    Skirt,
    Wipe,
// 2) Extrusion modifiers
    External,
    Solid,
    Ironing,
    Bridge,
// 3) Special types
    // Indicator that the extrusion role was mixed from multiple differing extrusion roles,
    // for example from Support and SupportInterface.
    Mixed,
    // Stopper, there should be maximum 16 modifiers defined for uint16_t bit mask.
    Count
};
// There should be maximum 16 modifiers defined for uint16_t bit mask.
static_assert(int(ExtrusionRoleModifier::Count) <= 16, "ExtrusionRoleModifier: there must be maximum 16 modifiers defined to fit a 16 bit bitmask");

using ExtrusionRoleModifiers = enum_bitmask<ExtrusionRoleModifier>;
ENABLE_ENUM_BITMASK_OPERATORS(ExtrusionRoleModifier);

struct ExtrusionRole : public ExtrusionRoleModifiers
{
    constexpr ExtrusionRole(const ExtrusionRoleModifier  bit) : ExtrusionRoleModifiers(bit) {}
    constexpr ExtrusionRole(const ExtrusionRoleModifiers bits) : ExtrusionRoleModifiers(bits) {}

    static constexpr const ExtrusionRoleModifiers None{};
    // Internal perimeter, not bridging.
    static constexpr const ExtrusionRoleModifiers Perimeter{ ExtrusionRoleModifier::Perimeter };
    // External perimeter, not bridging.
    static constexpr const ExtrusionRoleModifiers ExternalPerimeter{ ExtrusionRoleModifier::Perimeter | ExtrusionRoleModifier::External };
    // Perimeter, bridging. To be or'ed with ExtrusionRoleModifier::External for external bridging perimeter.
    static constexpr const ExtrusionRoleModifiers OverhangPerimeter{ ExtrusionRoleModifier::Perimeter | ExtrusionRoleModifier::Bridge };
    // Sparse internal infill.
    static constexpr const ExtrusionRoleModifiers InternalInfill{ ExtrusionRoleModifier::Infill };
    // Solid internal infill.
    static constexpr const ExtrusionRoleModifiers SolidInfill{ ExtrusionRoleModifier::Infill | ExtrusionRoleModifier::Solid };
    // Top solid infill (visible).
    //FIXME why there is no bottom solid infill type?
    static constexpr const ExtrusionRoleModifiers TopSolidInfill{ ExtrusionRoleModifier::Infill | ExtrusionRoleModifier::Solid | ExtrusionRoleModifier::External };
    // Ironing infill at the top surfaces.
    static constexpr const ExtrusionRoleModifiers Ironing{ ExtrusionRoleModifier::Infill | ExtrusionRoleModifier::Ironing | ExtrusionRoleModifier::External };
    // Visible bridging infill at the bottom of an object.
    static constexpr const ExtrusionRoleModifiers BridgeInfill{ ExtrusionRoleModifier::Infill | ExtrusionRoleModifier::Solid | ExtrusionRoleModifier::Bridge | ExtrusionRoleModifier::External };
//    static constexpr const ExtrusionRoleModifiers InternalBridgeInfill{ ExtrusionRoleModifier::Infill | ExtrusionRoleModifier::Solid | ExtrusionRoleModifier::Bridge };
    // Gap fill extrusion, currently used for any variable width extrusion: Thin walls outside of the outer extrusion,
    // gap fill in between perimeters, gap fill between the inner perimeter and infill.
    //FIXME revise GapFill and ThinWall types, split Gap Fill to Gap Fill and ThinWall.
    static constexpr const ExtrusionRoleModifiers GapFill{ ExtrusionRoleModifier::Thin }; // | ExtrusionRoleModifier::External };
//    static constexpr const ExtrusionRoleModifiers ThinWall{ ExtrusionRoleModifier::Thin };
    static constexpr const ExtrusionRoleModifiers Skirt{ ExtrusionRoleModifier::Skirt };
    // Support base material, printed with non-soluble plastic.
    static constexpr const ExtrusionRoleModifiers SupportMaterial{ ExtrusionRoleModifier::Support };
    // Support interface material, printed with soluble plastic.
    static constexpr const ExtrusionRoleModifiers SupportMaterialInterface{ ExtrusionRoleModifier::Support | ExtrusionRoleModifier::External };
    // Wipe tower material.
    static constexpr const ExtrusionRoleModifiers WipeTower{ ExtrusionRoleModifier::Wipe };
    // Extrusion role for a collection with multiple extrusion roles.
    static constexpr const ExtrusionRoleModifiers Mixed{ ExtrusionRoleModifier::Mixed };
};

// Special flags describing loop
enum ExtrusionLoopRole {
    elrDefault,
    elrContourInternalPerimeter,
    elrSkirt,
};

inline bool is_perimeter(ExtrusionRole role)
{
    return role == ExtrusionRole::Perimeter
        || role == ExtrusionRole::ExternalPerimeter
        || role == ExtrusionRole::OverhangPerimeter;
}

inline bool is_infill(ExtrusionRole role)
{
    return role == ExtrusionRole::BridgeInfill
        || role == ExtrusionRole::InternalInfill
        || role == ExtrusionRole::SolidInfill
        || role == ExtrusionRole::TopSolidInfill
        || role == ExtrusionRole::Ironing;
}

inline bool is_solid_infill(ExtrusionRole role)
{
    return role == ExtrusionRole::BridgeInfill
        || role == ExtrusionRole::SolidInfill
        || role == ExtrusionRole::TopSolidInfill
        || role == ExtrusionRole::Ironing;
}

inline bool is_bridge(ExtrusionRole role) {
    return role == ExtrusionRole::BridgeInfill
        || role == ExtrusionRole::OverhangPerimeter;
}

enum GCodeExtrusionRole : uint8_t {
    erNone,
    erPerimeter,
    erExternalPerimeter,
    erOverhangPerimeter,
    erInternalInfill,
    erSolidInfill,
    erTopSolidInfill,
    erIroning,
    erBridgeInfill,
    erGapFill,
    erSkirt,
    erSupportMaterial,
    erSupportMaterialInterface,
    erWipeTower,
    // Custom (user defined) G-code block, for example start / end G-code.
    erCustom,
    // Stopper to count number of enums.
    erCount
};

// Convert a rich bitmask based ExtrusionRole to a less expressive ordinal GCodeExtrusionRole.
// GCodeExtrusionRole is to be serialized into G-code and deserialized by G-code viewer,
GCodeExtrusionRole extrusion_role_to_gcode_extrusion_role(ExtrusionRole role);

std::string gcode_extrusion_role_to_string(GCodeExtrusionRole role);
GCodeExtrusionRole string_to_gcode_extrusion_role(const std::string_view role);

}

#endif // slic3r_ExtrusionRole_hpp_
