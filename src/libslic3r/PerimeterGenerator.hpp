#ifndef slic3r_PerimeterGenerator_hpp_
#define slic3r_PerimeterGenerator_hpp_

#include "libslic3r.h"
#include <vector>
#include "Flow.hpp"
#include "Polygon.hpp"
#include "PrintConfig.hpp"
#include "SurfaceCollection.hpp"

namespace Slic3r {

namespace PerimeterGenerator
{

struct Parameters {    
    Parameters(
        double                      layer_height,
        int                         layer_id,
        Flow                        perimeter_flow,
        Flow                        ext_perimeter_flow,
        Flow                        overhang_flow,
        Flow                        solid_infill_flow,
        const PrintRegionConfig    &config,
        const PrintObjectConfig    &object_config,
        const PrintConfig          &print_config,
        const bool                  spiral_vase) :   
            layer_height(layer_height),
            layer_id(layer_id),
            perimeter_flow(perimeter_flow), 
            ext_perimeter_flow(ext_perimeter_flow),
            overhang_flow(overhang_flow), 
            solid_infill_flow(solid_infill_flow),
            config(config), 
            object_config(object_config), 
            print_config(print_config),
            spiral_vase(spiral_vase),
            scaled_resolution(scaled<double>(print_config.gcode_resolution.value)),
            mm3_per_mm(perimeter_flow.mm3_per_mm()),
            ext_mm3_per_mm(ext_perimeter_flow.mm3_per_mm()), 
            mm3_per_mm_overhang(overhang_flow.mm3_per_mm())
        {
        }

    // Input parameters
    double                       layer_height;
    int                          layer_id;
    Flow                         perimeter_flow;
    Flow                         ext_perimeter_flow;
    Flow                         overhang_flow;
    Flow                         solid_infill_flow;
    const PrintRegionConfig     &config;
    const PrintObjectConfig     &object_config;
    const PrintConfig           &print_config;

    // Derived parameters
    bool                         spiral_vase;
    double                       scaled_resolution;
    double                       ext_mm3_per_mm;
    double                       mm3_per_mm;
    double                       mm3_per_mm_overhang;

private:
    Parameters() = delete;
};

void process_classic(
    // Inputs:
    const Parameters           &params,
    const SurfaceCollection    *slices,
    const ExPolygons           *lower_slices,
    // Cache:
    Polygons                   &lower_slices_polygons_cache,
    // Output:
    // Loops with the external thin walls
    ExtrusionEntityCollection  &out_loops,
    // Gaps without the thin walls
    ExtrusionEntityCollection  &out_gap_fill,
    // Infills without the gap fills
    ExPolygons                 &out_fill_expolygons);

void process_arachne(
    // Inputs:
    const Parameters           &params,
    const SurfaceCollection    *slices,
    const ExPolygons           *lower_slices,
    // Cache:
    Polygons                   &lower_slices_polygons_cache,
    // Output:
    // Loops with the external thin walls
    ExtrusionEntityCollection  &out_loops,
    // Gaps without the thin walls
    ExtrusionEntityCollection  &out_gap_fill,
    // Infills without the gap fills
    ExPolygons                 &out_fill_expolygons);

ExtrusionMultiPath thick_polyline_to_multi_path(const ThickPolyline &thick_polyline, ExtrusionRole role, const Flow &flow, float tolerance, float merge_tolerance);

} // namespace PerimeterGenerator
} // namespace Slic3r

#endif
