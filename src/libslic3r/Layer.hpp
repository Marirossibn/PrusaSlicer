#ifndef slic3r_Layer_hpp_
#define slic3r_Layer_hpp_

#include "libslic3r.h"
#include "Flow.hpp"
#include "SurfaceCollection.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "ExPolygonCollection.hpp"
#include "PolylineCollection.hpp"


namespace Slic3r {

class Layer;
class PrintRegion;
class PrintObject;

class LayerRegion
{
public:
    Layer*                      layer()         { return m_layer; }
    const Layer*                layer() const   { return m_layer; }
    PrintRegion*                region()        { return m_region; }
    const PrintRegion*          region() const  { return m_region; }

    // collection of surfaces generated by slicing the original geometry
    // divided by type top/bottom/internal
    SurfaceCollection           slices;

    // collection of extrusion paths/loops filling gaps
    // These fills are generated by the perimeter generator.
    // They are not printed on their own, but they are copied to this->fills during infill generation.
    ExtrusionEntityCollection   thin_fills;

    // Unspecified fill polygons, used for overhang detection ("ensure vertical wall thickness feature")
    // and for re-starting of infills.
    ExPolygons                  fill_expolygons;
    // collection of surfaces for infill generation
    SurfaceCollection           fill_surfaces;

    // Collection of perimeter surfaces. This is a cached result of diff(slices, fill_surfaces).
    // While not necessary, the memory consumption is meager and it speeds up calculation.
    // The perimeter_surfaces keep the IDs of the slices (top/bottom/)
    SurfaceCollection           perimeter_surfaces;

    // collection of expolygons representing the bridged areas (thus not
    // needing support material)
    Polygons                    bridged;

    // collection of polylines representing the unsupported bridge edges
    PolylineCollection          unsupported_bridge_edges;

    // ordered collection of extrusion paths/loops to build all perimeters
    // (this collection contains only ExtrusionEntityCollection objects)
    ExtrusionEntityCollection   perimeters;

    // ordered collection of extrusion paths to fill surfaces
    // (this collection contains only ExtrusionEntityCollection objects)
    ExtrusionEntityCollection   fills;
    
    Flow    flow(FlowRole role, bool bridge = false, double width = -1) const;
    void    slices_to_fill_surfaces_clipped();
    void    prepare_fill_surfaces();
    void    make_perimeters(const SurfaceCollection &slices, SurfaceCollection* fill_surfaces);
    void    process_external_surfaces(const Layer* lower_layer);
    double infill_area_threshold() const;

    void    export_region_slices_to_svg(const char *path) const;
    void    export_region_fill_surfaces_to_svg(const char *path) const;
    // Export to "out/LayerRegion-name-%d.svg" with an increasing index with every export.
    void    export_region_slices_to_svg_debug(const char *name) const;
    void    export_region_fill_surfaces_to_svg_debug(const char *name) const;

    // Is there any valid extrusion assigned to this LayerRegion?
    bool    has_extrusions() const { return ! this->perimeters.entities.empty() || ! this->fills.entities.empty(); }

protected:
    friend class Layer;

    LayerRegion(Layer *layer, PrintRegion *region) : m_layer(layer), m_region(region) {}
    ~LayerRegion() {}

private:
    Layer       *m_layer;
    PrintRegion *m_region;
};


typedef std::vector<LayerRegion*> LayerRegionPtrs;

class Layer 
{
public:
    size_t              id() const          { return m_id; }
    void                set_id(size_t id)   { m_id = id; }
    PrintObject*        object()            { return m_object; }
    const PrintObject*  object() const      { return m_object; }

    Layer              *upper_layer;
    Layer              *lower_layer;
    bool                slicing_errors;
    coordf_t            slice_z;       // Z used for slicing in unscaled coordinates
    coordf_t            print_z;       // Z used for printing in unscaled coordinates
    coordf_t            height;        // layer height in unscaled coordinates

    // collection of expolygons generated by slicing the original geometry;
    // also known as 'islands' (all regions and surface types are merged here)
    // The slices are chained by the shortest traverse distance and this traversal
    // order will be recovered by the G-code generator.
    ExPolygonCollection slices;

    size_t                  region_count() const { return m_regions.size(); }
    const LayerRegion*      get_region(int idx) const { return m_regions.at(idx); }
    LayerRegion*            get_region(int idx) { return m_regions[idx]; }
    LayerRegion*            add_region(PrintRegion* print_region);
    const LayerRegionPtrs&  regions() const { return m_regions; }
    // Test whether whether there are any slices assigned to this layer.
    bool                    empty() const;    
    void                    make_slices();
    void                    merge_slices();
    template <class T> bool any_internal_region_slice_contains(const T &item) const {
        for (const LayerRegion *layerm : m_regions) if (layerm->slices.any_internal_contains(item)) return true;
        return false;
    }
    template <class T> bool any_bottom_region_slice_contains(const T &item) const {
        for (const LayerRegion *layerm : m_regions) if (layerm->slices.any_bottom_contains(item)) return true;
        return false;
    }
    void                    make_perimeters();
    void                    make_fills();

    void                    export_region_slices_to_svg(const char *path) const;
    void                    export_region_fill_surfaces_to_svg(const char *path) const;
    // Export to "out/LayerRegion-name-%d.svg" with an increasing index with every export.
    void                    export_region_slices_to_svg_debug(const char *name) const;
    void                    export_region_fill_surfaces_to_svg_debug(const char *name) const;

    // Is there any valid extrusion assigned to this LayerRegion?
    virtual bool            has_extrusions() const { for (auto layerm : m_regions) if (layerm->has_extrusions()) return true; return false; }

protected:
    friend class PrintObject;

    Layer(size_t id, PrintObject *object, coordf_t height, coordf_t print_z, coordf_t slice_z) :
        upper_layer(nullptr), lower_layer(nullptr), slicing_errors(false),
        slice_z(slice_z), print_z(print_z), height(height),
        m_id(id), m_object(object) {}
    virtual ~Layer();

private:
    // sequential number of layer, 0-based
    size_t              m_id;
    PrintObject        *m_object;
    LayerRegionPtrs     m_regions;
};

class SupportLayer : public Layer 
{
public:
    // Polygons covered by the supports: base, interface and contact areas.
    ExPolygonCollection         support_islands;
    // Extrusion paths for the support base and for the support interface and contacts.
    ExtrusionEntityCollection   support_fills;

    // Is there any valid extrusion assigned to this LayerRegion?
    virtual bool                has_extrusions() const { return ! support_fills.empty(); }

protected:
    friend class PrintObject;

    // The constructor has been made public to be able to insert additional support layers for the skirt or a wipe tower
    // between the raft and the object first layer.
    SupportLayer(size_t id, PrintObject *object, coordf_t height, coordf_t print_z, coordf_t slice_z) :
        Layer(id, object, height, print_z, slice_z) {}
    virtual ~SupportLayer() {}
};

}

#endif
