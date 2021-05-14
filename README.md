# IBMulator


## WHAT IS IBMULATOR?

IBMulator is a free/libre, open source emulator for the IBM PS/1, able to run 
with the original ROM. The goal is to create a faithful simulator capable of
recreating the look and feel of the real machine.

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

First of all obtain the original ROM. You have the following options:

1. if you have a real PS/1, take the program ROMDUMP.EXE in the 'extra' folder
and put it in an empty floppy disk; insert the floppy disk in your PS/1 and
launch the executable: it will create the ROM image on the floppy disk
2. or open your PS/1, extract the EPROMs and read them with an EPROM 
reader (you also need to merge the 2 halves in 1 file, or 4 in 2 if you have a 
non-US model)
3. or scour the Internet (there are various ROM sets available.)

Launch IBMulator. A window will inform you that the file ibmulator.ini has 
been created and where it is placed.

Put the ROM set anywhere you like (inside the same directory of ibmulator.ini is
a good place) and update ibmulator.ini in the [system] section with the file
name of the ROM set. 

From now on IBMulator is ready to run.

You're not required to do anything else but IBMulator is very configurable. For
more information regarding the various configuration options, see the comments 
inside ibmulator.ini.

Being a faithful emulator of the PS/1, to configure the system (ie. the PS/1, 
not the emulator) after a configuration change (for instance, if you add or 
remove a floppy drive), you need a DOS program called CONFIGUR.EXE, otherwise 
you'll get various POST errors. Likewise, if you want to customize the way the 
system works, you need to use the program CUSTOMIZ.EXE. Both files are copyright 
IBM and you have to search the Internet in order to obtain them.

### ROM set

A ROM set can be:

1. a compressed archive in the ZIP format
2. a file with the *.BIN extension, named as you like
3. a directory 

Inside a ZIP file or directory there must be (file names are case insensitive):

* FC0000.BIN : the system BIOS ROM, 256KiB
* F80000.BIN : the regional ROM, 256KiB, optional, only for non-US versions. For
international models, this bin file can be merged with FC0000.BIN to form a 
single 512KiB bin file. In this case FC0000.BIN, if present, is ignored.

Any other file present in the archive or directory is ignored.

### HDD image

The first time you launch IBMulator an empty pre-formatted hard disk image will
be created.
 
If you have an original PS/1 backup disk-set you can restore the machine to its 
factory state. In order to do so:

1. insert a PC-DOS 4.0 floppy disk in drive A
2. go to the DOS prompt
4. run "a:restore a: c: /s"

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

### GUI modes

IBMulator has 3 different GUI modes.

* **Compact**: in this mode the VGA image fills the available window space and 
the control panel, in the shape of the PS/1 system unit, disappears when input 
is grabbed or CTRL+F1 is pressed. Use this mode if you want an experience 
similar to DOSBox.
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

You can select the GUI mode under the [gui] section of the ini file.

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

### MIDI output

In order to hear MIDI music you need to use an external sequencer, either
software (FluidSynth, Munt, etc.) or hardware.

The MIDI device must be connected to the host system before IBMulator starts.

#### Windows

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

#### Linux

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
* `MODE:m`: macro execution mode; `m` can be:
 * `default`: default mode of execution, i.e. macro starts when the binding is
activated and ends when deactivated
 * `1shot`: macro starts when the binding is activated and ends immediately
* `GROUP:g`: timed macros belonging to the same group `g` can't run concurrently

To specify multiple binding options separate them with spaces.

For keyboard events IBMulator uses the SDL library's identifiers.  Refer to
https://wiki.libsdl.org/SDL_Keycode for the list of SDL symbols.

`SKIP_TO` and `REPEAT` have an implicit minimum wait time dependent on the
screen refresh rate. To avoid event spamming, they should be used in conjunction
with `WAIT`.

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

SDL keycodes depend on the current keyboard layout loaded in your operating
system. SDL scancodes represent physical keyboard keys and are not affected by
any OS remapping.

For "modifier + key" combos to work you must specify them with the modifier(s)
first, for example:  
`KMOD_CTRL + SDLK_TAB = KEY_ALT_L + KEY_TAB`

Valid `FUNC_* ` functions are:
* `FUNC_GUI_MODE_ACTION(x)`: GUI Mode action number x (see below)
* `FUNC_TOGGLE_POWER`: toggle the machine's power button
* `FUNC_TOGGLE_PAUSE`: pause / resume emulation
* `FUNC_TOGGLE_DBG_WND`: show / hide the debug windows
* `FUNC_TAKE_SCREENSHOT`: take a screenshot
* `FUNC_TOGGLE_AUDIO_CAPTURE`: start / stop audio capture
* `FUNC_TOGGLE_VIDEO_CAPTURE`: start / stop video capture
* `FUNC_QUICK_SAVE_STATE`: quick save the current emulator's state
* `FUNC_QUICK_LOAD_STATE`: quick load the last saved state
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

These parameters won't apply when a mouse axis is mapped to a mouse axis; in
that case a direct (relative movement) translation will be applied.
For buttons to mouse axis you'll have to specify the direction; for joystick /
gamepad axes to mouse axes the direction is auto determined.

`JOY_j_AXIS_n` events can have 3 arguments when they are mapped to buttons:
* argument 1 (`v`): stop value, from -32768 to 32767 (sign sets the direction);
you can also use `-max` and `max` strings (no default)
* argument 2 (`t`): type of stick movement, 0=immediate, 1=constant speed
(default 0)
* argument 3 (`a`): speed of stick movement (default 500, applied only when t=1)

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

When IBMulator is launched for the first time a default keymap is copied inside
the user's directory. For the full list of identifiers and additional info
please see the default keymap file.

IBMulator can load multiple keymaps, although only one can be active at any
given time. This can be useful for switching controls depending on the running
program.

To specify the keymap(s) to load use the `[gui]:keymap` ini file value.  
You can load multiple keymaps concatenating their name with the `|` character,
like so:
```
keymap = my_keymap_1.map | my_keymap_2.map
```

#### Default key bindings

* CTRL+F1: GUI mode action 1:
    * compact mode: toggle the main interface window
    * realistic mode: toggle zoomed view
* SHIFT+F1: GUI mode action 2:
    * realistic mode: switch between bright and dark styles
* CTRL+F3: toggle the machine power button
* CTRL+F4: show/hide the debug windows
* CTRL+F5: take a screenshot
* CTRL+F6: start/stop audio capture
* CTRL+F7: start/stop video capture
* CTRL+F8: save current state
* CTRL+F9: load last state
* CTRL+F10: grab the mouse
* CTRL+F11: decrease emulation speed
* ALT+F11: set emulation speed to 10% (momentary)
* CTRL+F12: increase emulation speed
* ALT+F12: set emulation speed to 500% (momentary)
* CTRL+DEL: send CTRL+ALT+DEL to the guest OS
* CTRL+TAB: send ALT+TAB to the guest OS
* CTRL+INS: send SysRq to the guest OS
* CTRL+END: send Break to the guest OS
* SHIFT+SPACE: change the active keymap to the next available one
* ALT+ENTER: toggle fullscreen mode
* ALT+PAUSE: pause/resume emulation
* ALT+F4: exit the program

The mouse can be grabbed with the central mouse button as well.

### Command line options

-c PATH  Sets a configuration file to use  
-u PATH  Sets a user directory from where the program reads the ini file and 
stores new files, like screenshots and savestates  
-v NUM  Sets the logging verbosity level (from 0 to 2)


## COMPILING

For **Windows** instructions see [BUILD_WINDOWS.md](BUILD_WINDOWS.md)

For **Linux** instructions see [BUILD_LINUX.md](BUILD_LINUX.md)


## THANKS

I would like to thank the Bochs team. I've taken a huge amount of code from the 
project. Thank you guys, you made a terrific job! Without your work IBMulator 
would have taken at least a decade to reach the point where it is now.  
Also thanks to the DOSBox team. Some code from them as well and a lot of 
information and inspiration.
