package Slic3r::GUI;
use strict;
use warnings;
use utf8;

use FindBin;
use Slic3r::GUI::AboutDialog;
use Slic3r::GUI::ConfigWizard;
use Slic3r::GUI::Plater;
use Slic3r::GUI::OptionsGroup;
use Slic3r::GUI::SkeinPanel;
use Slic3r::GUI::Tab;

use Wx 0.9901 qw(:bitmap :dialog :frame :icon :id :misc :systemsettings);
use Wx::Event qw(EVT_CLOSE EVT_MENU);
use base 'Wx::App';

my $growler;
our $datadir;

our $small_font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
$small_font->SetPointSize(11) if !&Wx::wxMSW;
our $medium_font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
$medium_font->SetPointSize(12);

sub OnInit {
    my $self = shift;
    
    $self->SetAppName('Slic3r');
    Slic3r::debugf "wxWidgets version %s\n", &Wx::wxVERSION_STRING;
    
    if (eval "use Growl::GNTP; 1") {
        # register growl notifications
        eval {
            $growler = Growl::GNTP->new(AppName => 'Slic3r', AppIcon => "$Slic3r::var/Slic3r.png");
            $growler->register([{Name => 'SKEIN_DONE', DisplayName => 'Slicing Done'}]);
        };
    }
    
    # locate or create data directory
    $datadir = Wx::StandardPaths::Get->GetUserDataDir;
    Slic3r::debugf "Data directory: %s\n", $datadir;
    my $run_wizard = (-d $datadir) ? 0 : 1;
    for ($datadir, "$datadir/print", "$datadir/filament", "$datadir/printer") {
        mkdir or $self->fatal_error("Slic3r was unable to create its data directory at $_ (errno: $!).")
            unless -d $_;
    }
    
    # load settings
    if (-f "$datadir/slic3r.ini") {
        my $ini = eval { Slic3r::Config->read_ini("$datadir/slic3r.ini") };
        $Slic3r::Settings = $ini if $ini;
    }
    
    # application frame
    Wx::Image::AddHandler(Wx::PNGHandler->new);
    my $frame = Wx::Frame->new(undef, -1, 'Slic3r', wxDefaultPosition, [760,520], wxDEFAULT_FRAME_STYLE);
    $frame->SetIcon(Wx::Icon->new("$Slic3r::var/Slic3r_128px.png", wxBITMAP_TYPE_PNG) );
    $frame->{skeinpanel} = Slic3r::GUI::SkeinPanel->new($frame);
    $self->SetTopWindow($frame);
    
    # status bar
    $frame->{statusbar} = Slic3r::GUI::ProgressStatusBar->new($frame, -1);
    $frame->SetStatusBar($frame->{statusbar});
    
    # File menu
    my $fileMenu = Wx::Menu->new;
    {
        $fileMenu->Append(1, "Load Config…");
        $fileMenu->Append(2, "Export Config…");
        $fileMenu->AppendSeparator();
        $fileMenu->Append(3, "Quick Slice…");
        $fileMenu->Append(4, "Quick Slice (last file)");
        $fileMenu->Append(5, "Quick Slice and Save As…");
        $fileMenu->AppendSeparator();
        $fileMenu->Append(6, "Slice to SVG…");
        $fileMenu->AppendSeparator();
        $fileMenu->Append(wxID_EXIT, "&Quit");
        EVT_MENU($frame, 1, sub { $frame->{skeinpanel}->load_config });
        EVT_MENU($frame, 2, sub { $frame->{skeinpanel}->save_config });
        EVT_MENU($frame, 3, sub { $frame->{skeinpanel}->do_slice });
        EVT_MENU($frame, 4, sub { $frame->{skeinpanel}->do_slice(reslice => 1) });
        EVT_MENU($frame, 5, sub { $frame->{skeinpanel}->do_slice(save_as => 1) });
        EVT_MENU($frame, 6, sub { $frame->{skeinpanel}->do_slice(save_as => 1, export_svg => 1) });
        EVT_MENU($frame, wxID_EXIT, sub {$_[0]->Close(0)});
    }
    
    # Help menu
    my $helpMenu = Wx::Menu->new;
    {
        $helpMenu->Append(7, "Configuration Wizard…");
        $helpMenu->Append(8, "Slic3r Website");
        $helpMenu->Append(wxID_ABOUT, "&About Slic3r");
        EVT_MENU($frame, 7, sub { $frame->{skeinpanel}->config_wizard });
        EVT_MENU($frame, 8, sub { Wx::LaunchDefaultBrowser('http://slic3r.org/') });
        EVT_MENU($frame, wxID_ABOUT, \&about);
    }
    
    # menubar
    # assign menubar to frame after appending items, otherwise special items
    # will not be handled correctly
    {
        my $menubar = Wx::MenuBar->new;
        $menubar->Append($fileMenu, "&File");
        $menubar->Append($helpMenu, "&Help");
        $frame->SetMenuBar($menubar);
    }
    
    EVT_CLOSE($frame, \&on_close);

    $frame->SetMinSize($frame->GetSize);
    $frame->Show;
    $frame->Layout;
    
    $frame->{skeinpanel}->config_wizard if $run_wizard;
    
    return 1;
}

sub about {
    my $frame = shift;
    
    my $about = Slic3r::GUI::AboutDialog->new($frame);
    $about->ShowModal;
    $about->Destroy;
}

sub on_close {
    my ($frame, $event) = @_;
    $event->CanVeto ? $event->Skip($frame->{skeinpanel}->check_unsaved_changes) : $event->Skip(1);
}

sub catch_error {
    my ($self, $cb, $message_dialog) = @_;
    if (my $err = $@) {
        $cb->() if $cb;
        my @params = ($err, 'Error', wxOK | wxICON_ERROR);
        $message_dialog
            ? $message_dialog->(@params)
            : Wx::MessageDialog->new($self, @params)->ShowModal;
        return 1;
    }
    return 0;
}

sub show_error {
    my $self = shift;
    my ($message) = @_;
    Wx::MessageDialog->new($self, $message, 'Error', wxOK | wxICON_ERROR)->ShowModal;
}

sub fatal_error {
    my $self = shift;
    $self->show_error(@_);
    exit 1;
}

sub warning_catcher {
    my ($self, $message_dialog) = @_;
    return sub {
        my $message = shift;
        my @params = ($message, 'Warning', wxOK | wxICON_WARNING);
        $message_dialog
            ? $message_dialog->(@params)
            : Wx::MessageDialog->new($self, @params)->ShowModal;
    };
}

sub notify {
    my ($message) = @_;

    eval {
        $growler->notify(Event => 'SKEIN_DONE', Title => 'Slicing Done!', Message => $message)
            if $growler;
    };
}

package Slic3r::GUI::ProgressStatusBar;
use Wx qw(:gauge :misc);
use base 'Wx::StatusBar';

sub new {
    my $class = shift;
    my $self = $class->SUPER::new(@_);
    
    $self->{busy} = 0;
    $self->{timer} = Wx::Timer->new($self);
    $self->{prog} = Wx::Gauge->new($self, wxGA_HORIZONTAL, 100, wxDefaultPosition, wxDefaultSize);
    $self->{prog}->Hide;
    $self->{cancelbutton} = Wx::Button->new($self, -1, "Cancel", wxDefaultPosition, wxDefaultSize);
    $self->{cancelbutton}->Hide;
    
    $self->SetFieldsCount(3);
    $self->SetStatusWidths(-1, 150, 155);
    
    Wx::Event::EVT_TIMER($self, \&OnTimer, $self->{timer});
    Wx::Event::EVT_SIZE($self, \&OnSize);
    Wx::Event::EVT_BUTTON($self, $self->{cancelbutton}, sub {
        $self->{cancel_cb}->();
        $self->{cancelbutton}->Hide;
    });
    
    return $self;
}

sub DESTROY {
    my $self = shift;    
    $self->{timer}->Stop if $self->{timer} && $self->{timer}->IsRunning;
}

sub OnSize {
    my ($self, $event) = @_;
    
    my %fields = (
        # 0 is reserved for status text
        1 => $self->{cancelbutton},
        2 => $self->{prog},
    );

    foreach (keys %fields) {
        my $rect = $self->GetFieldRect($_);
        my $offset = &Wx::wxGTK ? 1 : 0; # add a cosmetic 1 pixel offset on wxGTK
        my $pos = [$rect->GetX + $offset, $rect->GetY + $offset];
        $fields{$_}->Move($pos);
        $fields{$_}->SetSize($rect->GetWidth - $offset, $rect->GetHeight);
    }

    $event->Skip;
}

sub OnTimer {
    my ($self, $event) = @_;
    
    if ($self->{prog}->IsShown) {
        $self->{timer}->Stop;
    }
    $self->{prog}->Pulse if $self->{_busy};
}

sub SetCancelCallback {
    my $self = shift;
    my ($cb) = @_;
    $self->{cancel_cb} = $cb;
    $cb ? $self->{cancelbutton}->Show : $self->{cancelbutton}->Hide;
}

sub Run {
    my $self = shift;
    my $rate = shift || 100;
    if (!$self->{timer}->IsRunning) {
        $self->{timer}->Start($rate);
    }
}

sub GetProgress {
    my $self = shift;
    return $self->{prog}->GetValue;
}

sub SetProgress {
    my $self = shift;
    my ($val) = @_;
    if (!$self->{prog}->IsShown) {
        $self->ShowProgress(1);
    }
    if ($val == $self->{prog}->GetRange) {
        $self->{prog}->SetValue(0);
        $self->ShowProgress(0);
    } else {
        $self->{prog}->SetValue($val);
    }
}

sub SetRange {
    my $self = shift;
    my ($val) = @_;
    
    if ($val != $self->{prog}->GetRange) {
        $self->{prog}->SetRange($val);
    }
}

sub ShowProgress {
    my $self = shift;
    my ($show) = @_;
    
    $self->{prog}->Show($show);
    $self->{prog}->Pulse;
}

sub StartBusy {
    my $self = shift;
    my $rate = shift || 100;
    
    $self->{_busy} = 1;
    $self->ShowProgress(1);
    if (!$self->{timer}->IsRunning) {
        $self->{timer}->Start($rate);
    }
}

sub StopBusy {
    my $self = shift;
    
    $self->{timer}->Stop;
    $self->ShowProgress(0);
    $self->{prog}->SetValue(0);
    $self->{_busy} = 0;
}

sub IsBusy {
    my $self = shift;
    return $self->{_busy};
}

1;
