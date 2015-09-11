# IBMulator


## WHAT IS IBMULATOR?

IBMulator is a free/libre, open source emulator for the IBM PS/1 model 2011, 
able to run with the original ROM. The goal is to create a faithful emulation of
the machine, complete of the characteristic 4-quadrant graphical user interface.

In order to use the program you need the original ROM, which is copyrighted by 
IBM. You won't find it distributed with this package.

IBMulator emulates only the PS/1 with the 80286 CPU (model 2011, sub-models n01, 
n34, n41.)

See the project page at https://barotto.github.io/IBMulator/ for screenshots, 
videos and additional information.


## LICENSE

IBMulator is distributed under the GNU GPLv3. See COPYING for details.

To obtain the source code go to github.com/barotto/IBMulator


## HARDWARE REQUIREMENTS

A 64-bit Linux or Windows operating system, a OpenGL 3.3 graphics card, and a 
2GHz dual core processor.


## USAGE

Obtain the original ROM. You have the following options:

1. if you have a real PS/1 model 2011, take the program ROMDUMP.EXE in the 
'extra' folder and put it in an empty floppy disk; insert the floppy disk in 
your PS/1 and launch the executable: it will create the ROM image on the floppy 
disk
2. or open your PS/1 model 2011, extract the EPROMs and read them with an EPROM 
reader (you also need to merge the 2 halves in 1 file, or 4 in 2 if you have a 
non-US model)
3. or scour the Internet (there are various ROM sets available.)

Launch IBMulator. A window will inform you that the file ibmulator.ini has 
been created and where it is placed.

Put the ROM set anywhere you like (inside the same directory of ibmulator.ini is 
a good place) and update ibmulator.ini with the file name of the ROM you 
want to use (see below for the correct format.)

You also want to select (or create) the correct keyboard mapping.

From now on IBMulator is ready to run.

For more information regarding the configuration options, see the comments 
inside ibmulator.ini.

Being a faithful emulator of the PS/1 model 2011, to configure the system (ie. 
the PS/1, not the emulator) after a configuration change (for instance, if you 
add or remove a floppy drive), you need a DOS program called CONFIGUR.EXE, 
otherwise you'll get various POST errors. Likewise, if you want to customize 
the way the system works, you need to use the program CUSTOMIZ.EXE. Both files 
are copyright IBM and you have to search the Internet in order to obtain them.

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

If you want to use a single BIN file, this can be 256KiB (US version) or 512KiB 
(international versions) in size.

### HDD image

The first time you launch IBMulator, an empty pre-formatted bootable hard disk 
image will be created.
 
If you have an original PS/1 backup disk-set you can restore the machine to its 
factory state. In order to do so:

1. insert a PC-DOS 4.0 floppy disk in drive A
2. go to the DOS prompt
4. run "a:restore a: c: /s"

Under Linux you can mount the HDD image using this command:  
$ mount -o loop,offset=16896 hdd.img /mnt/loop

The offset value is equal to "start sector"*512. The start sector value can be 
determined with:
$ fdisk -l hdd.img

*Note*: if you change the disk type from the default 35 (WDL-330P) to any 
other type, the automatically created image will be 0-filled and you'll need to 
use 'fdisk' and 'format' in order to use it.

### GUI modes

IBMulator has 3 different GUI modes.

* **Compact**: in this mode the VGA image fills the available window space and 
the control panel, in the shape of the PS/1 system unit, disappears when input 
is grabbed or CTRL-F1 is pressed. Use this mode if you want an experience 
similar to DosBox.
* **Normal**: this is the default mode. The control panel / system unit places 
itself at the bottom of the VGA display and is always visible.
* **Realistic**: this is the hardcore mode, for the retro enthusiasts who want 
to truly experience the PS/1. In this mode the system is rendered in its 
entirety, monitor included. There are no additional buttons and controls except 
the originals: volume, brightness, contrast, power, and floppy (you need to use 
the key bindings for any extra function.) The PS/1 model 2011 is 32 cm (12.6") 
wide and 34.9 cm (13.74") tall, so you need at least a 24" 16:10 monitor in 
portrait mode (rotated) to render it at real size.

You can select the GUI mode under the [gui] section of the ini file.

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

### Requirements

* GCC 4.9
* SDL 2.0.2
* SDL_image 2.0.0
* libRocket (latest version with a patch applied, see notes below)
* GLEW
* libarchive (optional)
* libsamplerate (optional)

If you cloned the code from the GitHub repo you also need the GNU Autotools.

You need libarchive if you want to use zipped ROM sets.

Without libsamplerate audio samples will not be played unless they are at the 
same rate as the mixer. 

### General instructions

$ ./configure  
$ make -j5  
$ make install

Use './configure --help' to read the variuous compilation options.

If you cloned the code from GitHub, before the 'configure' script you must run:  
$ autoreconf --install

### Linux

Just follow the general instructions. Any missing library will be pointed out;
use your distributon's software manager to install them (except libRocket).

If you are going to compile using your own version of SDL2, you should compile 
SDL2 with xinerama and xrandr, otherwise you could experience many problems 
switching to fullscreen, at least with NVIDIA cards.

If you're using Ubuntu 14.04 you can install GCC 4.9 from the Toolchain PPA.

### Windows

Currently you have two options:

1. under Linux, cross compile with MinGW
2. under Windows, compile with MinGW + MSYS

Follow the general instructions using this additional 'configure' option: 
--host=x86_64-w64-mingw32

### libRocket

IBMulator uses libRocket which must be downloaded from http://librocket.com. 
Unfortunately there is a critical bug that can cause the program to hang while 
opening the floppy selection window.

I was able to resolve the problem by doing the following:

In "void ElementDocument::_UpdateLayout()" (Core/ElementDocument.cpp:295)

move layout_dirty = false; to the end of the method, so it looks like this:

```cpp
// Updates the layout if necessary.
void ElementDocument::_UpdateLayout()
{
	lock_layout++;
	
	Vector2f containing_block(0, 0);
	if (GetParentNode() != NULL)
	    containing_block = GetParentNode()->GetBox().GetSize();
	
	LayoutEngine layout_engine;
	layout_engine.FormatElement(this, containing_block);
	
	lock_layout--;
	layout_dirty = false;
}
```

More info on this issue at https://github.com/libRocket/libRocket/issues/113


## THANKS

I would like to thank the Bochs team. I've taken a huge amount of code from the 
project. Thank you guys, you made a terrific job! 
Without your work IBMulator would have taken at least a decade to reach the 
point where it is now.  
Also thanks to the DOSBox team. Only a little bit of code from them but a lot of 
information and inspiration.
