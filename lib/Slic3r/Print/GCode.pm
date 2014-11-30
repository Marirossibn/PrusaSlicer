package Slic3r::Print::GCode;
use Moo;

has 'print'     => (is => 'ro', required => 1, handles => [qw(objects placeholder_parser config)]);
has 'fh'        => (is => 'ro', required => 1);

has '_gcodegen'                      => (is => 'rw');
has '_cooling_buffer'                => (is => 'rw');
has '_spiral_vase'                   => (is => 'rw');
has '_vibration_limit'               => (is => 'rw');
has '_arc_fitting'                   => (is => 'rw');
has '_pressure_regulator'            => (is => 'rw');
has '_skirt_done'                    => (is => 'rw', default => sub { {} });  # print_z => 1
has '_brim_done'                     => (is => 'rw');
has '_second_layer_things_done'      => (is => 'rw');
has '_last_obj_copy'                 => (is => 'rw');

use List::Util qw(first sum);
use Slic3r::Flow ':roles';
use Slic3r::Geometry qw(X Y scale unscale chained_path convex_hull);
use Slic3r::Geometry::Clipper qw(JT_SQUARE union_ex offset);

sub BUILD {
    my ($self) = @_;
    
    {
        # estimate the total number of layer changes
        # TODO: only do this when M73 is enabled
        my $layer_count;
        if ($self->config->complete_objects) {
            $layer_count = sum(map { $_->total_layer_count * @{$_->copies} } @{$self->objects});
        } else {
            # if sequential printing is not enable, all copies of the same object share the same layer change command(s)
            $layer_count = sum(map { $_->total_layer_count } @{$self->objects});
        }
    
        # set up our helper object
        my $gcodegen = Slic3r::GCode->new(
            placeholder_parser  => $self->placeholder_parser,
            layer_count         => $layer_count,
            enable_cooling_markers => 1,
        );
        $gcodegen->apply_print_config($self->config);
        $gcodegen->set_extruders($self->print->extruders);
        $self->_gcodegen($gcodegen);
    }
    
    $self->_cooling_buffer(Slic3r::GCode::CoolingBuffer->new(
        config      => $self->config,
        gcodegen    => $self->_gcodegen,
    ));
    
    $self->_spiral_vase(Slic3r::GCode::SpiralVase->new(config => $self->config))
        if $self->config->spiral_vase;
    
    $self->_vibration_limit(Slic3r::GCode::VibrationLimit->new(config => $self->config))
        if $self->config->vibration_limit > 0;
    
    $self->_arc_fitting(Slic3r::GCode::ArcFitting->new(config => $self->config))
        if $self->config->gcode_arcs;
    
    $self->_pressure_regulator(Slic3r::GCode::PressureRegulator->new(config => $self->config))
        if $self->config->pressure_advance > 0;
}

sub export {
    my ($self) = @_;
    
    my $fh          = $self->fh;
    my $gcodegen    = $self->_gcodegen;
    
    # write some information
    my @lt = localtime;
    printf $fh "; generated by Slic3r $Slic3r::VERSION on %04d-%02d-%02d at %02d:%02d:%02d\n\n",
        $lt[5] + 1900, $lt[4]+1, $lt[3], $lt[2], $lt[1], $lt[0];

    print $fh "; $_\n" foreach split /\R/, $self->config->notes;
    print $fh "\n" if $self->config->notes;
    
    my $first_object = $self->objects->[0];
    my $layer_height = $first_object->config->layer_height;
    for my $region_id (0..$#{$self->print->regions}) {
        my $region = $self->print->regions->[$region_id];
        printf $fh "; external perimeters extrusion width = %.2fmm\n",
            $region->flow(FLOW_ROLE_EXTERNAL_PERIMETER, $layer_height, 0, 0, -1, $first_object)->width;
        printf $fh "; perimeters extrusion width = %.2fmm\n",
            $region->flow(FLOW_ROLE_PERIMETER, $layer_height, 0, 0, -1, $first_object)->width;
        printf $fh "; infill extrusion width = %.2fmm\n",
            $region->flow(FLOW_ROLE_INFILL, $layer_height, 0, 0, -1, $first_object)->width;
        printf $fh "; solid infill extrusion width = %.2fmm\n",
            $region->flow(FLOW_ROLE_SOLID_INFILL, $layer_height, 0, 0, -1, $first_object)->width;
        printf $fh "; top infill extrusion width = %.2fmm\n",
            $region->flow(FLOW_ROLE_TOP_SOLID_INFILL, $layer_height, 0, 0, -1, $first_object)->width;
        printf $fh "; support material extrusion width = %.2fmm\n",
            $self->objects->[0]->support_material_flow->width
            if $self->print->has_support_material;
        printf $fh "; first layer extrusion width = %.2fmm\n",
            $region->flow(FLOW_ROLE_PERIMETER, $layer_height, 0, 1, -1, $self->objects->[0])->width
            if $region->config->first_layer_extrusion_width;
        print  $fh "\n";
    }
    
    # prepare the helper object for replacing placeholders in custom G-code and output filename
    $self->placeholder_parser->update_timestamp;
    
    print $fh $gcodegen->writer->set_fan(0, 1)
        if $self->config->cooling && $self->config->disable_fan_first_layers;
    
    # set bed temperature
    if ((my $temp = $self->config->first_layer_bed_temperature) && $self->config->start_gcode !~ /M(?:190|140)/i) {
        printf $fh $gcodegen->writer->set_bed_temperature($temp, 1);
    }
    
    # set extruder(s) temperature before and after start G-code
    $self->_print_first_layer_temperature(0);
    printf $fh "%s\n", $gcodegen->placeholder_parser->process($self->config->start_gcode);
    $self->_print_first_layer_temperature(1);
    
    # set other general things
    print $fh $gcodegen->preamble;
    
    # initialize a motion planner for object-to-object travel moves
    if ($self->config->avoid_crossing_perimeters) {
        my $distance_from_objects = 1;
        # compute the offsetted convex hull for each object and repeat it for each copy.
        my @islands = ();
        foreach my $obj_idx (0 .. ($self->print->object_count - 1)) {
            my $convex_hull = convex_hull([
                map @{$_->contour}, map @{$_->slices}, @{$self->objects->[$obj_idx]->layers},
            ]);
            # discard layers only containing thin walls (offset would fail on an empty polygon)
            if (@$convex_hull) {
                my $expolygon = Slic3r::ExPolygon->new($convex_hull);
                my @island = @{$expolygon->offset_ex(scale $distance_from_objects, 1, JT_SQUARE)};
                foreach my $copy (@{ $self->objects->[$obj_idx]->_shifted_copies }) {
                    push @islands, map { my $c = $_->clone; $c->translate(@$copy); $c } @island;
                }
            }
        }
        $gcodegen->avoid_crossing_perimeters->init_external_mp(union_ex([ map @$_, @islands ]));
    }
    
    # calculate wiping points if needed
    if ($self->config->ooze_prevention) {
        my @skirt_points = map @$_, map @$_, @{$self->print->skirt};
        if (@skirt_points) {
            my $outer_skirt = convex_hull(\@skirt_points);
            my @skirts = ();
            foreach my $extruder_id (@{$self->print->extruders}) {
                push @skirts, my $s = $outer_skirt->clone;
                $s->translate(map scale($_), @{$self->config->get_at('extruder_offset', $extruder_id)});
            }
            my $convex_hull = convex_hull([ map @$_, @skirts ]);
            
            $gcodegen->ooze_prevention->enable(1);
            $gcodegen->ooze_prevention->standby_points(
                [ map $_->clone, map @$_, map $_->subdivide(scale 10), @{offset([$convex_hull], scale 3)} ]
            );
        }
    }
    
    # set initial extruder only after custom start G-code
    print $fh $gcodegen->set_extruder($self->print->extruders->[0]);
    
    # do all objects for each layer
    if ($self->config->complete_objects) {
        # print objects from the smallest to the tallest to avoid collisions
        # when moving onto next object starting point
        my @obj_idx = sort { $self->objects->[$a]->size->z <=> $self->objects->[$b]->size->z } 0..($self->print->object_count - 1);
        
        my $finished_objects = 0;
        for my $obj_idx (@obj_idx) {
            my $object = $self->objects->[$obj_idx];
            for my $copy (@{ $self->objects->[$obj_idx]->_shifted_copies }) {
                # move to the origin position for the copy we're going to print.
                # this happens before Z goes down to layer 0 again, so that 
                # no collision happens hopefully.
                if ($finished_objects > 0) {
                    $gcodegen->set_origin(Slic3r::Pointf->new(map unscale $copy->[$_], X,Y));
                    print $fh $gcodegen->retract;
                    print $fh $gcodegen->travel_to(
                        $object->_copies_shift->negative,
                        undef,
                        'move to origin position for next object',
                    );
                }
                
                my @layers = sort { $a->print_z <=> $b->print_z } @{$object->layers}, @{$object->support_layers};
                for my $layer (@layers) {
                    # if we are printing the bottom layer of an object, and we have already finished
                    # another one, set first layer temperatures. this happens before the Z move
                    # is triggered, so machine has more time to reach such temperatures
                    if ($layer->id == 0 && $finished_objects > 0) {
                        printf $fh $gcodegen->writer->set_bed_temperature($self->config->first_layer_bed_temperature),
                            if $self->config->first_layer_bed_temperature;
                        $self->_print_first_layer_temperature(0);
                    }
                    $self->process_layer($layer, [$copy]);
                }
                $self->flush_cooling_buffer;
                $finished_objects++;
            }
        }
    } else {
        # order objects using a nearest neighbor search
        my @obj_idx = @{chained_path([ map Slic3r::Point->new(@{$_->_shifted_copies->[0]}), @{$self->objects} ])};
        
        # sort layers by Z
        my %layers = ();  # print_z => [ [layers], [layers], [layers] ]  by obj_idx
        foreach my $obj_idx (0 .. ($self->print->object_count - 1)) {
            my $object = $self->objects->[$obj_idx];
            foreach my $layer (@{$object->layers}, @{$object->support_layers}) {
                $layers{ $layer->print_z } ||= [];
                $layers{ $layer->print_z }[$obj_idx] ||= [];
                push @{$layers{ $layer->print_z }[$obj_idx]}, $layer;
            }
        }
        
        foreach my $print_z (sort { $a <=> $b } keys %layers) {
            foreach my $obj_idx (@obj_idx) {
                foreach my $layer (@{ $layers{$print_z}[$obj_idx] // [] }) {
                    $self->process_layer($layer, $layer->object->_shifted_copies);
                }
            }
        }
        $self->flush_cooling_buffer;
    }
    
    # write end commands to file
    print $fh $gcodegen->retract;   # TODO: process this retract through PressureRegulator in order to discharge fully
    print $fh $gcodegen->writer->set_fan(0);
    printf $fh "%s\n", $gcodegen->placeholder_parser->process($self->config->end_gcode);
    print $fh $gcodegen->writer->update_progress($gcodegen->layer_count, $gcodegen->layer_count, 1);  # 100%
    
    $self->print->total_used_filament(0);
    $self->print->total_extruded_volume(0);
    foreach my $extruder (@{$gcodegen->writer->extruders}) {
        my $used_filament = $extruder->used_filament;
        my $extruded_volume = $extruder->extruded_volume;
        
        printf $fh "; filament used = %.1fmm (%.1fcm3)\n",
            $used_filament, $extruded_volume/1000;
        
        $self->print->total_used_filament($self->print->total_used_filament + $used_filament);
        $self->print->total_extruded_volume($self->print->total_extruded_volume + $extruded_volume);
    }
    
    # append full config
    print $fh "\n";
    foreach my $config ($self->print->config, $self->print->default_object_config, $self->print->default_region_config) {
        foreach my $opt_key (sort @{$config->get_keys}) {
            next if $Slic3r::Config::Options->{$opt_key}{shortcut};
            printf $fh "; %s = %s\n", $opt_key, $config->serialize($opt_key);
        }
    }
}

sub _print_first_layer_temperature {
    my ($self, $wait) = @_;
    
    return if $self->config->start_gcode =~ /M(?:109|104)/i;
    for my $t (@{$self->print->extruders}) {
        my $temp = $self->config->get_at('first_layer_temperature', $t);
        $temp += $self->config->standby_temperature_delta if $self->config->ooze_prevention;
        printf {$self->fh} $self->_gcodegen->writer->set_temperature($temp, $wait, $t) if $temp > 0;
    }
}

sub process_layer {
    my $self = shift;
    my ($layer, $object_copies) = @_;
    my $gcode = "";
    
    my $object = $layer->object;
    $self->_gcodegen->config->apply_object_config($object->config);
    
    # check whether we're going to apply spiralvase logic
    if (defined $self->_spiral_vase) {
        $self->_spiral_vase->enable(
            ($layer->id > 0 || $self->print->config->brim_width == 0)
                && ($layer->id >= $self->print->config->skirt_height && $self->print->config->skirt_height != -1)
                && !defined(first { $_->config->bottom_solid_layers > $layer->id } @{$layer->regions})
                && !defined(first { @{$_->perimeters} > 1 } @{$layer->regions})
                && !defined(first { @{$_->fills} > 0 } @{$layer->regions})
        );
    }
    
    # if we're going to apply spiralvase to this layer, disable loop clipping
    $self->_gcodegen->enable_loop_clipping(!defined $self->_spiral_vase || !$self->_spiral_vase->enable);
    
    if (!$self->_second_layer_things_done && $layer->id == 1) {
        for my $extruder (@{$self->_gcodegen->writer->extruders}) {
            my $temperature = $self->config->get_at('temperature', $extruder->id);
            $gcode .= $self->_gcodegen->writer->set_temperature($temperature, 0, $extruder->id)
                if $temperature && $temperature != $self->config->get_at('first_layer_temperature', $extruder->id);
        }
        $gcode .= $self->_gcodegen->writer->set_bed_temperature($self->print->config->bed_temperature)
            if $self->print->config->bed_temperature && $self->print->config->bed_temperature != $self->print->config->first_layer_bed_temperature;
        $self->_second_layer_things_done(1);
    }
    
    # set new layer - this will change Z and force a retraction if retract_layer_change is enabled
    $gcode .= $self->_gcodegen->change_layer($layer);
    $gcode .= $self->_gcodegen->placeholder_parser->process($self->print->config->layer_gcode, {
        layer_num => $layer->id,
    }) . "\n" if $self->print->config->layer_gcode;
    
    # extrude skirt
    if (((values %{$self->_skirt_done}) < $self->print->config->skirt_height || $self->print->config->skirt_height == -1)
        && !$self->_skirt_done->{$layer->print_z}) {
        $self->_gcodegen->set_origin(Slic3r::Pointf->new(0,0));
        my @extruder_ids = map { $_->id } @{$self->_gcodegen->writer->extruders};
        $gcode .= $self->_gcodegen->set_extruder($extruder_ids[0]);
        # skip skirt if we have a large brim
        if ($layer->id < $self->print->config->skirt_height || $self->print->config->skirt_height == -1) {
            # distribute skirt loops across all extruders
            my @skirt_loops = @{$self->print->skirt};
            for my $i (0 .. $#skirt_loops) {
                # when printing layers > 0 ignore 'min_skirt_length' and 
                # just use the 'skirts' setting; also just use the current extruder
                last if ($layer->id > 0) && ($i >= $self->print->config->skirts);
                my $extruder_id = $extruder_ids[($i/@extruder_ids) % @extruder_ids];
                $gcode .= $self->_gcodegen->set_extruder($extruder_id)
                    if $layer->id == 0;
                $gcode .= $self->_gcodegen->extrude_loop($skirt_loops[$i], 'skirt', $object->config->support_material_speed);
            }
        }
        $self->_skirt_done->{$layer->print_z} = 1;
        $self->_gcodegen->avoid_crossing_perimeters->straight_once(1);
    }
    
    # extrude brim
    if (!$self->_brim_done) {
        $gcode .= $self->_gcodegen->set_extruder($self->print->objects->[0]->config->support_material_extruder-1);
        $self->_gcodegen->set_origin(Slic3r::Pointf->new(0,0));
        $gcode .= $self->_gcodegen->extrude_loop($_, 'brim', $object->config->support_material_speed)
            for @{$self->print->brim};
        $self->_brim_done(1);
        $self->_gcodegen->avoid_crossing_perimeters->straight_once(1);
    }
    
    for my $copy (@$object_copies) {
        $self->_gcodegen->avoid_crossing_perimeters->new_object(1) if ($self->_last_obj_copy // '') ne "$copy";
        $self->_last_obj_copy("$copy");
        
        $self->_gcodegen->set_origin(Slic3r::Pointf->new(map unscale $copy->[$_], X,Y));
        
        # extrude support material before other things because it might use a lower Z
        # and also because we avoid travelling on other things when printing it
        if ($layer->isa('Slic3r::Layer::Support')) {
            if ($layer->support_interface_fills->count > 0) {
                $gcode .= $self->_gcodegen->set_extruder($object->config->support_material_interface_extruder-1);
                $gcode .= $self->_gcodegen->extrude_path($_, 'support material interface', $object->config->get_abs_value('support_material_interface_speed')) 
                    for @{$layer->support_interface_fills->chained_path_from($self->_gcodegen->last_pos, 0)}; 
            }
            if ($layer->support_fills->count > 0) {
                $gcode .= $self->_gcodegen->set_extruder($object->config->support_material_extruder-1);
                $gcode .= $self->_gcodegen->extrude_path($_, 'support material', $object->config->get_abs_value('support_material_speed')) 
                    for @{$layer->support_fills->chained_path_from($self->_gcodegen->last_pos, 0)};
            }
        }
        
        # tweak region ordering to save toolchanges
        my @region_ids = 0 .. ($self->print->region_count-1);
        if ($self->_gcodegen->writer->multiple_extruders) {
            my $last_extruder_id = $self->_gcodegen->writer->extruder->id;
            my $best_region_id = first { $self->print->regions->[$_]->config->perimeter_extruder-1 == $last_extruder_id } @region_ids;
            @region_ids = ($best_region_id, grep $_ != $best_region_id, @region_ids) if $best_region_id;
        }
        
        foreach my $region_id (@region_ids) {
            my $layerm = $layer->regions->[$region_id] or next;
            my $region = $self->print->regions->[$region_id];
            $self->_gcodegen->config->apply_region_config($region->config);
            
            # group extrusions by island
            my @perimeters_by_island = map [], 0..$#{$layer->slices};   # slice idx => @perimeters
            my @infill_by_island     = map [], 0..$#{$layer->slices};   # slice idx => @fills
            
            # NOTE: we assume $layer->slices was already ordered with chained_path()!
            
            PERIMETER: foreach my $perimeter (@{$layerm->perimeters}) {
                for my $i (0 .. $#{$layer->slices}-1) {
                    if ($layer->slices->[$i]->contour->contains_point($perimeter->first_point)) {
                        push @{ $perimeters_by_island[$i] }, $perimeter;
                        next PERIMETER;
                    }
                }
                push @{ $perimeters_by_island[-1] }, $perimeter; # optimization
            }
            FILL: foreach my $fill (@{$layerm->fills}) {
                for my $i (0 .. $#{$layer->slices}-1) {
                    if ($layer->slices->[$i]->contour->contains_point($fill->first_point)) {
                        push @{ $infill_by_island[$i] }, $fill;
                        next FILL;
                    }
                }
                push @{ $infill_by_island[-1] }, $fill; # optimization
            }
            
            for my $i (0 .. $#{$layer->slices}) {
                # give priority to infill if we were already using its extruder and it wouldn't
                # be good for perimeters
                if ($self->print->config->infill_first
                    || ($self->_gcodegen->writer->multiple_extruders && $region->config->infill_extruder-1 == $self->_gcodegen->writer->extruder->id && $region->config->infill_extruder != $region->config->perimeter_extruder)) {
                    $gcode .= $self->_extrude_infill($infill_by_island[$i], $region);
                    $gcode .= $self->_extrude_perimeters($perimeters_by_island[$i], $region);
                } else {
                    $gcode .= $self->_extrude_perimeters($perimeters_by_island[$i], $region);
                    $gcode .= $self->_extrude_infill($infill_by_island[$i], $region);
                }
            }
        }
    }
    
    # apply spiral vase post-processing if this layer contains suitable geometry
    # (we must feed all the G-code into the post-processor, including the first 
    # bottom non-spiral layers otherwise it will mess with positions)
    # we apply spiral vase at this stage because it requires a full layer
    $gcode = $self->_spiral_vase->process_layer($gcode)
        if defined $self->_spiral_vase;
    
    # apply cooling logic; this may alter speeds
    $gcode = $self->_cooling_buffer->append(
        $gcode,
        $layer->object->ptr . ref($layer),  # differentiate $obj_id between normal layers and support layers
        $layer->id,
        $layer->print_z,
    ) if defined $self->_cooling_buffer && defined $layer;
    
    print {$self->fh} $self->filter($gcode);
}

sub _extrude_perimeters {
    my $self = shift;
    my ($island_perimeters, $region) = @_;
    
    return "" if !@$island_perimeters;
    
    my $gcode = "";
    $gcode .= $self->_gcodegen->set_extruder($region->config->perimeter_extruder-1);
    $gcode .= $self->_gcodegen->extrude($_, 'perimeter') for @$island_perimeters;
    return $gcode;
}

sub _extrude_infill {
    my $self = shift;
    my ($island_fills, $region) = @_;
    
    return "" if !@$island_fills;
    
    my $gcode = "";
    $gcode .= $self->_gcodegen->set_extruder($region->config->infill_extruder-1);
    for my $fill (@$island_fills) {
        if ($fill->isa('Slic3r::ExtrusionPath::Collection')) {
            $gcode .= $self->_gcodegen->extrude($_, 'fill') 
                for @{$fill->chained_path_from($self->_gcodegen->last_pos, 0)};
        } else {
            $gcode .= $self->_gcodegen->extrude($fill, 'fill') ;
        }
    }
    return $gcode;
}

sub flush_cooling_buffer {
    my ($self) = @_;
    print {$self->fh} $self->filter($self->_cooling_buffer->flush);
}

sub filter {
    my ($self, $gcode) = @_;
    
    # apply vibration limit if enabled;
    # this injects pauses according to time (thus depends on actual speeds)
    $gcode = $self->_vibration_limit->process($gcode)
        if $self->print->config->vibration_limit != 0;
    
    # apply pressure regulation if enabled;
    # this depends on actual speeds
    $gcode = $self->_pressure_regulator->process($gcode)
        if $self->print->config->pressure_advance > 0;
    
    # apply arc fitting if enabled;
    # this does not depend on speeds but changes G1 XY commands into G2/G2 IJ
    $gcode = $self->_arc_fitting->process($gcode)
        if $self->print->config->gcode_arcs;
    
    return $gcode;
}

1;
