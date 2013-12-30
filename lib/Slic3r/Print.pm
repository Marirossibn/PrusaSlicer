package Slic3r::Print;
use Moo;

use File::Basename qw(basename fileparse);
use File::Spec;
use List::Util qw(min max first);
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Geometry qw(X Y Z X1 Y1 X2 Y2 MIN MAX PI scale unscale move_points chained_path
    convex_hull);
use Slic3r::Geometry::Clipper qw(diff_ex union_ex union_pt intersection_ex intersection offset
    offset2 union union_pt_chained JT_ROUND JT_SQUARE);
use Slic3r::Print::State ':steps';

has 'config'                 => (is => 'rw', default => sub { Slic3r::Config->new_from_defaults }, trigger => \&init_config);
has 'extra_variables'        => (is => 'rw', default => sub {{}});
has 'objects'                => (is => 'rw', default => sub {[]});
has 'status_cb'              => (is => 'rw');
has 'extruders'              => (is => 'rw', default => sub {[]});
has 'regions'                => (is => 'rw', default => sub {[]});
has 'has_support_material'   => (is => 'lazy');
has '_state'                 => (is => 'ro', default => sub { Slic3r::Print::State->new });

# ordered collection of extrusion paths to build skirt loops
has 'skirt' => (is => 'rw', default => sub { Slic3r::ExtrusionPath::Collection->new });

# ordered collection of extrusion paths to build a brim
has 'brim' => (is => 'rw', default => sub { Slic3r::ExtrusionPath::Collection->new });

sub BUILD {
    my $self = shift;
    
    # call this manually because the 'default' coderef doesn't trigger the trigger
    $self->init_config;
}

# this method needs to be idempotent
sub init_config {
    my $self = shift;
    
    # legacy with existing config files
    $self->config->set('first_layer_height', $self->config->layer_height)
        if !$self->config->first_layer_height;
    $self->config->set_ifndef('small_perimeter_speed',  $self->config->perimeter_speed);
    $self->config->set_ifndef('bridge_speed',           $self->config->infill_speed);
    $self->config->set_ifndef('solid_infill_speed',     $self->config->infill_speed);
    $self->config->set_ifndef('top_solid_infill_speed', $self->config->solid_infill_speed);
    $self->config->set_ifndef('top_solid_layers',       $self->config->solid_layers);
    $self->config->set_ifndef('bottom_solid_layers',    $self->config->solid_layers);
    
    # G-code flavors
    $self->config->set('extrusion_axis', 'A') if $self->config->gcode_flavor eq 'mach3';
    $self->config->set('extrusion_axis', '')  if $self->config->gcode_flavor eq 'no-extrusion';
    
    # enforce some settings when spiral_vase is set
    if ($self->config->spiral_vase) {
        $self->config->set('perimeters', 1);
        $self->config->set('fill_density', 0);
        $self->config->set('top_solid_layers', 0);
        $self->config->set('support_material', 0);
        $self->config->set('support_material_enforce_layers', 0);
        $self->config->set('retract_layer_change', [0]);  # TODO: only apply this to the spiral layers
    }
    
    # force all retraction lift values to be the same
    $self->config->set('retract_lift', [ map $self->config->retract_lift->[0], @{$self->config->retract_lift} ]);
}

sub apply_config {
    my ($self, $config) = @_;
    
    $self->config->apply($config);
    $self->init_config;
    $_->init_config for @{$self->objects};
}

sub _build_has_support_material {
    my $self = shift;
    return (first { $_->config->support_material } @{$self->objects})
        || (first { $_->config->raft_layers > 0 } @{$self->objects})
        || (first { $_->config->support_material_enforce_layers > 0 } @{$self->objects});
}

# caller is responsible for supplying models whose objects don't collide
# and have explicit instance positions
sub add_model_object {
    my $self = shift;
    my ($object, $obj_idx) = @_;
    
    my %volumes = ();           # region_id => [ volume_id, ... ]
    foreach my $volume_id (0..$#{$object->volumes}) {
        my $volume = $object->volumes->[$volume_id];
        
        # get the config applied to this volume
        my $config;
        if (defined $volume->material_id) {
            my $config = $object->model->materials->{ $volume->material_id }->config;
        } else {
            $config = Slic3r::Config->new;
        }
        
        # find an existing print region with the same config
        my $region_id;
        foreach my $i (0..$#{$self->regions}) {
            my $region = $self->regions->[$i];
            if ($config->equals($region->config)) {
                $region_id = $i;
                last;
            }
        }
        
        # if no region exists with the same config, create a new one
        if (!defined $region_id) {
            push @{$self->regions}, Slic3r::Print::Region->new(config => $config->clone);
            $region_id = $#{$self->regions};
        }
        
        # assign volume to region
        $volumes{$region_id} //= [];
        push @{ $volumes{$region_id} }, $volume_id;
    }
    
    # initialize print object
    my $o = Slic3r::Print::Object->new(
        print               => $self,
        model_object        => $object,
        region_volumes      => [ map $volumes{$_}, 0..$#{$self->regions} ],
        copies              => [ map Slic3r::Point->new_scale(@{ $_->offset }), @{ $object->instances } ],
        config_overrides    => $object->config,
        layer_height_ranges => $object->layer_height_ranges,
    );
    if (defined $obj_idx) {
        splice @{$self->objects}, $obj_idx, 0, $o;
    } else {
        push @{$self->objects}, $o;
    }
    
    if (!defined $self->extra_variables->{input_filename}) {
        if (defined (my $input_file = $object->input_file)) {
            @{$self->extra_variables}{qw(input_filename input_filename_base)} = parse_filename($input_file);
        }
    }
    
    $self->_state->invalidate(STEP_SKIRT);
    $self->_state->invalidate(STEP_BRIM);
}

sub delete_object {
    my ($self, $obj_idx) = @_;
    
    splice @{$self->objects}, $obj_idx, 1;
    # TODO: purge unused regions
    
    $self->_state->invalidate(STEP_SKIRT);
    $self->_state->invalidate(STEP_BRIM);
}

sub delete_all_objects {
    my ($self) = @_;
    
    @{$self->objects} = ();
    @{$self->regions} = ();
    
    $self->_state->invalidate(STEP_SKIRT);
    $self->_state->invalidate(STEP_BRIM);
}

sub validate {
    my $self = shift;
    
    if ($self->config->complete_objects) {
        # check horizontal clearance
        {
            my @a = ();
            foreach my $object (@{$self->objects}) {
                # get convex hulls of all meshes assigned to this print object
                my @mesh_convex_hulls = map $object->model_object->volumes->[$_]->mesh->convex_hull,
                    map @$_,
                    grep defined $_,
                    @{$object->region_volumes};
                
                # make a single convex hull for all of them
                my $convex_hull = convex_hull([ map @$_, @mesh_convex_hulls ]);
                
                # apply the same transformations we apply to the actual meshes when slicing them
                $object->model_object->instances->[0]->transform_polygon($convex_hull, 1);
        
                # align object to Z = 0 and apply XY shift
                $convex_hull->translate(@{$object->_copies_shift});
                
                # grow convex hull with the clearance margin
                ($convex_hull) = @{offset([$convex_hull], scale $self->config->extruder_clearance_radius / 2, 1, JT_ROUND, scale(0.1))};
                
                # now we need that no instance of $convex_hull does not intersect any of the previously checked object instances
                for my $copy (@{$object->_shifted_copies}) {
                    my $p = $convex_hull->clone;
                    
                    $p->translate(@$copy);
                    if (@{ intersection(\@a, [$p]) }) {
                        die "Some objects are too close; your extruder will collide with them.\n";
                    }
                    @a = @{union([@a, $p])};
                }
            }
        }
        
        # check vertical clearance
        {
            my @object_height = ();
            foreach my $object (@{$self->objects}) {
                my $height = $object->size->[Z];
                push @object_height, $height for @{$object->copies};
            }
            @object_height = sort { $a <=> $b } @object_height;
            # ignore the tallest *copy* (this is why we repeat height for all of them):
            # it will be printed as last one so its height doesn't matter
            pop @object_height;
            if (@object_height && max(@object_height) > scale $self->config->extruder_clearance_height) {
                die "Some objects are too tall and cannot be printed without extruder collisions.\n";
            }
        }
    }
    
    if ($self->config->spiral_vase) {
        if ((map @{$_->copies}, @{$self->objects}) > 1) {
            die "The Spiral Vase option can only be used when printing a single object.\n";
        }
        if (@{$self->regions} > 1) {
            die "The Spiral Vase option can only be used when printing single material objects.\n";
        }
    }
}

sub init_extruders {
    my $self = shift;
    
    # map regions to extruders (ghetto mapping for now)
    my %extruder_mapping = map { $_ => $_ } 0..$#{$self->regions};
    
    # initialize all extruder(s) we need
    my @used_extruders = (
        0,
        (map $self->config->get("${_}_extruder")-1, qw(perimeter infill support_material support_material_interface)),
        (values %extruder_mapping),
    );
    for my $extruder_id (keys %{{ map {$_ => 1} @used_extruders }}) {
        $self->extruders->[$extruder_id] = Slic3r::Extruder->new(
            config => $self->config,
            id => $extruder_id,
            map { $_ => $self->config->get($_)->[$extruder_id] // $self->config->get($_)->[0] } #/
                @{&Slic3r::Extruder::OPTIONS}
        );
    }
    
    # calculate regions' flows
    for my $region_id (0 .. $#{$self->regions}) {
        my $region = $self->regions->[$region_id];
        
        # per-role extruders and flows
        for (qw(perimeter infill solid_infill top_infill)) {
            my $extruder_name = $_;
            $extruder_name =~ s/^(?:solid|top)_//;
            $region->extruders->{$_} = ($self->regions_count > 1)
                ? $self->extruders->[$extruder_mapping{$region_id}]
                : $self->extruders->[$self->config->get("${extruder_name}_extruder")-1];
            $region->flows->{$_} = $region->extruders->{$_}->make_flow(
                layer_height => $self->config->layer_height,
                width => $self->config->get("${_}_extrusion_width") || $self->config->extrusion_width,
                role  => $_,
            );
            $region->first_layer_flows->{$_} = $region->extruders->{$_}->make_flow(
                layer_height    => $self->config->get_value('first_layer_height'),
                width           => $self->config->first_layer_extrusion_width,
                role            => $_,
            ) if $self->config->first_layer_extrusion_width;
        }
    }
    
    # calculate support material flow
    # Note: we should calculate a different flow for support material interface
    # TODO: support material layers have their own variable layer heights, so we
    # probably need a DynamicFlow object that calculates flow on the fly
    # (or the Flow object must support a mutable layer_height)
    if ($self->has_support_material) {
        my $extruder = $self->extruders->[$self->config->support_material_extruder-1];
        $self->support_material_flow($extruder->make_flow(
            layer_height => $self->config->layer_height,  # WRONG!
            width => $self->config->support_material_extrusion_width || $self->config->extrusion_width,
            role  => 'support_material',
        ));
        $self->first_layer_support_material_flow($extruder->make_flow(
            layer_height    => $self->config->get_value('first_layer_height'),
            width           => $self->config->first_layer_extrusion_width,
            role            => 'support_material',
        ));
    }
    
    # enforce tall skirt if using ooze_prevention
    # NOTE: this is not idempotent (i.e. switching ooze_prevention off will not revert skirt settings)
    if ($self->config->ooze_prevention && @{$self->extruders} > 1) {
        $self->config->set('skirt_height', -1);
        $self->config->set('skirts', 1) if $self->config->skirts == 0;
    }
}

sub layer_count {
    my $self = shift;
    return max(map { scalar @{$_->layers} } @{$self->objects});
}

sub regions_count {
    my $self = shift;
    return scalar @{$self->regions};
}

sub bounding_box {
    my $self = shift;
    
    my @points = ();
    foreach my $object (@{$self->objects}) {
        foreach my $copy (@{$object->_shifted_copies}) {
            push @points,
                [ $copy->[X], $copy->[Y] ],
                [ $copy->[X] + $object->size->[X], $copy->[Y] + $object->size->[Y] ];
        }
    }
    return Slic3r::Geometry::BoundingBox->new_from_points([ map Slic3r::Point->new(@$_), @points ]);
}

sub size {
    my $self = shift;
    return $self->bounding_box->size;
}

sub _simplify_slices {
    my $self = shift;
    my ($distance) = @_;
    
    foreach my $layer (map @{$_->layers}, @{$self->objects}) {
        $layer->slices->simplify($distance);
        $_->slices->simplify($distance) for @{$layer->regions};
    }
}

sub process {
    my ($self) = @_;
    
    my $status_cb = $self->status_cb // sub {};
    
    my $print_step = sub {
        my ($step, $cb) = @_;
        if (!$self->_state->done($step)) {
            $self->_state->set_started($step);
            $cb->();
            ### Re-enable this for step-based slicing:
            ### $self->_state->set_done($step);
        }
    };
    my $object_step = sub {
        my ($step, $cb) = @_;
        for my $obj_idx (0..$#{$self->objects}) {
            my $object = $self->objects->[$obj_idx];
            if (!$object->_state->done($step)) {
                $object->_state->set_started($step);
                $cb->($obj_idx);
                ### Re-enable this for step-based slicing:
                ### $object->_state->set_done($step);
            }
        }
    };
    
    # STEP_INIT_EXTRUDERS
    $print_step->(STEP_INIT_EXTRUDERS, sub {
        $self->init_extruders;
    });
    
    # STEP_SLICE
    # skein the STL into layers
    # each layer has surfaces with holes
    $status_cb->(10, "Processing triangulated mesh");
    $object_step->(STEP_SLICE, sub {
        $self->objects->[$_[0]]->slice;
    });
    
    die "No layers were detected. You might want to repair your STL file(s) or check their size and retry.\n"
        if !grep @{$_->layers}, @{$self->objects};
    
    # make perimeters
    # this will add a set of extrusion loops to each layer
    # as well as generate infill boundaries
    $status_cb->(20, "Generating perimeters");
    $object_step->(STEP_PERIMETERS, sub {
        $self->objects->[$_[0]]->make_perimeters;
    });
    
    $status_cb->(30, "Preparing infill");
    $object_step->(STEP_PREPARE_INFILL, sub {
        my $object = $self->objects->[$_[0]];
        
        # this will assign a type (top/bottom/internal) to $layerm->slices
        # and transform $layerm->fill_surfaces from expolygon 
        # to typed top/bottom/internal surfaces;
        $object->detect_surfaces_type;
    
        # decide what surfaces are to be filled
        $_->prepare_fill_surfaces for map @{$_->regions}, @{$object->layers};
    
        # this will detect bridges and reverse bridges
        # and rearrange top/bottom/internal surfaces
        $object->process_external_surfaces;
    
        # detect which fill surfaces are near external layers
        # they will be split in internal and internal-solid surfaces
        $object->discover_horizontal_shells;
        $object->clip_fill_surfaces;
        
        # the following step needs to be done before combination because it may need
        # to remove only half of the combined infill
        $object->bridge_over_infill;
    
        # combine fill surfaces to honor the "infill every N layers" option
        $object->combine_infill;
    });
    
    # this will generate extrusion paths for each layer
    $status_cb->(70, "Infilling layers");
    $object_step->(STEP_INFILL, sub {
        my $object = $self->objects->[$_[0]];
        
        Slic3r::parallelize(
            threads => $self->config->threads,
            items => sub {
                my @items = ();  # [layer_id, region_id]
                for my $region_id (0 .. ($self->regions_count-1)) {
                    push @items, map [$_, $region_id], 0..($object->layer_count-1);
                }
                @items;
            },
            thread_cb => sub {
                my $q = shift;
                while (defined (my $obj_layer = $q->dequeue)) {
                    my ($layer_id, $region_id) = @$obj_layer;
                    my $layerm = $object->layers->[$layer_id]->regions->[$region_id];
                    $layerm->fills->append( $object->fill_maker->make_fill($layerm) );
                }
            },
            collect_cb => sub {},
            no_threads_cb => sub {
                foreach my $layerm (map @{$_->regions}, @{$object->layers}) {
                    $layerm->fills->append($object->fill_maker->make_fill($layerm));
                }
            },
        );
    
        ### we could free memory now, but this would make this step not idempotent
        ### $_->fill_surfaces->clear for map @{$_->regions}, @{$object->layers};
    });
    
    # generate support material
    $status_cb->(85, "Generating support material") if $self->has_support_material;
    $object_step->(STEP_SUPPORTMATERIAL, sub {
        $self->objects->[$_[0]]->generate_support_material;
    });
    
    # make skirt
    $status_cb->(88, "Generating skirt");
    $print_step->(STEP_SKIRT, sub {
        $self->make_skirt;
    });
    
    $status_cb->(88, "Generating skirt");
    $print_step->(STEP_BRIM, sub {
        $self->make_brim;  # must come after make_skirt
    });
    
    # time to make some statistics
    if (0) {
        eval "use Devel::Size";
        print  "MEMORY USAGE:\n";
        printf "  meshes        = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->meshes), @{$self->objects})/1024/1024;
        printf "  layer slices  = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->slices), map @{$_->layers}, @{$self->objects})/1024/1024;
        printf "  region slices = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->slices), map @{$_->regions}, map @{$_->layers}, @{$self->objects})/1024/1024;
        printf "  perimeters    = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->perimeters), map @{$_->regions}, map @{$_->layers}, @{$self->objects})/1024/1024;
        printf "  fills         = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->fills), map @{$_->regions}, map @{$_->layers}, @{$self->objects})/1024/1024;
        printf "  print object  = %.1fMb\n", Devel::Size::total_size($self)/1024/1024;
    }
    if (0) {
        eval "use Slic3r::Test::SectionCut";
        Slic3r::Test::SectionCut->new(print => $self)->export_svg("section_cut.svg");
    }
}

sub export_gcode {
    my $self = shift;
    my %params = @_;
    
    my $status_cb = $self->status_cb // sub {};
    
    # output everything to a G-code file
    my $output_file = $self->expanded_output_filepath($params{output_file});
    $status_cb->(90, "Exporting G-code" . ($output_file ? " to $output_file" : ""));
    $self->write_gcode($params{output_fh} || $output_file);
    
    # run post-processing scripts
    if (@{$self->config->post_process}) {
        $status_cb->(95, "Running post-processing scripts");
        $self->config->setenv;
        for (@{$self->config->post_process}) {
            Slic3r::debugf "  '%s' '%s'\n", $_, $output_file;
            system($_, $output_file);
        }
    }
}

sub export_svg {
    my $self = shift;
    my %params = @_;
    
    # this shouldn't be needed, but we're currently relying on ->make_surfaces() which
    # calls ->perimeter_flow
    $self->init_extruders;
    
    $_->slice for @{$self->objects};
    
    my $fh = $params{output_fh};
    if (!$fh) {
        my $output_file = $self->expanded_output_filepath($params{output_file});
        $output_file =~ s/\.gcode$/.svg/i;
        Slic3r::open(\$fh, ">", $output_file) or die "Failed to open $output_file for writing\n";
        print "Exporting to $output_file..." unless $params{quiet};
    }
    
    my $print_size = $self->size;
    print $fh sprintf <<"EOF", unscale($print_size->[X]), unscale($print_size->[Y]);
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.0//EN" "http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd">
<svg width="%s" height="%s" xmlns="http://www.w3.org/2000/svg" xmlns:svg="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns:slic3r="http://slic3r.org/namespaces/slic3r">
  <!-- 
  Generated using Slic3r $Slic3r::VERSION
  http://slic3r.org/
   -->
EOF
    
    my $print_polygon = sub {
        my ($polygon, $type) = @_;
        printf $fh qq{    <polygon slic3r:type="%s" points="%s" style="fill: %s" />\n},
            $type, (join ' ', map { join ',', map unscale $_, @$_ } @$polygon),
            ($type eq 'contour' ? 'white' : 'black');
    };
    
    my @previous_layer_slices = ();
    for my $layer_id (0..$self->layer_count-1) {
        my @layers = map $_->layers->[$layer_id], @{$self->objects};
        printf $fh qq{  <g id="layer%d" slic3r:z="%s">\n}, $layer_id, +(grep defined $_, @layers)[0]->slice_z;
        
        my @current_layer_slices = ();
        for my $obj_idx (0 .. $#{$self->objects}) {
            my $layer = $self->objects->[$obj_idx]->layers->[$layer_id] or next;
            
            # sort slices so that the outermost ones come first
            my @slices = sort { $a->contour->contains_point($b->contour->first_point) ? 0 : 1 } @{$layer->slices};
            foreach my $copy (@{$self->objects->[$obj_idx]->_shifted_copies}) {
                foreach my $slice (@slices) {
                    my $expolygon = $slice->clone;
                    $expolygon->translate(@$copy);
                    $print_polygon->($expolygon->contour, 'contour');
                    $print_polygon->($_, 'hole') for @{$expolygon->holes};
                    push @current_layer_slices, $expolygon;
                }
            }
        }
        # generate support material
        if ($self->has_support_material && $layer_id > 0) {
            my (@supported_slices, @unsupported_slices) = ();
            foreach my $expolygon (@current_layer_slices) {
                my $intersection = intersection_ex(
                    [ map @$_, @previous_layer_slices ],
                    $expolygon,
                );
                @$intersection
                    ? push @supported_slices, $expolygon
                    : push @unsupported_slices, $expolygon;
            }
            my @supported_points = map @$_, @$_, @supported_slices;
            foreach my $expolygon (@unsupported_slices) {
                # look for the nearest point to this island among all
                # supported points
                my $contour = $expolygon->contour;
                my $support_point = $contour->first_point->nearest_point(\@supported_points)
                    or next;
                my $anchor_point = $support_point->nearest_point([ @$contour ]);
                printf $fh qq{    <line x1="%s" y1="%s" x2="%s" y2="%s" style="stroke-width: 2; stroke: white" />\n},
                    map @$_, $support_point, $anchor_point;
            }
        }
        print $fh qq{  </g>\n};
        @previous_layer_slices = @current_layer_slices;
    }
    
    print $fh "</svg>\n";
    close $fh;
    print "Done.\n" unless $params{quiet};
}

sub make_skirt {
    my $self = shift;
    return unless $self->config->skirts > 0
        || ($self->config->ooze_prevention && @{$self->extruders} > 1);
    
    $self->skirt->clear;  # method must be idempotent
    
    # collect points from all layers contained in skirt height
    my @points = ();
    foreach my $obj_idx (0 .. $#{$self->objects}) {
        my $object = $self->objects->[$obj_idx];
        
        # get skirt layers
        my $skirt_height = ($self->config->skirt_height == -1)
            ? 1 + $#{$object->layers}
            : 1 + min($self->config->skirt_height-1, $#{$object->layers}+1);
        
        my @layer_points = (
            map @$_, map @$_, map @{$object->layers->[$_]->slices}, 0..($skirt_height-1),
        );
        if (@{ $object->support_layers }) {
            my @support_layers = map $object->support_layers->[$_], 0..min($self->config->skirt_height-1, $#{$object->support_layers});
            push @layer_points,
                (map @{$_->polyline}, map @{$_->support_fills}, grep $_->support_fills, @support_layers),
                (map @{$_->polyline}, map @{$_->support_interface_fills}, grep $_->support_interface_fills, @support_layers);
        }
        push @points, map move_points($_, @layer_points), @{$object->_shifted_copies};
    }
    return if @points < 3;  # at least three points required for a convex hull
    
    # find out convex hull
    my $convex_hull = convex_hull(\@points);
    
    my @extruded_length = ();  # for each extruder
    
    # TODO: use each extruder's own flow
    my $spacing = $self->flow(FLOW_ROLE_SUPPORT_MATERIAL)->spacing;
    
    my $first_layer_height = $self->config->get_value('first_layer_height');
    my @extruders_e_per_mm = ();
    my $extruder_idx = 0;
    
    # draw outlines from outside to inside
    # loop while we have less skirts than required or any extruder hasn't reached the min length if any
    my $distance = scale $self->config->skirt_distance;
    for (my $i = $self->config->skirts; $i > 0; $i--) {
        $distance += scale $spacing;
        my $loop = offset([$convex_hull], $distance, 1, JT_ROUND, scale(0.1))->[0];
        $self->skirt->append(Slic3r::ExtrusionLoop->new(
            polygon         => Slic3r::Polygon->new(@$loop),
            role            => EXTR_ROLE_SKIRT,
            flow_spacing    => $spacing,
        ));
        
        if ($self->config->min_skirt_length > 0) {
            $extruded_length[$extruder_idx]     ||= 0;
            $extruders_e_per_mm[$extruder_idx]  ||= $self->extruders->[$extruder_idx]->e_per_mm($spacing, $first_layer_height);
            $extruded_length[$extruder_idx]     += unscale $loop->length * $extruders_e_per_mm[$extruder_idx];
            $i++ if defined first { ($extruded_length[$_] // 0) < $self->config->min_skirt_length } 0 .. $#{$self->extruders};
            if ($extruded_length[$extruder_idx] >= $self->config->min_skirt_length) {
                if ($extruder_idx < $#{$self->extruders}) {
                    $extruder_idx++;
                    next;
                }
            }
        }
    }
    
    $self->skirt->reverse;
}

sub make_brim {
    my $self = shift;
    return unless $self->config->brim_width > 0;
    
    $self->brim->clear;  # method must be idempotent
    
    my $flow = $self->flow(FLOW_ROLE_SUPPORT_MATERIAL);
    
    my $grow_distance = $flow->scaled_width / 2;
    my @islands = (); # array of polygons
    foreach my $obj_idx (0 .. $#{$self->objects}) {
        my $object = $self->objects->[$obj_idx];
        my $layer0 = $object->layers->[0];
        my @object_islands = (
            (map $_->contour, @{$layer0->slices}),
        );
        if (@{ $object->support_layers }) {
            my $support_layer0 = $object->support_layers->[0];
            push @object_islands,
                (map @{$_->polyline->grow($grow_distance)}, @{$support_layer0->support_fills})
                if $support_layer0->support_fills;
            push @object_islands,
                (map @{$_->polyline->grow($grow_distance)}, @{$support_layer0->support_interface_fills})
                if $support_layer0->support_interface_fills;
        }
        foreach my $copy (@{$object->_shifted_copies}) {
            push @islands, map { $_->translate(@$copy); $_ } map $_->clone, @object_islands;
        }
    }
    
    # if brim touches skirt, make it around skirt too
    # TODO: calculate actual skirt width (using each extruder's flow in multi-extruder setups)
    if ($self->config->skirt_distance + (($self->config->skirts - 1) * $flow->spacing) <= $self->config->brim_width) {
        push @islands, map @{$_->split_at_first_point->polyline->grow($grow_distance)}, @{$self->skirt};
    }
    
    my @loops = ();
    my $num_loops = sprintf "%.0f", $self->config->brim_width / $flow->width;
    for my $i (reverse 1 .. $num_loops) {
        # JT_SQUARE ensures no vertex is outside the given offset distance
        # -0.5 because islands are not represented by their centerlines
        # (first offset more, then step back - reverse order than the one used for 
        # perimeters because here we're offsetting outwards)
        push @loops, @{offset2(\@islands, ($i + 0.5) * $flow->scaled_spacing, -1.0 * $flow->scaled_spacing, 100000, JT_SQUARE)};
    }
    
    $self->brim->append(map Slic3r::ExtrusionLoop->new(
        polygon         => Slic3r::Polygon->new(@$_),
        role            => EXTR_ROLE_SKIRT,
        flow_spacing    => $flow->spacing,
    ), reverse @{union_pt_chained(\@loops)});
}

sub write_gcode {
    my $self = shift;
    my ($file) = @_;
    
    # open output gcode file if we weren't supplied a file-handle
    my $fh;
    if (ref $file eq 'IO::Scalar') {
        $fh = $file;
    } else {
        Slic3r::open(\$fh, ">", $file)
            or die "Failed to open $file for writing\n";
    }
    
    # write some information
    my @lt = localtime;
    printf $fh "; generated by Slic3r $Slic3r::VERSION on %04d-%02d-%02d at %02d:%02d:%02d\n\n",
        $lt[5] + 1900, $lt[4]+1, $lt[3], $lt[2], $lt[1], $lt[0];

    print $fh "; $_\n" foreach split /\R/, $self->config->notes;
    print $fh "\n" if $self->config->notes;
    
    for (qw(layer_height perimeters top_solid_layers bottom_solid_layers fill_density perimeter_speed infill_speed travel_speed)) {
        printf $fh "; %s = %s\n", $_, $self->config->$_;
    }
    for (qw(nozzle_diameter filament_diameter extrusion_multiplier)) {
        printf $fh "; %s = %s\n", $_, $self->config->$_->[0];
    }
    
    for my $region_id (0..$#{$self->regions}) {
        printf $fh "; perimeters extrusion width = %.2fmm\n",
            $self->regions->[$region_id]->flow(FLOW_ROLE_PERIMETER)->width;
        printf $fh "; infill extrusion width = %.2fmm\n",
            $self->regions->[$region_id]->flow(FLOW_ROLE_INFILL)->width;
        printf $fh "; solid infill extrusion width = %.2fmm\n",
            $self->regions->[$region_id]->flow(FLOW_ROLE_SOLID_INFILL)->width;
        printf $fh "; top infill extrusion width = %.2fmm\n",
            $self->regions->[$region_id]->flow(FLOW_ROLE_TOP_SOLID_INFILL)->width;
        printf $fh "; support material extrusion width = %.2fmm\n",
            $self->flow(FLOW_ROLE_SUPPORT_MATERIAL)->width
            if $self->support_material_flow;
        printf $fh "; first layer extrusion width = %.2fmm\n",
            $self->flow(FLOW_ROLE_SUPPORT_MATERIAL, 0, 1)->width
            if $self->regions->[0]->first_layer_flows->{perimeter};
        print  $fh "\n";
    }
    
    # set up our extruder object
    my $gcodegen = Slic3r::GCode->new(
        config              => $self->config,
        extra_variables     => $self->extra_variables,
        extruders           => $self->extruders,    # we should only pass the *used* extruders (but maintain the Tx indices right!)
        layer_count         => $self->layer_count,
    );
    print $fh "G21 ; set units to millimeters\n" if $self->config->gcode_flavor ne 'makerware';
    print $fh $gcodegen->set_fan(0, 1) if $self->config->cooling && $self->config->disable_fan_first_layers;
    
    # set bed temperature
    if ((my $temp = $self->config->first_layer_bed_temperature) && $self->config->start_gcode !~ /M(?:190|140)/i) {
        printf $fh $gcodegen->set_bed_temperature($temp, 1);
    }
    
    # set extruder(s) temperature before and after start G-code
    my $print_first_layer_temperature = sub {
        my ($wait) = @_;
        
        return if $self->config->start_gcode =~ /M(?:109|104)/i;
        for my $t (0 .. $#{$self->extruders}) {
            my $temp = $self->extruders->[$t]->first_layer_temperature;
            $temp += $self->config->standby_temperature_delta if $self->config->ooze_prevention;
            printf $fh $gcodegen->set_temperature($temp, $wait, $t) if $temp > 0;
        }
    };
    $print_first_layer_temperature->(0);
    printf $fh "%s\n", $gcodegen->replace_variables($self->config->start_gcode);
    $print_first_layer_temperature->(1);
    
    # set other general things
    print  $fh "G90 ; use absolute coordinates\n" if $self->config->gcode_flavor ne 'makerware';
    if ($self->config->gcode_flavor =~ /^(?:reprap|teacup)$/) {
        printf $fh $gcodegen->reset_e;
        if ($self->config->use_relative_e_distances) {
            print $fh "M83 ; use relative distances for extrusion\n";
        } else {
            print $fh "M82 ; use absolute distances for extrusion\n";
        }
    }
    
    # always start with first extruder
    # TODO: make sure we select the first *used* extruder
    print $fh $gcodegen->set_extruder($self->extruders->[0]);
    
    # initialize a motion planner for object-to-object travel moves
    if ($self->config->avoid_crossing_perimeters) {
        my $distance_from_objects = 1;
        # compute the offsetted convex hull for each object and repeat it for each copy.
        my @islands = ();
        foreach my $obj_idx (0 .. $#{$self->objects}) {
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
        $gcodegen->external_mp(Slic3r::GCode::MotionPlanner->new(
            islands     => union_ex([ map @$_, @islands ]),
            internal    => 0,
        ));
    }
    
    # calculate wiping points if needed
    if ($self->config->ooze_prevention) {
        my @skirt_points = map @$_, @{$self->skirt};
        if (@skirt_points) {
            my $outer_skirt = convex_hull(\@skirt_points);
            my @skirts = ();
            foreach my $extruder (@{$self->extruders}) {
                push @skirts, my $s = $outer_skirt->clone;
                $s->translate(map scale($_), @{$extruder->extruder_offset});
            }
            my $convex_hull = convex_hull([ map @$_, @skirts ]);
            $gcodegen->standby_points([ map $_->clone, map @$_, map $_->subdivide(scale 10), @{offset([$convex_hull], scale 3)} ]);
        }
    }
    
    # prepare the layer processor
    my $layer_gcode = Slic3r::GCode::Layer->new(
        print       => $self,
        gcodegen    => $gcodegen,
    );
    
    # do all objects for each layer
    if ($self->config->complete_objects) {
        # print objects from the smallest to the tallest to avoid collisions
        # when moving onto next object starting point
        my @obj_idx = sort { $self->objects->[$a]->size->[Z] <=> $self->objects->[$b]->size->[Z] } 0..$#{$self->objects};
        
        my $finished_objects = 0;
        for my $obj_idx (@obj_idx) {
            for my $copy (@{ $self->objects->[$obj_idx]->_shifted_copies }) {
                # move to the origin position for the copy we're going to print.
                # this happens before Z goes down to layer 0 again, so that 
                # no collision happens hopefully.
                if ($finished_objects > 0) {
                    $gcodegen->set_shift(map unscale $copy->[$_], X,Y);
                    print $fh $gcodegen->retract;
                    print $fh $gcodegen->G0(Slic3r::Point->new(0,0), undef, 0, 'move to origin position for next object');
                }
                
                my $buffer = Slic3r::GCode::CoolingBuffer->new(
                    config      => $self->config,
                    gcodegen    => $gcodegen,
                );
                
                my $object = $self->objects->[$obj_idx];
                my @layers = sort { $a->print_z <=> $b->print_z } @{$object->layers}, @{$object->support_layers};
                for my $layer (@layers) {
                    # if we are printing the bottom layer of an object, and we have already finished
                    # another one, set first layer temperatures. this happens before the Z move
                    # is triggered, so machine has more time to reach such temperatures
                    if ($layer->id == 0 && $finished_objects > 0) {
                        printf $fh $gcodegen->set_bed_temperature($self->config->first_layer_bed_temperature),
                            if $self->config->first_layer_bed_temperature;
                        $print_first_layer_temperature->();
                    }
                    print $fh $buffer->append(
                        $layer_gcode->process_layer($layer, [$copy]),
                        $layer->object."",
                        $layer->id,
                        $layer->print_z,
                    );
                }
                print $fh $buffer->flush;
                $finished_objects++;
            }
        }
    } else {
        # order objects using a nearest neighbor search
        my @obj_idx = @{chained_path([ map Slic3r::Point->new(@{$_->_shifted_copies->[0]}), @{$self->objects} ])};
        
        # sort layers by Z
        my %layers = ();  # print_z => [ [layers], [layers], [layers] ]  by obj_idx
        foreach my $obj_idx (0 .. $#{$self->objects}) {
            my $object = $self->objects->[$obj_idx];
            foreach my $layer (@{$object->layers}, @{$object->support_layers}) {
                $layers{ $layer->print_z } ||= [];
                $layers{ $layer->print_z }[$obj_idx] ||= [];
                push @{$layers{ $layer->print_z }[$obj_idx]}, $layer;
            }
        }
        
        my $buffer = Slic3r::GCode::CoolingBuffer->new(
            config      => $self->config,
            gcodegen    => $gcodegen,
        );
        foreach my $print_z (sort { $a <=> $b } keys %layers) {
            foreach my $obj_idx (@obj_idx) {
                foreach my $layer (@{ $layers{$print_z}[$obj_idx] // [] }) {
                    print $fh $buffer->append(
                        $layer_gcode->process_layer($layer, $layer->object->_shifted_copies),
                        $layer->object . ref($layer),  # differentiate $obj_id between normal layers and support layers
                        $layer->id,
                        $layer->print_z,
                    );
                }
            }
        }
        print $fh $buffer->flush;
    }
    
    # write end commands to file
    print $fh $gcodegen->retract if $gcodegen->extruder;  # empty prints don't even set an extruder
    print $fh $gcodegen->set_fan(0);
    printf $fh "%s\n", $gcodegen->replace_variables($self->config->end_gcode);
    
    foreach my $extruder (@{$self->extruders}) {
        printf $fh "; filament used = %.1fmm (%.1fcm3)\n",
            $extruder->absolute_E, $extruder->extruded_volume/1000;
    }
    
    if ($self->config->gcode_comments) {
        # append full config
        print $fh "\n";
        foreach my $opt_key (sort @{$self->config->get_keys}) {
            next if $Slic3r::Config::Options->{$opt_key}{shortcut};
            printf $fh "; %s = %s\n", $opt_key, $self->config->serialize($opt_key);
        }
    }
    
    # close our gcode file
    close $fh;
}

# this method will return the supplied input file path after expanding its
# format variables with their values
sub expanded_output_filepath {
    my $self = shift;
    my ($path, $input_file) = @_;
    
    my $extra_variables = {};
    if ($input_file) {
        @$extra_variables{qw(input_filename input_filename_base)} = parse_filename($input_file);
    } else {
        # if no input file was supplied, take the first one from our objects
        $input_file = $self->objects->[0]->model_object->input_file // return undef;
    }
    
    if ($path && -d $path) {
        # if output path is an existing directory, we take that and append
        # the specified filename format
        $path = File::Spec->join($path, $self->config->output_filename_format);
    } elsif (!$path) {
        # if no explicit output file was defined, we take the input
        # file directory and append the specified filename format
        $path = (fileparse($input_file))[1] . $self->config->output_filename_format;
    } else {
        # path is a full path to a file so we use it as it is
    }
    return $self->config->replace_options($path, { %{$self->extra_variables}, %$extra_variables });
}

# given the path to a file, this function returns its filename with and without extension
sub parse_filename {
    my ($path) = @_;
    
    my $filename = my $filename_base = basename($path);
    $filename_base =~ s/\.[^.]+$//;
    return ($filename, $filename_base);
}

sub apply_extra_variables {
    my ($self, $extra) = @_;
    $self->extra_variables->{$_} = $extra->{$_} for keys %$extra;
}

sub invalidate_step {
    my ($self, $step, $obj_idx) = @_;
    
    # invalidate $step in the correct state object
    if ($Slic3r::Print::State::print_step->{$step}) {
        $self->_state->invalidate($step);
    } else {
        # object step
        if (defined $obj_idx) {
            $self->objects->[$obj_idx]->_state->invalidate($step);
        } else {
            $_->_state->invalidate($step) for @{$self->objects};
        }
    }
    
    # recursively invalidate steps depending on $step
    $self->invalidate_step($_)
        for grep { grep { $_ == $step } @{$Slic3r::Print::State::prereqs{$_}} }
            keys %Slic3r::Print::State::prereqs;
}

# This method assigns extruders to the volumes having a material
# but not having extruders set in the material config.
sub auto_assign_extruders {
    my ($self, $model_object) = @_;
    
    my $extruders = scalar @{ $self->config->nozzle_diameter };
    foreach my $i (0..$#{$model_object->volumes}) {
        my $volume = $model_object->volumes->[$i];
        if (defined $volume->material_id) {
            my $material = $model_object->model->materials->{ $volume->material_id };
            my $config = $material->config;
            $config->set_ifndef('perimeters_extruder', $i);
            $config->set_ifndef('infill_extruder', $i);
            $config->set_ifndef('support_material_extruder', $i);
            $config->set_ifndef('support_material_interface_extruder', $i);
        }
    }
}

sub flow {
    my ($self, $role, $layer_height, $bridge, $first_layer, $width) = @_;
    
    $bridge         //= 0;
    $first_layer    //= 0;
    
    # use the supplied custom width, if any
    my $config_width = $width;
    if (!defined $config_width) {
        # get extrusion width from configuration
        # (might be an absolute value, or a percent value, or zero for auto)
        if ($first_layer) {
            $config_width = $self->config->first_layer_extrusion_width;
        } elsif ($role == FLOW_ROLE_SUPPORT_MATERIAL || $role == FLOW_ROLE_SUPPORT_MATERIAL_INTERFACE) {
            $config_width = $self->config->support_material_extrusion_width;
        } else {
            die "Unknown role $role";
        }
    }
    
    # get the configured nozzle_diameter for the extruder associated
    # to the flow role requested
    my $extruder;  # 1-based
    if ($role == FLOW_ROLE_SUPPORT_MATERIAL) {
        $config_width = $self->config->support_material_extruder;
    } elsif ($role == FLOW_ROLE_SUPPORT_MATERIAL_INTERFACE) {
        $config_width = $self->config->support_material_interface_extruder;
    } else {
        die "Unknown role $role";
    }
    my $nozzle_diameter = $self->config->nozzle_diameter->[$extruder-1];
    
    return Slic3r::Flow->new(
        width               => $config_width,
        role                => $role,
        nozzle_diameter     => $nozzle_diameter,
        layer_height        => $layer_height,
        bridge_flow_ratio   => ($bridge ? $self->config->bridge_flow_ratio : 0),
    );
}

1;
