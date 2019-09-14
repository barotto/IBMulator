# IBMulator


## WHAT IS IBMULATOR?

IBMulator is a free/libre, open source emulator for the IBM PS/1, able to run 
with the original ROM. The goal is to create a faithful simulator capable of
recreate the look and feel of the real machine.

IBMulator v0.9 can emulate the following systems:
- IBM PS/1 model 2011 (286 10MHz)
- IBM PS/1 model 2121 (386sx 16MHz,20MHz)

In order to use the program you'll need the original ROM, which is copyrighted
by IBM. You won't find it distributed with this package.

See the project page at https://barotto.github.io/IBMulator/ for screenshots, 
videos and additional information.


## LICENSE

IBMulator is distributed under the GNU GPL version 3 or (at your option) any 
later version. See COPYING for details.

To obtain the source code go to github.com/barotto/IBMulator


## HARDWARE REQUIREMENTS

A 64-bit Linux or Windows operating system, a OpenGL 3.3 graphics card, and a 
2GHz dual core processor.


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
a good place) and update ibmulator.ini with the file name of the ROM you 
want to use (see below for the correct format.)

From now on IBMulator is ready to run.

For more information regarding the configuration options, see the comments 
inside ibmulator.ini.

Being a faithful emulator of the PS/1, to configure the system (ie. the PS/1, 
not the emulator) after a configuration change (for instance, if you add or 
remove a floppy drive), you need a DOS program called CONFIGUR.EXE, otherwise 
you'll get various POST errors. Likewise, if you want to customize the way the 
system works, you need to use the program CUSTOMIZ.EXE. Both files are copyright 
IBM and you have to search the Internet in order to obtain them.

### Keyboard

You may want to select the correct keyboard mapping under the [gui] section.

Currently valid keyboard mappings are:
- keymaps/pc-us.map (US keyboard)
- keymaps/pc-it.map (Italian keyboard)

If you want to create a new mapping for your language, use one of the available
files in the share/ibmulator/keymaps directory as a template.

If you'll create one please consider sending me a copy :-)

### ROM set

A ROM set can be:

1. a compressed archive in the ZIP format
2. a file with the *.BIN extension, named as you like
3. a directory 

A ZIP archive or directory must contain (case insensitive):

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
$ mount -o loop,offset=16896 hdd.img /mnt/loop
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
is grabbed or CTRL-F1 is pressed. Use this mode if you want an experience 
similar to DOSBox.
* **Normal**: this is the default mode. The control panel / system unit places 
itself at the bottom of the VGA display and is always visible.
* **Realistic**: this is the hardcore mode, for the retro enthusiasts who want 
to truly experience the PS/1. In this mode the system is rendered in its 
entirety, monitor included. There are no additional buttons and controls except 
the originals: volume, brightness, contrast, power, and floppy (you need to use 
the key bindings for any extra function.) The PS/1 type 2011 is 32 cm (12.6") 
wide and 34.9 cm (13.74") tall, so you need at least a 24" 16:10 monitor in 
portrait mode (rotated) to render it at real size.

You can select the GUI mode under the [gui] section of the ini file.

### Audio DSP filters

Sound cards' channels can be filtered with IIR filters.

A filter is defined with a string containing the filter name followed by its
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

| Filter name | Parameters               | Description            |
| ----------- | -------------------------|----------------------- |
| LowPass     | order, cutoff            | Butterworth Low Pass   |
| HighPass    | order, cutoff            | Butterworth High Pass  |
| BandPass    | order, center, bw        | Butterworth Band Pass  |
| BandStop    | order, center, bw        | Butterworth Band Stop  |
| LowShelf    | order, cutoff, gain      | Butterworth Low Shelf  |
| HighShelf   | order, cutoff, gain      | Butterworth High Shelf |
| BandShelf   | order, center, bw, gain  | Butterworth Band Shelf |

### Key bindings

* CTRL+F1: show/hide the main interface (only if GUI is in compact mode)
* CTRL+F3: toggle the machine power button
* CTRL+F4: show/hide the debug windows
* CTRL+F5: take a screenshot
* CTRL+F6: start/stop audio capture
* CTRL+F7: save current state
* CTRL+F8: load last state
* CTRL+F10: mouse grab (only if CTRL+F10 is the mouse grab method)
* CTRL+F11: CPU emulation speed down
* CTRL+F12: CPU emulation speed up
* CTRL+DEL: send CTRL+ALT+DEL to the guest OS
* CTRL+INS: send SysReq to the guest OS
* ALT+ENTER: toggle fullscreen mode
* ALT+PAUSE: pause/resume emulation
* ALT+F4: exit the program

If the grab method is 'MOUSE3', use the central mouse button to lock the
mouse.

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
