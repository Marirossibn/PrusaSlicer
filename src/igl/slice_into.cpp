// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "slice_into.h"
#include "colon.h"

// Bug in unsupported/Eigen/SparseExtra needs iostream first
#include <iostream>
#include <unsupported/Eigen/SparseExtra>

template <typename T>
IGL_INLINE void igl::slice_into(
  const Eigen::SparseMatrix<T>& X,
  const Eigen::Matrix<int,Eigen::Dynamic,1> & R,
  const Eigen::Matrix<int,Eigen::Dynamic,1> & C,
  Eigen::SparseMatrix<T>& Y)
{

#ifndef NDEBUG
  int xm = X.rows();
  int xn = X.cols();
  assert(R.size() == xm);
  assert(C.size() == xn);
  int ym = Y.size();
  int yn = Y.size();
  assert(R.minCoeff() >= 0);
  assert(R.maxCoeff() < ym);
  assert(C.minCoeff() >= 0);
  assert(C.maxCoeff() < yn);
#endif

  // create temporary dynamic sparse matrix
  Eigen::DynamicSparseMatrix<T, Eigen::RowMajor>  dyn_Y(Y);
  // Iterate over outside
  for(int k=0; k<X.outerSize(); ++k)
  {
    // Iterate over inside
    for(typename Eigen::SparseMatrix<T>::InnerIterator it (X,k); it; ++it)
    {
      dyn_Y.coeffRef(R(it.row()),C(it.col())) = it.value();
    }
  }
  Y = Eigen::SparseMatrix<T>(dyn_Y);
}

template <typename DerivedX, typename DerivedY>
IGL_INLINE void igl::slice_into(
  const Eigen::DenseBase<DerivedX> & X,
  const Eigen::Matrix<int,Eigen::Dynamic,1> & R,
  const Eigen::Matrix<int,Eigen::Dynamic,1> & C,
  Eigen::PlainObjectBase<DerivedY> & Y)
{

  int xm = X.rows();
  int xn = X.cols();
#ifndef NDEBUG
  assert(R.size() == xm);
  assert(C.size() == xn);
  int ym = Y.size();
  int yn = Y.size();
  assert(R.minCoeff() >= 0);
  assert(R.maxCoeff() < ym);
  assert(C.minCoeff() >= 0);
  assert(C.maxCoeff() < yn);
#endif

  // Build reindexing maps for columns and rows, -1 means not in map
  Eigen::Matrix<int,Eigen::Dynamic,1> RI;
  RI.resize(xm);
  for(int i = 0;i<xm;i++)
  {
    for(int j = 0;j<xn;j++)
    {
      Y(R(i),C(j)) = X(i,j);
    }
  }
}

template <typename MatX, typename MatY>
IGL_INLINE void igl::slice_into(
  const MatX& X,
  const Eigen::Matrix<int,Eigen::Dynamic,1> & R,
  const int dim,
  MatY& Y)
{
  Eigen::VectorXi C;
  switch(dim)
  {
    case 1:
      assert(R.size() == X.rows());
      // boring base case
      if(X.cols() == 0)
      {
        return;
      }
      igl::colon(0,X.cols()-1,C);
      return slice_into(X,R,C,Y);
    case 2:
      assert(R.size() == X.cols());
      // boring base case
      if(X.rows() == 0)
      {
        return;
      }
      igl::colon(0,X.rows()-1,C);
      return slice_into(X,C,R,Y);
    default:
      assert(false && "Unsupported dimension");
      return;
  }
}

template <typename DerivedX, typename DerivedY>
IGL_INLINE void igl::slice_into(
  const Eigen::DenseBase<DerivedX> & X,
  const Eigen::Matrix<int,Eigen::Dynamic,1> & R,
  Eigen::PlainObjectBase<DerivedY> & Y)
{
  // phony column indices
  Eigen::Matrix<int,Eigen::Dynamic,1> C;
  C.resize(1);
  C(0) = 0;
  return igl::slice_into(X,R,C,Y);
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
// generated by autoexplicit.sh
template void igl::slice_into<Eigen::Matrix<int, -1, 1, 0, -1, 1>, Eigen::Matrix<int, -1, 1, 0, -1, 1> >(Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, int, Eigen::Matrix<int, -1, 1, 0, -1, 1>&);
// generated by autoexplicit.sh
template void igl::slice_into<Eigen::Matrix<double, -1, 2, 0, -1, 2>, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 1, -1, -1> > >(Eigen::Matrix<double, -1, 2, 0, -1, 2> const&, Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, int, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 1, -1, -1> >&);
// generated by autoexplicit.sh
template void igl::slice_into<Eigen::Block<Eigen::Matrix<double, -1, -1, 0, -1, -1>, -1, -1, true>, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 1, -1, -1> > >(Eigen::Block<Eigen::Matrix<double, -1, -1, 0, -1, -1>, -1, -1, true> const&, Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, int, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 1, -1, -1> >&);
// generated by autoexplicit.sh
template void igl::slice_into<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > >(Eigen::Matrix<double, -1, -1, 0, -1, -1> const&, Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, int, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&);
// generated by autoexplicit.sh
template void igl::slice_into<Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> > >(Eigen::Matrix<double, -1, 1, 0, -1, 1> const&, Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, int, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> >&);
template void igl::slice_into<double>(Eigen::SparseMatrix<double, 0, int> const&, Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, Eigen::SparseMatrix<double, 0, int>&);
template void igl::slice_into<Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, 1, 0, -1, 1> >(Eigen::Matrix<double, -1, 1, 0, -1, 1> const&, Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, int, Eigen::Matrix<double, -1, 1, 0, -1, 1>&);
template void igl::slice_into<Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, 1, 0, -1, 1> >(Eigen::DenseBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> > const&, Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> >&);
template void igl::slice_into<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1> >(Eigen::Matrix<double, -1, -1, 0, -1, -1> const&, Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, int, Eigen::Matrix<double, -1, -1, 0, -1, -1>&);
template void igl::slice_into<Eigen::Matrix<int, -1, 1, 0, -1, 1>, Eigen::Matrix<int, -1, 1, 0, -1, 1> >(Eigen::DenseBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> > const&, Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> >&);
template void igl::slice_into<Eigen::SparseMatrix<double, 0, int>, Eigen::SparseMatrix<double, 0, int> >(Eigen::SparseMatrix<double, 0, int> const&, Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, int, Eigen::SparseMatrix<double, 0, int>&);
template void igl::slice_into<int>(Eigen::SparseMatrix<int, 0, int> const&, Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, Eigen::SparseMatrix<int, 0, int>&);
template void igl::slice_into<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1> >(Eigen::DenseBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&);
template void igl::slice_into<Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(Eigen::DenseBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&);
template void igl::slice_into<Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(Eigen::Matrix<int, -1, -1, 0, -1, -1> const&, Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, int, Eigen::Matrix<int, -1, -1, 0, -1, -1>&);
template void igl::slice_into<Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(Eigen::DenseBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&);
#endif
