_Q: Oh cool, a new RepRap slicer?_

A: Yes.

Slic3r
======
Prebuilt Windows, OSX and Linux binaries are available through the [git releases page](https://github.com/prusa3d/Slic3r/releases).

<img width=256 src=https://cloud.githubusercontent.com/assets/31754/22719818/09998c92-ed6d-11e6-9fa0-09de638f3a36.png />

Slic3r takes 3D models (STL, OBJ, AMF) and converts them into G-code instructions for 
3D printers. It's compatible with any modern printer based on the RepRap toolchain,
including all those based on the Marlin, Sprinter and Repetier firmware. It also works
with Mach3, LinuxCNC and Machinekit controllers.

See the [project homepage](http://slic3r.org/) at slic3r.org and the
[manual](http://manual.slic3r.org/) for more information.

### What language is it written in?

The core geometric algorithms and data structures are written in C++,
and Perl is used for high-level flow abstraction, GUI and testing.
If you're wondering why Perl, see https://xkcd.com/224/

The C++ API is public and its use in other projects is encouraged.
The goal is to make Slic3r fully modular so that any part of its logic
can be used separately.

### What are Slic3r's main features?

Key features are:

* **multi-platform** (Linux/Mac/Win) and packaged as standalone-app with no dependencies required
* complete **command-line interface** to use it with no GUI
* multi-material **(multiple extruders)** object printing
* multiple G-code flavors supported (RepRap, Makerbot, Mach3, Machinekit etc.)
* ability to plate **multiple objects having distinct print settings**
* **multithread** processing
* **STL auto-repair** (tolerance for broken models)
* wide automated unit testing

Other major features are:

* combine infill every 'n' perimeters layer to speed up printing
* **3D preview** (including multi-material files)
* **multiple layer heights** in a single print
* **spiral vase** mode for bumpless vases
* fine-grained configuration of speed, acceleration, extrusion width
* several infill patterns including honeycomb, spirals, Hilbert curves
* support material, raft, brim, skirt
* **standby temperature** and automatic wiping for multi-extruder printing
* customizable **G-code macros** and output filename with variable placeholders
* support for **post-processing scripts**
* **cooling logic** controlling fan speed and dynamic print speed

### How to install?

You can download a precompiled package from [slic3r.org](http://slic3r.org/);
it will run without the need for any dependency.

If you want to compile the source yourself follow the instructions on one of these wiki pages: 
* [Linux](https://github.com/alexrj/Slic3r/wiki/Running-Slic3r-from-git-on-GNU-Linux)
* [Windows](https://github.com/prusa3d/Slic3r/wiki/How-to-compile-Slic3r-Prusa-Edition-on-MS-Windows)
* [Mac OSX](https://github.com/alexrj/Slic3r/wiki/Running-Slic3r-from-git-on-OS-X)

### Can I help?

Sure! You can do the following to find things that are available to help with:
* [Pull Request Milestone](https://github.com/alexrj/Slic3r/milestone/31)
    * Please comment in the related github issue that you are working on it so that other people know. 
* Items in the [TODO](https://github.com/alexrj/Slic3r/wiki/TODO) wiki page.
    * Please comment in the related github issue that you are working on it so that other people know. 
* Drop me a line at aar@cpan.org.
* You can also find me (rarely) in #reprap and in #slic3r on [FreeNode](https://webchat.freenode.net) with the nickname _Sound_. Another contributor, _LoH_, is also in both channels.
* Add an [issue](https://github.com/alexrj/Slic3r/issues) to the github tracker if it isn't already present.

Before sending patches and pull requests contact me (preferably through opening a github issue or commenting on an existing, related, issue) to discuss your proposed
changes: this way we'll ensure nobody wastes their time and no conflicts arise
in development.

### What's Slic3r license?

Slic3r is licensed under the _GNU Affero General Public License, version 3_.
The author is Alessandro Ranellucci.

The [Silk icon set](http://www.famfamfam.com/lab/icons/silk/) used in Slic3r is
licensed under the _Creative Commons Attribution 3.0 License_.
The author of the Silk icon set is Mark James.

### How can I invoke slic3r.pl using the command line?

    Usage: slic3r.pl [ OPTIONS ] [ file.stl ] [ file2.stl ] ...
    
        --help              Output this usage screen and exit
        --version           Output the version of Slic3r and exit
        --save <file>       Save configuration to the specified file
        --load <file>       Load configuration from the specified file. It can be used
                            more than once to load options from multiple files.
        -o, --output <file> File to output gcode to (by default, the file will be saved
                            into the same directory as the input file using the
                            --output-filename-format to generate the filename.) If a
                            directory is specified for this option, the output will
                            be saved under that directory, and the filename will be
                            generated by --output-filename-format.

      Non-slicing actions (no G-code will be generated):
        --repair            Repair given STL files and save them as <name>_fixed.obj
        --cut <z>           Cut given input files at given Z (relative) and export
                            them as <name>_upper.stl and <name>_lower.stl
        --split             Split the shells contained in given STL file into several STL files
        --info              Output information about the supplied file(s) and exit
    
        -j, --threads <num> Number of threads to use (1+, default: 2)
    
      GUI options:
        --gui               Forces the GUI launch instead of command line slicing (if you
                            supply a model file, it will be loaded into the plater)
        --no-plater         Disable the plater tab
        --no-gui            Forces the command line slicing instead of gui. 
                            This takes precedence over --gui if both are present.
        --autosave <file>   Automatically export current configuration to the specified file
    
      Output options:
        --output-filename-format
                            Output file name format; all config options enclosed in brackets
                            will be replaced by their values, as well as [input_filename_base]
                            and [input_filename] (default: [input_filename_base].gcode)
        --post-process      Generated G-code will be processed with the supplied script;
                            call this more than once to process through multiple scripts.
        --export-svg        Export a SVG file containing slices instead of G-code.
        --export-png        Export zipped PNG files containing slices instead of G-code.
        -m, --merge         If multiple files are supplied, they will be composed into a single
                            print rather than processed individually.
    
      Printer options:
        --nozzle-diameter   Diameter of nozzle in mm (default: 0.5)
        --print-center      Coordinates in mm of the point to center the print around
                            (default: 100,100)
        --z-offset          Additional height in mm to add to vertical coordinates
                            (+/-, default: 0)
        --gcode-flavor      The type of G-code to generate (reprap/teacup/repetier/makerware/sailfish/mach3/machinekit/smoothie/no-extrusion,
                            default: reprap)
        --use-relative-e-distances Enable this to get relative E values (default: no)
        --use-firmware-retraction  Enable firmware-controlled retraction using G10/G11 (default: no)
        --use-volumetric-e  Express E in cubic millimeters and prepend M200 (default: no)
        --gcode-comments    Make G-code verbose by adding comments (default: no)
    
      Filament options:
        --filament-diameter Diameter in mm of your raw filament (default: 3)
        --extrusion-multiplier
                            Change this to alter the amount of plastic extruded. There should be
                            very little need to change this value, which is only useful to
                            compensate for filament packing (default: 1)
        --temperature       Extrusion temperature in degree Celsius, set 0 to disable (default: 200)
        --first-layer-temperature Extrusion temperature for the first layer, in degree Celsius,
                            set 0 to disable (default: same as --temperature)
        --bed-temperature   Heated bed temperature in degree Celsius, set 0 to disable (default: 0)
        --first-layer-bed-temperature Heated bed temperature for the first layer, in degree Celsius,
                            set 0 to disable (default: same as --bed-temperature)
    
      Speed options:
        --travel-speed      Speed of non-print moves in mm/s (default: 130)
        --perimeter-speed   Speed of print moves for perimeters in mm/s (default: 30)
        --small-perimeter-speed
                            Speed of print moves for small perimeters in mm/s or % over perimeter speed
                            (default: 30)
        --external-perimeter-speed
                            Speed of print moves for the external perimeter in mm/s or % over perimeter speed
                            (default: 70%)
        --infill-speed      Speed of print moves in mm/s (default: 60)
        --solid-infill-speed Speed of print moves for solid surfaces in mm/s or % over infill speed
                            (default: 60)
        --top-solid-infill-speed Speed of print moves for top surfaces in mm/s or % over solid infill speed
                            (default: 50)
        --support-material-speed
                            Speed of support material print moves in mm/s (default: 60)
        --support-material-interface-speed
                            Speed of support material interface print moves in mm/s or % over support material
                            speed (default: 100%)
        --bridge-speed      Speed of bridge print moves in mm/s (default: 60)
        --gap-fill-speed    Speed of gap fill print moves in mm/s (default: 20)
        --first-layer-speed Speed of print moves for bottom layer, expressed either as an absolute
                            value or as a percentage over normal speeds (default: 30%)
    
      Acceleration options:
        --perimeter-acceleration
                            Overrides firmware's default acceleration for perimeters. (mm/s^2, set zero
                            to disable; default: 0)
        --infill-acceleration
                            Overrides firmware's default acceleration for infill. (mm/s^2, set zero
                            to disable; default: 0)
        --bridge-acceleration
                            Overrides firmware's default acceleration for bridges. (mm/s^2, set zero
                            to disable; default: 0)
        --first-layer-acceleration
                            Overrides firmware's default acceleration for first layer. (mm/s^2, set zero
                            to disable; default: 0)
        --default-acceleration
                            Acceleration will be reset to this value after the specific settings above
                            have been applied. (mm/s^2, set zero to disable; default: 0)
    
      Accuracy options:
        --layer-height      Layer height in mm (default: 0.3)
        --first-layer-height Layer height for first layer (mm or %, default: 0.35)
        --infill-every-layers
                            Infill every N layers (default: 1)
        --solid-infill-every-layers
                            Force a solid layer every N layers (default: 0)
    
      Print options:
        --perimeters        Number of perimeters/horizontal skins (range: 0+, default: 3)
        --top-solid-layers  Number of solid layers to do for top surfaces (range: 0+, default: 3)
        --bottom-solid-layers  Number of solid layers to do for bottom surfaces (range: 0+, default: 3)
        --solid-layers      Shortcut for setting the two options above at once
        --fill-density      Infill density (range: 0%-100%, default: 40%)
        --fill-angle        Infill angle in degrees (range: 0-90, default: 45)
        --fill-pattern      Pattern to use to fill non-solid layers (default: honeycomb)
        --solid-fill-pattern Pattern to use to fill solid layers (default: rectilinear)
        --start-gcode       Load initial G-code from the supplied file. This will overwrite
                            the default command (home all axes [G28]).
        --end-gcode         Load final G-code from the supplied file. This will overwrite
                            the default commands (turn off temperature [M104 S0],
                            home X axis [G28 X], disable motors [M84]).
        --before-layer-gcode  Load before-layer-change G-code from the supplied file (default: nothing).
        --layer-gcode       Load after-layer-change G-code from the supplied file (default: nothing).
        --toolchange-gcode  Load tool-change G-code from the supplied file (default: nothing).
        --seam-position     Position of loop starting points (random/nearest/aligned, default: aligned).
        --external-perimeters-first Reverse perimeter order. (default: no)
        --spiral-vase       Experimental option to raise Z gradually when printing single-walled vases
                            (default: no)
        --only-retract-when-crossing-perimeters
                            Disable retraction when travelling between infill paths inside the same island.
                            (default: no)
        --solid-infill-below-area
                            Force solid infill when a region has a smaller area than this threshold
                            (mm^2, default: 70)
        --infill-only-where-needed
                            Only infill under ceilings (default: no)
        --infill-first      Make infill before perimeters (default: no)
    
       Quality options (slower slicing):
        --extra-perimeters  Add more perimeters when needed (default: yes)
        --avoid-crossing-perimeters Optimize travel moves so that no perimeters are crossed (default: no)
        --thin-walls        Detect single-width walls (default: yes)
        --overhangs         Experimental option to use bridge flow, speed and fan for overhangs
                            (default: yes)
    
       Support material options:
        --support-material  Generate support material for overhangs
        --support-material-threshold
                            Overhang threshold angle (range: 0-90, set 0 for automatic detection,
                            default: 0)
        --support-material-pattern
                            Pattern to use for support material (default: honeycomb)
        --support-material-spacing
                            Spacing between pattern lines (mm, default: 2.5)
        --support-material-angle
                            Support material angle in degrees (range: 0-90, default: 0)
        --support-material-contact-distance
                            Vertical distance between object and support material
                            (0+, default: 0.2)
        --support-material-interface-layers
                            Number of perpendicular layers between support material and object (0+, default: 3)
        --support-material-interface-spacing
                            Spacing between interface pattern lines (mm, set 0 to get a solid layer, default: 0)
        --raft-layers       Number of layers to raise the printed objects by (range: 0+, default: 0)
        --support-material-enforce-layers
                            Enforce support material on the specified number of layers from bottom,
                            regardless of --support-material and threshold (0+, default: 0)
        --dont-support-bridges
                            Experimental option for preventing support material from being generated under bridged areas (default: yes)
    
       Retraction options:
        --retract-length    Length of retraction in mm when pausing extrusion (default: 1)
        --retract-speed     Speed for retraction in mm/s (default: 30)
        --retract-restart-extra
                            Additional amount of filament in mm to push after
                            compensating retraction (default: 0)
        --retract-before-travel
                            Only retract before travel moves of this length in mm (default: 2)
        --retract-lift      Lift Z by the given distance in mm when retracting (default: 0)
        --retract-lift-above Only lift Z when above the specified height (default: 0)
        --retract-lift-below Only lift Z when below the specified height (default: 0)
        --retract-layer-change
                            Enforce a retraction before each Z move (default: no)
        --wipe              Wipe the nozzle while doing a retraction (default: no)
    
       Retraction options for multi-extruder setups:
        --retract-length-toolchange
                            Length of retraction in mm when disabling tool (default: 10)
        --retract-restart-extra-toolchange
                            Additional amount of filament in mm to push after
                            switching tool (default: 0)
    
       Cooling options:
        --cooling           Enable fan and cooling control
        --min-fan-speed     Minimum fan speed (default: 35%)
        --max-fan-speed     Maximum fan speed (default: 100%)
        --bridge-fan-speed  Fan speed to use when bridging (default: 100%)
        --fan-below-layer-time Enable fan if layer print time is below this approximate number
                            of seconds (default: 60)
        --slowdown-below-layer-time Slow down if layer print time is below this approximate number
                            of seconds (default: 30)
        --min-print-speed   Minimum print speed (mm/s, default: 10)
        --disable-fan-first-layers Disable fan for the first N layers (default: 1)
        --fan-always-on     Keep fan always on at min fan speed, even for layers that don't need
                            cooling
    
       Skirt options:
        --skirts            Number of skirts to draw (0+, default: 1)
        --skirt-distance    Distance in mm between innermost skirt and object
                            (default: 6)
        --skirt-height      Height of skirts to draw (expressed in layers, 0+, default: 1)
        --min-skirt-length  Generate no less than the number of loops required to consume this length
                            of filament on the first layer, for each extruder (mm, 0+, default: 0)
        --brim-width        Width of the brim that will get added to each object to help adhesion
                            (mm, default: 0)
    
       Transform options:
        --scale             Factor for scaling input object (default: 1)
        --rotate            Rotation angle in degrees (0-360, default: 0)
        --duplicate         Number of items with auto-arrange (1+, default: 1)
        --duplicate-grid    Number of items with grid arrangement (default: 1,1)
        --duplicate-distance Distance in mm between copies (default: 6)
        --dont-arrange      Don't arrange the objects on the build plate. The model coordinates
                            define the absolute positions on the build plate. 
                            The option --print-center will be ignored.
        --xy-size-compensation
                            Grow/shrink objects by the configured absolute distance (mm, default: 0)
    
       Sequential printing options:
        --complete-objects  When printing multiple objects and/or copies, complete each one before
                            starting the next one; watch out for extruder collisions (default: no)
        --extruder-clearance-radius Radius in mm above which extruder won't collide with anything
                            (default: 20)
        --extruder-clearance-height Maximum vertical extruder depth; i.e. vertical distance from
                            extruder tip and carriage bottom (default: 20)
    
       Miscellaneous options:
        --notes             Notes to be added as comments to the output file
        --resolution        Minimum detail resolution (mm, set zero for full resolution, default: 0)
    
       Flow options (advanced):
        --extrusion-width   Set extrusion width manually; it accepts either an absolute value in mm
                            (like 0.65) or a percentage over layer height (like 200%)
        --first-layer-extrusion-width
                            Set a different extrusion width for first layer
        --perimeter-extrusion-width
                            Set a different extrusion width for perimeters
        --external-perimeter-extrusion-width
                            Set a different extrusion width for external perimeters
        --infill-extrusion-width
                            Set a different extrusion width for infill
        --solid-infill-extrusion-width
                            Set a different extrusion width for solid infill
        --top-infill-extrusion-width
                            Set a different extrusion width for top infill
        --support-material-extrusion-width
                            Set a different extrusion width for support material
        --infill-overlap    Overlap between infill and perimeters (default: 15%)
        --bridge-flow-ratio Multiplier for extrusion when bridging (> 0, default: 1)
    
       Multiple extruder options:
        --extruder-offset   Offset of each extruder, if firmware doesn't handle the displacement
                            (can be specified multiple times, default: 0x0)
        --perimeter-extruder
                            Extruder to use for perimeters and brim (1+, default: 1)
        --infill-extruder   Extruder to use for infill (1+, default: 1)
        --solid-infill-extruder   Extruder to use for solid infill (1+, default: 1)
        --support-material-extruder
                            Extruder to use for support material, raft and skirt (1+, default: 1)
        --support-material-interface-extruder
                            Extruder to use for support material interface (1+, default: 1)
                            --ooze-prevention   Drop temperature and park extruders outside a full skirt for automatic wiping
                            (default: no)
        --ooze-prevention   Drop temperature and park extruders outside a full skirt for automatic wiping
                            (default: no)
        --standby-temperature-delta
                            Temperature difference to be applied when an extruder is not active and
                            --ooze-prevention is enabled (default: -5)


If you want to change a preset file, just do

    slic3r.pl --load config.ini --layer-height 0.25 --save config.ini

If you want to slice a file overriding an option contained in your preset file:

    slic3r.pl --load config.ini --layer-height 0.25 file.stl
