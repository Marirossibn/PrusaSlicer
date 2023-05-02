#include "libslic3r.h"
#include "Exception.hpp"
#include "Geometry.hpp"
#include "ClipperUtils.hpp"
#include "ExPolygon.hpp"
#include "Line.hpp"
#include "clipper.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <list>
#include <map>
#include <numeric>
#include <set>
#include <utility>
#include <stack>
#include <vector>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/log/trivial.hpp>

#if defined(_MSC_VER) && defined(__clang__)
#define BOOST_NO_CXX17_HDR_STRING_VIEW
#endif

namespace Slic3r { namespace Geometry {

bool directions_parallel(double angle1, double angle2, double max_diff)
{
    double diff = fabs(angle1 - angle2);
    max_diff += EPSILON;
    return diff < max_diff || fabs(diff - PI) < max_diff;
}

bool directions_perpendicular(double angle1, double angle2, double max_diff)
{
    double diff = fabs(angle1 - angle2);
    max_diff += EPSILON;
    return fabs(diff - 0.5 * PI) < max_diff || fabs(diff - 1.5 * PI) < max_diff;
}

template<class T>
bool contains(const std::vector<T> &vector, const Point &point)
{
    for (typename std::vector<T>::const_iterator it = vector.begin(); it != vector.end(); ++it) {
        if (it->contains(point)) return true;
    }
    return false;
}
template bool contains(const ExPolygons &vector, const Point &point);

void simplify_polygons(const Polygons &polygons, double tolerance, Polygons* retval)
{
    Polygons simplified_raw;
    for (const Polygon &source_polygon : polygons) {
        Points simplified = MultiPoint::douglas_peucker(to_polyline(source_polygon).points, tolerance);
        if (simplified.size() > 3) {
            simplified.pop_back();
            simplified_raw.push_back(Polygon{ std::move(simplified) });
        }
    }
    *retval = Slic3r::simplify_polygons(simplified_raw);
}

double linint(double value, double oldmin, double oldmax, double newmin, double newmax)
{
    return (value - oldmin) * (newmax - newmin) / (oldmax - oldmin) + newmin;
}

#if 0
// Point with a weight, by which the points are sorted.
// If the points have the same weight, sort them lexicographically by their positions.
struct ArrangeItem {
    ArrangeItem() {}
    Vec2d    pos;
    coordf_t  weight;
    bool operator<(const ArrangeItem &other) const {
        return weight < other.weight ||
            ((weight == other.weight) && (pos(1) < other.pos(1) || (pos(1) == other.pos(1) && pos(0) < other.pos(0))));
    }
};

Pointfs arrange(size_t num_parts, const Vec2d &part_size, coordf_t gap, const BoundingBoxf* bed_bounding_box)
{
    // Use actual part size (the largest) plus separation distance (half on each side) in spacing algorithm.
    const Vec2d       cell_size(part_size(0) + gap, part_size(1) + gap);

    const BoundingBoxf bed_bbox = (bed_bounding_box != NULL && bed_bounding_box->defined) ? 
        *bed_bounding_box :
        // Bogus bed size, large enough not to trigger the unsufficient bed size error.
        BoundingBoxf(
            Vec2d(0, 0),
            Vec2d(cell_size(0) * num_parts, cell_size(1) * num_parts));

    // This is how many cells we have available into which to put parts.
    size_t cellw = size_t(floor((bed_bbox.size()(0) + gap) / cell_size(0)));
    size_t cellh = size_t(floor((bed_bbox.size()(1) + gap) / cell_size(1)));
    if (num_parts > cellw * cellh)
        throw Slic3r::InvalidArgument("%zu parts won't fit in your print area!\n", num_parts);
    
    // Get a bounding box of cellw x cellh cells, centered at the center of the bed.
    Vec2d       cells_size(cellw * cell_size(0) - gap, cellh * cell_size(1) - gap);
    Vec2d       cells_offset(bed_bbox.center() - 0.5 * cells_size);
    BoundingBoxf cells_bb(cells_offset, cells_size + cells_offset);
    
    // List of cells, sorted by distance from center.
    std::vector<ArrangeItem> cellsorder(cellw * cellh, ArrangeItem());
    for (size_t j = 0; j < cellh; ++ j) {
        // Center of the jth row on the bed.
        coordf_t cy = linint(j + 0.5, 0., double(cellh), cells_bb.min(1), cells_bb.max(1));
        // Offset from the bed center.
        coordf_t yd = cells_bb.center()(1) - cy;
        for (size_t i = 0; i < cellw; ++ i) {
            // Center of the ith column on the bed.
            coordf_t cx = linint(i + 0.5, 0., double(cellw), cells_bb.min(0), cells_bb.max(0));
            // Offset from the bed center.
            coordf_t xd = cells_bb.center()(0) - cx;
            // Cell with a distance from the bed center.
            ArrangeItem &ci = cellsorder[j * cellw + i];
            // Cell center
            ci.pos(0) = cx;
            ci.pos(1) = cy;
            // Square distance of the cell center to the bed center.
            ci.weight = xd * xd + yd * yd;
        }
    }
    // Sort the cells lexicographically by their distances to the bed center and left to right / bttom to top.
    std::sort(cellsorder.begin(), cellsorder.end());
    cellsorder.erase(cellsorder.begin() + num_parts, cellsorder.end());

    // Return the (left,top) corners of the cells.
    Pointfs positions;
    positions.reserve(num_parts);
    for (std::vector<ArrangeItem>::const_iterator it = cellsorder.begin(); it != cellsorder.end(); ++ it)
        positions.push_back(Vec2d(it->pos(0) - 0.5 * part_size(0), it->pos(1) - 0.5 * part_size(1)));
    return positions;
}
#else
class ArrangeItem {
public:
    Vec2d pos = Vec2d::Zero();
    size_t index_x, index_y;
    coordf_t dist;
};
class ArrangeItemIndex {
public:
    coordf_t index;
    ArrangeItem item;
    ArrangeItemIndex(coordf_t _index, ArrangeItem _item) : index(_index), item(_item) {};
};

bool
arrange(size_t total_parts, const Vec2d &part_size, coordf_t dist, const BoundingBoxf* bb, Pointfs &positions)
{
    positions.clear();

    Vec2d part = part_size;

    // use actual part size (the largest) plus separation distance (half on each side) in spacing algorithm
    part(0) += dist;
    part(1) += dist;
    
    Vec2d area(Vec2d::Zero());
    if (bb != NULL && bb->defined) {
        area = bb->size();
    } else {
        // bogus area size, large enough not to trigger the error below
        area(0) = part(0) * total_parts;
        area(1) = part(1) * total_parts;
    }
    
    // this is how many cells we have available into which to put parts
    size_t cellw = floor((area(0) + dist) / part(0));
    size_t cellh = floor((area(1) + dist) / part(1));
    if (total_parts > (cellw * cellh))
        return false;
    
    // total space used by cells
    Vec2d cells(cellw * part(0), cellh * part(1));
    
    // bounding box of total space used by cells
    BoundingBoxf cells_bb;
    cells_bb.merge(Vec2d(0,0)); // min
    cells_bb.merge(cells);  // max
    
    // center bounding box to area
    cells_bb.translate(
        (area(0) - cells(0)) / 2,
        (area(1) - cells(1)) / 2
    );
    
    // list of cells, sorted by distance from center
    std::vector<ArrangeItemIndex> cellsorder;
    
    // work out distance for all cells, sort into list
    for (size_t i = 0; i <= cellw-1; ++i) {
        for (size_t j = 0; j <= cellh-1; ++j) {
            coordf_t cx = linint(i + 0.5, 0, cellw, cells_bb.min(0), cells_bb.max(0));
            coordf_t cy = linint(j + 0.5, 0, cellh, cells_bb.min(1), cells_bb.max(1));
            
            coordf_t xd = fabs((area(0) / 2) - cx);
            coordf_t yd = fabs((area(1) / 2) - cy);
            
            ArrangeItem c;
            c.pos(0) = cx;
            c.pos(1) = cy;
            c.index_x = i;
            c.index_y = j;
            c.dist = xd * xd + yd * yd - fabs((cellw / 2) - (i + 0.5));
            
            // binary insertion sort
            {
                coordf_t index = c.dist;
                size_t low = 0;
                size_t high = cellsorder.size();
                while (low < high) {
                    size_t mid = (low + ((high - low) / 2)) | 0;
                    coordf_t midval = cellsorder[mid].index;
                    
                    if (midval < index) {
                        low = mid + 1;
                    } else if (midval > index) {
                        high = mid;
                    } else {
                        cellsorder.insert(cellsorder.begin() + mid, ArrangeItemIndex(index, c));
                        goto ENDSORT;
                    }
                }
                cellsorder.insert(cellsorder.begin() + low, ArrangeItemIndex(index, c));
            }
            ENDSORT: ;
        }
    }
    
    // the extents of cells actually used by objects
    coordf_t lx = 0;
    coordf_t ty = 0;
    coordf_t rx = 0;
    coordf_t by = 0;

    // now find cells actually used by objects, map out the extents so we can position correctly
    for (size_t i = 1; i <= total_parts; ++i) {
        ArrangeItemIndex c = cellsorder[i - 1];
        coordf_t cx = c.item.index_x;
        coordf_t cy = c.item.index_y;
        if (i == 1) {
            lx = rx = cx;
            ty = by = cy;
        } else {
            if (cx > rx) rx = cx;
            if (cx < lx) lx = cx;
            if (cy > by) by = cy;
            if (cy < ty) ty = cy;
        }
    }
    // now we actually place objects into cells, positioned such that the left and bottom borders are at 0
    for (size_t i = 1; i <= total_parts; ++i) {
        ArrangeItemIndex c = cellsorder.front();
        cellsorder.erase(cellsorder.begin());
        coordf_t cx = c.item.index_x - lx;
        coordf_t cy = c.item.index_y - ty;
        
        positions.push_back(Vec2d(cx * part(0), cy * part(1)));
    }
    
    if (bb != NULL && bb->defined) {
        for (Pointfs::iterator p = positions.begin(); p != positions.end(); ++p) {
            p->x() += bb->min(0);
            p->y() += bb->min(1);
        }
    }
    
    return true;
}
#endif

// Euclidian distance of two boost::polygon points.
template<typename T>
T dist(const boost::polygon::point_data<T> &p1,const boost::polygon::point_data<T> &p2)
{
	T dx = p2(0) - p1(0);
	T dy = p2(1) - p1(1);
	return sqrt(dx*dx+dy*dy);
}

// Find a foot point of "px" on a segment "seg".
template<typename segment_type, typename point_type>
inline point_type project_point_to_segment(segment_type &seg, point_type &px)
{
    typedef typename point_type::coordinate_type T;
    const point_type &p0 = low(seg);
    const point_type &p1 = high(seg);
    const point_type  dir(p1(0)-p0(0), p1(1)-p0(1));
    const point_type  dproj(px(0)-p0(0), px(1)-p0(1));
    const T           t = (dir(0)*dproj(0) + dir(1)*dproj(1)) / (dir(0)*dir(0) + dir(1)*dir(1));
    assert(t >= T(-1e-6) && t <= T(1. + 1e-6));
    return point_type(p0(0) + t*dir(0), p0(1) + t*dir(1));
}

void assemble_transform(Transform3d& transform, const Vec3d& translation, const Vec3d& rotation, const Vec3d& scale, const Vec3d& mirror)
{
    transform = Transform3d::Identity();
    transform.translate(translation);
    transform.rotate(Eigen::AngleAxisd(rotation(2), Vec3d::UnitZ()) * Eigen::AngleAxisd(rotation(1), Vec3d::UnitY()) * Eigen::AngleAxisd(rotation(0), Vec3d::UnitX()));
    transform.scale(scale.cwiseProduct(mirror));
}

Transform3d assemble_transform(const Vec3d& translation, const Vec3d& rotation, const Vec3d& scale, const Vec3d& mirror)
{
    Transform3d transform;
    assemble_transform(transform, translation, rotation, scale, mirror);
    return transform;
}

void assemble_transform(Transform3d& transform, const Transform3d& translation, const Transform3d& rotation, const Transform3d& scale, const Transform3d& mirror)
{
    transform = translation * rotation * scale * mirror;
}

Transform3d assemble_transform(const Transform3d& translation, const Transform3d& rotation, const Transform3d& scale, const Transform3d& mirror)
{
    Transform3d transform;
    assemble_transform(transform, translation, rotation, scale, mirror);
    return transform;
}

void translation_transform(Transform3d& transform, const Vec3d& translation)
{
    transform = Transform3d::Identity();
    transform.translate(translation);
}

Transform3d translation_transform(const Vec3d& translation)
{
    Transform3d transform;
    translation_transform(transform, translation);
    return transform;
}

void rotation_transform(Transform3d& transform, const Vec3d& rotation)
{
    transform = Transform3d::Identity();
    transform.rotate(Eigen::AngleAxisd(rotation.z(), Vec3d::UnitZ()) * Eigen::AngleAxisd(rotation.y(), Vec3d::UnitY()) * Eigen::AngleAxisd(rotation.x(), Vec3d::UnitX()));
}

Transform3d rotation_transform(const Vec3d& rotation)
{
    Transform3d transform;
    rotation_transform(transform, rotation);
    return transform;
}

void scale_transform(Transform3d& transform, double scale)
{
    return scale_transform(transform, scale * Vec3d::Ones());
}

void scale_transform(Transform3d& transform, const Vec3d& scale)
{
    transform = Transform3d::Identity();
    transform.scale(scale);
}

Transform3d scale_transform(double scale)
{
    return scale_transform(scale * Vec3d::Ones());
}

Transform3d scale_transform(const Vec3d& scale)
{
    Transform3d transform;
    scale_transform(transform, scale);
    return transform;
}

Vec3d extract_rotation(const Eigen::Matrix<double, 3, 3, Eigen::DontAlign>& rotation_matrix)
{
    // The extracted "rotation" is a triplet of numbers such that Geometry::rotation_transform
    // returns the original transform. Because of the chosen order of rotations, the triplet
    // is not equivalent to Euler angles in the usual sense.
    Vec3d angles = rotation_matrix.eulerAngles(2,1,0);
    std::swap(angles(0), angles(2));
    return angles;
}

Vec3d extract_rotation(const Transform3d& transform)
{
    // use only the non-translational part of the transform
    Eigen::Matrix<double, 3, 3, Eigen::DontAlign> m = transform.matrix().block(0, 0, 3, 3);
    // remove scale
    m.col(0).normalize();
    m.col(1).normalize();
    m.col(2).normalize();
    return extract_rotation(m);
}

Transform3d Transformation::get_offset_matrix() const
{
    return translation_transform(get_offset());
}

static Transform3d extract_rotation_matrix(const Transform3d& trafo)
{
    Matrix3d rotation;
    Matrix3d scale;
    trafo.computeRotationScaling(&rotation, &scale);
    return Transform3d(rotation);
}

static Transform3d extract_scale(const Transform3d& trafo)
{
    Matrix3d rotation;
    Matrix3d scale;
    trafo.computeRotationScaling(&rotation, &scale);
    return Transform3d(scale);
}

static std::pair<Transform3d, Transform3d> extract_rotation_scale(const Transform3d& trafo)
{
    Matrix3d rotation;
    Matrix3d scale;
    trafo.computeRotationScaling(&rotation, &scale);
    return { Transform3d(rotation), Transform3d(scale) };
}

static bool contains_skew(const Transform3d& trafo)
{
    Matrix3d rotation;
    Matrix3d scale;
    trafo.computeRotationScaling(&rotation, &scale);

    if (scale.isDiagonal())
      return false;
    
    if (scale.determinant() >= 0.0)
      return true;

    // the matrix contains mirror
    const Matrix3d ratio = scale.cwiseQuotient(trafo.matrix().block<3,3>(0,0));

    auto check_skew = [&ratio](int i, int j, bool& skew) {
      if (!std::isnan(ratio(i, j)) && !std::isnan(ratio(j, i)))
        skew |= std::abs(ratio(i, j) * ratio(j, i) - 1.0) > EPSILON;
    };

    bool has_skew = false;
    check_skew(0, 1, has_skew);
    check_skew(0, 2, has_skew);
    check_skew(1, 2, has_skew);
    return has_skew;
}

Vec3d Transformation::get_rotation() const
{
    return extract_rotation(extract_rotation_matrix(m_matrix));
}

Transform3d Transformation::get_rotation_matrix() const
{
    return extract_rotation_matrix(m_matrix);
}

void Transformation::set_rotation(const Vec3d& rotation)
{
    const Vec3d offset = get_offset();
    m_matrix = rotation_transform(rotation) * extract_scale(m_matrix);
    m_matrix.translation() = offset;
}

void Transformation::set_rotation(Axis axis, double rotation)
{
    rotation = angle_to_0_2PI(rotation);
    if (is_approx(std::abs(rotation), 2.0 * double(PI)))
        rotation = 0.0;

    auto [curr_rotation, scale] = extract_rotation_scale(m_matrix);
    Vec3d angles = extract_rotation(curr_rotation);
    angles[axis] = rotation;

    const Vec3d offset = get_offset();
    m_matrix = rotation_transform(angles) * scale;
    m_matrix.translation() = offset;
}

Vec3d Transformation::get_scaling_factor() const
{
    const Transform3d scale = extract_scale(m_matrix);
    return { std::abs(scale(0, 0)), std::abs(scale(1, 1)), std::abs(scale(2, 2)) };
}

Transform3d Transformation::get_scaling_factor_matrix() const
{
    Transform3d scale = extract_scale(m_matrix);
    scale(0, 0) = std::abs(scale(0, 0));
    scale(1, 1) = std::abs(scale(1, 1));
    scale(2, 2) = std::abs(scale(2, 2));
    return scale;
}

void Transformation::set_scaling_factor(const Vec3d& scaling_factor)
{
    assert(scaling_factor.x() > 0.0 && scaling_factor.y() > 0.0 && scaling_factor.z() > 0.0);

    const Vec3d offset = get_offset();
    m_matrix = extract_rotation_matrix(m_matrix) * scale_transform(scaling_factor);
    m_matrix.translation() = offset;
}

void Transformation::set_scaling_factor(Axis axis, double scaling_factor)
{
    assert(scaling_factor > 0.0);

    auto [rotation, scale] = extract_rotation_scale(m_matrix);
    scale(axis, axis) = scaling_factor;

    const Vec3d offset = get_offset();
    m_matrix = rotation * scale;
    m_matrix.translation() = offset;
}

Vec3d Transformation::get_mirror() const
{
    const Transform3d scale = extract_scale(m_matrix);
    return { scale(0, 0) / std::abs(scale(0, 0)), scale(1, 1) / std::abs(scale(1, 1)), scale(2, 2) / std::abs(scale(2, 2)) };
}

Transform3d Transformation::get_mirror_matrix() const
{
    Transform3d scale = extract_scale(m_matrix);
    scale(0, 0) = scale(0, 0) / std::abs(scale(0, 0));
    scale(1, 1) = scale(1, 1) / std::abs(scale(1, 1));
    scale(2, 2) = scale(2, 2) / std::abs(scale(2, 2));
    return scale;
}

void Transformation::set_mirror(const Vec3d& mirror)
{
    Vec3d copy(mirror);
    const Vec3d abs_mirror = copy.cwiseAbs();
    for (int i = 0; i < 3; ++i) {
        if (abs_mirror(i) == 0.0)
            copy(i) = 1.0;
        else if (abs_mirror(i) != 1.0)
            copy(i) /= abs_mirror(i);
    }

    auto [rotation, scale] = extract_rotation_scale(m_matrix);
    const Vec3d curr_scales = { scale(0, 0), scale(1, 1), scale(2, 2) };
    const Vec3d signs = curr_scales.cwiseProduct(copy);

    if (signs[0] < 0.0) scale(0, 0) = -scale(0, 0);
    if (signs[1] < 0.0) scale(1, 1) = -scale(1, 1);
    if (signs[2] < 0.0) scale(2, 2) = -scale(2, 2);

    const Vec3d offset = get_offset();
    m_matrix = rotation * scale;
    m_matrix.translation() = offset;
}

void Transformation::set_mirror(Axis axis, double mirror)
{
    double abs_mirror = std::abs(mirror);
    if (abs_mirror == 0.0)
        mirror = 1.0;
    else if (abs_mirror != 1.0)
        mirror /= abs_mirror;

    auto [rotation, scale] = extract_rotation_scale(m_matrix);
    const double curr_scale = scale(axis, axis);
    const double sign = curr_scale * mirror;

    if (sign < 0.0) scale(axis, axis) = -scale(axis, axis);

    const Vec3d offset = get_offset();
    m_matrix = rotation * scale;
    m_matrix.translation() = offset;
}

bool Transformation::has_skew() const
{
    return contains_skew(m_matrix);
}

void Transformation::reset()
{
    m_matrix = Transform3d::Identity();
}

void Transformation::reset_rotation()
{
    const Geometry::TransformationSVD svd(*this);
    m_matrix = get_offset_matrix() * Transform3d(svd.v * svd.s * svd.v.transpose()) * svd.mirror_matrix();
}

void Transformation::reset_scaling_factor()
{
    const Geometry::TransformationSVD svd(*this);
    m_matrix = get_offset_matrix() * Transform3d(svd.u) * Transform3d(svd.v.transpose()) * svd.mirror_matrix();
}

void Transformation::reset_skew()
{
    auto new_scale_factor = [](const Matrix3d& s) {
        return pow(s(0, 0) * s(1, 1) * s(2, 2), 1. / 3.); // scale average
    };

    const Geometry::TransformationSVD svd(*this);
    m_matrix = get_offset_matrix() * Transform3d(svd.u) * scale_transform(new_scale_factor(svd.s)) * Transform3d(svd.v.transpose()) * svd.mirror_matrix();
}

Transform3d Transformation::get_matrix_no_offset() const
{
    Transformation copy(*this);
    copy.reset_offset();
    return copy.get_matrix();
}

Transform3d Transformation::get_matrix_no_scaling_factor() const
{
    Transformation copy(*this);
    copy.reset_scaling_factor();
    return copy.get_matrix();
}

Transformation Transformation::operator * (const Transformation& other) const
{
    return Transformation(get_matrix() * other.get_matrix());
}

TransformationSVD::TransformationSVD(const Transform3d& trafo)
{
    const auto &m0 = trafo.matrix().block<3, 3>(0, 0);
    mirror = m0.determinant() < 0.0;

    Matrix3d m;
    if (mirror)
        m = m0 * Eigen::DiagonalMatrix<double, 3, 3>(-1.0, 1.0, 1.0);
    else
        m = m0;
    const Eigen::JacobiSVD<Matrix3d> svd(m, Eigen::ComputeFullU | Eigen::ComputeFullV);
    u = svd.matrixU();
    v = svd.matrixV();
    s = svd.singularValues().asDiagonal();

    scale = !s.isApprox(Matrix3d::Identity());
    anisotropic_scale = ! is_approx(s(0, 0), s(1, 1)) || ! is_approx(s(1, 1), s(2, 2));
    rotation = !v.isApprox(u);

    if (anisotropic_scale) {
        rotation_90_degrees = true;
        for (int i = 0; i < 3; ++i) {
            const Vec3d row = v.row(i).cwiseAbs();
            const size_t num_zeros = is_approx(row[0], 0.) + is_approx(row[1], 0.) + is_approx(row[2], 0.);
            const size_t num_ones  = is_approx(row[0], 1.) + is_approx(row[1], 1.) + is_approx(row[2], 1.);
            if (num_zeros != 2 || num_ones != 1) {
                rotation_90_degrees = false;
                break;
            }
        }
        // Detect skew by brute force: check if the axes are still orthogonal after transformation
        const Matrix3d trafo_linear = trafo.linear();
        const std::array<Vec3d, 3> axes = { Vec3d::UnitX(), Vec3d::UnitY(), Vec3d::UnitZ() };
        std::array<Vec3d, 3> transformed_axes;
        for (int i = 0; i < 3; ++i) {
            transformed_axes[i] = trafo_linear * axes[i];
        }
        skew = std::abs(transformed_axes[0].dot(transformed_axes[1])) > EPSILON ||
               std::abs(transformed_axes[1].dot(transformed_axes[2])) > EPSILON ||
               std::abs(transformed_axes[2].dot(transformed_axes[0])) > EPSILON;

        // This following old code does not work under all conditions. The v matrix can become non diagonal (see SPE-1492) 
//        skew = ! rotation_90_degrees;
    } else
        skew = false;
}

// For parsing a transformation matrix from 3MF / AMF.
Transform3d transform3d_from_string(const std::string& transform_str)
{
    assert(is_decimal_separator_point()); // for atof
    Transform3d transform = Transform3d::Identity();

    if (!transform_str.empty()) {
        std::vector<std::string> mat_elements_str;
        boost::split(mat_elements_str, transform_str, boost::is_any_of(" "), boost::token_compress_on);

        const unsigned int size = (unsigned int)mat_elements_str.size();
        if (size == 16) {
            unsigned int i = 0;
            for (unsigned int r = 0; r < 4; ++r) {
                for (unsigned int c = 0; c < 4; ++c) {
                    transform(r, c) = ::atof(mat_elements_str[i++].c_str());
                }
            }
        }
    }

    return transform;
}

Eigen::Quaterniond rotation_xyz_diff(const Vec3d &rot_xyz_from, const Vec3d &rot_xyz_to)
{
    return
        // From the current coordinate system to world.
        Eigen::AngleAxisd(rot_xyz_to.z(), Vec3d::UnitZ()) * Eigen::AngleAxisd(rot_xyz_to.y(), Vec3d::UnitY()) * Eigen::AngleAxisd(rot_xyz_to.x(), Vec3d::UnitX()) *
        // From world to the initial coordinate system.
        Eigen::AngleAxisd(-rot_xyz_from.x(), Vec3d::UnitX()) * Eigen::AngleAxisd(-rot_xyz_from.y(), Vec3d::UnitY()) * Eigen::AngleAxisd(-rot_xyz_from.z(), Vec3d::UnitZ());
}

// This should only be called if it is known, that the two rotations only differ in rotation around the Z axis.
double rotation_diff_z(const Transform3d &trafo_from, const Transform3d &trafo_to)
{
    auto  m  = trafo_to.linear() * trafo_from.linear().inverse();
    assert(std::abs(m.determinant() - 1) < EPSILON);
    Vec3d vx = m * Vec3d(1., 0., 0);
    // Verify that the linear part of rotation from trafo_from to trafo_to rotates around Z and is unity.
    assert(std::abs(std::hypot(vx.x(), vx.y()) - 1.) < 1e-5);
    assert(std::abs(vx.z()) < 1e-5);
    return atan2(vx.y(), vx.x());
}

bool trafos_differ_in_rotation_by_z_and_mirroring_by_xy_only(const Transform3d &t1, const Transform3d &t2)
{
    if (std::abs(t1.translation().z() - t2.translation().z()) > EPSILON)
        // One of the object is higher than the other above the build plate (or below the build plate).
        return false;
    Matrix3d m1 = t1.matrix().block<3, 3>(0, 0);
    Matrix3d m2 = t2.matrix().block<3, 3>(0, 0);
    Matrix3d m = m2.inverse() * m1;
    Vec3d    z = m.block<3, 1>(0, 2);
    if (std::abs(z.x()) > EPSILON || std::abs(z.y()) > EPSILON || std::abs(z.z() - 1.) > EPSILON)
        // Z direction or length changed.
        return false;
    // Z still points in the same direction and it has the same length.
    Vec3d    x = m.block<3, 1>(0, 0);
    Vec3d    y = m.block<3, 1>(0, 1);
    if (std::abs(x.z()) > EPSILON || std::abs(y.z()) > EPSILON)
        return false;
    double   lx2 = x.squaredNorm();
    double   ly2 = y.squaredNorm();
    if (lx2 - 1. > EPSILON * EPSILON || ly2 - 1. > EPSILON * EPSILON)
        return false;
    // Verify whether the vectors x, y are still perpendicular.
    double   d   = x.dot(y);
    return std::abs(d * d) < EPSILON * lx2 * ly2;
}

}} // namespace Slic3r::Geometry
