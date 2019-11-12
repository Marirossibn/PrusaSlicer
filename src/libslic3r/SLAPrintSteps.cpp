#include <libslic3r/SLAPrintSteps.hpp>

#include <libslic3r/SLA/Concurrency.hpp>
#include <libslic3r/SLA/Pad.hpp>
#include <libslic3r/SLA/SupportPointGenerator.hpp>

#include <libslic3r/ClipperUtils.hpp>

// For geometry algorithms with native Clipper types (no copies and conversions)
#include <libnest2d/backends/clipper/geometries.hpp>

#include <boost/log/trivial.hpp>

#include "I18N.hpp"

//! macro used to mark string used at localization,
//! return same string
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

namespace {

const std::array<unsigned, slaposCount> OBJ_STEP_LEVELS = {
    5,  // slaposHollowing,
    20, // slaposObjectSlice,
    5,  // slaposDrillHolesIfHollowed
    20, // slaposSupportPoints,
    10, // slaposSupportTree,
    10, // slaposPad,
    30, // slaposSliceSupports,
};

std::string OBJ_STEP_LABELS(size_t idx)
{
    switch (idx) {
    case slaposHollowing:            return L("Hollowing out the model");
    case slaposObjectSlice:          return L("Slicing model");
    case slaposDrillHolesIfHollowed: return L("Drilling holes into hollowed model.");
    case slaposSupportPoints:        return L("Generating support points");
    case slaposSupportTree:          return L("Generating support tree");
    case slaposPad:                  return L("Generating pad");
    case slaposSliceSupports:        return L("Slicing supports");
    default:;
    }
    assert(false);
    return "Out of bounds!";
};

const std::array<unsigned, slapsCount> PRINT_STEP_LEVELS = {
    10, // slapsMergeSlicesAndEval
    90, // slapsRasterize
};

std::string PRINT_STEP_LABELS(size_t idx)
{
    switch (idx) {
    case slapsMergeSlicesAndEval:   return L("Merging slices and calculating statistics");
    case slapsRasterize:            return L("Rasterizing layers");
    default:;
    }
    assert(false); return "Out of bounds!";
};

}

SLAPrint::Steps::Steps(SLAPrint *print)
    : m_print{print}
    , objcount{m_print->m_objects.size()}
    , ilhd{m_print->m_material_config.initial_layer_height.getFloat()}
    , ilh{float(ilhd)}
    , ilhs{scaled(ilhd)}
    , objectstep_scale{(max_objstatus - min_objstatus) / (objcount * 100.0)}
{}

void SLAPrint::Steps::hollow_model(SLAPrintObject &po)
{
    
    if (!po.m_config.hollowing_enable.getBool()) {
        BOOST_LOG_TRIVIAL(info) << "Skipping hollowing step!";
        po.m_hollowing_data.reset();
        return;
    } else {
        BOOST_LOG_TRIVIAL(info) << "Performing hollowing step!";
    }
    
    if (!po.m_hollowing_data)
        po.m_hollowing_data.reset(new SLAPrintObject::HollowingData());
    
    double thickness = po.m_config.hollowing_min_thickness.getFloat();
    double quality  = po.m_config.hollowing_quality.getFloat();
    double closing_d = po.m_config.hollowing_closing_distance.getFloat();
    sla::HollowingConfig hlwcfg{thickness, quality, closing_d};
    auto meshptr = generate_interior(po.transformed_mesh(), hlwcfg);
    if (meshptr) po.m_hollowing_data->interior = *meshptr;
    
    if (po.m_hollowing_data->interior.empty())
        BOOST_LOG_TRIVIAL(warning) << "Hollowed interior is empty!";
}

// The slicing will be performed on an imaginary 1D grid which starts from
// the bottom of the bounding box created around the supported model. So
// the first layer which is usually thicker will be part of the supports
// not the model geometry. Exception is when the model is not in the air
// (elevation is zero) and no pad creation was requested. In this case the
// model geometry starts on the ground level and the initial layer is part
// of it. In any case, the model and the supports have to be sliced in the
// same imaginary grid (the height vector argument to TriangleMeshSlicer).
void SLAPrint::Steps::slice_model(SLAPrintObject &po)
{
    
    TriangleMesh hollowed_mesh;
    
    bool is_hollowing = po.m_config.hollowing_enable.getBool() &&
            po.m_hollowing_data;
    
    if (is_hollowing) {
        hollowed_mesh = po.transformed_mesh();
        hollowed_mesh.merge(po.m_hollowing_data->interior);
        hollowed_mesh.require_shared_vertices();
    }
    
    const TriangleMesh &mesh = is_hollowing ? hollowed_mesh :
                                              po.transformed_mesh();
    
    // We need to prepare the slice index...
    
    double  lhd  = m_print->m_objects.front()->m_config.layer_height.getFloat();
    float   lh   = float(lhd);
    coord_t lhs  = scaled(lhd);
    auto && bb3d = mesh.bounding_box();
    double  minZ = bb3d.min(Z) - po.get_elevation();
    double  maxZ = bb3d.max(Z);
    auto    minZf = float(minZ);
    coord_t minZs = scaled(minZ);
    coord_t maxZs = scaled(maxZ);
    
    po.m_slice_index.clear();
    
    size_t cap = size_t(1 + (maxZs - minZs - ilhs) / lhs);
    po.m_slice_index.reserve(cap);
    
    po.m_slice_index.emplace_back(minZs + ilhs, minZf + ilh / 2.f, ilh);
    
    for(coord_t h = minZs + ilhs + lhs; h <= maxZs; h += lhs)
        po.m_slice_index.emplace_back(h, unscaled<float>(h) - lh / 2.f, lh);
    
    // Just get the first record that is from the model:
    auto slindex_it =
        po.closest_slice_record(po.m_slice_index, float(bb3d.min(Z)));
    
    if(slindex_it == po.m_slice_index.end())
        //TRN To be shown at the status bar on SLA slicing error.
        throw std::runtime_error(
            L("Slicing had to be stopped due to an internal error: "
              "Inconsistent slice index."));
    
    po.m_model_height_levels.clear();
    po.m_model_height_levels.reserve(po.m_slice_index.size());
    for(auto it = slindex_it; it != po.m_slice_index.end(); ++it)
        po.m_model_height_levels.emplace_back(it->slice_level());
    
    TriangleMeshSlicer slicer(&mesh);
    
    po.m_model_slices.clear();
    slicer.slice(po.m_model_height_levels,
                 float(po.config().slice_closing_radius.value),
                 &po.m_model_slices,
                 [this](){ m_print->throw_if_canceled(); });
    
    auto mit = slindex_it;
    double doffs = m_print->m_printer_config.absolute_correction.getFloat();
    coord_t clpr_offs = scaled(doffs);
    for(size_t id = 0;
         id < po.m_model_slices.size() && mit != po.m_slice_index.end();
         id++)
    {
        // We apply the printer correction offset here.
        if(clpr_offs != 0)
            po.m_model_slices[id] =
                offset_ex(po.m_model_slices[id], float(clpr_offs));
        
        mit->set_model_slice_idx(po, id); ++mit;
    }
    
    if(po.m_config.supports_enable.getBool() ||
        po.m_config.pad_enable.getBool())
    {
        po.m_supportdata.reset(
            new SLAPrintObject::SupportData(po.transformed_mesh()) );
    }
}

// In this step we check the slices, identify island and cover them with
// support points. Then we sprinkle the rest of the mesh.
void SLAPrint::Steps::support_points(SLAPrintObject &po)
{
    // If supports are disabled, we can skip the model scan.
    if(!po.m_config.supports_enable.getBool()) return;
    
    if (!po.m_supportdata)
        po.m_supportdata.reset(
                    new SLAPrintObject::SupportData(po.transformed_mesh()));
    
    const ModelObject& mo = *po.m_model_object;
    
    BOOST_LOG_TRIVIAL(debug) << "Support point count "
                             << mo.sla_support_points.size();
    
    // Unless the user modified the points or we already did the calculation,
    // we will do the autoplacement. Otherwise we will just blindly copy the
    // frontend data into the backend cache.
    if (mo.sla_points_status != sla::PointsStatus::UserModified) {
        
        // calculate heights of slices (slices are calculated already)
        const std::vector<float>& heights = po.m_model_height_levels;
        
        throw_if_canceled();
        sla::SupportPointGenerator::Config config;
        const SLAPrintObjectConfig& cfg = po.config();
        
        // the density config value is in percents:
        config.density_relative = float(cfg.support_points_density_relative / 100.f);
        config.minimal_distance = float(cfg.support_points_minimal_distance);
        config.head_diameter    = float(cfg.support_head_front_diameter);
        
        // scaling for the sub operations
        double d = objectstep_scale * OBJ_STEP_LEVELS[slaposSupportPoints] / 100.0;
        double init = current_status();
        
        auto statuscb = [this, d, init](unsigned st)
        {
            double current = init + st * d;
            if(std::round(current_status()) < std::round(current))
                report_status(current, OBJ_STEP_LABELS(slaposSupportPoints));
        };
        
        // Construction of this object does the calculation.
        throw_if_canceled();
        sla::SupportPointGenerator auto_supports(
            po.m_supportdata->emesh, po.get_model_slices(), heights, config,
            [this]() { throw_if_canceled(); }, statuscb);

        // Now let's extract the result.
        const std::vector<sla::SupportPoint>& points = auto_supports.output();
        throw_if_canceled();
        po.m_supportdata->pts = points;
        
        BOOST_LOG_TRIVIAL(debug) << "Automatic support points: "
                                 << po.m_supportdata->pts.size();
        
        // Using RELOAD_SLA_SUPPORT_POINTS to tell the Plater to pass
        // the update status to GLGizmoSlaSupports
        report_status(-1, L("Generating support points"),
                      SlicingStatus::RELOAD_SLA_SUPPORT_POINTS);
    } else {
        // There are either some points on the front-end, or the user
        // removed them on purpose. No calculation will be done.
        po.m_supportdata->pts = po.transformed_support_points();
    }

    // If the zero elevation mode is engaged, we have to filter out all the
    // points that are on the bottom of the object
    if (is_zero_elevation(po.config())) {
        double tolerance = po.config().pad_enable.getBool() ?
                               po.m_config.pad_wall_thickness.getFloat() :
                               po.m_config.support_base_height.getFloat();

        remove_bottom_points(po.m_supportdata->pts,
                             po.m_supportdata->emesh.ground_level(),
                             tolerance);
    }
}

void SLAPrint::Steps::support_tree(SLAPrintObject &po)
{
    if(!po.m_supportdata) return;
    
    sla::PadConfig pcfg = make_pad_cfg(po.m_config);
    
    if (pcfg.embed_object)
        po.m_supportdata->emesh.ground_level_offset(pcfg.wall_thickness_mm);
    
    po.m_supportdata->cfg = make_support_cfg(po.m_config);
    
    // scaling for the sub operations
    double d = objectstep_scale * OBJ_STEP_LEVELS[slaposSupportTree] / 100.0;
    double init = current_status();
    sla::JobController ctl;
    
    ctl.statuscb = [this, d, init](unsigned st, const std::string &logmsg) {
        double current = init + st * d;
        if (std::round(current_status()) < std::round(current))
            report_status(current, OBJ_STEP_LABELS(slaposSupportTree),
                          SlicingStatus::DEFAULT, logmsg);
    };
    ctl.stopcondition = [this]() { return canceled(); };
    ctl.cancelfn = [this]() { throw_if_canceled(); };
    
    po.m_supportdata->create_support_tree(ctl);
    
    if (!po.m_config.supports_enable.getBool()) return;
    
    throw_if_canceled();
    
    // Create the unified mesh
    auto rc = SlicingStatus::RELOAD_SCENE;
    
    // This is to prevent "Done." being displayed during merged_mesh()
    report_status(-1, L("Visualizing supports"));
    
    BOOST_LOG_TRIVIAL(debug) << "Processed support point count "
                             << po.m_supportdata->pts.size();
    
    // Check the mesh for later troubleshooting.
    if(po.support_mesh().empty())
        BOOST_LOG_TRIVIAL(warning) << "Support mesh is empty";
    
    report_status(-1, L("Visualizing supports"), rc);
}

void SLAPrint::Steps::generate_pad(SLAPrintObject &po) {
    // this step can only go after the support tree has been created
    // and before the supports had been sliced. (or the slicing has to be
    // repeated)
    
    if(po.m_config.pad_enable.getBool())
    {
        // Get the distilled pad configuration from the config
        sla::PadConfig pcfg = make_pad_cfg(po.m_config);
        
        ExPolygons bp; // This will store the base plate of the pad.
        double   pad_h             = pcfg.full_height();
        const TriangleMesh &trmesh = po.transformed_mesh();
        
        if (!po.m_config.supports_enable.getBool() || pcfg.embed_object) {
            // No support (thus no elevation) or zero elevation mode
            // we sometimes call it "builtin pad" is enabled so we will
            // get a sample from the bottom of the mesh and use it for pad
            // creation.
            sla::pad_blueprint(trmesh, bp, float(pad_h),
                               float(po.m_config.layer_height.getFloat()),
                               [this](){ throw_if_canceled(); });
        }
        
        po.m_supportdata->support_tree_ptr->add_pad(bp, pcfg);
        auto &pad_mesh = po.m_supportdata->support_tree_ptr->retrieve_mesh(sla::MeshType::Pad);
        
        if (!validate_pad(pad_mesh, pcfg))
            throw std::runtime_error(
                    L("No pad can be generated for this model with the "
                      "current configuration"));
        
    } else if(po.m_supportdata && po.m_supportdata->support_tree_ptr) {
        po.m_supportdata->support_tree_ptr->remove_pad();
    }
    
    throw_if_canceled();
    report_status(-1, L("Visualizing supports"), SlicingStatus::RELOAD_SCENE);
}

// Slicing the support geometries similarly to the model slicing procedure.
// If the pad had been added previously (see step "base_pool" than it will
// be part of the slices)
void SLAPrint::Steps::slice_supports(SLAPrintObject &po) {
    auto& sd = po.m_supportdata;
    
    if(sd) sd->support_slices.clear();
    
    // Don't bother if no supports and no pad is present.
    if (!po.m_config.supports_enable.getBool() &&
            !po.m_config.pad_enable.getBool())
        return;
    
    if(sd && sd->support_tree_ptr) {
        
        std::vector<float> heights; heights.reserve(po.m_slice_index.size());
        
        for(auto& rec : po.m_slice_index) heights.emplace_back(rec.slice_level());
        
        sd->support_slices = sd->support_tree_ptr->slice(
                    heights, float(po.config().slice_closing_radius.value));
    }
    
    double doffs = m_print->m_printer_config.absolute_correction.getFloat();
    coord_t clpr_offs = scaled(doffs);

    for (size_t i = 0; i < sd->support_slices.size() && i < po.m_slice_index.size(); ++i) {
        // We apply the printer correction offset here.
        if (clpr_offs != 0)
            sd->support_slices[i] = offset_ex(sd->support_slices[i], float(clpr_offs));

        po.m_slice_index[i].set_support_slice_idx(po, i);
    }

    // Using RELOAD_SLA_PREVIEW to tell the Plater to pass the update
    // status to the 3D preview to load the SLA slices.
    report_status(-2, "", SlicingStatus::RELOAD_SLA_PREVIEW);
}

using ClipperPoint  = ClipperLib::IntPoint;
using ClipperPolygon = ClipperLib::Polygon; // see clipper_polygon.hpp in libnest2d
using ClipperPolygons = std::vector<ClipperPolygon>;

static ClipperPolygons polyunion(const ClipperPolygons &subjects)
{
    ClipperLib::Clipper clipper;
    
    bool closed = true;
    
    for(auto& path : subjects) {
        clipper.AddPath(path.Contour, ClipperLib::ptSubject, closed);
        clipper.AddPaths(path.Holes, ClipperLib::ptSubject, closed);
    }
    
    auto mode = ClipperLib::pftPositive;
    
    return libnest2d::clipper_execute(clipper, ClipperLib::ctUnion, mode, mode);
}

static ClipperPolygons polydiff(const ClipperPolygons &subjects, const ClipperPolygons& clips)
{
    ClipperLib::Clipper clipper;
    
    bool closed = true;
    
    for(auto& path : subjects) {
        clipper.AddPath(path.Contour, ClipperLib::ptSubject, closed);
        clipper.AddPaths(path.Holes, ClipperLib::ptSubject, closed);
    }
    
    for(auto& path : clips) {
        clipper.AddPath(path.Contour, ClipperLib::ptClip, closed);
        clipper.AddPaths(path.Holes, ClipperLib::ptClip, closed);
    }
    
    auto mode = ClipperLib::pftPositive;
    
    return libnest2d::clipper_execute(clipper, ClipperLib::ctDifference, mode, mode);
}

// get polygons for all instances in the object
static ClipperPolygons get_all_polygons(
    const ExPolygons &                           input_polygons,
    const std::vector<SLAPrintObject::Instance> &instances,
    bool                                         is_lefthanded)
{
    namespace sl = libnest2d::sl;
    
    ClipperPolygons polygons;
    polygons.reserve(input_polygons.size() * instances.size());
    
    for (const ExPolygon& polygon : input_polygons) {
        if(polygon.contour.empty()) continue;
        
        for (size_t i = 0; i < instances.size(); ++i)
        {
            ClipperPolygon poly;
            
            // We need to reverse if is_lefthanded is true but
            bool needreverse = is_lefthanded;
            
            // should be a move
            poly.Contour.reserve(polygon.contour.size() + 1);
            
            auto& cntr = polygon.contour.points;
            if(needreverse)
                for(auto it = cntr.rbegin(); it != cntr.rend(); ++it)
                    poly.Contour.emplace_back(it->x(), it->y());
            else
                for(auto& p : cntr)
                    poly.Contour.emplace_back(p.x(), p.y());
            
            for(auto& h : polygon.holes) {
                poly.Holes.emplace_back();
                auto& hole = poly.Holes.back();
                hole.reserve(h.points.size() + 1);
                
                if(needreverse)
                    for(auto it = h.points.rbegin(); it != h.points.rend(); ++it)
                        hole.emplace_back(it->x(), it->y());
                else
                    for(auto& p : h.points)
                        hole.emplace_back(p.x(), p.y());
            }
            
            if(is_lefthanded) {
                for(auto& p : poly.Contour) p.X = -p.X;
                for(auto& h : poly.Holes) for(auto& p : h) p.X = -p.X;
            }
            
            sl::rotate(poly, double(instances[i].rotation));
            sl::translate(poly, ClipperPoint{instances[i].shift(X),
                                             instances[i].shift(Y)});
            
            polygons.emplace_back(std::move(poly));
        }
    }
    
    return polygons;
}

void SLAPrint::Steps::initialize_printer_input()
{
    auto &printer_input = m_print->m_printer_input;
    
    // clear the rasterizer input
    printer_input.clear();
    
    size_t mx = 0;
    for(SLAPrintObject * o : m_print->m_objects) {
        if(auto m = o->get_slice_index().size() > mx) mx = m;
    }
    
    printer_input.reserve(mx);
    
    auto eps = coord_t(SCALED_EPSILON);
    
    for(SLAPrintObject * o : m_print->m_objects) {
        coord_t gndlvl = o->get_slice_index().front().print_level() - ilhs;
        
        for(const SliceRecord& slicerecord : o->get_slice_index()) {
            coord_t lvlid = slicerecord.print_level() - gndlvl;
            
            // Neat trick to round the layer levels to the grid.
            lvlid = eps * (lvlid / eps);

            auto it = std::lower_bound(printer_input.begin(),
                                       printer_input.end(),
                                       PrintLayer(lvlid));

            if(it == printer_input.end() || it->level() != lvlid)
                it = printer_input.insert(it, PrintLayer(lvlid));
            
            
            it->add(slicerecord);
        }
    }
}

// Merging the slices from all the print objects into one slice grid and
// calculating print statistics from the merge result.
void SLAPrint::Steps::merge_slices_and_eval_stats() {
    
    initialize_printer_input();
    
    auto &print_statistics = m_print->m_print_statistics;
    auto &printer_config   = m_print->m_printer_config;
    auto &material_config  = m_print->m_material_config;
    auto &printer_input    = m_print->m_printer_input;
    
    print_statistics.clear();
    
    // libnest calculates positive area for clockwise polygons, Slic3r is in counter-clockwise
    auto areafn = [](const ClipperPolygon& poly) { return - libnest2d::sl::area(poly); };
    
    const double area_fill = printer_config.area_fill.getFloat()*0.01;// 0.5 (50%);
    const double fast_tilt = printer_config.fast_tilt_time.getFloat();// 5.0;
    const double slow_tilt = printer_config.slow_tilt_time.getFloat();// 8.0;
    
    const double init_exp_time = material_config.initial_exposure_time.getFloat();
    const double exp_time      = material_config.exposure_time.getFloat();
    
    const int fade_layers_cnt = m_print->m_default_object_config.faded_layers.getInt();// 10 // [3;20]
    
    const auto width          = scaled<double>(printer_config.display_width.getFloat());
    const auto height         = scaled<double>(printer_config.display_height.getFloat());
    const double display_area = width*height;
    
    double supports_volume(0.0);
    double models_volume(0.0);
    
    double estim_time(0.0);
    
    size_t slow_layers = 0;
    size_t fast_layers = 0;
    
    const double delta_fade_time = (init_exp_time - exp_time) / (fade_layers_cnt + 1);
    double fade_layer_time = init_exp_time;
    
    sla::ccr::SpinningMutex mutex;
    using Lock = std::lock_guard<sla::ccr::SpinningMutex>;
    
    // Going to parallel:
    auto printlayerfn = [
            // functions and read only vars
            areafn, area_fill, display_area, exp_time, init_exp_time, fast_tilt, slow_tilt, delta_fade_time,
            
            // write vars
            &mutex, &models_volume, &supports_volume, &estim_time, &slow_layers,
            &fast_layers, &fade_layer_time](PrintLayer& layer, size_t sliced_layer_cnt)
    {
        // vector of slice record references
        auto& slicerecord_references = layer.slices();
        
        if(slicerecord_references.empty()) return;
        
        // Layer height should match for all object slices for a given level.
        const auto l_height = double(slicerecord_references.front().get().layer_height());
        
        // Calculation of the consumed material
        
        ClipperPolygons model_polygons;
        ClipperPolygons supports_polygons;
        
        size_t c = std::accumulate(layer.slices().begin(),
                                   layer.slices().end(),
                                   size_t(0),
                                   [](size_t a, const SliceRecord &sr) {
            return a + sr.get_slice(soModel).size();
        });
        
        model_polygons.reserve(c);
        
        c = std::accumulate(layer.slices().begin(),
                            layer.slices().end(),
                            size_t(0),
                            [](size_t a, const SliceRecord &sr) {
            return a + sr.get_slice(soModel).size();
        });
        
        supports_polygons.reserve(c);
        
        for(const SliceRecord& record : layer.slices()) {
            const SLAPrintObject *po = record.print_obj();
            
            const ExPolygons &modelslices = record.get_slice(soModel);
            
            bool is_lefth = record.print_obj()->is_left_handed();
            if (!modelslices.empty()) {
                ClipperPolygons v = get_all_polygons(modelslices, po->instances(), is_lefth);
                for(ClipperPolygon& p_tmp : v) model_polygons.emplace_back(std::move(p_tmp));
            }
            
            const ExPolygons &supportslices = record.get_slice(soSupport);
            
            if (!supportslices.empty()) {
                ClipperPolygons v = get_all_polygons(supportslices, po->instances(), is_lefth);
                for(ClipperPolygon& p_tmp : v) supports_polygons.emplace_back(std::move(p_tmp));
            }
        }
        
        model_polygons = polyunion(model_polygons);
        double layer_model_area = 0;
        for (const ClipperPolygon& polygon : model_polygons)
            layer_model_area += areafn(polygon);
        
        if (layer_model_area < 0 || layer_model_area > 0) {
            Lock lck(mutex); models_volume += layer_model_area * l_height;
        }
        
        if(!supports_polygons.empty()) {
            if(model_polygons.empty()) supports_polygons = polyunion(supports_polygons);
            else supports_polygons = polydiff(supports_polygons, model_polygons);
            // allegedly, union of subject is done withing the diff according to the pftPositive polyFillType
        }
        
        double layer_support_area = 0;
        for (const ClipperPolygon& polygon : supports_polygons)
            layer_support_area += areafn(polygon);
        
        if (layer_support_area < 0 || layer_support_area > 0) {
            Lock lck(mutex); supports_volume += layer_support_area * l_height;
        }
        
        // Here we can save the expensively calculated polygons for printing
        ClipperPolygons trslices;
        trslices.reserve(model_polygons.size() + supports_polygons.size());
        for(ClipperPolygon& poly : model_polygons) trslices.emplace_back(std::move(poly));
        for(ClipperPolygon& poly : supports_polygons) trslices.emplace_back(std::move(poly));
        
        layer.transformed_slices(polyunion(trslices));
        
        // Calculation of the slow and fast layers to the future controlling those values on FW
        
        const bool is_fast_layer = (layer_model_area + layer_support_area) <= display_area*area_fill;
        const double tilt_time = is_fast_layer ? fast_tilt : slow_tilt;
        
        { Lock lck(mutex);
            if (is_fast_layer)
                fast_layers++;
            else
                slow_layers++;
            
            
            // Calculation of the printing time
            
            if (sliced_layer_cnt < 3)
                estim_time += init_exp_time;
            else if (fade_layer_time > exp_time)
            {
                fade_layer_time -= delta_fade_time;
                estim_time += fade_layer_time;
            }
            else
                estim_time += exp_time;
            
            estim_time += tilt_time;
        }
    };
    
    // sequential version for debugging:
    // for(size_t i = 0; i < m_printer_input.size(); ++i) printlayerfn(i);
    sla::ccr::enumerate(printer_input.begin(), printer_input.end(), printlayerfn);
    
    auto SCALING2 = SCALING_FACTOR * SCALING_FACTOR;
    print_statistics.support_used_material = supports_volume * SCALING2;
    print_statistics.objects_used_material = models_volume  * SCALING2;
    
    // Estimated printing time
    // A layers count o the highest object
    if (printer_input.size() == 0)
        print_statistics.estimated_print_time = std::nan("");
    else
        print_statistics.estimated_print_time = estim_time;
    
    print_statistics.fast_layers_count = fast_layers;
    print_statistics.slow_layers_count = slow_layers;
    
    report_status(-2, "", SlicingStatus::RELOAD_SLA_PREVIEW);
}

// Rasterizing the model objects, and their supports
void SLAPrint::Steps::rasterize()
{
    if(canceled()) return;
    
    auto &print_statistics = m_print->m_print_statistics;
    auto &printer_input    = m_print->m_printer_input;
    
    // Set up the printer, allocate space for all the layers
    sla::RasterWriter &printer = m_print->init_printer();
    
    auto lvlcnt = unsigned(printer_input.size());
    printer.layers(lvlcnt);
    
    // coefficient to map the rasterization state (0-99) to the allocated
    // portion (slot) of the process state
    double sd = (100 - max_objstatus) / 100.0;
    
    // slot is the portion of 100% that is realted to rasterization
    unsigned slot = PRINT_STEP_LEVELS[slapsRasterize];
    
    // pst: previous state
    double pst = current_status();
    
    double increment = (slot * sd) / printer_input.size();
    double dstatus = current_status();
    
    sla::ccr::SpinningMutex slck;
    using Lock = std::lock_guard<sla::ccr::SpinningMutex>;
    
    // procedure to process one height level. This will run in parallel
    auto lvlfn =
        [this, &slck, &printer, increment, &dstatus, &pst]
        (PrintLayer& printlayer, size_t idx)
    {
        if(canceled()) return;
        auto level_id = unsigned(idx);
        
        // Switch to the appropriate layer in the printer
        printer.begin_layer(level_id);
        
        for(const ClipperLib::Polygon& poly : printlayer.transformed_slices())
            printer.draw_polygon(poly, level_id);
        
        // Finish the layer for later saving it.
        printer.finish_layer(level_id);
        
        // Status indication guarded with the spinlock
        {
            Lock lck(slck);
            dstatus += increment;
            double st = std::round(dstatus);
            if(st > pst) {
                report_status(st, PRINT_STEP_LABELS(slapsRasterize));
                pst = st;
            }
        }
    };
    
    // last minute escape
    if(canceled()) return;
    
    // Sequential version (for testing)
    // for(unsigned l = 0; l < lvlcnt; ++l) lvlfn(l);
    
    // Print all the layers in parallel
    sla::ccr::enumerate(printer_input.begin(), printer_input.end(), lvlfn);
    
    // Set statistics values to the printer
    sla::RasterWriter::PrintStatistics stats;
    stats.used_material = (print_statistics.objects_used_material +
                           print_statistics.support_used_material) / 1000;
    
    int num_fade = m_print->m_default_object_config.faded_layers.getInt();
    stats.num_fade = num_fade >= 0 ? size_t(num_fade) : size_t(0);
    stats.num_fast = print_statistics.fast_layers_count;
    stats.num_slow = print_statistics.slow_layers_count;
    stats.estimated_print_time_s = print_statistics.estimated_print_time;
    
    printer.set_statistics(stats);
}

std::string SLAPrint::Steps::label(SLAPrintObjectStep step)
{
    return OBJ_STEP_LABELS(step);
}

std::string SLAPrint::Steps::label(SLAPrintStep step)
{
    return PRINT_STEP_LABELS(step);
}

double SLAPrint::Steps::progressrange(SLAPrintObjectStep step) const
{
    return OBJ_STEP_LEVELS[step] * objectstep_scale;
}

double SLAPrint::Steps::progressrange(SLAPrintStep step) const
{
    return PRINT_STEP_LEVELS[step] * (100 - max_objstatus) / 100.0;
}

void SLAPrint::Steps::execute(SLAPrintObjectStep step, SLAPrintObject &obj)
{
    switch(step) {
    case slaposHollowing: hollow_model(obj); break;
    case slaposObjectSlice: slice_model(obj); break;
    case slaposDrillHolesIfHollowed: break;
    case slaposSupportPoints:  support_points(obj); break;
    case slaposSupportTree: support_tree(obj); break;
    case slaposPad: generate_pad(obj); break;
    case slaposSliceSupports: slice_supports(obj); break;
    case slaposCount: assert(false);
    }
}

void SLAPrint::Steps::execute(SLAPrintStep step)
{
    switch (step) {
    case slapsMergeSlicesAndEval: merge_slices_and_eval_stats(); break;
    case slapsRasterize: rasterize(); break;
    case slapsCount: assert(false);
    }
}

}
