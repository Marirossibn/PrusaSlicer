// Copyright (c) 2016 Scott Lenser
// Copyright (c) 2020 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef UTILS_SPARSE_POINT_GRID_H
#define UTILS_SPARSE_POINT_GRID_H

#include <cassert>
#include <vector>

#include "SparseGrid.hpp"

namespace Slic3r::Arachne {

/*! \brief Sparse grid which can locate spatially nearby elements efficiently.
 *
 * \tparam ElemT The element type to store.
 * \tparam Locator The functor to get the location from ElemT.  Locator
 *    must have: Point operator()(const ElemT &elem) const
 *    which returns the location associated with val.
 */
template<class ElemT, class Locator> class SparsePointGrid : public SparseGrid<ElemT>
{
public:
    using Elem = ElemT;

    /*! \brief Constructs a sparse grid with the specified cell size.
     *
     * \param[in] cell_size The size to use for a cell (square) in the grid.
     *    Typical values would be around 0.5-2x of expected query radius.
     * \param[in] elem_reserve Number of elements to research space for.
     * \param[in] max_load_factor Maximum average load factor before rehashing.
     */
    SparsePointGrid(coord_t cell_size, size_t elem_reserve = 0U, float max_load_factor = 1.0f);

    /*! \brief Inserts elem into the sparse grid.
     *
     * \param[in] elem The element to be inserted.
     */
    void insert(const Elem &elem);

    /*!
     * Get just any element that's within a certain radius of a point.
     *
     * Rather than giving a vector of nearby elements, this function just gives
     * a single element, any element, in no particular order.
     * \param query_pt The point to query for an object nearby.
     * \param radius The radius of what is considered "nearby".
     */
    const ElemT *getAnyNearby(const Point &query_pt, coord_t radius);

protected:
    using GridPoint = typename SparseGrid<ElemT>::GridPoint;

    /*! \brief Accessor for getting locations from elements. */
    Locator m_locator;
};

template<class ElemT, class Locator>
SparsePointGrid<ElemT, Locator>::SparsePointGrid(coord_t cell_size, size_t elem_reserve, float max_load_factor) : SparseGrid<ElemT>(cell_size, elem_reserve, max_load_factor) {}

template<class ElemT, class Locator>
void SparsePointGrid<ElemT, Locator>::insert(const Elem &elem)
{
    Point     loc      = m_locator(elem);
    GridPoint grid_loc = SparseGrid<ElemT>::toGridPoint(loc.template cast<int64_t>());

    SparseGrid<ElemT>::m_grid.emplace(grid_loc, elem);
}

template<class ElemT, class Locator>
const ElemT *SparsePointGrid<ElemT, Locator>::getAnyNearby(const Point &query_pt, coord_t radius)
{
    const ElemT                              *ret          = nullptr;
    const std::function<bool(const ElemT &)> &process_func = [&ret, query_pt, radius, this](const ElemT &maybe_nearby) {
        if (shorter_then(m_locator(maybe_nearby) - query_pt, radius)) {
            ret = &maybe_nearby;
            return false;
        }
        return true;
    };
    SparseGrid<ElemT>::processNearby(query_pt, radius, process_func);

    return ret;
}

} // namespace Slic3r::Arachne

#endif // UTILS_SPARSE_POINT_GRID_H
