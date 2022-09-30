#ifndef Slic3r_Measure_hpp_
#define Slic3r_Measure_hpp_

#include <optional>
#include <memory>

#include "Point.hpp"


struct indexed_triangle_set;



namespace Slic3r {
namespace Measure {


enum class SurfaceFeatureType : int {
    Undef  = 0,
    Point  = 1 << 0,
    Edge   = 1 << 1,
    Circle = 1 << 2,
    Plane  = 1 << 3
};

class SurfaceFeature {
public:
    SurfaceFeature(SurfaceFeatureType type, const Vec3d& pt1, const Vec3d& pt2, std::optional<Vec3d> pt3, double value)
    : m_type{type}, m_pt1{pt1}, m_pt2{pt2}, m_pt3{pt3}, m_value{value} {}

    explicit SurfaceFeature(const Vec3d& pt)
    : m_type{SurfaceFeatureType::Point}, m_pt1{pt} {}

    // Get type of this feature.
    SurfaceFeatureType get_type() const { return m_type; }

    // For points, return the point.
    Vec3d get_point() const { assert(m_type == SurfaceFeatureType::Point); return m_pt1; }

    // For edges, return start and end.
    std::pair<Vec3d, Vec3d> get_edge() const { assert(m_type == SurfaceFeatureType::Edge); return std::make_pair(m_pt1, m_pt2); }    

    // For circles, return center, radius and normal.
    std::tuple<Vec3d, double, Vec3d> get_circle() const { assert(m_type == SurfaceFeatureType::Circle); return std::make_tuple(m_pt1, m_value, m_pt2); }

    // For planes, return index into vector provided by Measuring::get_plane_triangle_indices, normal and point.
    std::tuple<int, Vec3d, Vec3d> get_plane() const { assert(m_type == SurfaceFeatureType::Plane); return std::make_tuple(int(m_value), m_pt1, m_pt2); }

    // For anything, return an extra point that should also be considered a part of this.
    std::optional<Vec3d> get_extra_point() const { assert(m_type != SurfaceFeatureType::Undef); return m_pt3; }

    bool operator == (const SurfaceFeature& other) const {
        if (this->m_type != other.m_type) return false;
        switch (this->m_type)
        {
        case SurfaceFeatureType::Undef: { break; }
        case SurfaceFeatureType::Point: { return (this->m_pt1.isApprox(other.m_pt1)); }
        case SurfaceFeatureType::Edge: {
            return (this->m_pt1.isApprox(other.m_pt1) && this->m_pt2.isApprox(other.m_pt2)) ||
                   (this->m_pt1.isApprox(other.m_pt2) && this->m_pt2.isApprox(other.m_pt1));
        }
        case SurfaceFeatureType::Plane:
        case SurfaceFeatureType::Circle: {
            return (this->m_pt1.isApprox(other.m_pt1) && this->m_pt2.isApprox(other.m_pt2) && std::abs(this->m_value - other.m_value) < EPSILON);
        }
        }

        return false;
    }

    bool operator != (const SurfaceFeature& other) const {
        return !operator == (other);
    }

private:
    SurfaceFeatureType m_type{ SurfaceFeatureType::Undef };
    Vec3d m_pt1{ Vec3d::Zero() };
    Vec3d m_pt2{ Vec3d::Zero() };
    std::optional<Vec3d> m_pt3;
    double m_value{ 0.0 };
};



class MeasuringImpl;


class Measuring {
public:
    // Construct the measurement object on a given its. The its must remain
    // valid and unchanged during the whole lifetime of the object.
    explicit Measuring(const indexed_triangle_set& its);
    ~Measuring();
    
    // Return a reference to a list of all features identified on the its.
    // Use only for debugging. Expensive, do not call often.
    std::vector<SurfaceFeature> get_all_features() const;

    // Given a face_idx where the mouse cursor points, return a feature that
    // should be highlighted (if any).
    std::optional<SurfaceFeature> get_feature(size_t face_idx, const Vec3d& point) const;

    // Returns a list of triangle indices for each identified plane. Each
    // Plane object contains an index into this vector. Expensive, do not
    // call too often.
    std::vector<std::vector<int>> get_planes_triangle_indices() const;
    
private: 
    std::unique_ptr<MeasuringImpl> priv;
};


struct DistAndPoints {
    DistAndPoints(double dist_, Vec3d from_, Vec3d to_) : dist(dist_), from(from_), to(to_) {}
    double dist;
    Vec3d from;
    Vec3d to;
};

struct AngleAndPoints {
    AngleAndPoints(double angle_, Vec3d center_, Vec3d e1_, Vec3d e2_, double radius_, bool coplanar_)
    : angle(angle_), center(center_), e1(e1_), e2(e2_), radius(radius_), coplanar(coplanar_) {}
    double angle;
    Vec3d center;
    Vec3d e1;
    Vec3d e2;
    double radius;
    bool coplanar;
};

struct MeasurementResult {
    std::optional<AngleAndPoints> angle;
    std::optional<DistAndPoints> distance_infinite;
    std::optional<DistAndPoints> distance_strict;
    std::optional<Vec3d>  distance_xyz;

    bool has_any_data() const {
        return angle.has_value() || distance_infinite.has_value() || distance_strict.has_value() || distance_xyz.has_value();
    }
};

// Returns distance/angle between two SurfaceFeatures.
MeasurementResult get_measurement(const SurfaceFeature& a, const SurfaceFeature& b);


} // namespace Measure
} // namespace Slic3r

#endif // Slic3r_Measure_hpp_
