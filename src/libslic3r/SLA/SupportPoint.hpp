#ifndef SLA_SUPPORTPOINT_HPP
#define SLA_SUPPORTPOINT_HPP

#include <libslic3r/Point.hpp>

namespace Slic3r {

class ModelObject;

namespace sla {

// An enum to keep track of where the current points on the ModelObject came from.
enum class PointsStatus {
    NoPoints,           // No points were generated so far.
    Generating,     // The autogeneration algorithm triggered, but not yet finished.
    AutoGenerated,  // Points were autogenerated (i.e. copied from the backend).
    UserModified    // User has done some edits.
};

struct SupportPoint
{
    Vec3f pos;
    float head_front_radius;
    bool  is_new_island;
    
    SupportPoint()
        : pos(Vec3f::Zero()), head_front_radius(0.f), is_new_island(false)
    {}
    
    SupportPoint(float pos_x,
                 float pos_y,
                 float pos_z,
                 float head_radius,
                 bool  new_island = false)
        : pos(pos_x, pos_y, pos_z)
        , head_front_radius(head_radius)
        , is_new_island(new_island)
    {}
    
    SupportPoint(Vec3f position, float head_radius, bool new_island = false)
        : pos(position)
        , head_front_radius(head_radius)
        , is_new_island(new_island)
    {}
    
    SupportPoint(Eigen::Matrix<float, 5, 1, Eigen::DontAlign> data)
        : pos(data(0), data(1), data(2))
        , head_front_radius(data(3))
        , is_new_island(data(4) != 0.f)
    {}
    
    bool operator==(const SupportPoint &sp) const
    {
        float rdiff = std::abs(head_front_radius - sp.head_front_radius);
        return (pos == sp.pos) && rdiff < float(EPSILON) &&
               is_new_island == sp.is_new_island;
    }
    
    bool operator!=(const SupportPoint &sp) const { return !(sp == (*this)); }
    
    template<class Archive> void serialize(Archive &ar)
    {
        ar(pos, head_front_radius, is_new_island);
    }
};

using SupportPoints = std::vector<SupportPoint>;

SupportPoints transformed_support_points(const ModelObject &mo,
                                         const Transform3d &trafo);

}} // namespace Slic3r::sla

#endif // SUPPORTPOINT_HPP
