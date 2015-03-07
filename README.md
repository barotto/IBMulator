# IBMulator


## WHAT IS IBMULATOR?

IBMulator is an open source emulator for the IBM PS/1 model 2011, able to run 
with the original ROM. The goal is to create a faithful emulation of the 
machine, complete of the characteristic 4-quadrant graphical user interface.

In order to use the program you need the original ROM, which is copyrighted by 
IBM. You won't find it distributed with this package.

IBMulator emulates only the PS/1 with the 10 MHz 286 CPU. Apparently there is
a model 2011 with a 386 and without the ROM DOS installed. IBMulator will not 
work with a BIOS from that particular model.


## LICENSE

IBMulator is distributed under the GNU GPLv3. See COPYING for details.

To obtain the source code go to github.com/barotto/IBMulator


## WHAT'S MISSING

* PS/1 Audio Card emulation.
* Optimizations: there are virtually none, and a lot can be done to speed up the
emulation.


## HARDWARE REQUIREMENTS

A 64bit Linux or Windows operating system with an OpenGL 3.3 video card.
CPU wise, unfortunately, due to the lack of optimizations you need a good one. 
I have tested IBMulator only on my i7-2600@3.4GHz. Extrapolating from what I 
see I think the program can run on a 2GHz dual core processor.  
Never tested the code on a 32bit o.s., maybe it will compile and run, maybe it 
won't.   
I will never have the means nor the time to port to OS X, iOS, Android, WinRT.
Patches are welcome!


## USAGE

Obtain the original ROM. You have the following options:

1. if you have a real PS/1 model 2011, take the program ROMDUMP.EXE in the 
'extra' folder and put it in an empty floppy disk; insert the floppy disk in 
your PS/1 and launch the executable: it will create the ROM image on the floppy 
disk
2. or open your PS/1 model 2011, extract the EPROMs and read them with an EPROM 
reader (you also need to merge the 2 halves in 1 file, or 4 in 2 if you have a 
non-US model)
3. or scour the Internet (I know that there are various ROM sets available...).

If you have the single bin files, create a ROM set first (see below for the 
correct format).

Launch IBMulator. A window will inform you that the file ibmulator.ini has 
been created and where it is placed.

Put the ROM set anywhere you like (inside the same directory of ibmulator.ini is 
a good place) and update ibmulator.ini with the file name of the ROM set you 
want to use.

You also want to select (or create) the correct keyboard mapping.

From now on IBMulator is ready to run.

For more information regarding the configuration options, see the comments 
inside ibmulator.ini.

I hope the various icons of the interface are self-explanatory enough.

If you want to experiment with VGA shaders, set 'fb-scanlines.fs' in the ini 
file. It will try to emulate the VGA monitor. You should also use the compact
GUI mode and go fullscreen, otherwise the higher res VGA modes can be a bit 
blurry (or maybe use a 4K high dpi monitor?)

Being a faithful emulator of the PS/1 model 2011, to configure the system (ie. 
the PS/1, not the emulator) after a configuration change (for instance, if you 
add or remove a floppy drive), you need a DOS program called CONFIGUR.EXE, 
otherwise you'll get various POST errors. Likewise, if you want to customize 
the way the system works, you need to use the program CUSTOMIZ.EXE. Both files 
are copyright IBM and you have to search the Internet in order to obtain them.

### ROM set

A ROM set is a compressed file in the zip format, containing the binary files of
the original ROM. Inside the zip file there must be (case insensitive):

* FC0000.BIN : the system BIOS ROM, 256KiB
* F80000.BIN : the regional ROM, 256KiB, optional, only for the non-US version. 
For international models, this bin file can be merged with FC0000.BIN
to form a single 512KiB bin file. In this case FC0000.BIN, if present, is 
ignored.

Any other file present in the archive is ignored.

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

*Nota bene*: if you change the disk type from the default 35 (WDL-330P) to any 
other type, the automatically created image will be 0-filled and you'll need to 
use 'fdisk' and 'format' in order to use it. Also, only types 35 and 38 have 
accurate performance emulation; any other type have the same performance as type
35.

### Key bindings

* CTRL+F1: show/hide the main interface (only if GUI is in compact mode)
* CTRL+F4: show/hide the debug windows
* CTRL+F5: take a screenshot
* CTRL+F6: start/stop audio capture
* CTRL+F10: mouse grab (only if CTRL+F10 is the mouse grab method)
* CTRL+F11: emulation speed down
* CTRL+F12: emulation speed up

If the grab method is 'MOUSE3', use the central mouse button to lock the
mouse.

### Command line options

-c PATH  Sets a configuration file to use  
-u PATH  Sets a user directory from where the program reads the ini file and 
stores new files, like screenshots and savestates  
-v NUM  Sets the verbosity level (from 0 to 2)


## KNOWN BUGS

### Emulation

* VGA scrolling does't work properly.
* VGA palette updates fail with some games (Civilization).
* The CPU speed is a bit faster than the original. This is due to the lack of 
emulation of the DRAM refresh cycle-eater and probably incorrect timing values 
for some opcode.
* Some programs (MS Works, CheckIt) fail to run if launched from ROMSHELL.
* Some Sierra's adventures (King's Quest 5, Space Quest 4) fail to run 
regardless of the way they are started.

### Linux

* When scaling is on and the window is maximized, GUI elements could be 
displaced when VGA resolution changes. This is due to a bug in SDL 2 that 
prevents the program to know when the window is maximized.
* Performance can be low if an On Demand CPU scaling governor is in use. Switch 
it to Performance (this is a known issue with the Linux CPUfreq governors).

### Windows

* High CPU usage when moving the mouse. 


## COMPILING

### Requirements

* GCC 4.9
* SDL 2.0.2
* SDL_image 2.0.0
* libRocket (latest version with a patch applied, see notes below)
* GLEW
* libarchive

If you cloned the code from the GitHub repo you also need the GNU Autotools. 

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
switching to fullscreen.

### Windows

Currently you have two options:

1. under Linux, cross compile with MinGW
2. under Windows, compile with MinGW + MSYS

Follow the general instructions using this additional 'configure' option: 
--host=x86_64-w64-mingw32

### Why GCC 4.9? Isn't the 4.8 version included with Ubuntu 14.04 enough?

IBMulator is written in C++11 and uses std::regex to filter out unwanted 
file types from the floppy selection window. GCC added proper support to
std::regex only from the 4.9 version.  
If you are using Ubuntu 14.04 you can install GCC 4.9 from the Toolchain PPA, as
I did.

### libRocket

IBMulator uses libRocket which must be downloaded from http://librocket.com. 
Unfortunately there is a critical bug that can cause the program to hang while 
opening the floppy selection window.

I was able to resolve the problem by doing the following:

In "void ElementDocument::_UpdateLayout()" (ElementDocument.cpp:295)

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

I would like to thank the Bochs team. I "borrowed" (flat out copied) a 
huge amount of code from the project. Thank you guys, you made a terrific job! 
Without your work IBMulator would have taken at least a decade to reach the 
point where it is now.
Also thanks to the DosBox team. Only a little bit of code from them but a lot of 
information and inspiration.
