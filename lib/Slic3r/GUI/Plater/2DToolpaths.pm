package Slic3r::GUI::Plater::2DToolpaths;
use strict;
use warnings;
use utf8;

use List::Util qw();
use Slic3r::Geometry qw();
use Wx qw(:misc :sizer :slider);
use Wx::Event qw(EVT_SLIDER);
use base 'Wx::Panel';

sub new {
    my $class = shift;
    my ($parent, $print) = @_;
    
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition);
    
    # init print
    $self->{print} = $print;
    $self->{layers} = {};  # print_z => [ layer*, layer* ... ]
    foreach my $object (@{$print->objects}) {
        foreach my $layer (@{$object->layers}, @{$object->support_layers}) {
            $self->{layers}{$layer->print_z} //= [];
            push @{ $self->{layers}{$layer->print_z} }, $layer;
        }
    }
    $self->{layers_z} = [ sort { $a <=> $b } keys %{$self->{layers}} ];   # [ z, z ... ]
    
    # init GUI elements
    my $sizer = Wx::BoxSizer->new(wxHORIZONTAL);
    my $canvas = $self->{canvas} = Slic3r::GUI::Plater::2DToolpaths::Canvas->new($self, $print);
    $sizer->Add($canvas, 1, wxALL | wxEXPAND, 10);
    my $slider = $self->{slider} = Wx::Slider->new(
        $self, -1,
        0,                              # default
        0,                              # min
        scalar(@{$self->{layers_z}})-1,    # max
        wxDefaultPosition,
        wxDefaultSize,
        wxVERTICAL | wxSL_INVERSE,
    );
    $sizer->Add($slider, 0, wxALL | wxEXPAND, 10);
    
    EVT_SLIDER($self, $slider, sub {
        my $z = $self->{layers_z}[$slider->GetValue];
        $canvas->set_layers($self->{layers}{$z});
    });
    
    $self->SetSizer($sizer);
    $self->SetMinSize($self->GetSize);
    $sizer->SetSizeHints($self);
    
    $canvas->set_layers($self->{layers}{ $self->{layers_z}[0] });
    
    return $self;
}


package Slic3r::GUI::Plater::2DToolpaths::Canvas;

use Wx::Event qw(EVT_PAINT EVT_SIZE EVT_ERASE_BACKGROUND EVT_IDLE EVT_MOUSEWHEEL EVT_MOUSE_EVENTS);
use OpenGL qw(:glconstants :glfunctions :glufunctions);
use base qw(Wx::GLCanvas Class::Accessor);
use Wx::GLCanvas qw(:all);
use List::Util qw(min);
use Slic3r::Geometry qw(scale unscale);

__PACKAGE__->mk_accessors(qw(print layers init dirty bb));

# make OpenGL::Array thread-safe
{
    no warnings 'redefine';
    *OpenGL::Array::CLONE_SKIP = sub { 1 };
}

sub new {
    my ($class, $parent, $print) = @_;
    
    my $self = $class->SUPER::new($parent);
    $self->print($print);
    $self->bb($self->print->bounding_box);
    
    EVT_PAINT($self, sub {
        my $dc = Wx::PaintDC->new($self);
        $self->Render($dc);
    });
    EVT_SIZE($self, sub { $self->dirty(1) });
    EVT_IDLE($self, sub {
        return unless $self->dirty;
        return if !$self->IsShownOnScreen;
        $self->Resize( $self->GetSizeWH );
        $self->Refresh;
    });
    
    return $self;
}

sub set_layers {
    my ($self, $layers) = @_;
    
    $self->layers($layers);
    $self->dirty(1);
}

sub Render {
    my ($self, $dc) = @_;
    
    # prevent calling SetCurrent() when window is not shown yet
    return unless $self->IsShownOnScreen;
    return unless my $context = $self->GetContext;
    $self->SetCurrent($context);
    $self->InitGL;
    
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    my $bb = $self->bb;
    my ($x1, $y1, $x2, $y2) = ($bb->x_min, $bb->y_min, $bb->x_max, $bb->y_max);
    my ($x, $y) = $self->GetSizeWH;
    if (($x2 - $x1)/($y2 - $y1) > $x/$y) {
        # adjust Y
        my $new_y = $y * ($x2 - $x1) / $x;
        $y1 = ($y2 + $y1)/2 - $new_y/2;
        $y2 = $y1 + $new_y;
    } else {
        my $new_x = $x * ($y2 - $y1) / $y;
        $x1 = ($x2 + $x1)/2 - $new_x/2;
        $x2 = $x1 + $new_x;
    }
    glOrtho($x1, $x2, $y1, $y2, 0, 1);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
    glClearColor(1, 1, 1, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    
    foreach my $layer (@{$self->layers}) {
        my $object = $layer->object;
        foreach my $layerm (@{$layer->regions}) {
            glColor3f(0.7, 0, 0);
            $self->_draw_extrusionpath($object, $_) for @{$layerm->perimeters};
            
            glColor3f(0, 0, 0.7);
            $self->_draw_extrusionpath($object, $_) for map @$_, @{$layerm->fills};
        }
        
        if ($layer->isa('Slic3r::Layer::Support')) {
            glColor3f(0, 0, 0);
            $self->_draw_extrusionpath($object, $_) for @{$layer->support_fills};
            $self->_draw_extrusionpath($object, $_) for @{$layer->support_interface_fills};
        }
    }
    
    glFlush();
    $self->SwapBuffers;
}

sub _draw_extrusionpath {
    my ($self, $object, $path) = @_;
    
    my $polyline = $path->isa('Slic3r::ExtrusionLoop')
        ? $path->polygon->split_at_first_point
        : $path->polyline;
    
    glLineWidth(1);
    foreach my $copy (@{ $object->_shifted_copies }) {
        foreach my $line (@{$polyline->lines}) {
            $line->translate(@$copy);
            glBegin(GL_LINES);
            glVertex2f(@{$line->a});
            glVertex2f(@{$line->b});
            glEnd();
        }
    }
}

sub InitGL {
    my $self = shift;
 
    return if $self->init;
    return unless $self->GetContext;
    $self->init(1);
    
    
}

sub GetContext {
    my ($self) = @_;
    
    if (Wx::wxVERSION >= 2.009) {
        return $self->{context} ||= Wx::GLContext->new($self);
    } else {
        return $self->SUPER::GetContext;
    }
}
 
sub SetCurrent {
    my ($self, $context) = @_;
    
    if (Wx::wxVERSION >= 2.009) {
        return $self->SUPER::SetCurrent($context);
    } else {
        return $self->SUPER::SetCurrent;
    }
}

sub Resize {
    my ($self, $x, $y) = @_;
 
    return unless $self->GetContext;
    $self->dirty(0);
 
    $self->SetCurrent($self->GetContext);
    
    my ($x1, $y1, $x2, $y2) = (0, 0, $x, $y);
    if (0 && $x > $y) {
        $x2 = $y;
        $x1 = ($x - $y)/2;
    }
    if (0 && $y > $x) {
        $y2 = $x;
        $y1 = ($y - $x)/2;
    }
    glViewport($x1, $y1, $x2, $y2);
}

sub line {
    my (
        $x1, $y1, $x2, $y2,     # coordinates of the line
        $w,                     # width/thickness of the line in pixel
        $Cr, $Cg, $Cb,          # RGB color components
        $Br, $Bg, $Bb,          # color of background when alphablend=false
                                # Br=alpha of color when alphablend=true
        $alphablend,            # use alpha blend or not
    ) = @_;
    
    my $t;
    my $R;
    my $f = $w - int($w);
    my $A;
    
    if ($alphablend) {
        $A = $Br;
    } else {
        $A = 1;
    }
    
    # determine parameters t,R
    if ($w >= 0 && $w < 1) {
        $t = 0.05; $R = 0.48 + 0.32 * $f;
        if (!$alphablend) {
            $Cr += 0.88 * (1-$f);
            $Cg += 0.88 * (1-$f);
            $Cb += 0.88 * (1-$f);
            $Cr = 1.0 if ($Cr > 1.0);
            $Cg = 1.0 if ($Cg > 1.0);
            $Cb = 1.0 if ($Cb > 1.0);
        } else {
            $A *= $f;
        }
    } elsif ($w >= 1.0 && $w < 2.0) {
        $t = 0.05 + $f*0.33; $R = 0.768 + 0.312*$f;
    } elsif ($w >= 2.0 && $w < 3.0) {
        $t = 0.38 + $f*0.58; $R = 1.08;
    } elsif ($w >= 3.0 && $w < 4.0) {
        $t = 0.96 + $f*0.48; $R = 1.08;
    } elsif ($w >= 4.0 && $w < 5.0) {
        $t= 1.44 + $f*0.46; $R = 1.08;
    } elsif ($w >= 5.0 && $w < 6.0) {
        $t= 1.9 + $f*0.6; $R = 1.08;
    } elsif ($w >= 6.0) {
        my $ff = $w - 6.0;
        $t = 2.5 + $ff*0.50; $R = 1.08;
    }
    #printf( "w=%f, f=%f, C=%.4f\n", $w, $f, $C);
    
    # determine angle of the line to horizontal
    my $tx = 0; my $ty = 0; # core thinkness of a line
    my $Rx = 0; my $Ry = 0; # fading edge of a line
    my $cx = 0; my $cy = 0; # cap of a line
    my $ALW = 0.01;
    my $dx = $x2 - $x1;
    my $dy = $y2 - $y1;
    if (abs($dx) < $ALW) {
        # vertical
        $tx = $t; $ty = 0;
        $Rx = $R; $Ry = 0;
        if ($w > 0.0 && $w < 1.0) {
            $tx *= 8;
        } elsif ($w == 1.0) {
            $tx *= 10;
        }
    } elsif (abs($dy) < $ALW) {
        #horizontal
        $tx = 0; $ty = $t;
        $Rx = 0; $Ry = $R;
        if ($w > 0.0 && $w < 1.0) {
            $ty *= 8;
        } elsif ($w == 1.0) {
            $ty *= 10;
        }
    } else {
        if ($w < 3) { # approximate to make things even faster
            my $m = $dy/$dx;
            # and calculate tx,ty,Rx,Ry
            if ($m > -0.4142 && $m <= 0.4142) {
                # -22.5 < $angle <= 22.5, approximate to 0 (degree)
                $tx = $t * 0.1; $ty = $t;
                $Rx = $R * 0.6; $Ry = $R;
            } elsif ($m > 0.4142 && $m <= 2.4142) {
                # 22.5 < $angle <= 67.5, approximate to 45 (degree)
                $tx = $t * -0.7071; $ty = $t * 0.7071;
                $Rx = $R * -0.7071; $Ry = $R * 0.7071;
            } elsif ($m > 2.4142 || $m <= -2.4142) {
                # 67.5 < $angle <= 112.5, approximate to 90 (degree)
                $tx = $t; $ty = $t*0.1;
                $Rx = $R; $Ry = $R*0.6;
            } elsif ($m > -2.4142 && $m < -0.4142) {
                # 112.5 < angle < 157.5, approximate to 135 (degree)
                $tx = $t * 0.7071; $ty = $t * 0.7071;
                $Rx = $R * 0.7071; $Ry = $R * 0.7071;
            } else {
                # error in determining angle
                printf("error in determining angle: m=%.4f\n", $m);
            }
        } else {  # calculate to exact
            $dx= $y1 - $y2;
            $dy= $x2 - $x1;
            my $L = sqrt($dx*$dx + $dy*$dy);
            $dx /= $L;
            $dy /= $L;
            $cx = -0.6*$dy; $cy=0.6*$dx;
            $tx = $t*$dx; $ty = $t*$dy;
            $Rx = $R*$dx; $Ry = $R*$dy;
        }
    }

    # draw the line by triangle strip
    glBegin(GL_TRIANGLE_STRIP);
    if (!$alphablend) {
        glColor3f($Br, $Bg, $Bb);
    } else {
        glColor4f($Cr, $Cg, $Cb, 0);
    }
    glVertex2f($x1 - $tx - $Rx, $y1 - $ty - $Ry);   # fading edge
    glVertex2f($x2 - $tx - $Rx, $y2 - $ty - $Ry);
    
    if (!$alphablend) {
        glColor3f($Cr, $Cg, $Cb);
    } else {
        glColor4f($Cr, $Cg, $Cb, $A);
    }
    glVertex2f($x1 - $tx, $y1 - $ty); # core
    glVertex2f($x2 - $tx, $y2 - $ty);
    glVertex2f($x1 + $tx, $y1 + $ty);
    glVertex2f($x2 + $tx, $y2 + $ty);
    
    if ((abs($dx) < $ALW || abs($dy) < $ALW) && $w <= 1.0) {
        # printf("skipped one fading edge\n");
    } else {
        if (!$alphablend) {
            glColor3f($Br, $Bg, $Bb);
        } else {
            glColor4f($Cr, $Cg, $Cb, 0);
        }
        glVertex2f($x1 + $tx+ $Rx, $y1 + $ty + $Ry);    # fading edge
        glVertex2f($x2 + $tx+ $Rx, $y2 + $ty + $Ry);
    }
    glEnd();

    # cap
    if ($w < 3) {
        # do not draw cap
    } else {
        # draw cap
        glBegin(GL_TRIANGLE_STRIP);
        if (!$alphablend) {
            glColor3f($Br, $Bg, $Bb);
        } else {
            glColor4f($Cr, $Cg, $Cb, 0);
        }
        glVertex2f($x1 - $Rx + $cx, $y1 - $Ry + $cy);
        glVertex2f($x1 + $Rx + $cx, $y1 + $Ry + $cy);
        glColor3f($Cr, $Cg, $Cb);
        glVertex2f($x1 - $tx - $Rx, $y1 - $ty - $Ry);
        glVertex2f($x1 + $tx + $Rx, $y1 + $ty + $Ry);
        glEnd();
        glBegin(GL_TRIANGLE_STRIP);
        if (!$alphablend) {
            glColor3f($Br, $Bg, $Bb);
        } else {
            glColor4f($Cr, $Cg, $Cb, 0);
        }
        glVertex2f($x2 - $Rx - $cx, $y2 - $Ry - $cy);
        glVertex2f($x2 + $Rx - $cx, $y2 + $Ry - $cy);
        glColor3f($Cr, $Cg, $Cb);
        glVertex2f($x2 - $tx - $Rx, $y2 - $ty - $Ry);
        glVertex2f($x2 + $tx + $Rx, $y2 + $ty + $Ry);
        glEnd();
    }
}


package Slic3r::GUI::Plater::2DToolpaths::Dialog;

use Wx qw(:dialog :id :misc :sizer);
use Wx::Event qw(EVT_CLOSE);
use base 'Wx::Dialog';

sub new {
    my $class = shift;
    my ($parent, $print) = @_;
    my $self = $class->SUPER::new($parent, -1, "Toolpaths", wxDefaultPosition, [500,500], wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    $sizer->Add(Slic3r::GUI::Plater::2DToolpaths->new($self, $print), 1, wxEXPAND, 0);
    $self->SetSizer($sizer);
    $self->SetMinSize($self->GetSize);
    
    # needed to actually free memory
    EVT_CLOSE($self, sub {
        $self->EndModal(wxID_OK);
        $self->Destroy;
    });
    
    return $self;
}

1;
