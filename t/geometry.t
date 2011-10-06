use Test::More;
use strict;
use warnings;

plan tests => 4;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;

#==========================================================

my $line1 = [ [73.6310778185108/0.0000001, 371.74239268924/0.0000001], [73.6310778185108/0.0000001, 501.74239268924/0.0000001] ];
my $line2 = [ [75/0.0000001, 437.9853/0.0000001], [62.7484/0.0000001, 440.4223/0.0000001] ];
isnt Slic3r::Geometry::line_intersection($line1, $line2, 1), undef, 'line_intersection';

#==========================================================

my $polyline = [
    [459190000, 5152739000], [147261000, 4612464000], [147261000, 3487535000], [339887000, 3153898000], 
    [437497000, 3438430000], [454223000, 3522515000], [523621000, 3626378000], [627484000, 3695776000], 
    [750000000, 3720147000], [872515000, 3695776000], [976378000, 3626378000], [1045776000, 3522515000], 
    [1070147000, 3400000000], [1045776000, 3277484000], [976378000, 3173621000], [872515000, 3104223000], 
    [827892000, 3095347000], [698461000, 2947261000], [2540810000, 2947261000], [2852739000, 3487535000], 
    [2852739000, 4612464000], [2540810000, 5152739000],
];

# this points belongs to $polyline
my $point = [2797980957.103410,3392691792.513960];

is_deeply Slic3r::Geometry::polygon_segment_having_point($polyline, $point), 
    [ [2540810000, 2947261000], [2852739000, 3487535000] ],
    'polygon_segment_having_point';

#==========================================================

$point = [ 736310778.185108, 5017423926.8924 ];
my $line = [ [627484000, 3695776000], [750000000, 3720147000] ];
is Slic3r::Geometry::point_in_segment($point, $line), 0, 'point_in_segment';

#==========================================================

my $polygons = [
    [ # contour, ccw
        [459190000, 5152739000], [147261000, 4612464000], [147261000, 3487535000], [339887000, 3153898000], 
        [437497000, 3438430000], [454223000, 3522515000], [523621000, 3626378000], [627484000, 3695776000], 
        [750000000, 3720147000], [872515000, 3695776000], [976378000, 3626378000], [1045776000, 3522515000], 
        [1070147000, 3400000000], [1045776000, 3277484000], [976378000, 3173621000], [872515000, 3104223000], 
        [827892000, 3095347000], [698461000, 2947261000], [2540810000, 2947261000], [2852739000, 3487535000], 
        [2852739000, 4612464000], [2540810000, 5152739000],

    ],
    [ # hole, cw
        [750000000, 5020147000], [872515000, 4995776000], [976378000, 4926378000], [1045776000, 4822515000], 
        [1070147000, 4700000000], [1045776000, 4577484000], [976378000, 4473621000], [872515000, 4404223000], 
        [750000000, 4379853000], [627484000, 4404223000], [523621000, 4473621000], [454223000, 4577484000], 
        [429853000, 4700000000], [454223000, 4822515000], [523621000, 4926378000], [627484000, 4995776000],
    ],
];

my $points = [
    [ 736310778.185108, 3717423926.892399788 ],
    [ 736310778.185108, 5017423926.8924 ],
];

is Slic3r::Geometry::can_connect_points(@$points, $polygons), 0, 'can_connect_points';

#==========================================================
