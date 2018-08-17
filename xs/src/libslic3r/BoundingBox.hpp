#ifndef slic3r_BoundingBox_hpp_
#define slic3r_BoundingBox_hpp_

#include "libslic3r.h"
#include "Point.hpp"
#include "Polygon.hpp"

namespace Slic3r {

typedef Point   Size;
typedef Point3  Size3;
typedef Pointf  Sizef;
typedef Pointf3 Sizef3;

template <class PointClass>
class BoundingBoxBase
{
public:
    PointClass min;
    PointClass max;
    bool defined;
    
    BoundingBoxBase() : defined(false) {};
    BoundingBoxBase(const PointClass &pmin, const PointClass &pmax) : 
        min(pmin), max(pmax), defined(pmin(0) < pmax(0) && pmin(1) < pmax(1)) {}
    BoundingBoxBase(const std::vector<PointClass>& points)
    {
        if (points.empty())
            CONFESS("Empty point set supplied to BoundingBoxBase constructor");

        typename std::vector<PointClass>::const_iterator it = points.begin();
        this->min(0) = this->max(0) = (*it)(0);
        this->min(1) = this->max(1) = (*it)(1);
        for (++it; it != points.end(); ++it)
        {
            this->min(0) = std::min((*it)(0), this->min(0));
            this->min(1) = std::min((*it)(1), this->min(1));
            this->max(0) = std::max((*it)(0), this->max(0));
            this->max(1) = std::max((*it)(1), this->max(1));
        }
        this->defined = (this->min(0) < this->max(0)) && (this->min(1) < this->max(1));
    }
    void merge(const PointClass &point);
    void merge(const std::vector<PointClass> &points);
    void merge(const BoundingBoxBase<PointClass> &bb);
    void scale(double factor);
    PointClass size() const;
    double radius() const;
    void translate(coordf_t x, coordf_t y) { assert(this->defined); PointClass v(x, y); this->min += v; this->max += v; }
    void translate(const Pointf &v) { this->min += v; this->max += v; }
    void offset(coordf_t delta);
    PointClass center() const;
    bool contains(const PointClass &point) const {
        return point(0) >= this->min(0) && point(0) <= this->max(0)
            && point(1) >= this->min(1) && point(1) <= this->max(1);
    }
    bool overlap(const BoundingBoxBase<PointClass> &other) const {
        return ! (this->max(0) < other.min(0) || this->min(0) > other.max(0) ||
                  this->max(1) < other.min(1) || this->min(1) > other.max(1));
    }
    bool operator==(const BoundingBoxBase<PointClass> &rhs) { return this->min == rhs.min && this->max == rhs.max; }
    bool operator!=(const BoundingBoxBase<PointClass> &rhs) { return ! (*this == rhs); }
};

template <class PointClass>
class BoundingBox3Base : public BoundingBoxBase<PointClass>
{
public:
    BoundingBox3Base() : BoundingBoxBase<PointClass>() {};
    BoundingBox3Base(const PointClass &pmin, const PointClass &pmax) : 
        BoundingBoxBase<PointClass>(pmin, pmax) 
        { if (pmin(2) >= pmax(2)) BoundingBoxBase<PointClass>::defined = false; }
    BoundingBox3Base(const std::vector<PointClass>& points)
        : BoundingBoxBase<PointClass>(points)
    {
        if (points.empty())
            CONFESS("Empty point set supplied to BoundingBox3Base constructor");

        typename std::vector<PointClass>::const_iterator it = points.begin();
        this->min(2) = this->max(2) = (*it)(2);
        for (++it; it != points.end(); ++it)
        {
            this->min(2) = std::min((*it)(2), this->min(2));
            this->max(2) = std::max((*it)(2), this->max(2));
        }
        this->defined &= (this->min(2) < this->max(2));
    }
    void merge(const PointClass &point);
    void merge(const std::vector<PointClass> &points);
    void merge(const BoundingBox3Base<PointClass> &bb);
    PointClass size() const;
    double radius() const;
    void translate(coordf_t x, coordf_t y, coordf_t z) { assert(this->defined); PointClass v(x, y, z); this->min += v; this->max += v; }
    void translate(const Pointf3 &v) { this->min += v; this->max += v; }
    void offset(coordf_t delta);
    PointClass center() const;
    coordf_t max_size() const;

    bool contains(const PointClass &point) const {
        return BoundingBoxBase<PointClass>::contains(point) && point(2) >= this->min(2) && point(2) <= this->max(2);
    }

    bool contains(const BoundingBox3Base<PointClass>& other) const {
        return contains(other.min) && contains(other.max);
    }

    bool intersects(const BoundingBox3Base<PointClass>& other) const {
        return (this->min(0) < other.max(0)) && (this->max(0) > other.min(0)) && (this->min(1) < other.max(1)) && (this->max(1) > other.min(1)) && (this->min(2) < other.max(2)) && (this->max(2) > other.min(2));
    }
};

class BoundingBox : public BoundingBoxBase<Point>
{
public:
    void polygon(Polygon* polygon) const;
    Polygon polygon() const;
    BoundingBox rotated(double angle) const;
    BoundingBox rotated(double angle, const Point &center) const;
    void rotate(double angle) { (*this) = this->rotated(angle); }
    void rotate(double angle, const Point &center) { (*this) = this->rotated(angle, center); }
    // Align the min corner to a grid of cell_size x cell_size cells,
    // to encompass the original bounding box.
    void align_to_grid(const coord_t cell_size);
    
    BoundingBox() : BoundingBoxBase<Point>() {};
    BoundingBox(const Point &pmin, const Point &pmax) : BoundingBoxBase<Point>(pmin, pmax) {};
    BoundingBox(const Points &points) : BoundingBoxBase<Point>(points) {};
    BoundingBox(const Lines &lines);

    friend BoundingBox get_extents_rotated(const Points &points, double angle);
};

class BoundingBox3  : public BoundingBox3Base<Point3> 
{
public:
    BoundingBox3() : BoundingBox3Base<Point3>() {};
    BoundingBox3(const Point3 &pmin, const Point3 &pmax) : BoundingBox3Base<Point3>(pmin, pmax) {};
    BoundingBox3(const Points3& points) : BoundingBox3Base<Point3>(points) {};
};

class BoundingBoxf : public BoundingBoxBase<Pointf> 
{
public:
    BoundingBoxf() : BoundingBoxBase<Pointf>() {};
    BoundingBoxf(const Pointf &pmin, const Pointf &pmax) : BoundingBoxBase<Pointf>(pmin, pmax) {};
    BoundingBoxf(const std::vector<Pointf> &points) : BoundingBoxBase<Pointf>(points) {};
};

class BoundingBoxf3 : public BoundingBox3Base<Pointf3> 
{
public:
    BoundingBoxf3() : BoundingBox3Base<Pointf3>() {};
    BoundingBoxf3(const Pointf3 &pmin, const Pointf3 &pmax) : BoundingBox3Base<Pointf3>(pmin, pmax) {};
    BoundingBoxf3(const std::vector<Pointf3> &points) : BoundingBox3Base<Pointf3>(points) {};

    BoundingBoxf3 transformed(const Transform3f& matrix) const;
};

template<typename VT>
inline bool empty(const BoundingBoxBase<VT> &bb)
{
    return ! bb.defined || bb.min(0) >= bb.max(0) || bb.min(1) >= bb.max(1);
}

template<typename VT>
inline bool empty(const BoundingBox3Base<VT> &bb)
{
    return ! bb.defined || bb.min(0) >= bb.max(0) || bb.min(1) >= bb.max(1) || bb.min(2) >= bb.max(2);
}

} // namespace Slic3r

#endif
