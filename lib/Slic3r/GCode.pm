package Slic3r::GCode;
use Moo;

use List::Util qw(min max first);
use Slic3r::ExtrusionLoop ':roles';
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Flow ':roles';
use Slic3r::Geometry qw(epsilon scale unscale scaled_epsilon points_coincide PI X Y B);
use Slic3r::Geometry::Clipper qw(union_ex offset_ex);
use Slic3r::Surface ':types';

extends 'Slic3r::GCode::Base';

has 'config'             => (is => 'ro', default => sub { Slic3r::Config::Full->new });
has 'placeholder_parser' => (is => 'rw', default => sub { Slic3r::GCode::PlaceholderParser->new });
has 'ooze_prevention'    => (is => 'rw');
has 'enable_loop_clipping' => (is => 'rw', default => sub {1});
has 'enable_wipe'        => (is => 'rw', default => sub {0});   # at least one extruder has wipe enabled
has 'layer_count'        => (is => 'ro');
has '_layer_index'       => (is => 'rw', default => sub {-1});  # just a counter
has 'layer'              => (is => 'rw');
has '_layer_islands'     => (is => 'rw');
has '_upper_layer_islands'  => (is => 'rw');
has '_seam_position'     => (is => 'ro', default => sub { {} });  # $object => pos
has 'shift_x'            => (is => 'rw', default => sub {0} );
has 'shift_y'            => (is => 'rw', default => sub {0} );
has 'z'                  => (is => 'rw');
has 'external_mp'        => (is => 'rw');
has 'layer_mp'           => (is => 'rw');
has 'new_object'         => (is => 'rw', default => sub {0});
has 'straight_once'      => (is => 'rw', default => sub {1});
has 'elapsed_time'       => (is => 'rw', default => sub {0} );  # seconds
has 'last_pos'           => (is => 'rw', default => sub { Slic3r::Point->new(0,0) } );
has 'wipe_path'          => (is => 'rw');

sub apply_print_config {
    my ($self, $print_config) = @_;
    
    $self->SUPER::apply_print_config($print_config);
    $self->config->apply_print_config($print_config);
}

sub set_extruders {
    my ($self, $extruder_ids) = @_;
    
    $self->SUPER::set_extruders($extruder_ids);
    
    # enable wipe path generation if any extruder has wipe enabled
    $self->enable_wipe(defined first { $_->wipe } values %{$self->_extruders});
}

sub set_shift {
    my ($self, @shift) = @_;
    
    # if shift increases (goes towards right), last_pos decreases because it goes towards left
    my @translate = (
        scale ($self->shift_x - $shift[X]),
        scale ($self->shift_y - $shift[Y]),
    );
    $self->last_pos->translate(@translate);
    $self->wipe_path->translate(@translate) if $self->wipe_path;
    
    $self->shift_x($shift[X]);
    $self->shift_y($shift[Y]);
}

sub change_layer {
    my ($self, $layer) = @_;
    
    $self->layer($layer);
    $self->_layer_index($self->_layer_index + 1);
    
    # avoid computing islands and overhangs if they're not needed
    $self->_layer_islands($layer->islands);
    $self->_upper_layer_islands($layer->upper_layer ? $layer->upper_layer->islands : []);
    if ($self->config->avoid_crossing_perimeters) {
        $self->layer_mp(Slic3r::MotionPlanner->new(
            union_ex([ map @$_, @{$layer->slices} ], 1),
        ));
    }
    
    my $gcode = "";
    if (defined $self->layer_count) {
        # TODO: cap this to 99% and add an explicit M73 P100 in the end G-code
        $gcode .= $self->update_progress(int(99 * ($self->_layer_index / ($self->layer_count - 1))));
    }
    
    my $z = $layer->print_z + $self->config->z_offset;  # in unscaled coordinates
    if ($self->_extruder->retract_layer_change && $self->will_move_z($z)) {
        $gcode .= $self->retract;
        $gcode .= $self->lift;
    }
    $gcode .= $self->travel_to_z($z, 'move to next layer (' . $self->layer->id . ')');
    return $gcode;
}

sub extrude {
    my $self = shift;
    
    $_[0]->isa('Slic3r::ExtrusionLoop')
        ? $self->extrude_loop(@_)
        : $self->extrude_path(@_);
}

sub extrude_loop {
    my ($self, $loop, $description, $speed) = @_;
    
    # make a copy; don't modify the orientation of the original loop object otherwise
    # next copies (if any) would not detect the correct orientation
    $loop = $loop->clone;
    
    # extrude all loops ccw
    my $was_clockwise = $loop->make_counter_clockwise;
    
    # find the point of the loop that is closest to the current extruder position
    # or randomize if requested
    my $last_pos = $self->last_pos;
    if ($self->config->spiral_vase) {
        $loop->split_at($last_pos);
    } elsif ($self->config->seam_position eq 'nearest' || $self->config->seam_position eq 'aligned') {
        # simplify polygon in order to skip false positives in concave/convex detection
        my $polygon = $loop->polygon;
        my @simplified = @{$polygon->simplify(scale $self->_extruder->nozzle_diameter/2)};
        
        # concave vertices have priority
        my @candidates = map @{$_->concave_points(PI*4/3)}, @simplified;
        
        # if no concave points were found, look for convex vertices
        @candidates = map @{$_->convex_points(PI*2/3)}, @simplified if !@candidates;
        
        # retrieve the last start position for this object
        my $obj_ptr;
        if (defined $self->layer) {
            $obj_ptr = $self->layer->object->ptr;
            if (defined $self->_seam_position->{$self->layer->object}) {
                $last_pos = $self->_seam_position->{$obj_ptr};
            }
        }
        
        my $point;
        if ($self->config->seam_position eq 'nearest') {
            @candidates = @$polygon if !@candidates;
            $point = $last_pos->nearest_point(\@candidates);
            $loop->split_at_vertex($point);
        } elsif (@candidates) {
            my @non_overhang = grep !$loop->has_overhang_point($_), @candidates;
            @candidates = @non_overhang if @non_overhang;
            $point = $last_pos->nearest_point(\@candidates);
            $loop->split_at_vertex($point);
        } else {
            $point = $last_pos->projection_onto_polygon($polygon);
            $loop->split_at($point);
        }
        $self->_seam_position->{$obj_ptr} = $point;
    } elsif ($self->config->seam_position eq 'random') {
        if ($loop->role == EXTRL_ROLE_CONTOUR_INTERNAL_PERIMETER) {
            my $polygon = $loop->polygon;
            my $centroid = $polygon->centroid;
            $last_pos = Slic3r::Point->new($polygon->bounding_box->x_max, $centroid->y);  #))
            $last_pos->rotate(rand(2*PI), $centroid);
        }
        $loop->split_at($last_pos);
    }
    
    # clip the path to avoid the extruder to get exactly on the first point of the loop;
    # if polyline was shorter than the clipping distance we'd get a null polyline, so
    # we discard it in that case
    my $clip_length = $self->enable_loop_clipping
        ? scale($self->_extruder->nozzle_diameter) * &Slic3r::LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER
        : 0;
    
    # get paths
    my @paths = @{$loop->clip_end($clip_length)};
    return '' if !@paths;
    
    # apply the small perimeter speed
    if ($paths[0]->is_perimeter && $loop->length <= &Slic3r::SMALL_PERIMETER_LENGTH) {
        $speed //= $self->config->get_abs_value('small_perimeter_speed');
    }
    $speed //= -1;
    
    # extrude along the path
    my $gcode = join '', map $self->_extrude_path($_, $description, $speed), @paths;
    
    # reset acceleration
    $gcode .= $self->set_acceleration($self->config->default_acceleration);
    
    $self->wipe_path($paths[-1]->polyline->clone) if $self->enable_wipe;  # TODO: don't limit wipe to last path
    
    # make a little move inwards before leaving loop
    if ($paths[-1]->role == EXTR_ROLE_EXTERNAL_PERIMETER && defined $self->layer && $self->config->perimeters > 1) {
        my $last_path_polyline = $paths[-1]->polyline;
        # detect angle between last and first segment
        # the side depends on the original winding order of the polygon (left for contours, right for holes)
        my @points = $was_clockwise ? (-2, 1) : (1, -2);
        my $angle = Slic3r::Geometry::angle3points(@$last_path_polyline[0, @points]) / 3;
        $angle *= -1 if $was_clockwise;
        
        # create the destination point along the first segment and rotate it
        # we make sure we don't exceed the segment length because we don't know
        # the rotation of the second segment so we might cross the object boundary
        my $first_segment = Slic3r::Line->new(@$last_path_polyline[0,1]);
        my $distance = min(scale($self->_extruder->nozzle_diameter), $first_segment->length);
        my $point = $first_segment->point_at($distance);
        $point->rotate($angle, $last_path_polyline->first_point);
        
        # generate the travel move
        $gcode .= $self->travel_to($point, $paths[-1]->role, "move inwards before travel");
    }
    
    return $gcode;
}

sub extrude_path {
    my ($self, $path, $description, $speed) = @_;
    
    my $gcode = $self->_extrude_path($path, $description, $speed);
    
    # reset acceleration
    $gcode .= $self->set_acceleration($self->config->default_acceleration);
    
    return $gcode;
}

sub _extrude_path {
    my ($self, $path, $description, $speed) = @_;
    
    $path->simplify(&Slic3r::SCALED_RESOLUTION);
    
    # go to first point of extrusion path
    my $gcode = "";
    {
        my $first_point = $path->first_point;
        $gcode .= $self->travel_to($first_point, $path->role, "move to first $description point")
            if !defined $self->last_pos || !$self->last_pos->coincides_with($first_point);
    }
    
    # compensate retraction
    $gcode .= $self->unretract;
    
    # adjust acceleration
    {
        my $acceleration;
        if ($self->config->first_layer_acceleration && $self->layer->id == 0) {
            $acceleration = $self->config->first_layer_acceleration;
        } elsif ($self->config->perimeter_acceleration && $path->is_perimeter) {
            $acceleration = $self->config->perimeter_acceleration;
        } elsif ($self->config->infill_acceleration && $path->is_fill) {
            $acceleration = $self->config->infill_acceleration;
        } elsif ($self->config->bridge_acceleration && $path->is_bridge) {
            $acceleration = $self->config->bridge_acceleration;
        } else {
            $acceleration = $self->config->default_acceleration;
        }
        $gcode .= $self->set_acceleration($acceleration);
    }
    
    # calculate extrusion length per distance unit
    my $e_per_mm = $self->_extruder->e_per_mm3 * $path->mm3_per_mm;
    $e_per_mm = 0 if !$self->_extrusion_axis;
    
    # set speed
    my $F;
    if ($path->role == EXTR_ROLE_PERIMETER) {
        $F = $self->config->get_abs_value('perimeter_speed');
    } elsif ($path->role == EXTR_ROLE_EXTERNAL_PERIMETER) {
        $F = $self->config->get_abs_value('external_perimeter_speed');
    } elsif ($path->role == EXTR_ROLE_OVERHANG_PERIMETER || $path->role == EXTR_ROLE_BRIDGE) {
        $F = $self->config->get_abs_value('bridge_speed');
    } elsif ($path->role == EXTR_ROLE_FILL) {
        $F = $self->config->get_abs_value('infill_speed');
    } elsif ($path->role == EXTR_ROLE_SOLIDFILL) {
        $F = $self->config->get_abs_value('solid_infill_speed');
    } elsif ($path->role == EXTR_ROLE_TOPSOLIDFILL) {
        $F = $self->config->get_abs_value('top_solid_infill_speed');
    } elsif ($path->role == EXTR_ROLE_GAPFILL) {
        $F = $self->config->get_abs_value('gap_fill_speed');
    } else {
        $F = $speed // -1;
        die "Invalid speed" if $F < 0;   # $speed == -1
    }
    $F *= 60;  # convert mm/sec to mm/min
    
    if ($self->layer->id == 0) {
        $F = $self->config->get_abs_value_over('first_layer_speed', $F/60) * 60;
    }
    
    # extrude arc or line
    $gcode .= ";_BRIDGE_FAN_START\n" if $path->is_bridge;
    my $path_length = unscale $path->length;
    {
        $gcode .= $path->gcode($self->_extruder, $e_per_mm, $F,
            $self->shift_x - $self->_extruder->extruder_offset->x,
            $self->shift_y - $self->_extruder->extruder_offset->y,  #,,
            $self->_extrusion_axis,
            $self->config->gcode_comments ? " ; $description" : "");

        if ($self->enable_wipe) {
            $self->wipe_path($path->polyline->clone);
            $self->wipe_path->reverse;
        }
    }
    $gcode .= ";_BRIDGE_FAN_END\n" if $path->is_bridge;
    $self->last_pos($path->last_point);
    
    if ($self->config->cooling) {
        my $path_time = $path_length / $F * 60;
        $self->elapsed_time($self->elapsed_time + $path_time);
    }
    
    return $gcode;
}

sub travel_to {
    my ($self, $point, $role, $comment) = @_;
    
    my $gcode = "";
    my $travel = Slic3r::Line->new($self->last_pos, $point);
    
    # move travel back to original layer coordinates for the island check.
    # note that we're only considering the current object's islands, while we should
    # build a more complete configuration space
    $travel->translate(-$self->shift_x, -$self->shift_y);
    
    # skip retraction if the travel move is contained in an island in the current layer
    # *and* in an island in the upper layer (so that the ooze will not be visible)
    if ($travel->length < scale $self->_extruder->retract_before_travel
        || ($self->config->only_retract_when_crossing_perimeters
            && $self->config->fill_density > 0
            && (first { $_->contains_line($travel) } @{$self->_upper_layer_islands})
            && (first { $_->contains_line($travel) } @{$self->_layer_islands}))
        || (defined $role && $role == EXTR_ROLE_SUPPORTMATERIAL && (first { $_->contains_line($travel) } @{$self->layer->support_islands}))
        ) {
        $self->straight_once(0);
        $gcode .= $self->travel_to_xy($self->point_to_gcode($point), $comment);
    } elsif (!$self->config->avoid_crossing_perimeters || $self->straight_once) {
        $self->straight_once(0);
        $gcode .= $self->retract;
        $gcode .= $self->travel_to_xy($self->point_to_gcode($point), $comment);
    } else {
        if ($self->new_object) {
            $self->new_object(0);
            
            # represent $point in G-code coordinates
            $point = $point->clone;
            my @shift = ($self->shift_x, $self->shift_y);
            $point->translate(map scale $_, @shift);
            
            # calculate path (external_mp uses G-code coordinates so we temporary need a null shift)
            $self->set_shift(0,0);
            $gcode .= $self->_plan($self->external_mp, $point, $comment);
            $self->set_shift(@shift);
        } else {
            $gcode .= $self->_plan($self->layer_mp, $point, $comment);
        }
    }
    
    return $gcode;
}

sub _plan {
    my ($self, $mp, $point, $comment) = @_;
    
    my $gcode = "";
    my @travel = @{$mp->shortest_path($self->last_pos, $point)->lines};
    
    # if the path is not contained in a single island we need to retract
    my $need_retract = !$self->config->only_retract_when_crossing_perimeters;
    if (!$need_retract) {
        $need_retract = 1;
        foreach my $island (@{$self->_upper_layer_islands}) {
            # discard the island if at any line is not enclosed in it
            next if first { !$island->contains_line($_) } @travel;
            # okay, this island encloses the full travel path
            $need_retract = 0;
            last;
        }
    }
    
    # perform the retraction
    $gcode .= $self->retract if $need_retract;
    
    # append the actual path and return
    # use G1 because we rely on paths being straight (G0 may make round paths)
    $gcode .= join '',
        map $self->travel_to_xy($self->point_to_gcode($_->b), $comment),
        @travel;
    return $gcode;
}

sub retract {
    my ($self, $toolchange) = @_;
    
    return "" if !defined $self->_extruder;
    
    my $gcode = "";
    
    # wipe (if it's enabled for this extruder and we have a stored wipe path)
    if ($self->_extruder->wipe && $self->wipe_path) {
        # Reduce feedrate a bit; travel speed is often too high to move on existing material.
        # Too fast = ripping of existing material; too slow = short wipe path, thus more blob.
        my $wipe_speed = $self->gcode_config->travel_speed * 0.8;
        
        # get the retraction length
        my $length = $toolchange
            ? $self->_extruder->retract_length
            : $self->_extruder->retract_length_toolchange;
        
        # Calculate how long we need to travel in order to consume the required
        # amount of retraction. In other words, how far do we move in XY at $wipe_speed
        # for the time needed to consume retract_length at retract_speed?
        my $wipe_dist = scale($length / $self->_extruder->retract_speed * $wipe_speed);
        
        # Take the stored wipe path and replace first point with the current actual position
        # (they might be different, for example, in case of loop clipping).
        my $wipe_path = Slic3r::Polyline->new(
            $self->last_pos,
            @{$self->wipe_path}[1..$#{$self->wipe_path}],
        );
        # 
        $wipe_path->clip_end($wipe_path->length - $wipe_dist);
        
        # subdivide the retraction in segments
        my $retracted = 0;
        foreach my $line (@{$wipe_path->lines}) {
            my $segment_length = $line->length;
            # Reduce retraction length a bit to avoid effective retraction speed to be greater than the configured one
            # due to rounding (TODO: test and/or better math for this)
            my $dE = $length * ($segment_length / $wipe_dist) * 0.95;
            $gcode .= $self->set_speed($wipe_speed*60);
            $gcode .= $self->extrude_to_xy(
                $self->point_to_gcode($line->b),
                -$dE,
                'retract;_WIPE',
            );
            $retracted += $dE;
        }
        $self->_extruder->set_retracted($self->_extruder->retracted + $retracted);
    }
    
    # The parent class will decide whether we need to perform an actual retraction
    # (the extruder might be already retracted fully or partially). We call these 
    # methods even if we performed wipe, since this will ensure the entire retraction
    # length is honored in case wipe path was too short.p
    $gcode .= $toolchange ? $self->retract_for_toolchange : $self->SUPER::retract;
    
    $gcode .= $self->reset_e;
    $gcode .= $self->lift;
    
    return $gcode;
}

sub unretract {
    my ($self) = @_;
    
    my $gcode = "";
    $gcode .= $self->unlift;
    $gcode .= $self->SUPER::unretract('compensate retraction');
    return $gcode;
}

sub G0 {
    my $self = shift;
    return $self->G1(@_) if !($self->config->g0 || $self->config->gcode_flavor eq 'mach3');
    return $self->_G0_G1("G0", @_);
}

sub G1 {
    my $self = shift;
    return $self->_G0_G1("G1", @_);
}

sub _G0_G1 {
    my ($self, $gcode, $point, $z, $e, $F, $comment) = @_;
    
    if ($point) {
        $gcode .= sprintf " X%.3f Y%.3f", 
            ($point->x * &Slic3r::SCALING_FACTOR) + $self->shift_x - $self->_extruder->extruder_offset->x,
            ($point->y * &Slic3r::SCALING_FACTOR) + $self->shift_y - $self->_extruder->extruder_offset->y; #**
        $self->last_pos($point->clone);
    }
    if (defined $z && (!defined $self->z || $z != $self->z)) {
        $self->z($z);
        $gcode .= sprintf " Z%.3f", $z;
    }
    
    return $self->_Gx($gcode, $e, $F, $comment);
}

# convert a model-space scaled point into G-code coordinates
sub point_to_gcode {
    my ($self, $point) = @_;
    
    return Slic3r::Pointf->new(
        ($point->x * &Slic3r::SCALING_FACTOR) + $self->shift_x - $self->_extruder->extruder_offset->x,
        ($point->y * &Slic3r::SCALING_FACTOR) + $self->shift_y - $self->_extruder->extruder_offset->y, #**
    );
}

sub _Gx {
    my ($self, $gcode, $e, $F, $comment) = @_;
    
    $gcode .= sprintf " F%.3f", $F;
    
    # output extrusion distance
    if ($e && $self->_extrusion_axis) {
        $gcode .= sprintf " %s%.5f", $self->_extrusion_axis, $self->_extruder->extrude($e);
    }
    
    $gcode .= " ; $comment" if $comment && $self->config->gcode_comments;
    return "$gcode\n";
}

sub set_extruder {
    my ($self, $extruder_id) = @_;
    
    return "" if !$self->need_toolchange($extruder_id);
    
    # if we are running a single-extruder setup, just set the extruder and return nothing
    if (!$self->_multiple_extruders) {
        return $self->_toolchange($extruder_id);
    }
    
    # prepend retraction on the current extruder
    my $gcode = $self->retract(1);
    
    # append custom toolchange G-code
    if (defined $self->_extruder && $self->config->toolchange_gcode) {
        $gcode .= sprintf "%s\n", $self->placeholder_parser->process($self->config->toolchange_gcode, {
            previous_extruder   => $self->_extruder->id,
            next_extruder       => $extruder_id,
        });
    }
    
    # if ooze prevention is enabled, park current extruder in the nearest
    # standby point and set it to the standby temperature
    $gcode .= $self->ooze_prevention->pre_toolchange($self)
        if $self->ooze_prevention && defined $self->_extruder;
    
    # append the toolchange command
    $gcode .= $self->_toolchange($extruder_id);
    
    # set the new extruder to the operating temperature
    $gcode .= $self->ooze_prevention->post_toolchange($self)
        if $self->ooze_prevention;
    
    return $gcode;
}

1;
