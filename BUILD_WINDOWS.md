# How to build on Windows
Building IBMulator is a three-step process:

1. setup a 64bit MinGW build environment
2. build RmlUi
3. build IBMulator

**NOTE:** Do not use folder names with spaces. In this document I'll use
C:\msys64 for MSYS2 installation and C:\workspace for source packages.

**NOTE:** You can copy the shell commands in this document and then paste them
in the MSYS shell by pressing the middle mouse button, using Shift+Ins, or
clicking the right mouse button and choosing 'paste'.


## STEP 1. Setup a 64bit MinGW build environment
We will use mingw-w64 (mingw-w64.org) under **MSYS2** (www.msys2.org)

1. Go to the [MSYS2 website](http://www.msys2.org), download the x86_64 package
and follow its installation instructions.
2. Open the **MSYS2 MSYS** shell and install the toolchain:  
`pacman -S mingw-w64-x86_64-binutils mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake make autoconf automake libtool git`
3. Install the libraries needed by RmlUi and IBMulator:  
`pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-glew mingw-w64-x86_64-libsamplerate mingw-w64-x86_64-freetype mingw-w64-x86_64-lzo2`

You can compile IBMulator with dynamic or static linking; see
[below](#installation) for more info on the two versions. I suggest static
linking to get a more convenient and smaller package. The following instructions
assume static linking.


## STEP 2. Build RmlUi
[RmlUi](https://github.com/mikke89/RmlUi) is a C++ GUI toolkit based on the HTML
and CSS standards.  
IBMulator requires version 5.1 of this library but unfortunately it doesn't
compile with current versions of MSYS2. For this reason we are going to use my
patched fork.

Open the **MSYS2 MINGW64** shell (you should read MINGW64 on the command line)
and:

1. Move inside your workspace:
`cd /c/workspace`
2. Clone the repo:  
`git clone https://github.com/barotto/RmlUi`
3. Switch to the correct branch:
`cd RmlUi && git switch 5.1-fix`
4. Create and move inside the build directory:  
`mkdir build && cd build`
5. Create the Makefile with cmake:  
`cmake .. -G"MSYS Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/mingw64/x86_64-w64-mingw32 -DBUILD_SHARED_LIBS=OFF`  
(for dynamic linking remove `-DBUILD_SHARED_LIBS=OFF`)
6. Build and install:  
`make -j$(nproc) && make install`


## STEP 3. Build IBMulator
Open the **MSYS2 MINGW64** shell and:

1. Move inside your workspace:  
`cd /c/workspace`
2. Clone the repo:  
`git clone https://github.com/barotto/IBMulator.git`
3. Move inside the IBMulator directory:  
`cd IBMulator`
4. run autoreconf (only once, not needed if you build again):  
`autoreconf --install`
5. Configure IBMulator:  
`./configure --enable-static --prefix=/c/workspace/IBMulator/release`  
(for dynamic linking remove `--enable-static`)  
use the `--help` option to see the available build options;  
see [below](#installation) for more info about the `--prefix` option.  
6. Build:  
`make -j$(nproc)`
7. Install (only if you used the `--prefix` option):  
`make install`

If you followed the instructions you'll find IBMulator inside the `release`
folder. You can now move and rename the `release` package.

### Installation
The installation process is not a Windows program installation. Its purpose is
to organize the executable and its assets inside a folder from where the program
can run. 

This is the needed directory structure:

```
[install dir]
  ├ bin
  │ ├ ibmulator.exe
  │ └ [DLLs...] (only for dynamic linking without PATH variable modification)
  └ share
    └ ibmulator
      └ [assets...]
```

To automatically organize this structure and copy the files use the `--prefix`
configure option and the `make install` command after `make`.
You'll need to use an absolute path without spaces. So, for example, to
create a release package inside the source directory:
`--prefix="C:\workspace\IBMulator\release"`

You are free to move this package inside `C:\Program Files`, your desktop, or
any other folder you like, as IBMulator uses relative paths to load its assets.

If you opted for the **statically linked build** you are now ready to use
IBMulator.

If you opted for the **dynamically linked build** you now need to make the
needed DLLs available to ibmulator.exe. You have two options:
1. modify your PATH system variable
2. copy the DLLs inside the ibmulator.exe folder.

#### option 1: modify the PATH system variable
Assuming that you've installed MSYS2 in C:\msys64, append this to your PATH
environment variable:  
`C:\msys64\mingw64\bin;C:\msys64\mingw64\x86_64-w64-mingw32\bin`

How to modify a system variable depends on the Windows version you are
using. For example: System->Advanced System Settings->Advanced tab->Environment
Variables.

#### option 2: copy the library files
Use Dependency Walker to determine the up to date list of DLLs the executable
depends on and copy them inside the IBMulator's `bin` folder. 


## Debugging
Install the GDB debugger:  
`pacman -S gdb`

Use dynamic linking and build a debug version of the executable:
1. configure RmlUi with `-DCMAKE_BUILD_TYPE=Debug`
2. configure IBMulator with `--enable-debug`

You don't need to install the program in order to debug it, just run
ibmulator.exe inside the `src` directory.

If you don't want to interact with gdb, you can use an IDE like Eclipse CDT.

Sometimes using gdb is too inconveninent and slow and the only thing you want is
a backtrace. A useful tool to catch and analyze segmentation faults is
`catchsegv`, which is included in the `drmingw` package:  
`pacman -S mingw-w64-x86_64-drmingw`

To catch a segmentation fault and obtain a backtrace run:  
`catchsegv src/ibmulator.exe`
