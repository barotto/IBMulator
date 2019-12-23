# How to build on Windows
Building IBMulator is a three-step process:

1. setup a 64bit MinGW build environment
2. build libRocket
3. build IBMulator

**NOTE:** Do not use folder names with spaces. In this document I'll use
C:\msys64 for MSYS2 installation and C:\workspace for source packages.

**NOTE:** You can copy the shell commands in this document and then paste them
in the MSYS shell by pressing the middle mouse button, using Shift+Ins, or
clicking the right mouse button and choosing 'paste'.

**NOTE:** Current (post march 2019) versions of MSYS2 will produce a binary with
unstable timings when the thread_sync program option is enabled. See GitHub's
issue #47.

## STEP 1. Setup a 64bit MinGW build environment
We will use mingw-w64 (mingw-w64.org) under **MSYS2** (www.msys2.org)

1. Go to the [MSYS2 website](http://www.msys2.org), download the x86_64 package
and follow its install instructions.
2. Open the **MSYS2 MSYS** shell and using the package manager `pacman`:    
install the toolchain:  
`pacman -S mingw-w64-x86_64-binutils mingw-w64-x86_64-gcc`  
install cmake, make, Autotools, and git:  
`pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-pkg-config make pkgconfig autoconf automake libtool git`  
3. Install the libraries needed by libRocket and IBMulator  
`pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_image mingw-w64-x86_64-glew mingw-w64-x86_64-libsamplerate mingw-w64-x86_64-libarchive mingw-w64-x86_64-freetype`

You can compile IBMulator with dynamic or static linking; see
[below](#installation) for more info on the two versions. I suggest static
linking to get a more convenient and smaller package.  


## STEP 2. Build libRocket
libRocket is a C++ GUI toolkit based on the HTML and CSS standards.  
IBMulator needs various fixes, so use the version from my repository instead of
the official one.

Open the **MSYS2 MinGW 64-bit** shell (you should read MINGW64 on the command
line) and:

1. Move inside your workspace:
`cd /c/workspace`
2. Clone the repo:  
`git clone https://github.com/barotto/libRocket.git`
3. Move inside the libRocket's Build directory:  
`cd libRocket/Build`
4. Create the Makefile with cmake:  
`cmake -G"MSYS Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/mingw64/x86_64-w64-mingw32`  
for static linking add `-DBUILD_SHARED_LIBS=OFF`
5. Build and install (change the 4 with the number of your CPU's cores):  
`make -j4 && make install`

## STEP 3. Build IBMulator
Open the **MSYS2 MinGW 64-bit** shell and:

1. Move inside your workspace:  
`cd /c/workspace`
2. Clone the repo:  
`git clone https://github.com/barotto/IBMulator.git`
3. Move inside the IBMulator directory:  
`cd IBMulator`
4. run autoreconf (only once, not needed if you build again):  
`autoreconf --install`
5. Configure IBMulator:  
`./configure`  
for static linking add `--enable-static`  
use the `--help` option to see the available build options;  
use the `--prefix` option to install the program with all its assets inside a
folder of your choosing (see [below](#installation) for more info).
6. Build:  
`make -j4`
7. Install if you used the `--prefix` option:  
`make install`


### Installation
In order to use the program you'll need to "install" it. The install process is
not a Windows program installation. Its purpose is to organize executables and
assets inside a folder from which the program can run. 

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

You are free to move this package inside `C:\Program Files`  or any other
folder you like.

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
Copy the following DLLs inside IBMulator's `bin` folder.  

From C:\msys64\mingw64\bin:  

```
glew32.dll
libarchive-13.dll
libbz2-1.dll
libexpat-1.dll
libfreetype-6.dll
libgcc_s_seh-1.dll
libglib-2.0-0.dll
libgraphite2.dll
libharfbuzz-0.dll
libiconv-2.dll
libintl-8.dll
libjpeg-8.dll
liblz4.dll
liblzma-5.dll
libnettle-6.dll
libpcre-1.dll
libpng16-16.dll
libsamplerate-0.dll
libstdc++-6.dll
libtiff-5.dll
libwebp-7.dll
libwinpthread-1.dll
libzstd.dll
SDL2.dll
SDL2_image.dll
zlib1.dll
```

From the libRocket/Build directory:  

```
libRocketControls.dll
libRocketCore.dll
libRocketDebugger.dll
```

**NOTE:** this list changes periodically depending on the current version of
MSYS2/MinGW. You can use the Dependency Walker program to determine the up to
date list of libraries.


## Debugging
Install the GDB debugger:  
`pacman -S gdb`

Use dynamic linking and build a debug version of the executable:
1. configure libRocket with `-DCMAKE_BUILD_TYPE=Debug`
2. configure IBMulator with `--enable-debug`

You don't need to install the program in order to debug it, just run
ibmulator.exe inside the `src` directory.

If you don't want to interact with gdb, you can use an IDE like Eclipse CDT.
