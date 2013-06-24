#include <admesh/stl.h>

extern "C" {
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
}

class TriangleMesh
{
    public:
    TriangleMesh();
    ~TriangleMesh();
    void ReadSTLFile(char* input_file);
    void Repair();
    void WriteOBJFile(char* output_file);
    AV* ToPerl();
    private:
    stl_file stl;
};


