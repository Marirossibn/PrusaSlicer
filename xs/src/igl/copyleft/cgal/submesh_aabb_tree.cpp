// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2016 Alec Jacobson
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#include "submesh_aabb_tree.h"
#include <stdexcept>

template<
  typename DerivedV,
  typename DerivedF,
  typename DerivedI,
  typename Kernel>
IGL_INLINE void igl::copyleft::cgal::submesh_aabb_tree(
  const Eigen::PlainObjectBase<DerivedV>& V,
  const Eigen::PlainObjectBase<DerivedF>& F,
  const Eigen::PlainObjectBase<DerivedI>& I,
  CGAL::AABB_tree<
    CGAL::AABB_traits<
      Kernel, 
      CGAL::AABB_triangle_primitive<
        Kernel, typename std::vector<
          typename Kernel::Triangle_3 >::iterator > > > & tree,
  std::vector<typename Kernel::Triangle_3 > & triangles,
  std::vector<bool> & in_I)
{
  in_I.resize(F.rows(), false);
  const size_t num_faces = I.rows();
  for (size_t i=0; i<num_faces; i++) 
  {
    const Eigen::Vector3i f = F.row(I(i, 0));
    in_I[I(i,0)] = true;
    triangles.emplace_back(
      typename Kernel::Point_3(V(f[0], 0), V(f[0], 1), V(f[0], 2)),
      typename Kernel::Point_3(V(f[1], 0), V(f[1], 1), V(f[1], 2)),
      typename Kernel::Point_3(V(f[2], 0), V(f[2], 1), V(f[2], 2)));
#ifndef NDEBUG
    if (triangles.back().is_degenerate()) 
    {
      throw std::runtime_error(
        "Input facet components contains degenerated triangles");
    }
#endif
  }
  tree.insert(triangles.begin(), triangles.end());
  tree.accelerate_distance_queries();
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
// generated by autoexplicit.sh
template void igl::copyleft::cgal::submesh_aabb_tree<Eigen::Matrix<CGAL::Lazy_exact_nt<CGAL::Gmpq>, -1, -1, 1, -1, -1>, Eigen::Matrix<int, -1, 3, 1, -1, 3>, Eigen::Matrix<int, -1, 1, 0, -1, 1>, CGAL::Epeck>(Eigen::PlainObjectBase<Eigen::Matrix<CGAL::Lazy_exact_nt<CGAL::Gmpq>, -1, -1, 1, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 3, 1, -1, 3> > const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> > const&, CGAL::AABB_tree<CGAL::AABB_traits<CGAL::Epeck, CGAL::AABB_triangle_primitive<CGAL::Epeck, std::vector<CGAL::Epeck::Triangle_3, std::allocator<CGAL::Epeck::Triangle_3> >::iterator, CGAL::Boolean_tag<false> > > >&, std::vector<CGAL::Epeck::Triangle_3, std::allocator<CGAL::Epeck::Triangle_3> >&, std::vector<bool, std::allocator<bool> >&);
// generated by autoexplicit.sh
template void igl::copyleft::cgal::submesh_aabb_tree<Eigen::Matrix<CGAL::Lazy_exact_nt<CGAL::Gmpq>, -1, -1, 1, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, 1, 0, -1, 1>, CGAL::Epeck>(Eigen::PlainObjectBase<Eigen::Matrix<CGAL::Lazy_exact_nt<CGAL::Gmpq>, -1, -1, 1, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> > const&, CGAL::AABB_tree<CGAL::AABB_traits<CGAL::Epeck, CGAL::AABB_triangle_primitive<CGAL::Epeck, std::vector<CGAL::Epeck::Triangle_3, std::allocator<CGAL::Epeck::Triangle_3> >::iterator, CGAL::Boolean_tag<false> > > >&, std::vector<CGAL::Epeck::Triangle_3, std::allocator<CGAL::Epeck::Triangle_3> >&, std::vector<bool, std::allocator<bool> >&);
template void igl::copyleft::cgal::submesh_aabb_tree<Eigen::Matrix<CGAL::Lazy_exact_nt<CGAL::Gmpq>, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, 1, 0, -1, 1>, CGAL::Epeck>(Eigen::PlainObjectBase<Eigen::Matrix<CGAL::Lazy_exact_nt<CGAL::Gmpq>, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> > const&, CGAL::AABB_tree<CGAL::AABB_traits<CGAL::Epeck, CGAL::AABB_triangle_primitive<CGAL::Epeck, std::vector<CGAL::Epeck::Triangle_3, std::allocator<CGAL::Epeck::Triangle_3> >::iterator, CGAL::Boolean_tag<false> > > >&, std::vector<CGAL::Epeck::Triangle_3, std::allocator<CGAL::Epeck::Triangle_3> >&, std::vector<bool, std::allocator<bool> >&);
#endif
