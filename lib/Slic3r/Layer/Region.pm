package Slic3r::Layer::Region;
use Moo;

use List::Util qw(sum first);
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Geometry qw(PI A B scale chained_path_items points_coincide);
use Slic3r::Geometry::Clipper qw(union_ex diff_ex intersection_ex 
    offset offset2 offset2_ex union_pt traverse_pt diff intersection
    union diff);
use Slic3r::Surface ':types';

has 'layer' => (
    is          => 'ro',
    weak_ref    => 1,
    required    => 1,
    trigger     => 1,
    handles     => [qw(id slice_z print_z height flow object print config)],
);
has 'region'            => (is => 'ro', required => 1, handles => [qw(extruders)]);
has 'perimeter_flow'    => (is => 'rw');
has 'infill_flow'       => (is => 'rw');
has 'solid_infill_flow' => (is => 'rw');
has 'top_infill_flow'   => (is => 'rw');
has 'infill_area_threshold' => (is => 'lazy');
has 'overhang_width'    => (is => 'lazy');

# collection of surfaces generated by slicing the original geometry
# divided by type top/bottom/internal
has 'slices' => (is => 'rw', default => sub { Slic3r::Surface::Collection->new });

# collection of polygons or polylines representing thin walls contained 
# in the original geometry
has 'thin_walls' => (is => 'rw', default => sub { Slic3r::ExtrusionPath::Collection->new });

# collection of extrusion paths/loops filling gaps
has 'thin_fills' => (is => 'rw', default => sub { Slic3r::ExtrusionPath::Collection->new });

# collection of surfaces for infill generation
has 'fill_surfaces' => (is => 'rw', default => sub { Slic3r::Surface::Collection->new });

# ordered collection of extrusion paths/loops to build all perimeters
has 'perimeters' => (is => 'rw', default => sub { Slic3r::ExtrusionPath::Collection->new });

# ordered collection of extrusion paths to fill surfaces
has 'fills' => (is => 'rw', default => sub { Slic3r::ExtrusionPath::Collection->new });

sub BUILD {
    my $self = shift;
    $self->_update_flows;
}

sub _trigger_layer {
    my $self = shift;
    $self->_update_flows;
}

sub _update_flows {
    my $self = shift;
    return if !$self->region;
    
    if ($self->id == 0) {
        for (qw(perimeter infill solid_infill top_infill)) {
            my $method = "${_}_flow";
            $self->$method
                ($self->region->first_layer_flows->{$_} || $self->region->flows->{$_});
        } 
    } else {
        $self->perimeter_flow($self->region->flows->{perimeter});
        $self->infill_flow($self->region->flows->{infill});
        $self->solid_infill_flow($self->region->flows->{solid_infill});
        $self->top_infill_flow($self->region->flows->{top_infill});
    }
}

sub _build_overhang_width {
    my $self = shift;
    my $threshold_rad = PI/2 - atan2($self->perimeter_flow->width / $self->height / 2, 1);
    return scale($self->height * ((cos $threshold_rad) / (sin $threshold_rad)));
}

sub _build_infill_area_threshold {
    my $self = shift;
    return $self->solid_infill_flow->scaled_spacing ** 2;
}

# build polylines from lines
sub make_surfaces {
    my $self = shift;
    my ($loops) = @_;
    
    return if !@$loops;
    $self->slices->clear;
    $self->slices->append(_merge_loops($loops));
    
    if (0) {
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output("surfaces.svg",
            #polylines         => $loops,
            red_polylines       => [ grep $_->is_counter_clockwise, @$loops ],
            green_polylines     => [ grep !$_->is_counter_clockwise, @$loops ],
            expolygons          => [ map $_->expolygon, @{$self->slices} ],
        );
    }
    
    # detect thin walls by offsetting slices by half extrusion inwards
    if ($self->config->thin_walls) {
        $self->thin_walls([]);
        # we use spacing here because there could be a case where
        # the slice collapses with width but doesn't collapse with spacing,
        # thus causing both perimeters and medial axis to be generated
        my $width = $self->perimeter_flow->scaled_spacing;
        my $diff = diff_ex(
            [ map $_->p, @{$self->slices} ],
            offset2([ map $_->p, @{$self->slices} ], -$width*0.5, +$width*0.5),
            1,
        );
        
        my $area_threshold = $width ** 2;
        if (@$diff = grep { $_->area > $area_threshold } @$diff) {
            @{$self->thin_walls} = map $_->medial_axis($width), @$diff;
            Slic3r::debugf "  %d thin walls detected\n", scalar(@{$self->thin_walls});
        }
    }
}

sub _merge_loops {
    my ($loops, $safety_offset) = @_;
    
    # Input loops are not suitable for evenodd nor nonzero fill types, as we might get
    # two consecutive concentric loops having the same winding order - and we have to 
    # respect such order. In that case, evenodd would create wrong inversions, and nonzero
    # would ignore holes inside two concentric contours.
    # So we're ordering loops and collapse consecutive concentric loops having the same 
    # winding order.
    # TODO: find a faster algorithm for this, maybe with some sort of binary search.
    # If we computed a "nesting tree" we could also just remove the consecutive loops
    # having the same winding order, and remove the extra one(s) so that we could just
    # supply everything to offset_ex() instead of performing several union/diff calls.
    
    # we sort by area assuming that the outermost loops have larger area;
    # the previous sorting method, based on $b->encloses_point($a->[0]), failed to nest
    # loops correctly in some edge cases when original model had overlapping facets
    my @abs_area = map abs($_), my @area = map $_->area, @$loops;
    my @sorted = sort { $abs_area[$b] <=> $abs_area[$a] } 0..$#$loops;  # outer first
    
    # we don't perform a safety offset now because it might reverse cw loops
    my $slices = [];
    for my $i (@sorted) {
        # we rely on the already computed area to determine the winding order
        # of the loops, since the Orientation() function provided by Clipper
        # would do the same, thus repeating the calculation
        $slices = ($area[$i] >= 0)
            ? union([ $loops->[$i], @$slices ])
            : diff($slices, [$loops->[$i]]);
    }
    
    # perform a safety offset to merge very close facets (TODO: find test case for this)
    $safety_offset //= scale 0.0499;
    $slices = offset2_ex($slices, +$safety_offset, -$safety_offset);
    
    Slic3r::debugf "  %d surface(s) having %d holes detected from %d polylines\n",
        scalar(@$slices), scalar(map @{$_->holes}, @$slices), scalar(@$loops) if $Slic3r::debug;
    
    return map Slic3r::Surface->new(expolygon => $_, surface_type => S_TYPE_INTERNAL), @$slices;
}

sub make_perimeters {
    my $self = shift;
    
    my $perimeter_spacing   = $self->perimeter_flow->scaled_spacing;
    my $infill_spacing      = $self->solid_infill_flow->scaled_spacing;
    my $gap_area_threshold  = $self->perimeter_flow->scaled_width ** 2;
    
    $self->perimeters->clear;
    $self->fill_surfaces->clear;
    $self->thin_fills->clear;
    
    my @contours    = ();    # array of Polygons with ccw orientation
    my @holes       = ();    # array of Polygons with cw orientation
    my @gaps        = ();    # array of ExPolygons
    
    # we need to process each island separately because we might have different
    # extra perimeters for each one
    foreach my $surface (@{$self->slices}) {
        # detect how many perimeters must be generated for this island
        my $loop_number = $self->config->perimeters + ($surface->extra_perimeters || 0);
        
        # generate loops
        # (one more than necessary so that we can detect gaps even after the desired
        # number of perimeters has been generated)
        my @last = @{$surface->expolygon};
        my @this_gaps = ();
        for my $i (0 .. $loop_number) {
            # external loop only needs half inset distance
            my $spacing = ($i == 0)
                ? $perimeter_spacing / 2
                : $perimeter_spacing;
            
            my @offsets = @{offset2_ex(\@last, -1.5*$spacing,  +0.5*$spacing)};
            my @contours_offsets    = map $_->contour, @offsets;
            my @holes_offsets       = map @{$_->holes}, @offsets;
            @offsets = (@contours_offsets, @holes_offsets);     # turn @offsets from ExPolygons to Polygons
            
            # where offset2() collapses the expolygon, then there's no room for an inner loop
            # and we can extract the gap for later processing
            if ($Slic3r::Config->gap_fill_speed > 0 && $self->object->config->fill_density > 0) {
                my $diff = diff_ex(
                    offset(\@last, -0.5*$spacing),
                    # +2 on the offset here makes sure that Clipper float truncation 
                    # won't shrink the clip polygon to be smaller than intended.
                    offset(\@offsets, +0.5*$spacing + 2),
                );
                push @gaps, (@this_gaps = grep $_->area >= $gap_area_threshold, @$diff);
            }
            
            last if !@offsets || $i == $loop_number;
            push @contours, @contours_offsets;
            push @holes,    @holes_offsets;
            @last = @offsets;
        }
        
        # make sure we don't infill narrow parts that are already gap-filled
        # (we only consider this surface's gaps to reduce the diff() complexity)
        @last = @{diff(\@last, [ map @$_, @this_gaps ])};
        
        # create one more offset to be used as boundary for fill
        # we offset by half the perimeter spacing (to get to the actual infill boundary)
        # and then we offset back and forth by half the infill spacing to only consider the
        # non-collapsing regions
        $self->fill_surfaces->append(
            @{offset2_ex(
                [ map $_->simplify_as_polygons(&Slic3r::SCALED_RESOLUTION), @{union_ex(\@last)} ],
                -($perimeter_spacing/2 + $infill_spacing/2),
                +$infill_spacing/2,
            )}
        );
    }
    
    $self->_fill_gaps(\@gaps);
    
    # find nesting hierarchies separately for contours and holes
    my $contours_pt = union_pt(\@contours);
    my $holes_pt    = union_pt(\@holes);
    
    # prepare a coderef for traversing the PolyTree object
    # external contours are root items of $contours_pt
    # internal contours are the ones next to external
    my $traverse;
    $traverse = sub {
        my ($polynodes, $depth, $is_contour) = @_;
        
        # use a nearest neighbor search to order these children
        # TODO: supply second argument to chained_path_items() too?
        my @nodes = @{Slic3r::Geometry::chained_path_items(
            [ map [ Slic3r::Point->new(@{$_->{outer} ? $_->{outer}[0] : $_->{hole}[0]}), $_ ], @$polynodes ],
        )};
        
        my @loops = ();
        foreach my $polynode (@nodes) {
            push @loops, $traverse->($polynode->{children}, $depth+1, $is_contour);
            
            # return ccw contours and cw holes
            # GCode.pm will convert all of them to ccw, but it needs to know
            # what the holes are in order to compute the correct inwards move
            my $polygon = Slic3r::Polygon->new(defined $polynode->{outer} ? @{$polynode->{outer}} : reverse @{$polynode->{hole}});
            $polygon->reverse if !$is_contour;
            
            my $role = EXTR_ROLE_PERIMETER;
            if ($is_contour ? $depth == 0 : !@{ $polynode->{children} }) {
                # external perimeters are root level in case of contours
                # and items with no children in case of holes
                $role = EXTR_ROLE_EXTERNAL_PERIMETER;
            } elsif ($depth == 1 && $is_contour) {
                $role = EXTR_ROLE_CONTOUR_INTERNAL_PERIMETER;
            }
            
            push @loops, Slic3r::ExtrusionLoop->new(
                polygon         => $polygon,
                role            => $role,
                flow_spacing    => $self->perimeter_flow->spacing,
            );
        }
        return @loops;
    };
    
    # order loops from inner to outer (in terms of object slices)
    my @loops = (
        (reverse $traverse->($holes_pt, 0)),
        $traverse->($contours_pt, 0, 1),
    );
    
    # if brim will be printed, reverse the order of perimeters so that
    # we continue inwards after having finished the brim
    # TODO: add test for perimeter order
    @loops = reverse @loops
        if $Slic3r::Config->external_perimeters_first
            || ($self->layer->id == 0 && $Slic3r::Config->brim_width > 0);
    
    # append perimeters
    $self->perimeters->append(@loops);
    
    # add thin walls as perimeters
    push @{ $self->perimeters }, Slic3r::ExtrusionPath::Collection->new(
        map {
            Slic3r::ExtrusionPath->new(
                polyline        => ($_->isa('Slic3r::Polygon') ? $_->split_at_first_point : $_),
                role            => EXTR_ROLE_EXTERNAL_PERIMETER,
                flow_spacing    => $self->perimeter_flow->spacing,
            );
        } @{ $self->thin_walls }
    )->chained_path;
}

sub _fill_gaps {
    my $self = shift;
    my ($gaps) = @_;
    
    return unless @$gaps;
    
    my $filler = $self->layer->object->fill_maker->filler('rectilinear');
    $filler->layer_id($self->layer->id);
    
    # we should probably use this code to handle thin walls and remove that logic from
    # make_surfaces(), but we need to enable dynamic extrusion width before as we can't
    # use zigzag for thin walls.
    # in the mean time we subtract thin walls from the detected gaps so that we don't
    # reprocess them, causing overlapping thin walls and zigzag.
    @$gaps = @{diff_ex(
        [ map @$_, @$gaps ],
        [ map $_->grow($self->perimeter_flow->scaled_width), @{$self->{thin_walls}} ],
        1,
    )};
    
    # medial axis-based gap fill should benefit from detection of larger gaps too, so 
    # we could try with 1.5*$w for example, but that doesn't work well for zigzag fill
    # because it tends to create very sparse points along the gap when the infill direction
    # is not parallel to the gap (1.5*$w thus may only work well with a straight line)
    my $w = $self->perimeter_flow->width;
    my @widths = ($w, 0.4 * $w);  # worth trying 0.2 too?
    foreach my $width (@widths) {
        my $flow = $self->perimeter_flow->clone(width => $width);
        
        # extract the gaps having this width
        my @this_width = map @{$_->offset_ex(+0.5*$flow->scaled_width)},
            map @{$_->noncollapsing_offset_ex(-0.5*$flow->scaled_width)},
            @$gaps;
        
        if (0) {  # remember to re-enable t/dynamic.t
            # fill gaps using dynamic extrusion width, by treating them like thin polygons,
            # thus generating the skeleton and using it to fill them
            my %path_args = (
                role            => EXTR_ROLE_SOLIDFILL,
                flow_spacing    => $flow->spacing,
            );
            $self->thin_fills->append(map {
                $_->isa('Slic3r::Polygon')
                    ? Slic3r::ExtrusionLoop->new(polygon => $_, %path_args)->split_at_first_point  # we should keep these as loops
                    : Slic3r::ExtrusionPath->new(polyline => $_, %path_args),
            } map $_->medial_axis($flow->scaled_width), @this_width);
        
            Slic3r::debugf "  %d gaps filled with extrusion width = %s\n", scalar @this_width, $width
                if @{ $self->thin_fills };
            
        } else {
            # fill gaps using zigzag infill
            
            # since this is infill, we have to offset by half-extrusion width inwards
            my @infill = map @{$_->offset_ex(-0.5*$flow->scaled_width)}, @this_width;
            
            foreach my $expolygon (@infill) {
                my ($params, @paths) = $filler->fill_surface(
                    Slic3r::Surface->new(expolygon => $expolygon, surface_type => S_TYPE_INTERNALSOLID),
                    density         => 1,
                    flow_spacing    => $flow->spacing,
                );
                
                push @{ $self->thin_fills },
                    map {
                        $_->simplify($flow->scaled_width/3);
                        $_;
                    }
                    map Slic3r::ExtrusionPath->new(
                        polyline        => Slic3r::Polyline->new(@$_),
                        role            => EXTR_ROLE_GAPFILL,
                        height          => $self->height,
                        flow_spacing    => $params->{flow_spacing},
                    ),
                    # Split polylines into lines so that the chained_path() search
                    # at the final stage has more freedom and will choose starting
                    # points closer than last positions. OTOH, this will make such
                    # search slower. Probably, ExtrusionPath objects should support
                    # splitting nearby a given position so that we can choose the right
                    # entry point even in the middle of the path without needing a 
                    # complex, slow, chained_path() search on all segments. TODO.
                    # Such logic will also avoid all the small travel moves that this 
                    # line-splitting causes, and it will be applicable to other things
                    # too.
                    map Slic3r::Polyline->new(@$_)->lines,
                    @paths;
            }
        }
        
        # check what's left
        @$gaps = @{diff_ex(
            [ map @$_, @$gaps ],
            [ map @$_, @this_width ],
        )};
    }
}

sub prepare_fill_surfaces {
    my $self = shift;
    
    my @surfaces = @{$self->fill_surfaces};
    
    # if no solid layers are requested, turn top/bottom surfaces to internal
    if ($self->config->top_solid_layers == 0) {
        for my $i (0..$#surfaces) {
            next unless $surfaces[$i]->surface_type == S_TYPE_TOP;
            $self->fill_surfaces->set_surface_type($i, S_TYPE_INTERNAL);
        }
    }
    if ($self->config->bottom_solid_layers == 0) {
        for my $i (0..$#surfaces) {
            next unless $surfaces[$i]->surface_type == S_TYPE_BOTTOM;
            $self->fill_surfaces->set_surface_type($i, S_TYPE_INTERNAL);
        }
    }
        
    # turn too small internal regions into solid regions according to the user setting
    if ($self->config->fill_density > 0) {
        my $min_area = scale scale $self->config->solid_infill_below_area; # scaling an area requires two calls!
        for my $i (0..$#surfaces) {
            next unless $surfaces[$i]->surface_type == S_TYPE_INTERNAL && $surfaces[$i]->expolygon->contour->area <= $min_area;
            $self->fill_surfaces->set_surface_type($i, S_TYPE_INTERNALSOLID);
        }
    }
}

sub process_external_surfaces {
    my $self = shift;
    
    my $margin = scale &Slic3r::EXTERNAL_INFILL_MARGIN;
    
    my @bottom = ();
    foreach my $surface (grep $_->surface_type == S_TYPE_BOTTOM, @{$self->fill_surfaces}) {
        my $grown = $surface->expolygon->offset_ex(+$margin);
        
        # detect bridge direction before merging grown surfaces otherwise adjacent bridges
        # would get merged into a single one while they need different directions
        # also, supply the original expolygon instead of the grown one, because in case
        # of very thin (but still working) anchors, the grown expolygon would go beyond them
        my $angle = $self->id > 0
            ? $self->_detect_bridge_direction($surface->expolygon)
            : undef;
        
        push @bottom, map $surface->clone(expolygon => $_, bridge_angle => $angle), @$grown;
    }
    
    my @top = ();
    foreach my $surface (grep $_->surface_type == S_TYPE_TOP, @{$self->fill_surfaces}) {
        # give priority to bottom surfaces
        my $grown = diff_ex(
            $surface->expolygon->offset(+$margin),
            [ map $_->p, @bottom ],
        );
        push @top, map $surface->clone(expolygon => $_), @$grown;
    }
    
    # if we're slicing with no infill, we can't extend external surfaces
    # over non-existent infill
    my @fill_boundaries = $self->object->config->fill_density > 0
        ? @{$self->fill_surfaces}
        : grep $_->surface_type != S_TYPE_INTERNAL, @{$self->fill_surfaces};
    
    # intersect the grown surfaces with the actual fill boundaries
    my @new_surfaces = ();
    foreach my $group (Slic3r::Surface->group(@top, @bottom)) {
        push @new_surfaces,
            map $group->[0]->clone(expolygon => $_),
            @{intersection_ex(
                [ map $_->p, @$group ],
                [ map $_->p, @fill_boundaries ],
                1,  # to ensure adjacent expolygons are unified
            )};
    }
    
    # subtract the new top surfaces from the other non-top surfaces and re-add them
    my @other = grep $_->surface_type != S_TYPE_TOP && $_->surface_type != S_TYPE_BOTTOM, @{$self->fill_surfaces};
    foreach my $group (Slic3r::Surface->group(@other)) {
        push @new_surfaces, map $group->[0]->clone(expolygon => $_), @{diff_ex(
            [ map $_->p, @$group ],
            [ map $_->p, @new_surfaces ],
        )};
    }
    @{$self->fill_surfaces} = @new_surfaces;
}

sub _detect_bridge_direction {
    my $self = shift;
    my ($expolygon) = @_;
    
    my $grown = $expolygon->offset_ex(+$self->perimeter_flow->scaled_width);
    my @lower = @{$self->layer->object->layers->[ $self->id - 1 ]->slices};       # expolygons
    
    # detect what edges lie on lower slices
    my @edges = (); # polylines
    foreach my $lower (@lower) {
        # turn bridge contour and holes into polylines and then clip them
        # with each lower slice's contour
        my @clipped = map $_->split_at_first_point->clip_with_polygon($lower->contour), map @$_, @$grown;
        if (@clipped == 2) {
            # If the split_at_first_point() call above happens to split the polygon inside the clipping area
            # we would get two consecutive polylines instead of a single one, so we use this ugly hack to 
            # recombine them back into a single one in order to trigger the @edges == 2 logic below.
            # This needs to be replaced with something way better.
            if (points_coincide($clipped[0][0], $clipped[-1][-1])) {
                @clipped = (Slic3r::Polyline->new(@{$clipped[-1]}, @{$clipped[0]}));
            }
            if (points_coincide($clipped[-1][0], $clipped[0][-1])) {
                @clipped = (Slic3r::Polyline->new(@{$clipped[0]}, @{$clipped[1]}));
            }
        }
        push @edges, @clipped;
    }
    
    Slic3r::debugf "Found bridge on layer %d with %d support(s)\n", $self->id, scalar(@edges);
    return undef if !@edges;
    
    my $bridge_angle = undef;
    
    if (0) {
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output("bridge_$expolygon.svg",
            expolygons      => [ $expolygon ],
            red_expolygons  => [ @lower ],
            polylines       => [ @edges ],
        );
    }
    
    if (@edges == 2) {
        my @chords = map Slic3r::Line->new($_->[0], $_->[-1]), @edges;
        my @midpoints = map $_->midpoint, @chords;
        my $line_between_midpoints = Slic3r::Line->new(@midpoints);
        $bridge_angle = Slic3r::Geometry::rad2deg_dir($line_between_midpoints->direction);
    } elsif (@edges == 1) {
        # TODO: this case includes both U-shaped bridges and plain overhangs;
        # we need a trapezoidation algorithm to detect the actual bridged area
        # and separate it from the overhang area.
        # in the mean time, we're treating as overhangs all cases where
        # our supporting edge is a straight line
        if (@{$edges[0]} > 2) {
            my $line = Slic3r::Line->new($edges[0]->[0], $edges[0]->[-1]);
            $bridge_angle = Slic3r::Geometry::rad2deg_dir($line->direction);
        }
    } elsif (@edges) {
        # inset the bridge expolygon; we'll use this one to clip our test lines
        my $inset = [ $expolygon->offset_ex($self->infill_flow->scaled_width) ];
        
        # detect anchors as intersection between our bridge expolygon and the lower slices
        my $anchors = intersection_ex(
            [ @$grown ],
            [ map @$_, @lower ],
            undef,
            1,  # safety offset required to avoid Clipper from detecting empty intersection while Boost actually found some @edges
        );
        
        # we'll now try several directions using a rudimentary visibility check:
        # bridge in several directions and then sum the length of lines having both
        # endpoints within anchors
        my %directions = ();  # angle => score
        my $angle_increment = PI/36; # 5°
        my $line_increment = $self->infill_flow->scaled_width;
        for (my $angle = 0; $angle <= PI; $angle += $angle_increment) {
            # rotate everything - the center point doesn't matter
            $_->rotate($angle, [0,0]) for @$inset, @$anchors;
            
            # generate lines in this direction
            my $bounding_box = Slic3r::Geometry::BoundingBox->new_from_points([ map @$_, map @$_, @$anchors ]);
            
            my @lines = ();
            for (my $x = $bounding_box->x_min; $x <= $bounding_box->x_max; $x += $line_increment) {
                push @lines, [ [$x, $bounding_box->y_min], [$x, $bounding_box->y_max] ];
            }
            
            # TODO: use a multi_polygon_multi_linestring_intersection() call
            my @clipped_lines = map @{ Boost::Geometry::Utils::polygon_multi_linestring_intersection($_, \@lines) }, @$inset;
            
            # remove any line not having both endpoints within anchors
            @clipped_lines = grep {
                my $line = $_;
                !(first { $_->encloses_point_quick($line->a) } @$anchors)
                    && !(first { $_->encloses_point_quick($line->b) } @$anchors);
            } @clipped_lines;
            
            # sum length of bridged lines
            $directions{-$angle} = sum(map Slic3r::Geometry::line_length($_), @clipped_lines) // 0;
        }
        
        # this could be slightly optimized with a max search instead of the sort
        my @sorted_directions = sort { $directions{$a} <=> $directions{$b} } keys %directions;
        
        # the best direction is the one causing most lines to be bridged
        $bridge_angle = Slic3r::Geometry::rad2deg_dir($sorted_directions[-1]);
    }
    
    Slic3r::debugf "  Optimal infill angle of bridge on layer %d is %d degrees\n",
        $self->id, $bridge_angle if defined $bridge_angle;
    
    return $bridge_angle;
}

1;
