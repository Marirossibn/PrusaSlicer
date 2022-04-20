#include "SLAArchiveReader.hpp"

#include "libslic3r/MarchingSquares.hpp"
#include "libslic3r/SlicesToTriangleMesh.hpp"
#include "libslic3r/PNGReadWrite.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Execution/ExecutionTBB.hpp"
#include "libslic3r/miniz_extension.hpp"

#include "libslic3r/SLA/RasterBase.hpp"

#include <boost/property_tree/ini_parser.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/algorithm/string.hpp>

#include <miniz.h>

namespace marchsq {

template<> struct _RasterTraits<Slic3r::png::ImageGreyscale> {
    using Rst = Slic3r::png::ImageGreyscale;

       // The type of pixel cell in the raster
    using ValueType = uint8_t;

       // Value at a given position
    static uint8_t get(const Rst &rst, size_t row, size_t col)
    {
        return rst.get(row, col);
    }

       // Number of rows and cols of the raster
    static size_t rows(const Rst &rst) { return rst.rows; }
    static size_t cols(const Rst &rst) { return rst.cols; }
};

} // namespace marchsq

namespace Slic3r {

struct PNGBuffer { std::vector<uint8_t> buf; std::string fname; };
struct ArchiveData {
    boost::property_tree::ptree profile, config;
    std::vector<PNGBuffer> images;
};

static const constexpr char *CONFIG_FNAME  = "config.ini";
static const constexpr char *PROFILE_FNAME = "prusaslicer.ini";

namespace {

boost::property_tree::ptree read_ini(const mz_zip_archive_file_stat &entry,
                                     MZ_Archive &                    zip)
{
    std::string buf(size_t(entry.m_uncomp_size), '\0');

    if (!mz_zip_reader_extract_file_to_mem(&zip.arch, entry.m_filename,
                                           buf.data(), buf.size(), 0))
        throw Slic3r::FileIOError(zip.get_errorstr());

    boost::property_tree::ptree tree;
    std::stringstream ss(buf);
    boost::property_tree::read_ini(ss, tree);
    return tree;
}

PNGBuffer read_png(const mz_zip_archive_file_stat &entry,
                   MZ_Archive &                    zip,
                   const std::string &             name)
{
    std::vector<uint8_t> buf(entry.m_uncomp_size);

    if (!mz_zip_reader_extract_file_to_mem(&zip.arch, entry.m_filename,
                                           buf.data(), buf.size(), 0))
        throw Slic3r::FileIOError(zip.get_errorstr());

    return {std::move(buf), (name.empty() ? entry.m_filename : name)};
}

ArchiveData extract_sla_archive(const std::string &zipfname,
                                const std::string &exclude)
{
    ArchiveData arch;

       // Little RAII
    struct Arch: public MZ_Archive {
        Arch(const std::string &fname) {
            if (!open_zip_reader(&arch, fname))
                throw Slic3r::FileIOError(get_errorstr());
        }

        ~Arch() { close_zip_reader(&arch); }
    } zip (zipfname);

    mz_uint num_entries = mz_zip_reader_get_num_files(&zip.arch);

    for (mz_uint i = 0; i < num_entries; ++i)
    {
        mz_zip_archive_file_stat entry;

        if (mz_zip_reader_file_stat(&zip.arch, i, &entry))
        {
            std::string name = entry.m_filename;
            boost::algorithm::to_lower(name);

            if (boost::algorithm::contains(name, exclude)) continue;

            if (name == CONFIG_FNAME) arch.config = read_ini(entry, zip);
            if (name == PROFILE_FNAME) arch.profile = read_ini(entry, zip);

            std::string ext = boost::filesystem::path(name).extension().string();
            boost::algorithm::to_lower(ext);

            if (ext == ".png") {
                auto it = std::lower_bound(
                    arch.images.begin(), arch.images.end(), PNGBuffer{{}, name},
                    [](const PNGBuffer &r1, const PNGBuffer &r2) {
                        return std::less<std::string>()(r1.fname, r2.fname);
                    });

                arch.images.insert(it, read_png(entry, zip, name));
            }
        }
    }

    return arch;
}

ExPolygons rings_to_expolygons(const std::vector<marchsq::Ring> &rings,
                               double px_w, double px_h)
{
    auto polys = reserve_vector<ExPolygon>(rings.size());

    for (const marchsq::Ring &ring : rings) {
        Polygon poly; Points &pts = poly.points;
        pts.reserve(ring.size());

        for (const marchsq::Coord &crd : ring)
            pts.emplace_back(scaled(crd.c * px_w), scaled(crd.r * px_h));

        polys.emplace_back(poly);
    }

    // TODO: Is a union necessary?
    return union_ex(polys);
}

template<class Fn> void foreach_vertex(ExPolygon &poly, Fn &&fn)
{
    for (auto &p : poly.contour.points) fn(p);
    for (auto &h : poly.holes)
        for (auto &p : h.points) fn(p);
}

void invert_raster_trafo(ExPolygons &                  expolys,
                         const sla::RasterBase::Trafo &trafo,
                         coord_t                       width,
                         coord_t                       height)
{
    if (trafo.flipXY) std::swap(height, width);

    for (auto &expoly : expolys) {
        if (trafo.mirror_y)
            foreach_vertex(expoly, [height](Point &p) {p.y() = height - p.y(); });

        if (trafo.mirror_x)
            foreach_vertex(expoly, [width](Point &p) {p.x() = width - p.x(); });

        expoly.translate(-trafo.center_x, -trafo.center_y);

        if (trafo.flipXY)
            foreach_vertex(expoly, [](Point &p) { std::swap(p.x(), p.y()); });

        if ((trafo.mirror_x + trafo.mirror_y + trafo.flipXY) % 2) {
            expoly.contour.reverse();
            for (auto &h : expoly.holes) h.reverse();
        }
    }
}

struct RasterParams {
    sla::RasterBase::Trafo trafo; // Raster transformations
    coord_t        width, height; // scaled raster dimensions (not resolution)
    double         px_h, px_w;    // pixel dimesions
    marchsq::Coord win;           // marching squares window size
};

RasterParams get_raster_params(const DynamicPrintConfig &cfg)
{
    auto *opt_disp_cols = cfg.option<ConfigOptionInt>("display_pixels_x");
    auto *opt_disp_rows = cfg.option<ConfigOptionInt>("display_pixels_y");
    auto *opt_disp_w    = cfg.option<ConfigOptionFloat>("display_width");
    auto *opt_disp_h    = cfg.option<ConfigOptionFloat>("display_height");
    auto *opt_mirror_x  = cfg.option<ConfigOptionBool>("display_mirror_x");
    auto *opt_mirror_y  = cfg.option<ConfigOptionBool>("display_mirror_y");
    auto *opt_orient    = cfg.option<ConfigOptionEnum<SLADisplayOrientation>>("display_orientation");

    if (!opt_disp_cols || !opt_disp_rows || !opt_disp_w || !opt_disp_h ||
        !opt_mirror_x || !opt_mirror_y || !opt_orient)
        throw MissingProfileError("Invalid SL1 / SL1S file");

    RasterParams rstp;

    rstp.px_w = opt_disp_w->value / (opt_disp_cols->value - 1);
    rstp.px_h = opt_disp_h->value / (opt_disp_rows->value - 1);

    rstp.trafo = sla::RasterBase::Trafo{opt_orient->value == sladoLandscape ?
                                            sla::RasterBase::roLandscape :
                                            sla::RasterBase::roPortrait,
                                        {opt_mirror_x->value, opt_mirror_y->value}};

    rstp.height = scaled(opt_disp_h->value);
    rstp.width  = scaled(opt_disp_w->value);

    return rstp;
}

struct SliceParams { double layerh = 0., initial_layerh = 0.; };

SliceParams get_slice_params(const DynamicPrintConfig &cfg)
{
    auto *opt_layerh = cfg.option<ConfigOptionFloat>("layer_height");
    auto *opt_init_layerh = cfg.option<ConfigOptionFloat>("initial_layer_height");

    if (!opt_layerh || !opt_init_layerh)
        throw MissingProfileError("Invalid SL1 / SL1S file");

    return SliceParams{opt_layerh->getFloat(), opt_init_layerh->getFloat()};
}

std::vector<ExPolygons> extract_slices_from_sla_archive(
    ArchiveData &            arch,
    const RasterParams &     rstp,
    std::function<bool(int)> progr)
{
    auto jobdir = arch.config.get<std::string>("jobDir");
    for (auto &c : jobdir) c = std::tolower(c);

    std::vector<ExPolygons> slices(arch.images.size());

    struct Status
    {
        double                                 incr, val, prev;
        bool                                   stop  = false;
        execution::SpinningMutex<ExecutionTBB> mutex = {};
    } st{100. / slices.size(), 0., 0.};

    execution::for_each(
        ex_tbb, size_t(0), arch.images.size(),
        [&arch, &slices, &st, &rstp, progr](size_t i) {
            // Status indication guarded with the spinlock
            {
                std::lock_guard lck(st.mutex);
                if (st.stop) return;

                st.val += st.incr;
                double curr = std::round(st.val);
                if (curr > st.prev) {
                    st.prev = curr;
                    st.stop = !progr(int(curr));
                }
            }

            png::ImageGreyscale img;
            png::ReadBuf        rb{arch.images[i].buf.data(),
                            arch.images[i].buf.size()};
            if (!png::decode_png(rb, img)) return;

            constexpr uint8_t isoval = 128;
            auto              rings = marchsq::execute(img, isoval, rstp.win);
            ExPolygons        expolys = rings_to_expolygons(rings, rstp.px_w,
                                                            rstp.px_h);

            // Invert the raster transformations indicated in the profile metadata
            invert_raster_trafo(expolys, rstp.trafo, rstp.width, rstp.height);

            slices[i] = std::move(expolys);
        },
        execution::max_concurrency(ex_tbb));

    if (st.stop) slices = {};

    return slices;
}

// If the profile is missing from the archive (older PS versions did not have
// it), profile_out's initial value will be used as fallback. profile_out will be empty on
// function return if the archive did not contain any profile.
ConfigSubstitutions import_sla_archive(const std::string       &zipfname,
                                       Vec2i                    windowsize,
                                       indexed_triangle_set    &out,
                                       DynamicPrintConfig      &profile_out,
                                       std::function<bool(int)> progr)
{
    // Ensure minimum window size for marching squares
    windowsize.x() = std::max(2, windowsize.x());
    windowsize.y() = std::max(2, windowsize.y());

    std::string exclude_entries{"thumbnail"};
    ArchiveData arch = extract_sla_archive(zipfname, exclude_entries);
    DynamicPrintConfig profile_in, profile_use;
    ConfigSubstitutions config_substitutions =
        profile_in.load(arch.profile,
                        ForwardCompatibilitySubstitutionRule::Enable);

    if (profile_in.empty()) { // missing profile... do guess work
        // try to recover the layer height from the config.ini which was
        // present in all versions of sl1 files.
        if (auto lh_opt = arch.config.find("layerHeight");
            lh_opt != arch.config.not_found())
        {
            auto lh_str = lh_opt->second.data();

            size_t pos;
            double lh = string_to_double_decimal_point(lh_str, &pos);
            if (pos) { // TODO: verify that pos is 0 when parsing fails
                profile_out.set("layer_height", lh);
                profile_out.set("initial_layer_height", lh);
            }
        }
    }

    // If the archive contains an empty profile, use the one that was passed as output argument
    // then replace it with the readed profile to report that it was empty.
    profile_use = profile_in.empty() ? profile_out : profile_in;
    profile_out = profile_in;

    RasterParams rstp = get_raster_params(profile_use);
    rstp.win          = {windowsize.y(), windowsize.x()};

    SliceParams slicp = get_slice_params(profile_use);

    std::vector<ExPolygons> slices =
        extract_slices_from_sla_archive(arch, rstp, progr);

    if (!slices.empty())
        out = slices_to_mesh(slices, 0, slicp.layerh, slicp.initial_layerh);

    return config_substitutions;
}

} // namespace

//inline ConfigSubstitutions import_sla_archive(
//    const std::string &      zipfname,
//    Vec2i                    windowsize,
//    indexed_triangle_set &   out,
//    std::function<bool(int)> progr = [](int) { return true; })
//{
//    DynamicPrintConfig profile;
//    return import_sla_archive(zipfname, windowsize, out, profile, progr);
//}

ConfigSubstitutions import_sla_archive(const std::string &zipfname, DynamicPrintConfig &out)
{
    ArchiveData arch = extract_sla_archive(zipfname, "png");
    return out.load(arch.profile, ForwardCompatibilitySubstitutionRule::Enable);
}

ConfigSubstitutions import_sla_archive(const std::string       &zipfname,
                                       indexed_triangle_set    &out,
                                       DynamicPrintConfig      &profile,
                                       SLAImportQuality         quality,
                                       std::function<bool(int)> progr)
{
    Vec2i window;

    switch(quality)
    {
    case SLAImportQuality::Fast: window = {8, 8}; break;
    case SLAImportQuality:: Balanced: window = {4, 4}; break;
    default:
    case SLAImportQuality::Accurate:
        window = {2, 2};
    };

    return import_sla_archive(zipfname, window, out, profile, progr);
}

} // namespace Slic3r
