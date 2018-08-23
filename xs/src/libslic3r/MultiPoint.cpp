#include "MultiPoint.hpp"
#include "BoundingBox.hpp"

namespace Slic3r {

MultiPoint::operator Points() const
{
    return this->points;
}

void MultiPoint::scale(double factor)
{
    for (Point &pt : points)
        pt *= factor;
}

void MultiPoint::translate(double x, double y)
{
    Vector v(x, y);
    for (Point &pt : points)
        pt += v;
}

void MultiPoint::translate(const Point &v)
{
    for (Point &pt : points)
        pt += v;
}

void MultiPoint::rotate(double cos_angle, double sin_angle)
{
    for (Point &pt : this->points) {
        double cur_x = double(pt(0));
        double cur_y = double(pt(1));
        pt(0) = coord_t(round(cos_angle * cur_x - sin_angle * cur_y));
        pt(1) = coord_t(round(cos_angle * cur_y + sin_angle * cur_x));
    }
}

void MultiPoint::rotate(double angle, const Point &center)
{
    double s = sin(angle);
    double c = cos(angle);
    for (Point &pt : points) {
        Vec2crd v(pt - center);
        pt(0) = (coord_t)round(double(center(0)) + c * v[0] - s * v[1]);
        pt(1) = (coord_t)round(double(center(1)) + c * v[1] + s * v[0]);
    }
}

void MultiPoint::reverse()
{
    std::reverse(this->points.begin(), this->points.end());
}

Point MultiPoint::first_point() const
{
    return this->points.front();
}

double
MultiPoint::length() const
{
    Lines lines = this->lines();
    double len = 0;
    for (Lines::iterator it = lines.begin(); it != lines.end(); ++it) {
        len += it->length();
    }
    return len;
}

int
MultiPoint::find_point(const Point &point) const
{
    for (const Point &pt : this->points)
        if (pt == point)
            return &pt - &this->points.front();
    return -1;  // not found
}

bool
MultiPoint::has_boundary_point(const Point &point) const
{
    double dist = (point.projection_onto(*this) - point).cast<double>().norm();
    return dist < SCALED_EPSILON;
}

BoundingBox
MultiPoint::bounding_box() const
{
    return BoundingBox(this->points);
}

bool 
MultiPoint::has_duplicate_points() const
{
    for (size_t i = 1; i < points.size(); ++i)
        if (points[i-1] == points[i])
            return true;
    return false;
}

bool
MultiPoint::remove_duplicate_points()
{
    size_t j = 0;
    for (size_t i = 1; i < points.size(); ++i) {
        if (points[j] == points[i]) {
            // Just increase index i.
        } else {
            ++ j;
            if (j < i)
                points[j] = points[i];
        }
    }
    if (++ j < points.size()) {
        points.erase(points.begin() + j, points.end());
        return true;
    }
    return false;
}

bool
MultiPoint::intersection(const Line& line, Point* intersection) const
{
    Lines lines = this->lines();
    for (Lines::const_iterator it = lines.begin(); it != lines.end(); ++it) {
        if (it->intersection(line, intersection)) return true;
    }
    return false;
}

bool MultiPoint::first_intersection(const Line& line, Point* intersection) const
{
    bool   found = false;
    double dmin  = 0.;
    for (const Line &l : this->lines()) {
        Point ip;
        if (l.intersection(line, &ip)) {
            if (! found) {
                found = true;
                dmin = (line.a - ip).cast<double>().norm();
                *intersection = ip;
            } else {
                double d = (line.a - ip).cast<double>().norm();
                if (d < dmin) {
                    dmin = d;
                    *intersection = ip;
                }
            }
        }
    }
    return found;
}

//FIXME This is very inefficient in term of memory use.
// The recursive algorithm shall run in place, not allocating temporary data in each recursion.
Points
MultiPoint::_douglas_peucker(const Points &points, const double tolerance)
{
    assert(points.size() >= 2);
    Points results;
    double dmax = 0;
    size_t index = 0;
    Line full(points.front(), points.back());
    for (Points::const_iterator it = points.begin() + 1; it != points.end(); ++it) {
        // we use shortest distance, not perpendicular distance
        double d = full.distance_to(*it);
        if (d > dmax) {
            index = it - points.begin();
            dmax = d;
        }
    }
    if (dmax >= tolerance) {
        Points dp0;
        dp0.reserve(index + 1);
        dp0.insert(dp0.end(), points.begin(), points.begin() + index + 1);
        // Recursive call.
        Points dp1 = MultiPoint::_douglas_peucker(dp0, tolerance);
        results.reserve(results.size() + dp1.size() - 1);
        results.insert(results.end(), dp1.begin(), dp1.end() - 1);
        
        dp0.clear();
        dp0.reserve(points.size() - index);
        dp0.insert(dp0.end(), points.begin() + index, points.end());
        // Recursive call.
        dp1 = MultiPoint::_douglas_peucker(dp0, tolerance);
        results.reserve(results.size() + dp1.size());
        results.insert(results.end(), dp1.begin(), dp1.end());
    } else {
        results.push_back(points.front());
        results.push_back(points.back());
    }
    return results;
}

void MultiPoint3::translate(double x, double y)
{
    for (Vec3crd &p : points) {
        p(0) += x;
        p(1) += y;
    }
}

void MultiPoint3::translate(const Point& vector)
{
    this->translate(vector(0), vector(1));
}

double MultiPoint3::length() const
{
    double len = 0.0;
    for (const Line3& line : this->lines())
        len += line.length();
    return len;
}

BoundingBox3 MultiPoint3::bounding_box() const
{
    return BoundingBox3(points);
}

bool MultiPoint3::remove_duplicate_points()
{
    size_t j = 0;
    for (size_t i = 1; i < points.size(); ++i) {
        if (points[j] == points[i]) {
            // Just increase index i.
        } else {
            ++ j;
            if (j < i)
                points[j] = points[i];
        }
    }

    if (++j < points.size())
    {
        points.erase(points.begin() + j, points.end());
        return true;
    }

    return false;
}

BoundingBox get_extents(const MultiPoint &mp)
{ 
    return BoundingBox(mp.points);
}

BoundingBox get_extents_rotated(const Points &points, double angle)
{ 
    BoundingBox bbox;
    if (! points.empty()) {
        double s = sin(angle);
        double c = cos(angle);
        Points::const_iterator it = points.begin();
        double cur_x = (double)(*it)(0);
        double cur_y = (double)(*it)(1);
        bbox.min(0) = bbox.max(0) = (coord_t)round(c * cur_x - s * cur_y);
        bbox.min(1) = bbox.max(1) = (coord_t)round(c * cur_y + s * cur_x);
        for (++it; it != points.end(); ++it) {
            double cur_x = (double)(*it)(0);
            double cur_y = (double)(*it)(1);
            coord_t x = (coord_t)round(c * cur_x - s * cur_y);
            coord_t y = (coord_t)round(c * cur_y + s * cur_x);
            bbox.min(0) = std::min(x, bbox.min(0));
            bbox.min(1) = std::min(y, bbox.min(1));
            bbox.max(0) = std::max(x, bbox.max(0));
            bbox.max(1) = std::max(y, bbox.max(1));
        }
        bbox.defined = true;
    }
    return bbox;
}

BoundingBox get_extents_rotated(const MultiPoint &mp, double angle)
{
    return get_extents_rotated(mp.points, angle);
}

}
