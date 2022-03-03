//Copyright (c) 2018 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef UTILS_POLYGONS_POINT_INDEX_H
#define UTILS_POLYGONS_POINT_INDEX_H

#include <vector>

#include "../../Point.hpp"
#include "../../Polygon.hpp"


namespace Slic3r::Arachne
{

/*!
 * A class for iterating over the points in one of the polygons in a \ref Polygons object
 */
class PolygonsPointIndex
{
public:
    /*!
     * The polygons into which this index is indexing.
     */
    const Polygons* polygons; // (pointer to const polygons)

    unsigned int poly_idx; //!< The index of the polygon in \ref PolygonsPointIndex::polygons

    unsigned int point_idx; //!< The index of the point in the polygon in \ref PolygonsPointIndex::polygons

    /*!
     * Constructs an empty point index to no polygon.
     *
     * This is used as a placeholder for when there is a zero-construction
     * needed. Since the `polygons` field is const you can't ever make this
     * initialisation useful.
     */
    PolygonsPointIndex() : polygons(nullptr), poly_idx(0), point_idx(0) {}

    /*!
     * Constructs a new point index to a vertex of a polygon.
     * \param polygons The Polygons instance to which this index points.
     * \param poly_idx The index of the sub-polygon to point to.
     * \param point_idx The index of the vertex in the sub-polygon.
     */
    PolygonsPointIndex(const Polygons *polygons, unsigned int poly_idx, unsigned int point_idx)
        : polygons(polygons), poly_idx(poly_idx), point_idx(point_idx) {}

    /*!
     * Copy constructor to copy these indices.
     */
    PolygonsPointIndex(const PolygonsPointIndex& original) = default;

    Point p() const
    {
        if (!polygons)
            return {0, 0};

        return (*polygons)[poly_idx][point_idx];
    }

    /*!
     * Test whether two iterators refer to the same polygon in the same polygon list.
     * 
     * \param other The PolygonsPointIndex to test for equality
     * \return Wether the right argument refers to the same polygon in the same ListPolygon as the left argument.
     */
    bool operator==(const PolygonsPointIndex &other) const
    {
        return polygons == other.polygons && poly_idx == other.poly_idx && point_idx == other.point_idx;
    }
    bool                operator!=(const PolygonsPointIndex &other) const { return !(*this == other); }
    bool                operator<(const PolygonsPointIndex &other) const { return this->p() < other.p(); }
    PolygonsPointIndex &operator=(const PolygonsPointIndex &other)
    {
        polygons  = other.polygons;
        poly_idx  = other.poly_idx;
        point_idx = other.point_idx;
        return *this;
    }
    //! move the iterator forward (and wrap around at the end)
    PolygonsPointIndex &operator++()
    {
        point_idx = (point_idx + 1) % (*polygons)[poly_idx].size();
        return *this;
    }
    //! move the iterator backward (and wrap around at the beginning)
    PolygonsPointIndex &operator--()
    {
        if (point_idx == 0)
            point_idx = (*polygons)[poly_idx].size();
        point_idx--;
        return *this;
    }
    //! move the iterator forward (and wrap around at the end)
    PolygonsPointIndex next() const
    {
        PolygonsPointIndex ret(*this);
        ++ret;
        return ret;
    }
    //! move the iterator backward (and wrap around at the beginning)
    PolygonsPointIndex prev() const
    {
        PolygonsPointIndex ret(*this);
        --ret;
        return ret;
    }
};


}//namespace Slic3r::Arachne

namespace std
{
/*!
 * Hash function for \ref PolygonsPointIndex
 */
template <>
struct hash<Slic3r::Arachne::PolygonsPointIndex>
{
    size_t operator()(const Slic3r::Arachne::PolygonsPointIndex& lpi) const
    {
        return Slic3r::PointHash{}(lpi.p());
    }
};
}//namespace std



#endif//UTILS_POLYGONS_POINT_INDEX_H
