package Slic3r::Polygon;
use strict;
use warnings;

# a polygon is a closed polyline.
use parent 'Slic3r::Polyline';

use Slic3r::Geometry qw(polygon_lines polygon_remove_parallel_continuous_edges
    polygon_remove_acute_vertices polygon_segment_having_point point_in_polygon
    PI X1 X2 Y1 Y2 epsilon);
use Slic3r::Geometry::Clipper qw(JT_MITER);

sub lines {
    my $self = shift;
    return polygon_lines($self);
}

sub wkt {
    my $self = shift;
    return sprintf "POLYGON((%s))", join ',', map "$_->[0] $_->[1]", @$self;
}

sub is_counter_clockwise {
    my $self = shift;
    return Slic3r::Geometry::Clipper::is_counter_clockwise($self->arrayref_pp);
}

sub make_counter_clockwise {
    my $self = shift;
    if (!$self->is_counter_clockwise) {
        $self->reverse;
        return 1;
    }
    return 0;
}

sub make_clockwise {
    my $self = shift;
    if ($self->is_counter_clockwise) {
        $self->reverse;
        return 1;
    }
    return 0;
}

sub merge_continuous_lines {
    my $self = shift;
    
    polygon_remove_parallel_continuous_edges($self);
}

sub remove_acute_vertices {
    my $self = shift;
    polygon_remove_acute_vertices($self);
}

sub encloses_point {
    my $self = shift;
    my ($point) = @_;
    return Boost::Geometry::Utils::point_covered_by_polygon($point->arrayref, [$self->arrayref_pp]);
}

sub area {
    my $self = shift;
    return Slic3r::Geometry::Clipper::area($self->arrayref_pp);
}

sub grow {
    my $self = shift;
    return $self->split_at_first_point->grow(@_);
}

sub simplify {
    my $self = shift;
    return Slic3r::Geometry::Clipper::simplify_polygon( $self->SUPER::simplify(@_) );
}

# this method subdivides the polygon segments to that no one of them
# is longer than the length provided
sub subdivide {
    my $self = shift;
    my ($max_length) = @_;
    
    for (my $i = 0; $i <= $#$self; $i++) {
        my $len = Slic3r::Geometry::line_length([ $self->[$i-1], $self->[$i] ]);
        my $num_points = int($len / $max_length) - 1;
        $num_points++ if $len % $max_length;
        
        # $num_points is the number of points to add between $i-1 and $i
        next if $num_points == -1;
        my $spacing = $len / ($num_points + 1);
        my @new_points = map Slic3r::Point->new($_),
            map Slic3r::Geometry::point_along_segment($self->[$i-1], $self->[$i], $spacing * $_),
            1..$num_points;
        
        splice @$self, $i, 0, @new_points;
        $i += @new_points;
    }
}

# returns false if the polygon is too tight to be printed
sub is_printable {
    my $self = shift;
    my ($width) = @_;
    
    # try to get an inwards offset
    # for a distance equal to half of the extrusion width;
    # if no offset is possible, then polyline is not printable.
    # we use flow_width here because this has to be consistent 
    # with the thin wall detection in Layer->make_surfaces, 
    # otherwise we could lose surfaces as that logic wouldn't
    # detect them and we would be discarding them.
    my $p = $self->clone;
    $p->make_counter_clockwise;
    return Slic3r::Geometry::Clipper::offset([$p], -$width / 2) ? 1 : 0;
}

sub is_valid {
    my $self = shift;
    return @$self >= 3;
}

sub split_at_index {
    my $self = shift;
    my ($index) = @_;
    
    return Slic3r::Polyline->new(
        @$self[$index .. $#$self], 
        @$self[0 .. $index],
    );
}

sub split_at {
    my $self = shift;
    my ($point) = @_;
    
    # find index of point
    my $i = -1;
    for (my $n = 0; $n <= $#$self; $n++) {
        if (Slic3r::Geometry::same_point($point, $self->[$n])) {
            $i = $n;
            last;
        }
    }
    die "Point not found" if $i == -1;
    
    return $self->split_at_index($i);
}

sub split_at_first_point {
    my $self = shift;
    return $self->split_at_index(0);
}

# for cw polygons this will return convex points!
sub concave_points {
    my $self = shift;
    
    return map $self->[$_],
        grep Slic3r::Geometry::angle3points(@$self[$_, $_-1, $_+1]) < PI - epsilon,
        -1 .. ($#$self-1);
}

package Slic3r::Polygon::XS;
use parent -norequire, qw(Slic3r::Polygon Slic3r::Polyline::XS);

1;