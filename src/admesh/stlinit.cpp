/*  ADMesh -- process triangulated solid meshes
 *  Copyright (C) 1995, 1996  Anthony D. Martin <amartin@engr.csulb.edu>
 *  Copyright (C) 2013, 2014  several contributors, see AUTHORS
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.

 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.

 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *  Questions, comments, suggestions, etc to
 *           https://github.com/admesh/admesh/issues
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include <boost/nowide/cstdio.hpp>
#include <boost/detail/endian.hpp>

#include "stl.h"

#ifndef SEEK_SET
#error "SEEK_SET not defined"
#endif

static FILE* stl_open_count_facets(stl_file *stl, const char *file) 
{
  long           file_size;
  uint32_t       header_num_facets;
  uint32_t       num_facets;
  int            i;
  size_t         s;
  unsigned char  chtest[128];
  int            num_lines = 1;
  char           *error_msg;

  /* Open the file in binary mode first */
  FILE *fp = boost::nowide::fopen(file, "rb");
  if (fp == nullptr) {
    error_msg = (char*)
                malloc(81 + strlen(file)); /* Allow 80 chars+file size for message */
    sprintf(error_msg, "stl_initialize: Couldn't open %s for reading",
            file);
    perror(error_msg);
    free(error_msg);
    return nullptr;
  }
  /* Find size of file */
  fseek(fp, 0, SEEK_END);
  file_size = ftell(fp);

  /* Check for binary or ASCII file */
  fseek(fp, HEADER_SIZE, SEEK_SET);
  if (!fread(chtest, sizeof(chtest), 1, fp)) {
    perror("The input is an empty file");
    fclose(fp);
    return nullptr;
  }
  stl->stats.type = ascii;
  for(s = 0; s < sizeof(chtest); s++) {
    if(chtest[s] > 127) {
      stl->stats.type = binary;
      break;
    }
  }
  rewind(fp);

  /* Get the header and the number of facets in the .STL file */
  /* If the .STL file is binary, then do the following */
  if(stl->stats.type == binary) {
    /* Test if the STL file has the right size  */
    if(((file_size - HEADER_SIZE) % SIZEOF_STL_FACET != 0)
        || (file_size < STL_MIN_FILE_SIZE)) {
      fprintf(stderr, "The file %s has the wrong size.\n", file);
      fclose(fp);
      return nullptr;
    }
    num_facets = (file_size - HEADER_SIZE) / SIZEOF_STL_FACET;

    /* Read the header */
    if (fread(stl->stats.header, LABEL_SIZE, 1, fp) > 79) {
      stl->stats.header[80] = '\0';
    }

    /* Read the int following the header.  This should contain # of facets */
    bool header_num_faces_read = fread(&header_num_facets, sizeof(uint32_t), 1, fp) != 0;
#ifndef BOOST_LITTLE_ENDIAN
    // Convert from little endian to big endian.
    stl_internal_reverse_quads((char*)&header_num_facets, 4);
#endif /* BOOST_LITTLE_ENDIAN */
    if (! header_num_faces_read || num_facets != header_num_facets) {
      fprintf(stderr,
              "Warning: File size doesn't match number of facets in the header\n");
    }
  }
  /* Otherwise, if the .STL file is ASCII, then do the following */
  else {
    /* Reopen the file in text mode (for getting correct newlines on Windows) */
    // fix to silence a warning about unused return value.
    // obviously if it fails we have problems....
    fp = boost::nowide::freopen(file, "r", fp);

    // do another null check to be safe
    if(fp == nullptr) {
      error_msg = (char*)
        malloc(81 + strlen(file)); /* Allow 80 chars+file size for message */
      sprintf(error_msg, "stl_initialize: Couldn't open %s for reading",
          file);
      perror(error_msg);
      free(error_msg);
      fclose(fp);
      return nullptr;
    }
    
    /* Find the number of facets */
    char linebuf[100];
    while (fgets(linebuf, 100, fp) != nullptr) {
        /* don't count short lines */
        if (strlen(linebuf) <= 4) continue;
        
        /* skip solid/endsolid lines as broken STL file generators may put several of them */
        if (strncmp(linebuf, "solid", 5) == 0 || strncmp(linebuf, "endsolid", 8) == 0) continue;
        
        ++num_lines;
    }
    
    rewind(fp);
    
    /* Get the header */
    for(i = 0;
        (i < 80) && (stl->stats.header[i] = getc(fp)) != '\n'; i++);
    stl->stats.header[i] = '\0'; /* Lose the '\n' */
    stl->stats.header[80] = '\0';

    num_facets = num_lines / ASCII_LINES_PER_FACET;
  }
  stl->stats.number_of_facets += num_facets;
  stl->stats.original_num_facets = stl->stats.number_of_facets;
  return fp;
}

/* Reads the contents of the file pointed to by fp into the stl structure,
   starting at facet first_facet.  The second argument says if it's our first
   time running this for the stl and therefore we should reset our max and min stats. */
static bool stl_read(stl_file *stl, FILE *fp, int first_facet, bool first)
{
  stl_facet facet;

  if(stl->stats.type == binary) {
    fseek(fp, HEADER_SIZE, SEEK_SET);
  } else {
    rewind(fp);
  }

  char normal_buf[3][32];
  for(uint32_t i = first_facet; i < stl->stats.number_of_facets; i++) {
    if(stl->stats.type == binary)
      /* Read a single facet from a binary .STL file */
    {
      /* we assume little-endian architecture! */
      if (fread(&facet, 1, SIZEOF_STL_FACET, fp) != SIZEOF_STL_FACET)
      	return false;
#ifndef BOOST_LITTLE_ENDIAN
      // Convert the loaded little endian data to big endian.
      stl_internal_reverse_quads((char*)&facet, 48);
#endif /* BOOST_LITTLE_ENDIAN */
    } else
      /* Read a single facet from an ASCII .STL file */
    {
      // skip solid/endsolid
      // (in this order, otherwise it won't work when they are paired in the middle of a file)
      fscanf(fp, "endsolid%*[^\n]\n");
      fscanf(fp, "solid%*[^\n]\n");  // name might contain spaces so %*s doesn't work and it also can be empty (just "solid")
      // Leading space in the fscanf format skips all leading white spaces including numerous new lines and tabs.
      int res_normal     = fscanf(fp, " facet normal %31s %31s %31s", normal_buf[0], normal_buf[1], normal_buf[2]);
      assert(res_normal == 3);
      int res_outer_loop = fscanf(fp, " outer loop");
      assert(res_outer_loop == 0);
      int res_vertex1    = fscanf(fp, " vertex %f %f %f", &facet.vertex[0](0), &facet.vertex[0](1), &facet.vertex[0](2));
      assert(res_vertex1 == 3);
      int res_vertex2    = fscanf(fp, " vertex %f %f %f", &facet.vertex[1](0), &facet.vertex[1](1), &facet.vertex[1](2));
      assert(res_vertex2 == 3);
      int res_vertex3    = fscanf(fp, " vertex %f %f %f", &facet.vertex[2](0), &facet.vertex[2](1), &facet.vertex[2](2));
      assert(res_vertex3 == 3);
      int res_endloop    = fscanf(fp, " endloop");
      assert(res_endloop == 0);
      // There is a leading and trailing white space around endfacet to eat up all leading and trailing white spaces including numerous tabs and new lines.
      int res_endfacet   = fscanf(fp, " endfacet ");
      if (res_normal != 3 || res_outer_loop != 0 || res_vertex1 != 3 || res_vertex2 != 3 || res_vertex3 != 3 || res_endloop != 0 || res_endfacet != 0) {
        perror("Something is syntactically very wrong with this ASCII STL!");
        return false;
      }

      // The facet normal has been parsed as a single string as to workaround for not a numbers in the normal definition.
	  if (sscanf(normal_buf[0], "%f", &facet.normal(0)) != 1 ||
		  sscanf(normal_buf[1], "%f", &facet.normal(1)) != 1 ||
		  sscanf(normal_buf[2], "%f", &facet.normal(2)) != 1) {
		  // Normal was mangled. Maybe denormals or "not a number" were stored?
		  // Just reset the normal and silently ignore it.
		  memset(&facet.normal, 0, sizeof(facet.normal));
	  }
    }

#if 0
      // Report close to zero vertex coordinates. Due to the nature of the floating point numbers,
      // close to zero values may be represented with singificantly higher precision than the rest of the vertices.
      // It may be worth to round these numbers to zero during loading to reduce the number of errors reported
      // during the STL import.
      for (size_t j = 0; j < 3; ++ j) {
        if (facet.vertex[j](0) > -1e-12f && facet.vertex[j](0) < 1e-12f)
            printf("stl_read: facet %d(0) = %e\r\n", j, facet.vertex[j](0));
        if (facet.vertex[j](1) > -1e-12f && facet.vertex[j](1) < 1e-12f)
            printf("stl_read: facet %d(1) = %e\r\n", j, facet.vertex[j](1));
        if (facet.vertex[j](2) > -1e-12f && facet.vertex[j](2) < 1e-12f)
            printf("stl_read: facet %d(2) = %e\r\n", j, facet.vertex[j](2));
      }
#endif

    /* Write the facet into memory. */
    stl->facet_start[i] = facet;
    stl_facet_stats(stl, facet, first);
  }
  stl->stats.size = stl->stats.max - stl->stats.min;
  stl->stats.bounding_diameter = stl->stats.size.norm();
  return true;
}

bool stl_open(stl_file *stl, const char *file)
{
	stl_reset(stl);
	FILE *fp = stl_open_count_facets(stl, file);
	if (fp == nullptr)
		return false;
	stl_allocate(stl);
	bool result = stl_read(stl, fp, 0, true);
  	fclose(fp);
  	return result;
}

void stl_reset(stl_file *stl)
{
	stl->facet_start.clear();
	stl->neighbors_start.clear();
  	memset(&stl->stats, 0, sizeof(stl_stats));
  	stl->stats.volume = -1.0;
}

#ifndef BOOST_LITTLE_ENDIAN
extern void stl_internal_reverse_quads(char *buf, size_t cnt);
#endif /* BOOST_LITTLE_ENDIAN */

void stl_allocate(stl_file *stl) 
{
  	//  Allocate memory for the entire .STL file.
  	stl->facet_start.assign(stl->stats.number_of_facets, stl_facet());
  	// Allocate memory for the neighbors list.
  	stl->neighbors_start.assign(stl->stats.number_of_facets, stl_neighbors());
}

void stl_reallocate(stl_file *stl) 
{
	stl->facet_start.resize(stl->stats.number_of_facets);
	stl->neighbors_start.resize(stl->stats.number_of_facets);
}

void stl_facet_stats(stl_file *stl, stl_facet facet, bool &first)
{
  // While we are going through all of the facets, let's find the
  // maximum and minimum values for x, y, and z

  if (first) {
	// Initialize the max and min values the first time through
    stl->stats.min = facet.vertex[0];
    stl->stats.max = facet.vertex[0];
    stl_vertex diff = (facet.vertex[1] - facet.vertex[0]).cwiseAbs();
    stl->stats.shortest_edge = std::max(diff(0), std::max(diff(1), diff(2)));
    first = false;
  }

  // Now find the max and min values.
  for (size_t i = 0; i < 3; ++ i) {
  	stl->stats.min = stl->stats.min.cwiseMin(facet.vertex[i]);
  	stl->stats.max = stl->stats.max.cwiseMax(facet.vertex[i]);
  }
}
