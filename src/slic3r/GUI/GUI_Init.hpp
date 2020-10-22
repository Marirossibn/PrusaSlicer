#ifndef slic3r_GUI_Init_hpp_
#define slic3r_GUI_Init_hpp_

namespace Slic3r {
namespace GUI {

struct GUI_InitParams
{
	int		                    argc;
	char	                  **argv;

    std::vector<std::string>    load_configs;
    DynamicPrintConfig          extra_config;
    std::vector<std::string>    input_files;

	bool	                    start_as_gcodeviewer;
};

int GUI_Run(GUI_InitParams &params);

} // namespace GUI
} // namespace Slic3r

#endif slic3r_GUI_Init_hpp_
