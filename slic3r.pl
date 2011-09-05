#!/usr/bin/perl

use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/lib";
}

use Getopt::Long;
use Slic3r;
use XXX;

my %opt;
GetOptions(
    'help'                  => sub { usage() },

    'debug'                 => \$Slic3r::debug,
    'o|output'              => \$opt{output},
    
    'layer-height=f'        => \$Slic3r::layer_height,
    'resolution=f'          => \$Slic3r::resolution,
    'perimeters=i'          => \$Slic3r::perimeter_offsets,
    'fill-density=f'        => \$Slic3r::fill_density,
    'flow-width=f'          => \$Slic3r::flow_width,
    'temperature=i'         => \$Slic3r::temperature,
    'flow-rate=i'           => \$Slic3r::flow_rate,
    'print-feed-rate=i'     => \$Slic3r::print_feed_rate,
    'travel-feed-rate=i'    => \$Slic3r::travel_feed_rate,
    'bottom-layer-speed-ratio=f'    => \$Slic3r::bottom_layer_speed_ratio,
    'use-relative-e-distances'      => \$Slic3r::use_relative_e_distances,
    'print-center=s'        => \$Slic3r::print_center,
    '',
);

# validate configuration
{
    # --layer-height
    die "Invalid value for --layer-height\n"
        if $Slic3r::layer_height < 0;
    die "--layer-height must be a multiple of print resolution\n"
        if $Slic3r::layer_height / $Slic3r::resolution % 1 != 0;
    
    # --flow-width
    die "Invalid value for --flow-width\n"
        if $Slic3r::flow_width < 0;
    die "--flow-width must be a multiple of print resolution\n"
        if $Slic3r::flow_width / $Slic3r::resolution % 1 != 0;
    
    # --perimeters
    die "Invalid value for --perimeters\n"
        if $Slic3r::perimeter_offsets < 1;
    
    # --print-center
    die "Invalid value for --print-center\n"
        if !ref $Slic3r::print_center 
            && (!$Slic3r::print_center || $Slic3r::print_center !~ /^\d+,\d+$/);
    $Slic3r::print_center = [ split /,/, $Slic3r::print_center ]
        if !ref $Slic3r::print_center;
    
    # --fill-density
    die "Invalid value for --fill-density\n"
        if $Slic3r::fill_density < 0 || $Slic3r::fill_density > 1;
}

my $stl_parser = Slic3r::STL->new;
my $action = 'skein';

if ($action eq 'skein') {
    my $input_file = $ARGV[0] or usage(1);
    die "Input file must have .stl extension\n" 
        if $input_file !~ /\.stl$/i;
    
    my $print = $stl_parser->parse_file($input_file);
    $print->extrude_perimeters;
    $print->extrude_fills;
    
    my $output_file = $input_file;
    $output_file =~ s/\.stl$/.gcode/i;
    $print->export_gcode($opt{output} || $output_file);
}

sub usage {
    my ($exit_code) = @_;
    
    print <<"EOF";
Usage: slic3r.pl [ OPTIONS ] file.stl

    --help              Output this usage screen and exit
    --layer-height      Layer height in mm (default: $Slic3r::layer_height)
    --resolution        Print resolution in mm (default: $Slic3r::resolution)
    --perimeters        Number of perimeters/horizontal skins 
                        (range: 1+, default: $Slic3r::perimeter_offsets)
    --fill-density      Infill density (range: 0-1, default: $Slic3r::fill_density)
    --flow-width        Width of extruded flow in mm (default: $Slic3r::flow_width)
    --flow-rate         Speed of extrusion in mm/sec; should be equal to 
                        --print-feed-rate (default: $Slic3r::flow_rate)
    --print-feed-rate   Speed of print moves in mm/sec (default: $Slic3r::print_feed_rate)
    --travel-feed-rate  Speed of non-print moves in mm/sec (default: $Slic3r::travel_feed_rate)
    --bottom-layer-speed-ratio
                        Factor to increase/decrease speeds on bottom layer by 
                        (default: $Slic3r::bottom_layer_speed_ratio)
    --use-relative-e-distances
                        Use relative distances for extrusion in GCODE output
    --print-center      Coordinates of the point to center the print around 
                        (default: 100,100)
    --temperature       Extrusion temperature (default: $Slic3r::temperature)
    -o, --output        File to output gcode to (default: <inputfile>.gcode)
    
EOF
    exit $exit_code || 0;
}

__END__
