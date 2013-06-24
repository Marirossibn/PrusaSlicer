#ifndef _myinit_h_
#define _myinit_h_

#include <vector>

////////////////
class ZTable
{
    public:
    ZTable(std::vector<unsigned int>* z_array);
    std::vector<unsigned int> get_range(unsigned int min_z, unsigned int max_z);
    std::vector<unsigned int> z;
};

ZTable::ZTable(std::vector<unsigned int>* ztable) :
    z(*ztable)
{
}
////////////////

#include "TriangleMesh.hpp"

#endif
