// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#include "adjacency_list.h"

#include "verbose.h"
#include <algorithm>

template <typename Index, typename IndexVector>
IGL_INLINE void igl::adjacency_list(
    const Eigen::PlainObjectBase<Index>  & F,
    std::vector<std::vector<IndexVector> >& A,
    bool sorted)
{
  A.clear(); 
  A.resize(F.maxCoeff()+1);
  
  // Loop over faces
  for(int i = 0;i<F.rows();i++)
  {
    // Loop over this face
    for(int j = 0;j<F.cols();j++)
    {
      // Get indices of edge: s --> d
      int s = F(i,j);
      int d = F(i,(j+1)%F.cols());
      A.at(s).push_back(d);
      A.at(d).push_back(s);
    }
  }
  
  // Remove duplicates
  for(int i=0; i<(int)A.size();++i)
  {
    std::sort(A[i].begin(), A[i].end());
    A[i].erase(std::unique(A[i].begin(), A[i].end()), A[i].end());
  }
  
  // If needed, sort every VV
  if (sorted)
  {
    // Loop over faces
    
    // for every vertex v store a set of ordered edges not incident to v that belongs to triangle incident on v.
    std::vector<std::vector<std::vector<int> > > SR; 
    SR.resize(A.size());
    
    for(int i = 0;i<F.rows();i++)
    {
      // Loop over this face
      for(int j = 0;j<F.cols();j++)
      {
        // Get indices of edge: s --> d
        int s = F(i,j);
        int d = F(i,(j+1)%F.cols());
        // Get index of opposing vertex v
        int v = F(i,(j+2)%F.cols());
        
        std::vector<int> e(2);
        e[0] = d;
        e[1] = v;
        SR[s].push_back(e);
      }
    }
    
    for(int v=0; v<(int)SR.size();++v)
    {
      std::vector<IndexVector>& vv = A.at(v);
      std::vector<std::vector<int> >& sr = SR[v];
      
      std::vector<std::vector<int> > pn = sr;
      
      // Compute previous/next for every element in sr
      for(int i=0;i<(int)sr.size();++i)
      {
        int a = sr[i][0];
        int b = sr[i][1];
        
        // search for previous
        int p = -1;
        for(int j=0;j<(int)sr.size();++j)
          if(sr[j][1] == a)
            p = j;
        pn[i][0] = p;
        
        // search for next
        int n = -1;
        for(int j=0;j<(int)sr.size();++j)
          if(sr[j][0] == b)
            n = j;
        pn[i][1] = n;
        
      }
      
      // assume manifoldness (look for beginning of a single chain)
      int c = 0;
      for(int j=0; j<=(int)sr.size();++j)
        if (pn[c][0] != -1)
          c = pn[c][0];
      
      if (pn[c][0] == -1) // border case
      {
        // finally produce the new vv relation
        for(int j=0; j<(int)sr.size();++j)
        {
          vv[j] = sr[c][0];
          if (pn[c][1] != -1)
            c = pn[c][1];
        }
        vv.back() = sr[c][1];
      }
      else
      {
        // finally produce the new vv relation
        for(int j=0; j<(int)sr.size();++j)
        {
          vv[j] = sr[c][0];
          
          c = pn[c][1];
        }
      }
    }
  }
}

template <typename Index>
IGL_INLINE void igl::adjacency_list(
  const std::vector<std::vector<Index> > & F,
  std::vector<std::vector<Index> >& A)
{
  A.clear(); 
  A.resize(F.maxCoeff()+1);
  
  // Loop over faces
  for(int i = 0;i<F.size();i++)
  {
    // Loop over this face
    for(int j = 0;j<F[i].size();j++)
    {
      // Get indices of edge: s --> d
      int s = F(i,j);
      int d = F(i,(j+1)%F[i].size());
      A.at(s).push_back(d);
      A.at(d).push_back(s);
    }
  }
  
  // Remove duplicates
  for(int i=0; i<(int)A.size();++i)
  {
    std::sort(A[i].begin(), A[i].end());
    A[i].erase(std::unique(A[i].begin(), A[i].end()), A[i].end());
  }
  
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
// generated by autoexplicit.sh
template void igl::adjacency_list<Eigen::Matrix<int, -1, 2, 0, -1, 2>, int>(Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 2, 0, -1, 2> > const&, std::vector<std::vector<int, std::allocator<int> >, std::allocator<std::vector<int, std::allocator<int> > > >&, bool);
// generated by autoexplicit.sh
template void igl::adjacency_list<Eigen::Matrix<int, -1, -1, 0, -1, -1>, int>(Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, std::vector<std::vector<int, std::allocator<int> >, std::allocator<std::vector<int, std::allocator<int> > > >&, bool);
template void igl::adjacency_list<Eigen::Matrix<int, -1, 3, 0, -1, 3>, int>(Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 3, 0, -1, 3> > const&, std::vector<std::vector<int, std::allocator<int> >, std::allocator<std::vector<int, std::allocator<int> > > >&, bool);
#endif
