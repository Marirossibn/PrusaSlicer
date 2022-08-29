#ifndef _prusaslicer_technologies_h_
#define _prusaslicer_technologies_h_

//=============
// debug techs
//=============
// Shows camera target in the 3D scene
#define ENABLE_SHOW_CAMERA_TARGET 0
// Log debug messages to console when changing selection
#define ENABLE_SELECTION_DEBUG_OUTPUT 0
// Renders a small sphere in the center of the bounding box of the current selection when no gizmo is active
#define ENABLE_RENDER_SELECTION_CENTER 0
// Shows an imgui dialog with camera related data
#define ENABLE_CAMERA_STATISTICS 0
// Enable extracting thumbnails from selected gcode and save them as png files
#define ENABLE_THUMBNAIL_GENERATOR_DEBUG 0
// Disable synchronization of unselected instances
#define DISABLE_INSTANCES_SYNCH 0
// Use wxDataViewRender instead of wxDataViewCustomRenderer
#define ENABLE_NONCUSTOM_DATA_VIEW_RENDERING 0
// Enable G-Code viewer statistics imgui dialog
#define ENABLE_GCODE_VIEWER_STATISTICS 0
// Enable G-Code viewer comparison between toolpaths height and width detected from gcode and calculated at gcode generation 
#define ENABLE_GCODE_VIEWER_DATA_CHECKING 0
// Enable project dirty state manager debug window
#define ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW 0
// Disable using instanced models to render options in gcode preview
#define DISABLE_GCODEVIEWER_INSTANCED_MODELS 1


// Enable rendering of objects using environment map
#define ENABLE_ENVIRONMENT_MAP 0
// Enable smoothing of objects normals
#define ENABLE_SMOOTH_NORMALS 0


//====================
// 2.5.0.alpha1 techs
//====================
#define ENABLE_2_5_0_ALPHA1 1

// Enable changes in preview layout
#define ENABLE_PREVIEW_LAYOUT (1 && ENABLE_2_5_0_ALPHA1)
// Enable drawing the items in legend toolbar using icons
#define ENABLE_LEGEND_TOOLBAR_ICONS (1 && ENABLE_PREVIEW_LAYOUT)
// Enable coloring of toolpaths in preview by layer time
#define ENABLE_PREVIEW_LAYER_TIME (1 && ENABLE_2_5_0_ALPHA1)
// Enable showing time estimate for travel moves in legend
#define ENABLE_TRAVEL_TIME (1 && ENABLE_2_5_0_ALPHA1)
// Enable not killing focus in object manipulator fields when hovering over 3D scene
#define ENABLE_OBJECT_MANIPULATOR_FOCUS (0 && ENABLE_2_5_0_ALPHA1)
// Enable removal of wipe tower magic object_id equal to 1000
#define ENABLE_WIPETOWER_OBJECTID_1000_REMOVAL (1 && ENABLE_2_5_0_ALPHA1)
// Enable removal of legacy OpenGL calls
#define ENABLE_LEGACY_OPENGL_REMOVAL (1 && ENABLE_2_5_0_ALPHA1)
// Enable OpenGL ES
#define ENABLE_OPENGL_ES (0 && ENABLE_LEGACY_OPENGL_REMOVAL)
// Enable OpenGL core profile context (tested against Mesa 20.1.8 on Windows)
#define ENABLE_GL_CORE_PROFILE (1 && ENABLE_LEGACY_OPENGL_REMOVAL && !ENABLE_OPENGL_ES)
// Enable OpenGL debug messages using debug context
#define ENABLE_OPENGL_DEBUG_OPTION (1 && ENABLE_GL_CORE_PROFILE)
// Shows an imgui dialog with GLModel statistics data
#define ENABLE_GLMODEL_STATISTICS (0 && ENABLE_LEGACY_OPENGL_REMOVAL)
// Enable rework of Reload from disk command
#define ENABLE_RELOAD_FROM_DISK_REWORK (1 && ENABLE_2_5_0_ALPHA1)
// Enable recalculating toolpaths when switching to/from volumetric rate visualization
#define ENABLE_VOLUMETRIC_RATE_TOOLPATHS_RECALC (1 && ENABLE_2_5_0_ALPHA1)
// Enable editing volumes transformation in world coordinates and instances in local coordinates
#define ENABLE_WORLD_COORDINATE (1 && ENABLE_2_5_0_ALPHA1)
// Enable modified camera control using mouse
#define ENABLE_NEW_CAMERA_MOVEMENTS (1 && ENABLE_2_5_0_ALPHA1)
// Enable alternative version of file_wildcards()
#define ENABLE_ALTERNATIVE_FILE_WILDCARDS_GENERATOR (1 && ENABLE_2_5_0_ALPHA1)
// Enable processing of gcode G2 and G3 lines
#define ENABLE_PROCESS_G2_G3_LINES (1 && ENABLE_2_5_0_ALPHA1)
// Enable fix of used filament data exported to gcode file
#define ENABLE_USED_FILAMENT_POST_PROCESS (1 && ENABLE_2_5_0_ALPHA1)
// Enable picking using raytracing
#define ENABLE_RAYCAST_PICKING (1 && ENABLE_LEGACY_OPENGL_REMOVAL)
#define ENABLE_RAYCAST_PICKING_DEBUG (0 && ENABLE_RAYCAST_PICKING)
// Enable Measure Gizmo
#define ENABLE_MEASURE_GIZMO (1 && ENABLE_RAYCAST_PICKING)
// Enable debug code for Measure Gizmo
#define ENABLE_MEASURE_GIZMO_DEBUG (0 && ENABLE_MEASURE_GIZMO)


#endif // _prusaslicer_technologies_h_
