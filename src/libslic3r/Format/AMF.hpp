#ifndef slic3r_Format_AMF_hpp_
#define slic3r_Format_AMF_hpp_

namespace Slic3r {

class Model;
class DynamicPrintConfig;

// Load the content of an amf file into the given model and configuration.
extern bool load_amf(const char* path, DynamicPrintConfig* config, Model* model, bool check_version);

// Save the given model and the config data into an amf file.
// The model could be modified during the export process if meshes are not repaired or have no shared vertices
#if ENABLE_CONFIGURABLE_PATHS_EXPORT_TO_3MF_AND_AMF
extern bool store_amf(const char* path, Model* model, const DynamicPrintConfig* config, bool fullpath_sources);
#else
extern bool store_amf(const char *path, Model *model, const DynamicPrintConfig *config);
#endif // ENABLE_CONFIGURABLE_PATHS_EXPORT_TO_3MF_AND_AMF

}; // namespace Slic3r

#endif /* slic3r_Format_AMF_hpp_ */
