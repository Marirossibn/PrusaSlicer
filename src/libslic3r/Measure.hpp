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
    SurfaceFeature(SurfaceFeatureType type, const Vec3d& pt1, const Vec3d& pt2, std::optional<Vec3d> pt3 = std::nullopt, double value = 0.0)
        : m_type(type), m_pt1(pt1), m_pt2(pt2), m_pt3(pt3), m_value(value) {}

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
    
    // Returns the surface features of the plane with the given index
    const std::vector<SurfaceFeature>& get_plane_features(unsigned int plane_id) const;

private: 
    std::unique_ptr<MeasuringImpl> priv;
};


struct DistAndPoints {
    DistAndPoints(double dist_, Vec3d from_, Vec3d to_) : dist(dist_), from(from_), to(to_) {}
    double dist;
    Vec3d from;
    Vec3d to;
};

struct AngleAndEdges {
    AngleAndEdges(double angle_, const Vec3d& center_, const std::pair<Vec3d, Vec3d>& e1_, const std::pair<Vec3d, Vec3d>& e2_, double radius_, bool coplanar_)
        : angle(angle_), center(center_), e1(e1_), e2(e2_), radius(radius_), coplanar(coplanar_) {}
    double angle;
    Vec3d center;
    std::pair<Vec3d, Vec3d> e1;
    std::pair<Vec3d, Vec3d> e2;
    double radius;
    bool coplanar;

    static const AngleAndEdges Dummy;
};

struct MeasurementResult {
    std::optional<AngleAndEdges> angle;
    std::optional<DistAndPoints> distance_infinite;
    std::optional<DistAndPoints> distance_strict;
    std::optional<Vec3d>  distance_xyz;

    bool has_distance_data() const {
        return distance_infinite.has_value() || distance_strict.has_value();
    }

    bool has_any_data() const {
        return angle.has_value() || distance_infinite.has_value() || distance_strict.has_value() || distance_xyz.has_value();
    }
};

// Returns distance/angle between two SurfaceFeatures.
MeasurementResult get_measurement(const SurfaceFeature& a, const SurfaceFeature& b, const Measuring* measuring = nullptr);

inline Vec3d edge_direction(const Vec3d& from, const Vec3d& to) { return (to - from).normalized(); }
inline Vec3d edge_direction(const std::pair<Vec3d, Vec3d>& e) { return edge_direction(e.first, e.second); }
inline Vec3d edge_direction(const SurfaceFeature& edge) {
    assert(edge.get_type() == SurfaceFeatureType::Edge);
    return edge_direction(edge.get_edge());
}

inline Vec3d plane_normal(const SurfaceFeature& plane) {
    assert(plane.get_type() == SurfaceFeatureType::Plane);
    return std::get<1>(plane.get_plane());
}

inline bool are_parallel(const Vec3d& v1, const Vec3d& v2) { return std::abs(std::abs(v1.dot(v2)) - 1.0) < EPSILON; }
inline bool are_perpendicular(const Vec3d& v1, const Vec3d& v2) { return std::abs(v1.dot(v2)) < EPSILON; }

inline bool are_parallel(const std::pair<Vec3d, Vec3d>& e1, const std::pair<Vec3d, Vec3d>& e2) {
    return are_parallel(e1.second - e1.first, e2.second - e2.first);
}
inline bool are_parallel(const SurfaceFeature& f1, const SurfaceFeature& f2) {
    if (f1.get_type() == SurfaceFeatureType::Edge && f2.get_type() == SurfaceFeatureType::Edge)
        return are_parallel(edge_direction(f1), edge_direction(f2));
    else if (f1.get_type() == SurfaceFeatureType::Edge && f2.get_type() == SurfaceFeatureType::Plane)
        return are_perpendicular(edge_direction(f1), plane_normal(f2));
    else
        return false;
}

inline bool are_perpendicular(const SurfaceFeature& f1, const SurfaceFeature& f2) {
    if (f1.get_type() == SurfaceFeatureType::Edge && f2.get_type() == SurfaceFeatureType::Edge)
        return are_perpendicular(edge_direction(f1), edge_direction(f2));
    else if (f1.get_type() == SurfaceFeatureType::Edge && f2.get_type() == SurfaceFeatureType::Plane)
        return are_parallel(edge_direction(f1), plane_normal(f2));
    else
        return false;
}

} // namespace Measure
} // namespace Slic3r

#endif // Slic3r_Measure_hpp_
