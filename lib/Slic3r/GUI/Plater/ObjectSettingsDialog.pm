package Slic3r::GUI::Plater::ObjectSettingsDialog;
use strict;
use warnings;
use utf8;

use Wx qw(:dialog :id :misc :sizer :systemsettings :notebook wxTAB_TRAVERSAL);
use Wx::Event qw(EVT_BUTTON);
use base 'Wx::Dialog';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, "Settings for " . $params{object}->name, wxDefaultPosition, [700,500], wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    $self->{$_} = $params{$_} for keys %params;
    
    $self->{tabpanel} = Wx::Notebook->new($self, -1, wxDefaultPosition, wxDefaultSize, wxNB_TOP | wxTAB_TRAVERSAL);
    $self->{tabpanel}->AddPage($self->{settings} = Slic3r::GUI::Plater::ObjectDialog::SettingsTab->new($self->{tabpanel}), "Settings");
    $self->{tabpanel}->AddPage($self->{layers} = Slic3r::GUI::Plater::ObjectDialog::LayersTab->new($self->{tabpanel}), "Layers");
    $self->{tabpanel}->AddPage($self->{parts} = Slic3r::GUI::Plater::ObjectPartsPanel->new($self->{tabpanel}, model_object => $params{model_object}), "Parts");
    $self->{tabpanel}->AddPage($self->{materials} = Slic3r::GUI::Plater::ObjectDialog::MaterialsTab->new($self->{tabpanel}), "Materials");
    
    my $buttons = $self->CreateStdDialogButtonSizer(wxOK);
    EVT_BUTTON($self, wxID_OK, sub {
        # validate user input
        return if !$self->{settings}->CanClose;
        return if !$self->{layers}->CanClose;
        
        # notify tabs
        $self->{layers}->Closing;
        $self->{materials}->Closing;
        
        $self->EndModal(wxID_OK);
        $self->Destroy;
    });
    
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    $sizer->Add($self->{tabpanel}, 1, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 10);
    $sizer->Add($buttons, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
    
    $self->SetSizer($sizer);
    $self->SetMinSize($self->GetSize);
    
    return $self;
}

package Slic3r::GUI::Plater::ObjectDialog::BaseTab;
use base 'Wx::Panel';

sub model_object {
    my ($self) = @_;
    return $self->GetParent->GetParent->{model_object};
}

package Slic3r::GUI::Plater::ObjectDialog::SettingsTab;
use Wx qw(:dialog :id :misc :sizer :systemsettings :button :icon);
use Wx::Grid;
use Wx::Event qw(EVT_BUTTON);
use base 'Slic3r::GUI::Plater::ObjectDialog::BaseTab';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize);
    
    $self->{sizer} = Wx::BoxSizer->new(wxVERTICAL);
    
    # descriptive text
    {
        my $label = Wx::StaticText->new($self, -1, "You can use this section to override some settings just for this object.",
            wxDefaultPosition, [-1, 25]);
        $label->SetFont(Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
        $self->{sizer}->Add($label, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 10);
    }
    
    $self->{settings_panel} = Slic3r::GUI::Plater::OverrideSettingsPanel->new(
        $self,
        config => $self->model_object->config,
        opt_keys => [ map @{$_->get_keys}, Slic3r::Config::PrintObject->new, Slic3r::Config::PrintRegion->new ],
    );
    $self->{sizer}->Add($self->{settings_panel}, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);
    
    $self->SetSizer($self->{sizer});
    $self->{sizer}->SetSizeHints($self);
    
    return $self;
}

sub CanClose {
    my $self = shift;
    
    # validate options before allowing user to dismiss the dialog
    # the validate method only works on full configs so we have
    # to merge our settings with the default ones
    my $config = Slic3r::Config->merge($self->GetParent->GetParent->GetParent->GetParent->GetParent->config, $self->model_object->config);
    eval {
        $config->validate;
    };
    return 0 if Slic3r::GUI::catch_error($self);    
    return 1;
}

package Slic3r::GUI::Plater::ObjectDialog::LayersTab;
use Wx qw(:dialog :id :misc :sizer :systemsettings);
use Wx::Grid;
use Wx::Event qw(EVT_GRID_CELL_CHANGED);
use base 'Slic3r::GUI::Plater::ObjectDialog::BaseTab';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize);
    
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    
    {
        my $label = Wx::StaticText->new($self, -1, "You can use this section to override the default layer height for parts of this object. Set layer height to zero to skip portions of the input file.",
            wxDefaultPosition, [-1, 40]);
        $label->SetFont(Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
        $sizer->Add($label, 0, wxEXPAND | wxALL, 10);
    }
    
    my $grid = $self->{grid} = Wx::Grid->new($self, -1, wxDefaultPosition, wxDefaultSize);
    $sizer->Add($grid, 1, wxEXPAND | wxALL, 10);
    $grid->CreateGrid(0, 3);
    $grid->DisableDragRowSize;
    $grid->HideRowLabels if &Wx::wxVERSION_STRING !~ / 2\.8\./;
    $grid->SetColLabelValue(0, "Min Z (mm)");
    $grid->SetColLabelValue(1, "Max Z (mm)");
    $grid->SetColLabelValue(2, "Layer height (mm)");
    $grid->SetColSize($_, 135) for 0..2;
    $grid->SetDefaultCellAlignment(wxALIGN_CENTRE, wxALIGN_CENTRE);
    
    # load data
    foreach my $range (@{ $self->model_object->layer_height_ranges }) {
        $grid->AppendRows(1);
        my $i = $grid->GetNumberRows-1;
        $grid->SetCellValue($i, $_, $range->[$_]) for 0..2;
    }
    $grid->AppendRows(1); # append one empty row
    
    EVT_GRID_CELL_CHANGED($grid, sub {
        my ($grid, $event) = @_;
        
        # remove any non-numeric character
        my $value = $grid->GetCellValue($event->GetRow, $event->GetCol);
        $value =~ s/,/./g;
        $value =~ s/[^0-9.]//g;
        $grid->SetCellValue($event->GetRow, $event->GetCol, $value);
        
        # if there's no empty row, let's append one
        for my $i (0 .. $grid->GetNumberRows-1) {
            if (!grep $grid->GetCellValue($i, $_), 0..2) {
                return;
            }
        }
        $grid->AppendRows(1);
    });
    
    $self->SetSizer($sizer);
    $sizer->SetSizeHints($self);
    
    return $self;
}

sub CanClose {
    my $self = shift;
    
    # validate ranges before allowing user to dismiss the dialog
    
    foreach my $range ($self->_get_ranges) {
        my ($min, $max, $height) = @$range;
        if ($max <= $min) {
            Slic3r::GUI::show_error($self, "Invalid Z range $min-$max.");
            return 0;
        }
        if ($min < 0 || $max < 0) {
            Slic3r::GUI::show_error($self, "Invalid Z range $min-$max.");
            return 0;
        }
        if ($height < 0) {
            Slic3r::GUI::show_error($self, "Invalid layer height $height.");
            return 0;
        }
        # TODO: check for overlapping ranges
    }
    
    return 1;
}

sub Closing {
    my $self = shift;
    
    # save ranges into the plater object
    $self->model_object->layer_height_ranges([ $self->_get_ranges ]);
}

sub _get_ranges {
    my $self = shift;
    
    my @ranges = ();
    for my $i (0 .. $self->{grid}->GetNumberRows-1) {
        my ($min, $max, $height) = map $self->{grid}->GetCellValue($i, $_), 0..2;
        next if $min eq '' || $max eq '' || $height eq '';
        push @ranges, [ $min, $max, $height ];
    }
    return sort { $a->[0] <=> $b->[0] } @ranges;
}

package Slic3r::GUI::Plater::ObjectDialog::MaterialsTab;
use Wx qw(:dialog :id :misc :sizer :systemsettings :button :icon);
use Wx::Grid;
use Wx::Event qw(EVT_BUTTON);
use base 'Slic3r::GUI::Plater::ObjectDialog::BaseTab';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize);
    $self->{object} = $params{object};
    
    $self->{sizer} = Wx::BoxSizer->new(wxVERTICAL);
    
    # descriptive text
    {
        my $label = Wx::StaticText->new($self, -1, "In this section you can assign object materials to your extruders.",
            wxDefaultPosition, [-1, 25]);
        $label->SetFont(Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
        $self->{sizer}->Add($label, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 10);
    }
    
    # get unique materials used in this object
    $self->{materials} = [ $self->model_object->unique_materials ];
    
    # get the current mapping
    $self->{mapping} = {};
    foreach my $material_id (@{ $self->{materials}}) {
        my $config = $self->model_object->model->materials->{ $material_id }->config;
        $self->{mapping}{$material_id} = ($config->perimeter_extruder // 0) + 1;
    }
    
    if (@{$self->{materials}} > 0) {
        # build an OptionsGroup
        my $optgroup = Slic3r::GUI::OptionsGroup->new(
            parent      => $self,
            title       => 'Extruders',
            label_width => 300,
            options => [
                map {
                    my $i           = $_;
                    my $material_id = $self->{materials}[$i];
                    {
                        opt_key     => "material_extruder_$_",
                        type        => 'i',
                        label       => $self->model_object->model->get_material_name($material_id),
                        min         => 1,
                        default     => $self->{mapping}{$material_id} // 1,
                        on_change   => sub { $self->{mapping}{$material_id} = $_[0] },
                    }
                } 0..$#{ $self->{materials} }
            ],
        );
        $self->{sizer}->Add($optgroup->sizer, 0, wxEXPAND | wxALL, 10);
    } else {
        my $label = Wx::StaticText->new($self, -1, "This object does not contain named materials.",
            wxDefaultPosition, [-1, 25]);
        $label->SetFont(Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
        $self->{sizer}->Add($label, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 10);
    }
    
    $self->SetSizer($self->{sizer});
    $self->{sizer}->SetSizeHints($self);
    
    return $self;
}

sub Closing {
    my $self = shift;
    
    # save mappings into the plater object
    foreach my $volume (@{$self->model_object->volumes}) {
        if (defined $volume->material_id) {
            my $config = $self->model_object->model->materials->{ $volume->material_id }->config;
            
            # temporary hack for handling volumes added after the window was launched
            $self->{mapping}{ $volume->material_id } //= 0;
            
            $config->set('extruder', $self->{mapping}{ $volume->material_id }-1);
        }
    }
}

1;
