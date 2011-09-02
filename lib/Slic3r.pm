package Slic3r;

use strict;
use warnings;

use Slic3r::Layer;
use Slic3r::Line;
use Slic3r::Perimeter;
use Slic3r::Point;
use Slic3r::Polyline;
use Slic3r::Polyline::Closed;
use Slic3r::Print;
use Slic3r::STL;
use Slic3r::Surface;

our $layer_height       = 0.4;
our $resolution         = 0.1;
our $perimeter_offsets  = 3;
our $flow_width         = 0.4;  # make sure this is > $resolution

1;
