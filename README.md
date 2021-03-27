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

When the emulator is launched for the first time a default keymap is copied
inside the user directory.

Multiple output events can be concatenated to form a macro, for example:
```
JOY_0_BUTTON_2 = KEY_D + KEY_I + KEY_R + KEY_ENTER
```

For the syntax and identifiers to use to define custom key bindings please see
the comments inside the default keymap file.

IBMulator can load multiple keymaps, although only one can be active at any
given time. This can be useful for switching controls depending on the running
program.

To specify the keymap(s) to load use the `[gui]:keymap` ini file value.  
You can load multiple keymaps concatenating their name with the `|` character,
like so:
```
keymap=my_keymap_1.map|my_keymap_2.map
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
