# How to build under Windows
Building IBMulator is a three-step process:

1. setup a 64bit MinGW build environment
2. build libRocket
3. build IBMulator

**NOTE:** Do not use folder names with spaces. I suggest C:\msys64 for MSYS2
and C:\workspace for source packages.

**NOTE:** You can copy the shell commands in this document and then paste them
in the MSYS shell pressing the middle mouse button.
Otherwise press Shift+Ins or click the right mouse button and choose 'paste'.

## STEP 1. Setup a 64bit MinGW build environment
We will use mingw-w64 (mingw-w64.org) under **MSYS2** (www.msys2.org)

1. Go to the [MSYS2 website](http://www.msys2.org), download the x86_64 package
and follow the instructions to install the environment.
2. Open the **MSYS2 MSYS** shell and, with the package manager `pacman`:    
install the toolchain:  
`pacman -S mingw-w64-x86_64-binutils mingw-w64-x86_64-gcc`  
install cmake, make, and Autotools:  
`pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-pkg-config make pkgconfig autoconf automake libtool`  
install git (unless you want to juggle with zip files):  
`pacman -S git`
3. Install the libraries needed by libRocket and IBMulator  
`pacman -S mingw-w64-x86_64-SDL2`  
`pacman -S mingw-w64-x86_64-SDL2_image`  
`pacman -S mingw-w64-x86_64-glew`  
`pacman -S mingw-w64-x86_64-libsamplerate`  
`pacman -S mingw-w64-x86_64-libarchive`  
Unless you want to build a statically linked version, also install:  
`pacman -S mingw-w64-x86_64-freetype`  
See [below](#statically-linked-version) for an explanation about static/dynamic
linking.

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
4. run this (only once, not needed if you build again):  
`autoreconf --install`
5. Configure IBMulator:  
`./configure --prefix="ABSOLUTE\PATH\TO\INSTALL\DIR"`  
use the `--help` option to see the available build options;  
see the installation section below for the `--prefix` explanation.
6. Build:  
`make -j4`

### Installation
In order to use the program you'll need to install it first.

This is the needed directory structure:

```
bin
|_ibmulator.exe
|_DLLs (only if dynamic linking without PATH variable modification)
share
|_ibmulator
  |_assets files
```

To automatically organize this structure and copy the files use the `--prefix`
configure option and the `make install` command after `make`.
You'll need to use an absolute path (without spaces!). So, for example, to
create a release package inside the source directory:
```
./configure --prefix="C:\workspace\IBMulator\release"
make
make install
```

You are not forced to keep IBMulator inside the installation directory forever,
you can move this package wherever you want and name it however you like.  

To complete the installation you now need to make the needed DLLs available
to ibmulator.exe. You have **2 options**: modify your PATH system variable OR
copy the DLLs inside the ibmulator.exe folder.

#### option 1. Modify the PATH system variable
Assuming that you've installed MSYS2 in C:\msys64, append this to your PATH
environment variable:  
`C:\msys64\mingw64\bin;C:\msys64\mingw64\x86_64-w64-mingw32\bin`

How to modify a system variable depends on the Windows version you are
using. For example: System->Advanced System Settings->Advanced tab->Environment
Variables.

#### option 2. Copy the library files
Copy the following DLLs inside the ibmulator.exe folder.  

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
liblzo2-2.dll
libnettle-6.dll
libpcre-1.dll
libpng16-16.dll
libsamplerate-0.dll
libstdc++-6.dll
libtiff-5.dll
libwebp-7.dll
libwinpthread-1.dll
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

**NOTE:** this list can change depending on the current version of MSYS2/MinGW.

## Statically linked version
If you want to remove external DLLs dependencies and statically link
ibmulator.exe follow this procedure. It can be useful in order to create a
streamlined release package.

### 1. Rebuild FreeType
The version of FreeType available through the mingw64 repo is not statically
linkable because it depends on a HarfBuzz version which, at this time, has
problems linking to `graphite2.a`. This problem should be resolved in a 
future MSYS2 version.  
Relevant issue: https://github.com/harfbuzz/harfbuzz/issues/720

Open the **MSYS2 MSYS** shell and remove the previously installed FreeType
package:  
`pacman -Rsc mingw-w64-x86_64-freetype`

Open the **MSYS2 MinGW 64-bit** shell and move inside your workspace. Download,
build, and install the current version of FreeType:  
```
git clone git://git.sv.nongnu.org/freetype/freetype2.git
cd freetype2
mkdir release
cd release
cmake -G"MSYS Makefiles" -DWITH_ZLIB=OFF -DWITH_BZip2=OFF -DWITH_PNG=OFF -DWITH_HarfBuzz=OFF -DCMAKE_INSTALL_PREFIX=/mingw64/x86_64-w64-mingw32 ..
make && make install
```

### 2. Build a static version of libRocket
Move inside your `libRocket/Build` folder and delete the `CMakeCache.txt` file.
Then follow the **STEP 2** instructions adding `-DBUILD_SHARED_LIBS=OFF` to the
cmake command.

### 3. Build IBMulator
Follow the **STEP 3** instructions adding `--enable-static` to the configure
options.  
Remember to clean up the sources if you've already done a build:  
`make clean`
