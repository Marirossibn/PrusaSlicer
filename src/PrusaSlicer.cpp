#ifdef WIN32
    // Why?
    #define _WIN32_WINNT 0x0502
    // The standard Windows includes.
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <Windows.h>
    #include <wchar.h>
    #ifdef SLIC3R_GUI
    extern "C"
    {
        // Let the NVIDIA and AMD know we want to use their graphics card
        // on a dual graphics card system.
        __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
        __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
    }
    #endif /* SLIC3R_GUI */
#endif /* WIN32 */

#include <cstdio>
#include <string>
#include <cstring>
#include <iostream>
#include <math.h>
#include <boost/filesystem.hpp>
#include <boost/nowide/args.hpp>
#include <boost/nowide/cenv.hpp>
#include <boost/nowide/iostream.hpp>
#include <boost/nowide/integration/filesystem.hpp>

#include "unix/fhs.hpp"  // Generated by CMake from ../platform/unix/fhs.hpp.in

#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Format/AMF.hpp"
#include "libslic3r/Format/3mf.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Format/OBJ.hpp"
#include "libslic3r/Utils.hpp"

#include "PrusaSlicer.hpp"

#ifdef SLIC3R_GUI
    #include "slic3r/GUI/GUI.hpp"
    #include "slic3r/GUI/GUI_App.hpp"
    #include "slic3r/GUI/3DScene.hpp"
#endif /* SLIC3R_GUI */

using namespace Slic3r;

PrinterTechnology get_printer_technology(const DynamicConfig &config)
{
    const ConfigOptionEnum<PrinterTechnology> *opt = config.option<ConfigOptionEnum<PrinterTechnology>>("printer_technology");
    return (opt == nullptr) ? ptUnknown : opt->value;
}

int CLI::run(int argc, char **argv)
{
	// Switch boost::filesystem to utf8.
    try {
        boost::nowide::nowide_filesystem();
    } catch (const std::runtime_error& ex) {
        std::string caption = std::string(SLIC3R_APP_NAME) + " Error";
        std::string text = std::string("An error occured while setting up locale.\n") + (
#if !defined(_WIN32) && !defined(__APPLE__)
        	// likely some linux system
        	"You may need to reconfigure the missing locales, likely by running the \"locale-gen\" and \"dpkg-reconfigure locales\" commands.\n"
#endif
        	SLIC3R_APP_NAME " will now terminate.\n\n") + ex.what();
    #if defined(_WIN32) && defined(SLIC3R_GUI)
        if (m_actions.empty())
        	// Empty actions means Slicer is executed in the GUI mode. Show a GUI message.
            MessageBoxA(NULL, text.c_str(), caption.c_str(), MB_OK | MB_ICONERROR);
    #endif
        boost::nowide::cerr << text.c_str() << std::endl;
        return 1;
    }

	if (! this->setup(argc, argv))
		return 1;

    m_extra_config.apply(m_config, true);
    m_extra_config.normalize();

    bool							start_gui			= m_actions.empty() &&
        // cutting transformations are setting an "export" action.
        std::find(m_transforms.begin(), m_transforms.end(), "cut") == m_transforms.end() &&
        std::find(m_transforms.begin(), m_transforms.end(), "cut_x") == m_transforms.end() &&
        std::find(m_transforms.begin(), m_transforms.end(), "cut_y") == m_transforms.end();
    PrinterTechnology				printer_technology	= get_printer_technology(m_extra_config);
    const std::vector<std::string> &load_configs		= m_config.option<ConfigOptionStrings>("load", true)->values;

    // load config files supplied via --load
    for (auto const &file : load_configs) {
        if (! boost::filesystem::exists(file)) {
            if (m_config.opt_bool("ignore_nonexistent_config")) {
                continue;
            } else {
                boost::nowide::cerr << "No such file: " << file << std::endl;
                return 1;
            }
        }
        DynamicPrintConfig config;
        try {
            config.load(file);
        } catch (std::exception &ex) {
            boost::nowide::cerr << "Error while reading config file: " << ex.what() << std::endl;
            return 1;
        }
        config.normalize();
        PrinterTechnology other_printer_technology = get_printer_technology(config);
        if (printer_technology == ptUnknown) {
            printer_technology = other_printer_technology;
        } else if (printer_technology != other_printer_technology && other_printer_technology != ptUnknown) {
            boost::nowide::cerr << "Mixing configurations for FFF and SLA technologies" << std::endl;
            return 1;
        }
        m_print_config.apply(config);
    }

    // Read input file(s) if any.
    for (const std::string &file : m_input_files) {
        if (! boost::filesystem::exists(file)) {
            boost::nowide::cerr << "No such file: " << file << std::endl;
            exit(1);
        }
        Model model;
        try {
            // When loading an AMF or 3MF, config is imported as well, including the printer technology.
            DynamicPrintConfig config;
            model = Model::read_from_file(file, &config, true);
            PrinterTechnology other_printer_technology = get_printer_technology(config);
            if (printer_technology == ptUnknown) {
                printer_technology = other_printer_technology;
            } else if (printer_technology != other_printer_technology && other_printer_technology != ptUnknown) {
                boost::nowide::cerr << "Mixing configurations for FFF and SLA technologies" << std::endl;
                return 1;
            }
            // config is applied to m_print_config before the current m_config values.
            config += std::move(m_print_config);
            m_print_config = std::move(config);
        } catch (std::exception &e) {
            boost::nowide::cerr << file << ": " << e.what() << std::endl;
            return 1;
        }
        if (model.objects.empty()) {
            boost::nowide::cerr << "Error: file is empty: " << file << std::endl;
            continue;
        }
        m_models.push_back(model);
    }

    // Apply command line options to a more specific DynamicPrintConfig which provides normalize()
    // (command line options override --load files)
    m_print_config.apply(m_extra_config, true);
    // Normalizing after importing the 3MFs / AMFs
    m_print_config.normalize();

    if (printer_technology == ptUnknown)
        printer_technology = std::find(m_actions.begin(), m_actions.end(), "export_sla") == m_actions.end() ? ptFFF : ptSLA;

    // Initialize full print configs for both the FFF and SLA technologies.
    FullPrintConfig    fff_print_config;
    SLAFullPrintConfig sla_print_config;
    
    // Synchronize the default parameters and the ones received on the command line.
    if (printer_technology == ptFFF) {
        fff_print_config.apply(m_print_config, true);
        m_print_config.apply(fff_print_config, true);
    } else if (printer_technology == ptSLA) {
        // The default value has to be different from the one in fff mode.
        sla_print_config.output_filename_format.value = "[input_filename_base].sl1";
        
        // The default bed shape should reflect the default display parameters
        // and not the fff defaults.
        double w = sla_print_config.display_width.getFloat();
        double h = sla_print_config.display_height.getFloat();
        sla_print_config.bed_shape.values = { Vec2d(0, 0), Vec2d(w, 0), Vec2d(w, h), Vec2d(0, h) };
        
        sla_print_config.apply(m_print_config, true);
        m_print_config.apply(sla_print_config, true);
    }
    
    // Loop through transform options.
    bool user_center_specified = false;
    for (auto const &opt_key : m_transforms) {
        if (opt_key == "merge") {
            Model m;
            for (auto &model : m_models)
                for (ModelObject *o : model.objects)
                    m.add_object(*o);
            // Rearrange instances unless --dont-arrange is supplied
            if (! m_config.opt_bool("dont_arrange")) {
                m.add_default_instances();
                const BoundingBoxf &bb = fff_print_config.bed_shape.values;
                m.arrange_objects(
                    fff_print_config.min_object_distance(),
                    // If we are going to use the merged model for printing, honor
                    // the configured print bed for arranging, otherwise do it freely.
                    this->has_print_action() ? &bb : nullptr
                );
            }
            m_models.clear();
            m_models.emplace_back(std::move(m));
        } else if (opt_key == "duplicate") {
            const BoundingBoxf &bb = fff_print_config.bed_shape.values;
            for (auto &model : m_models) {
                const bool all_objects_have_instances = std::none_of(
                    model.objects.begin(), model.objects.end(),
                    [](ModelObject* o){ return o->instances.empty(); }
                );
                if (all_objects_have_instances) {
                    // if all input objects have defined position(s) apply duplication to the whole model
                    model.duplicate(m_config.opt_int("duplicate"), fff_print_config.min_object_distance(), &bb);
                } else {
                    model.add_default_instances();
                    model.duplicate_objects(m_config.opt_int("duplicate"), fff_print_config.min_object_distance(), &bb);
                }
            }
        } else if (opt_key == "duplicate_grid") {
            std::vector<int> &ints = m_config.option<ConfigOptionInts>("duplicate_grid")->values;
            const int x = ints.size() > 0 ? ints.at(0) : 1;
            const int y = ints.size() > 1 ? ints.at(1) : 1;
            const double distance = fff_print_config.duplicate_distance.value;
            for (auto &model : m_models)
                model.duplicate_objects_grid(x, y, (distance > 0) ? distance : 6);  // TODO: this is not the right place for setting a default
        } else if (opt_key == "center") {
        	user_center_specified = true;
            for (auto &model : m_models) {
                model.add_default_instances();
                // this affects instances:
                model.center_instances_around_point(m_config.option<ConfigOptionPoint>("center")->value);
                // this affects volumes:
                //FIXME Vojtech: Who knows why the complete model should be aligned with Z as a single rigid body?
                //model.align_to_ground();
                BoundingBoxf3 bbox;
                for (ModelObject *model_object : model.objects)
                    // We are interested into the Z span only, therefore it is sufficient to measure the bounding box of the 1st instance only.
                    bbox.merge(model_object->instance_bounding_box(0, false));
                for (ModelObject *model_object : model.objects)
                    for (ModelInstance *model_instance : model_object->instances)
                        model_instance->set_offset(Z, model_instance->get_offset(Z) - bbox.min.z());
            }
        } else if (opt_key == "align_xy") {
            const Vec2d &p = m_config.option<ConfigOptionPoint>("align_xy")->value;
            for (auto &model : m_models) {
                BoundingBoxf3 bb = model.bounding_box();
                // this affects volumes:
                model.translate(-(bb.min.x() - p.x()), -(bb.min.y() - p.y()), -bb.min.z());
            }
        } else if (opt_key == "dont_arrange") {
            // do nothing - this option alters other transform options
        } else if (opt_key == "rotate") {
            for (auto &model : m_models)
                for (auto &o : model.objects)
                    // this affects volumes:
                    o->rotate(Geometry::deg2rad(m_config.opt_float(opt_key)), Z);
        } else if (opt_key == "rotate_x") {
            for (auto &model : m_models)
                for (auto &o : model.objects)
                    // this affects volumes:
                    o->rotate(Geometry::deg2rad(m_config.opt_float(opt_key)), X);
        } else if (opt_key == "rotate_y") {
            for (auto &model : m_models)
                for (auto &o : model.objects)
                    // this affects volumes:
                    o->rotate(Geometry::deg2rad(m_config.opt_float(opt_key)), Y);
        } else if (opt_key == "scale") {
            for (auto &model : m_models)
                for (auto &o : model.objects)
                    // this affects volumes:
                    o->scale(m_config.get_abs_value(opt_key, 1));
        } else if (opt_key == "scale_to_fit") {
            const Vec3d &opt = m_config.opt<ConfigOptionPoint3>(opt_key)->value;
            if (opt.x() <= 0 || opt.y() <= 0 || opt.z() <= 0) {
                boost::nowide::cerr << "--scale-to-fit requires a positive volume" << std::endl;
                return 1;
            }
            for (auto &model : m_models)
                for (auto &o : model.objects)
                    // this affects volumes:
                    o->scale_to_fit(opt);
        } else if (opt_key == "cut" || opt_key == "cut_x" || opt_key == "cut_y") {
            std::vector<Model> new_models;
            for (auto &model : m_models) {
                model.translate(0, 0, -model.bounding_box().min.z());  // align to z = 0
                size_t num_objects = model.objects.size();
                for (size_t i = 0; i < num_objects; ++ i) {

#if 0
                    if (opt_key == "cut_x") {
                        o->cut(X, m_config.opt_float("cut_x"), &out);
                    } else if (opt_key == "cut_y") {
                        o->cut(Y, m_config.opt_float("cut_y"), &out);
                    } else if (opt_key == "cut") {
                        o->cut(Z, m_config.opt_float("cut"), &out);
                    }
#else
                    model.objects.front()->cut(0, m_config.opt_float("cut"), true, true, true);
#endif
                    model.delete_object(size_t(0));
                }
            }

            // TODO: copy less stuff around using pointers
            m_models = new_models;

            if (m_actions.empty())
                m_actions.push_back("export_stl");
        }
#if 0
        else if (opt_key == "cut_grid") {
            std::vector<Model> new_models;
            for (auto &model : m_models) {
                TriangleMesh mesh = model.mesh();
                mesh.repair();

                TriangleMeshPtrs meshes = mesh.cut_by_grid(m_config.option<ConfigOptionPoint>("cut_grid")->value);
                size_t i = 0;
                for (TriangleMesh* m : meshes) {
                    Model out;
                    auto o = out.add_object();
                    o->add_volume(*m);
                    o->input_file += "_" + std::to_string(i++);
                    delete m;
                }
            }

            // TODO: copy less stuff around using pointers
            m_models = new_models;

            if (m_actions.empty())
                m_actions.push_back("export_stl");
        }
#endif
        else if (opt_key == "split") {
            for (Model &model : m_models) {
                size_t num_objects = model.objects.size();
                for (size_t i = 0; i < num_objects; ++ i) {
                    model.objects.front()->split(nullptr);
                    model.delete_object(size_t(0));
                }
            }
        } else if (opt_key == "repair") {
            // Models are repaired by default.
            //for (auto &model : m_models)
            //    model.repair();
        } else {
            boost::nowide::cerr << "error: option not implemented yet: " << opt_key << std::endl;
            return 1;
        }
    }

    // loop through action options
    for (auto const &opt_key : m_actions) {
        if (opt_key == "help") {
            this->print_help();
        } else if (opt_key == "help_fff") {
            this->print_help(true, ptFFF);
        } else if (opt_key == "help_sla") {
            this->print_help(true, ptSLA);
        } else if (opt_key == "save") {
            //FIXME check for mixing the FFF / SLA parameters.
            // or better save fff_print_config vs. sla_print_config
            m_print_config.save(m_config.opt_string("save"));
        } else if (opt_key == "info") {
            // --info works on unrepaired model
            for (Model &model : m_models) {
                model.add_default_instances();
                model.print_info();
            }
        } else if (opt_key == "export_stl") {
            for (auto &model : m_models)
                model.add_default_instances();
            if (! this->export_models(IO::STL))
                return 1;
        } else if (opt_key == "export_obj") {
            for (auto &model : m_models)
                model.add_default_instances();
            if (! this->export_models(IO::OBJ))
                return 1;
        } else if (opt_key == "export_amf") {
            if (! this->export_models(IO::AMF))
                return 1;
        } else if (opt_key == "export_3mf") {
            if (! this->export_models(IO::TMF))
                return 1;
        } else if (opt_key == "export_gcode" || opt_key == "export_sla" || opt_key == "slice") {
            if (opt_key == "export_gcode" && printer_technology == ptSLA) {
                boost::nowide::cerr << "error: cannot export G-code for an FFF configuration" << std::endl;
                return 1;
            } else if (opt_key == "export_sla" && printer_technology == ptFFF) {
                boost::nowide::cerr << "error: cannot export SLA slices for a SLA configuration" << std::endl;
                return 1;
            }
            // Make a copy of the model if the current action is not the last action, as the model may be
            // modified by the centering and such.
            Model model_copy;
            bool  make_copy = &opt_key != &m_actions.back();
            for (Model &model_in : m_models) {
                if (make_copy)
                    model_copy = model_in;
                Model &model = make_copy ? model_copy : model_in;
                // If all objects have defined instances, their relative positions will be
                // honored when printing (they will be only centered, unless --dont-arrange
                // is supplied); if any object has no instances, it will get a default one
                // and all instances will be rearranged (unless --dont-arrange is supplied).
                std::string outfile = m_config.opt_string("output");
                Print       fff_print;
                SLAPrint    sla_print;

                sla_print.set_status_callback(
                            [](const PrintBase::SlicingStatus& s)
                {
                    if(s.percent >= 0) // FIXME: is this sufficient?
                        printf("%3d%s %s\n", s.percent, "% =>", s.text.c_str());
                });

                PrintBase  *print = (printer_technology == ptFFF) ? static_cast<PrintBase*>(&fff_print) : static_cast<PrintBase*>(&sla_print);
                if (! m_config.opt_bool("dont_arrange")) {
                    //FIXME make the min_object_distance configurable.
                    model.arrange_objects(fff_print.config().min_object_distance());
                    model.center_instances_around_point((! user_center_specified && m_print_config.has("bed_shape")) ? 
                    	BoundingBoxf(m_print_config.opt<ConfigOptionPoints>("bed_shape")->values).center() : 
                    	m_config.option<ConfigOptionPoint>("center")->value);
                }
                if (printer_technology == ptFFF) {
                    for (auto* mo : model.objects)
                        fff_print.auto_assign_extruders(mo);
                }
                print->apply(model, m_print_config);
                std::string err = print->validate();
                if (! err.empty()) {
                    boost::nowide::cerr << err << std::endl;
                    return 1;
                }
                if (print->empty())
                    boost::nowide::cout << "Nothing to print for " << outfile << " . Either the print is empty or no object is fully inside the print volume." << std::endl;
                else
                    try {
                        std::string outfile_final;
                        print->process();
                        if (printer_technology == ptFFF) {
                            // The outfile is processed by a PlaceholderParser.
                            outfile = fff_print.export_gcode(outfile, nullptr);
                            outfile_final = fff_print.print_statistics().finalize_output_path(outfile);
                        } else {
                            outfile = sla_print.output_filepath(outfile);
                            // We need to finalize the filename beforehand because the export function sets the filename inside the zip metadata
                            outfile_final = sla_print.print_statistics().finalize_output_path(outfile);
                            sla_print.export_raster(outfile_final);
                        }
                        if (outfile != outfile_final && Slic3r::rename_file(outfile, outfile_final)) {
                            boost::nowide::cerr << "Renaming file " << outfile << " to " << outfile_final << " failed" << std::endl;
                            return 1;
                        }
                        boost::nowide::cout << "Slicing result exported to " << outfile << std::endl;
                    } catch (const std::exception &ex) {
                        boost::nowide::cerr << ex.what() << std::endl;
                        return 1;
                    }
/*
                print.center = ! m_config.has("center")
                    && ! m_config.has("align_xy")
                    && ! m_config.opt_bool("dont_arrange");
                print.set_model(model);

                // start chronometer
                typedef std::chrono::high_resolution_clock clock_;
                typedef std::chrono::duration<double, std::ratio<1> > second_;
                std::chrono::time_point<clock_> t0{ clock_::now() };

                const std::string outfile = this->output_filepath(model, IO::Gcode);
                try {
                    print.export_gcode(outfile);
                } catch (std::runtime_error &e) {
                    boost::nowide::cerr << e.what() << std::endl;
                    return 1;
                }
                boost::nowide::cout << "G-code exported to " << outfile << std::endl;

                // output some statistics
                double duration { std::chrono::duration_cast<second_>(clock_::now() - t0).count() };
                boost::nowide::cout << std::fixed << std::setprecision(0)
                    << "Done. Process took " << (duration/60) << " minutes and "
                    << std::setprecision(3)
                    << std::fmod(duration, 60.0) << " seconds." << std::endl
                    << std::setprecision(2)
                    << "Filament required: " << print.total_used_filament() << "mm"
                    << " (" << print.total_extruded_volume()/1000 << "cm3)" << std::endl;
*/
            }
        } else {
            boost::nowide::cerr << "error: option not supported yet: " << opt_key << std::endl;
            return 1;
        }
    }

    if (start_gui) {
#ifdef SLIC3R_GUI
// #ifdef USE_WX
        GUI::GUI_App *gui = new GUI::GUI_App();
//		gui->autosave = m_config.opt_string("autosave");
        GUI::GUI_App::SetInstance(gui);
        gui->CallAfter([gui, this, &load_configs] {
            if (!gui->initialized()) {
                return;
            }
#if 0
            // Load the cummulative config over the currently active profiles.
            //FIXME if multiple configs are loaded, only the last one will have an effect.
            // We need to decide what to do about loading of separate presets (just print preset, just filament preset etc).
            // As of now only the full configs are supported here.
            if (!m_print_config.empty())
                gui->mainframe->load_config(m_print_config);
#endif
            if (! load_configs.empty())
                // Load the last config to give it a name at the UI. The name of the preset may be later
                // changed by loading an AMF or 3MF.
                //FIXME this is not strictly correct, as one may pass a print/filament/printer profile here instead of a full config.
                gui->mainframe->load_config_file(load_configs.back());
            // If loading a 3MF file, the config is loaded from the last one.
            if (! m_input_files.empty())
                gui->plater()->load_files(m_input_files, true, true);
            if (! m_extra_config.empty())
                gui->mainframe->load_config(m_extra_config);
        });
        int result = wxEntry(argc, argv);
#if !ENABLE_NON_STATIC_CANVAS_MANAGER
        //FIXME this is a workaround for the PrusaSlicer 2.1 release.
		_3DScene::destroy();
#endif // !ENABLE_NON_STATIC_CANVAS_MANAGER
        return result;
#else /* SLIC3R_GUI */
        // No GUI support. Just print out a help.
        this->print_help(false);
        // If started without a parameter, consider it to be OK, otherwise report an error code (no action etc).
        return (argc == 0) ? 0 : 1;
#endif /* SLIC3R_GUI */
    }

    return 0;
}

bool CLI::setup(int argc, char **argv)
{
    {
	    Slic3r::set_logging_level(1);
        const char *loglevel = boost::nowide::getenv("SLIC3R_LOGLEVEL");
        if (loglevel != nullptr) {
            if (loglevel[0] >= '0' && loglevel[0] <= '9' && loglevel[1] == 0)
                set_logging_level(loglevel[0] - '0');
            else
                boost::nowide::cerr << "Invalid SLIC3R_LOGLEVEL environment variable: " << loglevel << std::endl;
        }
    }

    boost::filesystem::path path_to_binary = boost::filesystem::system_complete(argv[0]);

    // Path from the Slic3r binary to its resources.
#ifdef __APPLE__
    // The application is packed in the .dmg archive as 'Slic3r.app/Contents/MacOS/Slic3r'
    // The resources are packed to 'Slic3r.app/Contents/Resources'
    boost::filesystem::path path_resources = path_to_binary.parent_path() / "../Resources";
#elif defined _WIN32
    // The application is packed in the .zip archive in the root,
    // The resources are packed to 'resources'
    // Path from Slic3r binary to resources:
    boost::filesystem::path path_resources = path_to_binary.parent_path() / "resources";
#elif defined SLIC3R_FHS
    // The application is packaged according to the Linux Filesystem Hierarchy Standard
    // Resources are set to the 'Architecture-independent (shared) data', typically /usr/share or /usr/local/share
    boost::filesystem::path path_resources = SLIC3R_FHS_RESOURCES;
#else
    // The application is packed in the .tar.bz archive (or in AppImage) as 'bin/slic3r',
    // The resources are packed to 'resources'
    // Path from Slic3r binary to resources:
    boost::filesystem::path path_resources = path_to_binary.parent_path() / "../resources";
#endif

    set_resources_dir(path_resources.string());
    set_var_dir((path_resources / "icons").string());
    set_local_dir((path_resources / "localization").string());

    // Parse all command line options into a DynamicConfig.
    // If any option is unsupported, print usage and abort immediately.
    t_config_option_keys opt_order;
    if (! m_config.read_cli(argc, argv, &m_input_files, &opt_order)) {
        // Separate error message reported by the CLI parser from the help.
        boost::nowide::cerr << std::endl;
        this->print_help();
        return false;
    }
    // Parse actions and transform options.
    for (auto const &opt_key : opt_order) {
        if (cli_actions_config_def.has(opt_key))
            m_actions.emplace_back(opt_key);
        else if (cli_transform_config_def.has(opt_key))
            m_transforms.emplace_back(opt_key);
    }

    {
        const ConfigOptionInt *opt_loglevel = m_config.opt<ConfigOptionInt>("loglevel");
        if (opt_loglevel != 0)
            set_logging_level(opt_loglevel->value);
    }

    // Initialize with defaults.
    for (const t_optiondef_map *options : { &cli_actions_config_def.options, &cli_transform_config_def.options, &cli_misc_config_def.options })
        for (const std::pair<t_config_option_key, ConfigOptionDef> &optdef : *options)
            m_config.option(optdef.first, true);

    set_data_dir(m_config.opt_string("datadir"));

    return true;
}

void CLI::print_help(bool include_print_options, PrinterTechnology printer_technology) const
{
    boost::nowide::cout
        << SLIC3R_BUILD_ID << " " << "based on Slic3r"
#ifdef SLIC3R_GUI
        << " (with GUI support)"
#else /* SLIC3R_GUI */
        << " (without GUI support)"
#endif /* SLIC3R_GUI */
        << std::endl
        << "https://github.com/prusa3d/PrusaSlicer" << std::endl << std::endl
        << "Usage: prusa-slicer [ ACTIONS ] [ TRANSFORM ] [ OPTIONS ] [ file.stl ... ]" << std::endl
        << std::endl
        << "Actions:" << std::endl;
    cli_actions_config_def.print_cli_help(boost::nowide::cout, false);

    boost::nowide::cout
        << std::endl
        << "Transform options:" << std::endl;
        cli_transform_config_def.print_cli_help(boost::nowide::cout, false);

    boost::nowide::cout
        << std::endl
        << "Other options:" << std::endl;
        cli_misc_config_def.print_cli_help(boost::nowide::cout, false);

    boost::nowide::cout
        << std::endl
        << "Print options are processed in the following order:" << std::endl
        << "\t1) Config keys from the command line, for example --fill-pattern=stars" << std::endl
        << "\t   (highest priority, overwrites everything below)" << std::endl
        << "\t2) Config files loaded with --load" << std::endl
	    << "\t3) Config values loaded from amf or 3mf files" << std::endl;

    if (include_print_options) {
        boost::nowide::cout << std::endl;
        print_config_def.print_cli_help(boost::nowide::cout, true, [printer_technology](const ConfigOptionDef &def)
            { return printer_technology == ptAny || def.printer_technology == ptAny || printer_technology == def.printer_technology; });
    } else {
        boost::nowide::cout
            << std::endl
            << "Run --help-fff / --help-sla to see the full listing of print options." << std::endl;
    }
}

bool CLI::export_models(IO::ExportFormat format)
{
    for (Model &model : m_models) {
        const std::string path = this->output_filepath(model, format);
        bool success = false;
        switch (format) {
            case IO::AMF: success = Slic3r::store_amf(path.c_str(), &model, nullptr, false); break;
            case IO::OBJ: success = Slic3r::store_obj(path.c_str(), &model);          break;
            case IO::STL: success = Slic3r::store_stl(path.c_str(), &model, true);    break;
            case IO::TMF: success = Slic3r::store_3mf(path.c_str(), &model, nullptr, false); break;
            default: assert(false); break;
        }
        if (success)
            std::cout << "File exported to " << path << std::endl;
        else {
            std::cerr << "File export to " << path << " failed" << std::endl;
            return false;
        }
    }
    return true;
}

std::string CLI::output_filepath(const Model &model, IO::ExportFormat format) const
{
    std::string ext;
    switch (format) {
        case IO::AMF: ext = ".zip.amf"; break;
        case IO::OBJ: ext = ".obj"; break;
        case IO::STL: ext = ".stl"; break;
        case IO::TMF: ext = ".3mf"; break;
        default: assert(false); break;
    };
    auto proposed_path = boost::filesystem::path(model.propose_export_file_name_and_path(ext));
    // use --output when available
    std::string cmdline_param = m_config.opt_string("output");
    if (! cmdline_param.empty()) {
        // if we were supplied a directory, use it and append our automatically generated filename
        boost::filesystem::path cmdline_path(cmdline_param);
        if (boost::filesystem::is_directory(cmdline_path))
            proposed_path = cmdline_path / proposed_path.filename();
        else
            proposed_path = cmdline_path;
    }
    return proposed_path.string();
}

#if defined(_MSC_VER) || defined(__MINGW32__)
extern "C" {
    __declspec(dllexport) int __stdcall slic3r_main(int argc, wchar_t **argv)
    {
        // Convert wchar_t arguments to UTF8.
        std::vector<std::string> 	argv_narrow;
        std::vector<char*>			argv_ptrs(argc + 1, nullptr);
        for (size_t i = 0; i < argc; ++ i)
            argv_narrow.emplace_back(boost::nowide::narrow(argv[i]));
        for (size_t i = 0; i < argc; ++ i)
            argv_ptrs[i] = argv_narrow[i].data();
        // Call the UTF8 main.
        return CLI().run(argc, argv_ptrs.data());
    }
}
#else /* _MSC_VER */
int main(int argc, char **argv)
{
    return CLI().run(argc, argv);
}
#endif /* _MSC_VER */
