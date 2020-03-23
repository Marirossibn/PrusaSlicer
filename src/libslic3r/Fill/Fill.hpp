#ifndef slic3r_Fill_hpp_
#define slic3r_Fill_hpp_

#include <memory.h>
#include <float.h>
#include <stdint.h>

#include "../libslic3r.h"
#include "../BoundingBox.hpp"
#include "../PrintConfig.hpp"

#include "FillBase.hpp"

namespace Slic3r {

class ExtrusionEntityCollection;
class LayerRegion;

// An interface class to Perl, aggregating an instance of a Fill and a FillData.
class Filler
{
public:
    Filler() : fill(NULL) {}
    ~Filler() { 
        delete fill; 
        fill = NULL;
    }
    Fill        *fill;
    FillParams   params;
};

} // namespace Slic3r

#endif // slic3r_Fill_hpp_
