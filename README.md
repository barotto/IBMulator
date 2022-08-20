# IBMulator

* [WHAT IS IBMULATOR?](#what-is-ibmulator)
* [LICENSE](#license)
* [HARDWARE REQUIREMENTS](#hardware-requirements)
* [USAGE](#usage)
  * [Installation](#installation)
  * [CMOS and system configuration](#cmos-and-system-configuration)
  * [ROM set](#rom-set)
  * [HDD image](#hdd-image)
  * [Floppy disk images](#floppy-disk-images)
    * [High-level raw sector based emulation](#high-level-raw-sector-based-emulation)
    * [Low-level flux based emulation](#low-level-flux-based-emulation)
  * [GUI modes](#gui-modes)
    * [Integer scaling](#integer-scaling)
  * [Savestates](#savestates)
    * [Limitations](#limitations)
  * [Audio DSP filters](#audio-dsp-filters)
  * [Joystick](#joystick)
  * [Emulation speed adjustments](#emulation-speed-adjustments)
  * [Serial port](#serial-port)
    * [Null modem connections](#null-modem-connections)
  * [MIDI output](#midi-output)
    * [MIDI on Windows](#midi-on-windows)
    * [MIDI on Linux](#midi-on-linux)
    * [SysEx delays and the Roland MT-32 "Exc. Buffer overflow" error](#sysex-delays-and-the-roland-mt-32-exc-buffer-overflow-error)
  * [Keymaps](#keymaps)
    * [Default key bindings](#default-key-bindings)
  * [UI related key bindings](#ui-related-key-bindings)
  * [Command line options](#command-line-options)
* [COMPILING](#compiling)
* [THANKS](#thanks)


## WHAT IS IBMULATOR?

IBMulator is a free/libre, open source IBM PS/1 emulator, able to run with the
original ROM. The goal is to create a faithful simulator capable of recreating
the look and feel of the real machine.

IBMulator can emulate the following systems:
- IBM PS/1 model 2011 (286 @ 10MHz)
- IBM PS/1 model 2121 (386SX @ 16MHz ~ 20MHz)

In order to use the program you'll need the original ROM, which is copyrighted
by IBM. You won't find it distributed with this package.

See the **[project site](https://barotto.github.io/IBMulator)** for screenshots,
videos, FAQs, and additional information.


## LICENSE

IBMulator is distributed under the GNU GPL version 3 or (at your option) any 
later version. See COPYING for details.

To obtain the source code go to https://github.com/barotto/IBMulator


## HARDWARE REQUIREMENTS

A 64-bit Linux or Windows operating system and a 2GHz dual core processor.

At this stage of development optimizations have very low priority, so a
modern-ish CPU and a discrete graphics card are recommended.

For shaders support you'll also need an OpenGL 3.3 compatible graphics adapter.


## USAGE

### Installation

First of all obtain the original ROM. You have the following options:

1. if you have a real PS/1, take the program `ROMDUMP.EXE` in the 'extra' folder
and put it in an empty floppy disk; insert the floppy disk in your PS/1 and
launch the executable: it will create the ROM image on the floppy disk
2. or open your PS/1, extract the EPROMs and read them with an EPROM 
reader (you also need to merge the 2 halves in 1 file, or 4 in 2 if you have a 
non-US model)
3. or scour the Internet (there are various ROM sets available.)

Launch IBMulator. A window will inform you that the file **ibmulator.ini** has 
been created and where it is placed.

Put the ROM set anywhere you like (inside the same directory of ibmulator.ini is
a good place) and update the ibmulator.ini `[system]:romset` setting with the
file name of the ROM set.

From now on IBMulator is ready to run.

You're not required to do anything else but IBMulator is very configurable. For
more information regarding the various configuration options, see the comments 
inside ibmulator.ini.

### CMOS and system configuration

Being a faithful emulator of the PS/1, after a configuration change (for example
if you add more RAM) you need to update the PS/1's CMOS data, otherwise you'll
get various POST errors (162, 164) at boot.  
Modern PCs have a built-in BIOS menu but unfortunately the PS/1 relies on a two
DOS programs:

* `CONFIGUR.EXE`: the system configuration updater
* `CUSTOMIZ.EXE`: used to customize the way the system works

You can usually find these programs inside the DOS directory after you restore
the IBM's original preloaded software from a backup disk-set.  
Both files are copyrighted so you won't find them bundled with IBMulator.

### ROM set

Unless you download a ready-to-go PS/1 ROM set, you have to prepare ROM files in
a specific way.

A ROM set can be either:

1. a compressed archive in the ZIP format
2. or a file with the *.BIN extension, named as you like
3. or a directory 

Inside a ZIP file or directory there must be (file names are case insensitive):

* `FC0000.BIN`: the system BIOS ROM, 256KiB
* `F80000.BIN`: the regional ROM, 256KiB, optional, only for non-US versions.
For international models, this bin file can be merged with FC0000.BIN to form a
single 512KiB bin file. In this case FC0000.BIN, if present, is ignored.

Any other file present in the archive or directory is ignored.

### HDD image

The first time you launch IBMulator an empty pre-formatted hard disk image will
be created.

If you have an original PS/1 backup disk-set you can restore the machine to its 
factory state. In order to do so:

1. insert a PC-DOS 4.0 floppy disk in drive A
2. go to the DOS command prompt
3. run "a:restore a: c: /s"

Under Linux you can mount the HDD image using this command:
```
$ udisksctl loop-setup -f hdd.img
```

In order to mount partitioned loop devices, you'll probably require to add this
kernel parameter to your grub configuration:
```
loop.max_part=31
```

Alternatively you can use the mount command:
```
$ sudo mount -o loop,offset=16896 hdd.img /mnt/loop
```

The offset value is equal to "start sector"*512. The start sector value can be 
determined with:
```
$ fdisk -l hdd.img
```
*Note*: if you use the custom HDD type 47, the automatically created image will
be 0-filled and you'll need to use 'fdisk' and 'format' in order to use it.

### Floppy disk images

IBMulator has two types of floppy drive emulations: raw sector based and flux
based.

You can create new floppy disk images using the floppy disk image selector
window. The types of images available depend on the type of emulation used.
All new images are pre-formatted.

#### High-level raw sector based emulation

This is the fastest type and is enabled by default. With this type only stardard
DOS-formatted images can be used (512 bytes per sector). The supported file
formats are:
* Raw sector image (*.img, *.ima) read/write
* ImageDisk (*.imd) read-only (standard DOS-formatted disks only)

You usually want to use this type unless you need to use copy-protected floppy
disks or want to experiment with low-level floppy emulation.

This type is enabled by setting the following option in ibmulator.ini:
```
[drives]
fdc_type=raw
```

#### Low-level flux based emulation

This is the more precise and computationally heavy type. With this type you can 
use non-standard floppy images with arbitrary layouts and sector sizes. The
supported file formats are:
* Raw sector image (*.img, *.ima) read/write
* HxC Floppy Emulator HFE v.1 (*.hfe) read/write
* SPS IPF (*.ipf) read-only
* TeleDisk TD0 (*.td0) read-only
* ImageDisk (*.imd) read-only

You should use this type to make copy-protected floppy disks work. Except for
raw sector image, the other supported formats allow to emulate various types of
copy protection.

This type is enabled by setting the following option in ibmulator.ini:
```
[drives]
fdc_type=flux
```

### GUI modes

IBMulator has 3 different GUI modes.

* **Compact**: in this mode the VGA image fills the available window space and 
the control panel, in the shape of a semi-transparent bar or the PS/1 system
unit, auto-hides after a while and disappears when input is grabbed. Use this
mode if you want an experience similar to DOSBox.
* **Normal**: this is the default mode. The control panel / system unit places 
itself at the bottom of the VGA display and is always visible.
* **Realistic**: this is the hardcore mode, for the retro enthusiasts who want 
to truly experience the PS/1. In this mode the system is rendered in its 
entirety, monitor included. There are no additional buttons and controls except 
the originals: volume, brightness, contrast, power, and floppy (you need to use 
the key bindings for any extra function). Two styles are available in this mode:
"bright" (daytime) and "dark" (nighttime). You can also zoom in to the monitor
to have a better view. See ibmulator.ini and the Key bindings section for more
info.

You can select the starting GUI mode with the `[gui]:mode` setting of
ibmulator.ini. You can also switch between Compact and Normal modes while using
IBMulator (see below for the default key binding).

#### Integer scaling

If you like the pixel art style of the early '90 (who doesn't) you might want to
enjoy it with the crispiest possible image quality.  
In order to achieve image perfection, in _Normal_ and _Compact_ GUI modes you
can use integer scaling.

To enable integer scaling set these variables in ibmulator.ini:
```
[gui]
mode=normal ; or compact

[display]
normal_scale=integer
normal_filter=nearest
normal_aspect=area
```
Other options are available for `normal_aspect` to try and force the image to a
particular shape (see comments in ibmulator.ini for more info).

### Savestates

IBMulator supports multiple savestates. Every savestate is stored in a folder
(slot) called `savestate_xxxx` where `xxxx` is a number (except for the
"quick" slot) inside the `capture` folder.

In every folder/slot there are various files that describe the savestate:

* `state.bin`: the actual binary state of the machine (CPU registers, RAM, I/O
devices, ...)
* `state.ini`: the machine's configuration (not to be modified)
* `state.png`: the VGA buffer image
* `state.txt`: information about the savestate, ie. version, description, and a
summary of the machine's configuration
* `state-hdd.img`: the image file of the installed HDD

Only a subset of the settings memorized in `state.ini` are used to load a state,
specifically those related to the hardware configuration. Any other setting
pertaining the program (GUI, mixer, ...) are kept from the originally loaded
ibmulator.ini.

By default any modification to a savestate's HDD image is discarded after a new
savestate is loaded or when IBMulator is closed.  
If you want the modifications to be permanent set `[hdd]:save` to `yes` so that
the currently loaded HDD image can be used to overwrite the original image file,
the path of which is memorized in `state.ini`.  

#### Limitations

1. Floppy disks are not saved like HDDs are. Saving a state while floppy disks
are actively written to is not recommended. This will be addressed in a future
version of IBMulator.
2. Null modem connections cannot be restored (see below).

### Audio DSP filters

Sound cards' channels can be filtered with IIR filters.

A filter is defined by a string containing the filter name followed by its
parameters separated by commas, like so:
```
LowPass,order=5,cutoff=5000
```
Multiple filters can be concatenated with the '|' character, like so:
```
LowPass,order=3,cutoff=4000|HighPass,order=10,cutoff=500
```
A parameter's value is specified by an integer or real number. This is the list
of available parameters:

| Parameter name | Description           |
| -------------- | --------------------- |
| order          | Filter's order (1-50) |
| cutoff         | Cutoff frequency (Hz) |
| center         | Center frequency (Hz) |
| bw             | Bandwidth (Hz)        |
| gain           | Gain (dB)             |

This is the list of available filters with their accepted parameters:

| Filter name | Parameters               |
| ----------- | -------------------------|
| LowPass     | order, cutoff            |
| HighPass    | order, cutoff            |
| BandPass    | order, center, bw        |
| BandStop    | order, center, bw        |
| LowShelf    | order, cutoff, gain      |
| HighShelf   | order, cutoff, gain      |
| BandShelf   | order, center, bw, gain  |

The implemented DSP filter type is the Butterworth filter, a description of
which can be found on
[Wikipedia](https://en.wikipedia.org/wiki/Butterworth_filter).

A possible filter combination to emulate the response of the typical PC speaker
could be:
```
LowPass,order=5,cutoff=5000|HighPass,order=5,cutoff=500
```

The most effective values depend on your particular audio setup so you'll have
to experiment.

### Joystick

IBMulator supports Game Port emulation with dual 2-axes / 2-buttons joysticks.

If your game controllers are already connected when IBMulator starts,
joysticks A & B will be mapped according to the order by which the SDL library
reports them.

Otherwise, joysticks A & B mapping will depend by the order you plug your game
controllers in your system: the first one will be joystick A, the second one
joystick B, and any subsequent controller will be ignored.

The axes and buttons mapping can be specified in the `keymap.map` file.

### Emulation speed adjustments

The entire machine's emulation speed can be altered with CTRL+F11 (slow down)
and CTRL+F12 (speed up). This is equivalent to warping time and it can go as low
as 0.01% and as high as 500% the normal speed.

An indicator in the upper right corner of the screen will show the current speed
when different from 100%. When the indicator is shown, video rendering will be
desynchronized and tearing will be visible despite the vsync setting. Sound from
emulated audio cards will accelerate or decelerate accordingly as well.

If capturing is enabled the resulting files will be at 100% speed regardless,
without stuttering.

The speed actually achievable depends on how fast your PC is. Keep in mind that
the higher the emulated CPU core frequency is, the hardest it is to then
accelerate it.

### Serial port

The serial port can be configured to connect to various devices via the
`[serial]:mode` configuration option:

* `auto`: the attached device is either a mouse or a dummy device without
input/output; to connect a serial mouse set `[gui]:mouse` to `serial`.
* `dummy`: dummy connection with no I/O.
* `file`: dump the serial output to a file; the `dev` parameter must be set with
the path to the file.
* `term`: terminal connection for Linux systems. This can be a real serial
line, or a pty. To use a pty launch a terminal emulator (eg. xterm), launch the
`tty` command and use the result as the `dev` parameter.
* `net-server`: network server that accepts incoming connections; the `dev`
paramenter must be set with the address and port to listen to in the form
`address:port`, for example `dev=192.168.1.100:6667`.
* `net-client`: network client that connects to a network server at launch; the 
`dev` paramenter must be set with the address and port to connect to in the form
`address:port`, for example `dev=192.168.1.100:6667`.

If you need to use both a mouse and a serial device, you have to configure a
PS/2 mouse in `[gui]:mouse` (default).

The serial port will keep connections open when a state is restored. The only
exception is for the serial mouse, which takes precedence and will always be
reconnected if the machine is configured with one when a state is saved.
In this case, any open connection will be closed before the serial mouse is
reconnected.

#### Null modem connections

`net-server` and `net-client` modes can be used to create a null modem
connection between two instances of IBMulator or between IBMulator and DOSBox.

To establish a network connection with DOSBox you need to configure DOSBox's
serial port with `transparent:1`, for example:

```
serial1=nullmodem server:192.168.1.100 port:6667 transparent:1
```

Network connections with emulators other than DOSBox, while not tested are still
expected to work, provided no data is transmitted other than what is generated
by the running guest program.

It's currently not possible to use programs that rely on hardware handshaking
to operate.

When IBMulator is configured as a server it will remain listening for incoming
connections on the configured address:port after it is started, and will start
listening again after a client disconnects.  
If IBMulator is configured as a client and it fails to connect to a server, it
has to be restarted to retry the connection.

You can enable the status indicators with `[gui]:show_indicators` or `SHIFT+F4`
to see the current status of the connection. When a connection is successfully
established the `NET` indicator will appear green.

To reduce latency and improve responsiveness you can use two additional
configuration options:
* `tx_delay`: data is accumulated and sent every this amount of milliseconds
(default: `20`); the higher this value the higher the latency, but reducing this
too much will also increase the network load.
* `tcp_nodelay`: if `yes` the TCP_NODELAY socket option of the host OS will be
enabled, which will disable the Nagle's algorithm (default: `yes`).

To reduce the chances of desynchronization try using the same configuration for
both client and server.

Network modes have some limitations:
* changing the emulator's speed will probably result in desynchronization;
* savestates won't work while the serial port is in use, as it's currently not
possible to save and restore the same state on both the client and the server
(you can always save just before starting using the port, ie. just before
establishing a null modem connection in a game).

### MIDI output

In order to hear MIDI music you need to use an external sequencer, either
software (FluidSynth, Munt, etc.) or hardware.

The MIDI device must be connected to the host system before IBMulator starts.

#### MIDI on Windows

The `device` key in the `[midi]` ini section should be set either with the MIDI
device number you want to use or its name.

For example:
```
device=1
```
or
```
device=MT-32 Synth Emulator
```

If left empty then the default device #0 is used, which usually is the
Microsoft GS Wavetable Synth.

#### MIDI on Linux

On Linux the ALSA subsystem is used. The `[midi]:device` key can be set with the
ALSA port of the device you want to use. For example:
```
device=128:0
```
A string corresponding to the client name is also valid (port 0 will be used):
```
device=Munt MT-32
```
or if you want to use a specific port add its number after the name string:
```
device=My Synth:1
```

If you leave the `device` key value empty, IBMulator will use the first suitable
port it will find.

To get the list of valid name strings and available ports you can launch
IBMulator with the `device` configuration empty and look at the log file, or use
the `pmidi` program:
```
$ pmidi -l
```

#### SysEx delays and the Roland MT-32 "Exc. Buffer overflow" error

Using the `[midi]:sysex_delay` ini parameter, SysEx messages can be delayed for
a specified amount of milliseconds to accommodate external MIDI modules needs.

If you're the lucky owner of a real Roland MT-32 sound module and you're getting
the "Exc. Buffer overflow" error, you can increase this value until the problem
is solved.

Another symptom of a needed extra delay is when the wrong instruments or sounds
are played. For example, in Sierra's adventure games, without a proper SysEx
delay you would not get a buffer overflow error but still data would not be
uploaded to the unit correctly and you would hear the wrong sound effects.

Set the value to `auto` to apply a default amount of delay to all MT-32 SysEx
messages. Delays specified as a positive integer number will be applied
regardless of the device model for which they are sent. A typical value for
MT-32 external modules is `20` or more.

Higher values will increase the machine state restore times when SysEx data is
present. If you're using the Munt MT-32 emulator or any other software synth you
can set this parameter to `0` to disable all delays.

### Keymaps

A keymap is a file that defines mappings between input events, like key presses,
and emulator's events.

When IBMulator is launched for the first time a default keymap named
`keymap.map` is copied inside the user's directory.

IBMulator can load multiple keymaps, although only one can be active at any
given time. This can be useful for switching controls depending on the running
program.

To specify the keymap(s) to load use the `[gui]:keymap` ini file value.  
You can load multiple keymaps concatenating their name with the `|` character,
like so:
```
keymap = my_keymap_1.map | my_keymap_2.map
```

Along with simple key presses or axes motion, a keymap allows you to specify
macros with multiple timed commands too.

The general syntax for a binding line in the keymap file is:  
`INPUT_EVENT = IBMULATOR_EVENT [+IBMULATOR_EVENT...] [; OPTION...]`

`INPUT_EVENT` can be:
* `SDLK_*`: SDL keyboard keycode symbol
* `SDL_SCANCODE_*`: SDL keyboard scancode symbol
* `KMOD_*[+KMOD_*...]`: SDL keyboard modifier(s) followed by 1 of `SDLK_*` or
`SDL_SCANCODE_*`
* `JOY_j_BUTTON_n`: joystick/gamepad j's button number n (j and n start from 0)
* `JOY_j_AXIS_n`: joystick/gamepad j's axis number n (j and n start from 0)
* `MOUSE_AXIS_a`: mouse axis a (a=X or Y)
* `MOUSE_BUTTON_n`: mouse button number n (1=left, 2=right, 3=center, other
buttons have n>3)

`IBMULATOR_EVENT` can be:
* `KEY_*`: an emulator (guest OS) keyboard key
* `JOY_j_BUTTON_n`: joystick j's button number n (j=A or B, n=1 or 2)
* `JOY_j_AXIS_n`: joystick j's axis n (j=A or B, n=X or Y), for params see below
* `MOUSE_BUTTON_n`: mouse button n (1=left, 2=center, 3=right)
* `MOUSE_AXIS_n(p,t,a)`: mouse axis n (n=X or Y), for params see below
* `FUNC_*`: GUI, program, or emulator function, see below
* `WAIT(x)`: wait x milliseconds before firing the next event
* `RELEASE(i)`: release key/button at index i (1-based); if i=0 or not specified
then all keys will be released
* `SKIP_TO(i)`: skip event execution to event at index i (1-based)
* `REPEAT`: equivalent to `SKIP_TO(1)`
* `AUTOFIRE(x)`: will be expanded to `WAIT(x/2) + RELEASE + WAIT(x/2) + REPEAT`;
default for x is 50, ie. ~20 clicks per second

`OPTION` can be:
* `MODE:m`: macro execution mode; possible values for `m` are:
    * `default`: default mode of execution, i.e. macro starts when the binding
is activated and ends when deactivated
    * `1shot`: macro starts and ends immediately; timed macros will run until
their completion (endless macros will run forever)
    * `latched`: macro starts when the binding is activated and ends when it's
activated again
* `GROUP:g`: timed macros belonging to the same group `g` (a string) can't run
concurrently
* `TYPEMATIC:NO`: disables the typematic keyboard feature

To specify multiple binding options separate them with spaces.

For keyboard events IBMulator uses the SDL library's identifiers. Refer to
https://wiki.libsdl.org/SDL_Keycode for the list of SDL symbols.

SDL keycodes depend on the current keyboard layout loaded in your operating
system. SDL scancodes represent physical keyboard keys and are not affected by
any OS remapping.

`SKIP_TO` and `REPEAT` commands have an implicit minimum wait time dependent on
the screen refresh rate. To avoid event spamming, they should be used in
conjunction with `WAIT`.

Valid `KMOD_*` keyboard modifiers are:
* `KMOD_SHIFT`: any shift
* `KMOD_LSHIFT`: left shift
* `KMOD_RSHIFT`: right shift
* `KMOD_CTRL`: any control
* `KMOD_LCTRL`: left control
* `KMOD_RCTRL`: right control
* `KMOD_ALT`: any alt
* `KMOD_LALT`: left alt
* `KMOD_RALT`: right alt
* `KMOD_GUI`: any meta (the "Win" key)
* `KMOD_LGUI`: left meta
* `KMOD_RGUI`: right meta

Key modifiers bindings have some limitations:
* you must specify combos with the modifier(s) first, for example:
`KMOD_CTRL + SDLK_TAB = KEY_ALT_L + KEY_TAB`
* SDL key bindings for modifiers (e.g. `SDLK_LCTRL`) won't work as expected when
their mode of operation is not `default` and they are also used in combos (e.g.
`KMOD_CTRL + SDLK_TAB`)
* on keyboards with the `AltGr` key the `KMOD_RALT` modifier might not work, as
`AltGr` could be translated with `LCTRL+RALT` by the host OS.

Valid `FUNC_* ` functions are:
* `FUNC_GUI_MODE_ACTION(x)`: GUI Mode action number x (see below)
* `FUNC_TOGGLE_POWER`: toggle the machine's power button
* `FUNC_TOGGLE_PAUSE`: pause / resume emulation
* `FUNC_TOGGLE_STATUS_IND`: show / hide the status indicators
* `FUNC_TOGGLE_DBG_WND`: show / hide the debug windows
* `FUNC_TAKE_SCREENSHOT`: take a screenshot
* `FUNC_TOGGLE_AUDIO_CAPTURE`: start / stop audio capture
* `FUNC_TOGGLE_VIDEO_CAPTURE`: start / stop video capture
* `FUNC_INSERT_FLOPPY`: open the floppy disk select dialog
* `FUNC_EJECT_FLOPPY`: eject floppy disk from active drive
* `FUNC_CHANGE_FLOPPY_DRIVE`: change active floppy drive (A↔B)
* `FUNC_SAVE_STATE`: open the save state dialog
* `FUNC_LOAD_STATE`: open the load state dialog
* `FUNC_QUICK_SAVE_STATE`: save the state to the quicksave slot
* `FUNC_QUICK_LOAD_STATE`: load the state from the quicksave slot
* `FUNC_GRAB_MOUSE`: lock / unlock mouse to emulator
* `FUNC_SYS_SPEED_UP`: increase emulation speed (whole system)
* `FUNC_SYS_SPEED_DOWN`: decrease emulation speed (whole system)
* `FUNC_SYS_SPEED(x,m)`: set the emulation speed to x%, mode m (1=momentary,
0=latched)
* `FUNC_TOGGLE_FULLSCREEN`: toggle fullscreen mode
* `FUNC_SWITCH_KEYMAPS`: change the active keymap to the next available one
* `FUNC_EXIT`: close IBMulator

`MOUSE_AXIS_n` events can have 3 arguments when mapped to joystick axes or
buttons, all optional:
* argument 1 (`p`): amount of pixels / maximum speed of the mouse pointer
(per 10ms, default 10)
* argument 2 (`t`): type of movement, 0=continuous/proportional, 1=accelerated,
2=single shot (default 0)
* argument 3 (`a`): amount of acceleration (default 5, applied only when t=1)

For buttons to mouse axis you must specify the direction with the sign of `p`;
for joystick/gamepad axes to mouse axes the direction is auto determined and a
negative `p` value will invert the direction.
These parameters won't apply when a mouse axis is mapped to a mouse axis; in
that case a direct (relative movement) translation will be applied (only
exception is the first parameter `p` that can be set to `-1` to invert the
axis).

`JOY_j_AXIS_n` events can have 3 arguments:
* argument 1 (`v`): stop value, from -32768 to 32767 (sign sets the direction);
you can also use `-max` and `max` strings (no default)
* argument 2 (`t`): type of stick movement, 0=immediate, 1=constant speed
(default 0)
* argument 3 (`a`): speed of stick movement (default 500, applied only when t=1)

Parameters `t` and `a` are applied only for keys and buttons; movement is
automatically determined when you map mouse and joystick axes (a negative `v`
value will invert the direction).

`KEY_*` events have an implicit chain of commands at the end to emulate
typematic repeats, but only when the key event is not part of a macro (except
when combined with key modifiers).

Keywords and identifiers are case insensitive, so for example `SDLK_a` is the
same as `sdlk_a` and `SDLK_A`.

Some examples:
* Joystick A mapped into the numeric keypad, with emulated stick movement:
```
SDLK_KP_2 = JOY_A_AXIS_Y(max,1)
SDLK_KP_4 = JOY_A_AXIS_X(-max,1)
SDLK_KP_6 = JOY_A_AXIS_X(max,1)
SDLK_KP_8 = JOY_A_AXIS_Y(-max,1)
```
* Normal 'D' key press, but if kept pressed for >250ms runs the DOS "dir"
command instead of typematic repeats:
```
SDLK_d = KEY_D + wait(250) + KEY_I + KEY_R + KEY_ENTER
```

For the full list of identifiers and additional info please see the default
keymap file.

#### Default key bindings

* <kbd>CTRL</kbd>+<kbd>F1</kbd>     : GUI mode action 1:
    * in Compact mode: toggle the main interface window
    * in Realistic mode: toggle zoomed view
* <kbd>SHIFT</kbd>+<kbd>F1</kbd>    : GUI mode action 2:
    * in Normal and Compact modes: switch between Normal and Compact modes
    * in Realistic mode: switch between bright and dark styles
* <kbd>CTRL</kbd>+<kbd>F3</kbd>     : toggle the machine power button
* <kbd>SHIFT</kbd>+<kbd>F4</kbd>    : show/hide the status indicators
* <kbd>CTRL</kbd>+<kbd>F4</kbd>     : show/hide the debug windows
* <kbd>CTRL</kbd>+<kbd>F5</kbd>     : take a screenshot
* <kbd>CTRL</kbd>+<kbd>F6</kbd>     : start/stop audio capture
* <kbd>SHIFT</kbd>+<kbd>F6</kbd>    : start/stop video capture
* <kbd>SHIFT</kbd>+<kbd>F7</kbd>    : open floppy select dialog for the active drive
* <kbd>CTRL</kbd>+<kbd>F7</kbd>     : eject floppy inserted in the active drive
* <kbd>CTRL</kbd>+<kbd>SHIFT</kbd>+<kbd>F7</kbd>: change the active floppy drive (A↔B)
* <kbd>SHIFT</kbd>+<kbd>F8</kbd>    : open the save state dialog
* <kbd>SHIFT</kbd>+<kbd>F9</kbd>    : open the load state dialog
* <kbd>CTRL</kbd>+<kbd>F8</kbd>     : quick save state
* <kbd>CTRL</kbd>+<kbd>F9</kbd>     : quick load state
* <kbd>CTRL</kbd>+<kbd>F10</kbd>    : grab the mouse
* <kbd>CTRL</kbd>+<kbd>F11</kbd>    : decrease emulation speed
* <kbd>SHIFT</kbd>+<kbd>F11</kbd>   : set emulation speed to 10% (press again for 100%)
* <kbd>CTRL</kbd>+<kbd>F12</kbd>    : increase emulation speed
* <kbd>SHIFT</kbd>+<kbd>F12</kbd>   : set emulation speed to 500% (press again for 100%)
* <kbd>CTRL</kbd>+<kbd>DEL</kbd>    : send CTRL+ALT+DEL to the guest OS
* <kbd>CTRL</kbd>+<kbd>TAB</kbd>    : send ALT+TAB to the guest OS
* <kbd>CTRL</kbd>+<kbd>INS</kbd>    : send SysRq to the guest OS
* <kbd>CTRL</kbd>+<kbd>END</kbd>    : send Break to the guest OS
* <kbd>SHIFT</kbd>+<kbd>SPACE</kbd> : change the active keymap to the next available one
* <kbd>SHIFT</kbd>+<kbd>PAUSE</kbd> : pause/resume emulation
* <kbd>ALT</kbd>+<kbd>ENTER</kbd>   : toggle fullscreen mode
* <kbd>ALT</kbd>+<kbd>F4</kbd>      : exit the program

The mouse can be grabbed with the central mouse button as well.  
Fullscreen mode can also be activated by double-clicking the display area.

### UI related key bindings

These keys apply only to UI dialogs and cannot currently be changed.

All dialog windows:
* <kbd>TAB</kbd>: focus input on next control
* <kbd>SHIFT</kbd>+<kbd>TAB</kbd>: focus input on previous control
* <kbd>ENTER</kbd>: click on focused control
* <kbd>ESC</kbd>: cancel and close dialog

Floppy select and savestate dialogs:
* <kbd>←</kbd>, <kbd>↑</kbd>, <kbd>↓</kbd>, <kbd>→</kbd>,
<kbd>HOME</kbd>, <kbd>END</kbd>, <kbd>PGUP</kbd>, <kbd>PGDN</kbd>: move item
selection
* <kbd>CTRL</kbd>+<kbd>1</kbd>: grid view mode
* <kbd>CTRL</kbd>+<kbd>2</kbd>: list view mode
* <kbd>+</kbd>/<kbd>-</kbd>: increase/decrease items size

Floppy select dialog:
* <kbd>CTRL</kbd>+<kbd>S</kbd>: use selected disk image
* <kbd>CTRL</kbd>+<kbd>W</kbd>: toggle write protected flag
* <kbd>CTRL</kbd>+<kbd>N</kbd>: new disk image
* <kbd>ALT</kbd>+<kbd>HOME</kbd>: go to the media directory
* <kbd>ALT</kbd>+<kbd>↑</kbd>, <kbd>BACKSPACE</kbd>: go to upper directory
* <kbd>ALT</kbd>+<kbd>←</kbd>: go to previous path
* <kbd>ALT</kbd>+<kbd>→</kbd>: go to next path
* <kbd>F5</kbd>: reload current directory
* <kbd>F9</kbd>: toggle image info panel

Load machine state dialog:
* <kbd>CTRL</kbd>+<kbd>L</kbd>: load selected slot
* <kbd>DELETE</kbd>: delete selected slot

Save machine state dialog:
* <kbd>CTRL</kbd>+<kbd>S</kbd>: save selected slot
* <kbd>DELETE</kbd>: delete selected slot
* <kbd>CTRL</kbd>+<kbd>N</kbd>: new savestate

### Command line options

* `-c PATH` : Sets a configuration file to use
* `-u PATH` : Sets a user directory from where the program reads the ini file
and stores new files, like screenshots and savestates
* `-v NUM`  : Sets the logging verbosity level (from 0 to 2)


## COMPILING

For **Windows** instructions see [BUILD_WINDOWS.md](BUILD_WINDOWS.md)

For **Linux** instructions see [BUILD_LINUX.md](BUILD_LINUX.md)


## THANKS

I would like to thank the authors of the following projects:
* Bochs https://bochs.sourceforge.io
* DOSBox https://www.dosbox.com
* MAME https://www.mamedev.org
