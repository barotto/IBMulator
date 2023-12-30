# How to build on Linux

## Requirements

* Autotools
* GCC 7.0+
* SDL 2.0.18+
* RmlUi (see instructions below)
* GLEW
* libsamplerate (optional)
* libasound (optional)

Without libsamplerate the PC Speaker will not emit any sound and other audio 
sources will not be played unless they are at the same rate as the mixer.
So while not technically required, it effectively is.

Without libasound there'll be no MIDI output support.

## Ubuntu specific

Install the following packages:
```
$ sudo apt install build-essential git autoconf cmake libfreetype6-dev \
libsdl2-dev libasound2-dev libharfbuzz-dev libbz2-dev libglew-dev \
libsamplerate0-dev
```
Then follow the general instructions.

## General instructions

### RmlUi

[RmlUi](https://github.com/mikke89/RmlUi) is a C++ user interface library based
on the HTML and CSS standards.  
IBMulator requires version 5.1 of this library but it might fail to compile on
current versions of GCC, so we are going to use my patched fork.

To compile RmlUi you need **cmake** and **libfreetype**.

```
$ git clone https://github.com/barotto/RmlUi
$ cd RmlUi && git switch 5.1-fix
$ mkdir build && cd build
$ cmake .. -G"Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
$ make -j`nproc` && make install
```

If you don't want to install RmlUi as a system library, use the following
option:
```
-DCMAKE_INSTALL_PREFIX:PATH=/destination/full/path
```

### SDL2

If your distribution doesn't have the minimum required version of SDL2 (2.0.18),
e.g. Ubuntu 20.04 LTS, you have to compile it from source, then use the
`--with-sdl-prefix` IBMulator configuration option.

### IBMulator

Download the sources:
```
git clone https://github.com/barotto/IBMulator
```

Enter the directory and run `autoreconf`:
```
$ cd IBMulator
$ autoreconf --install
```

Then configure, build, and install:  
```
$ ./configure
$ make -j`nproc`
$ make install
```  
Use `./configure --help` to read the various compilation options.

You might want to use the `--prefix` option to install to a local directory
instead of the default `/usr/local`.

If you did not install RmlUi as a system library use `--with-rmlui-prefix`.

Any missing library will be pointed out; use your distributon's software
manager to install them.

## Statically linked version

Static linking is not recommended on Linux.  

This procedure works only on my Ubuntu system with some library recompiled.
These notes are for my reference and convenience only.

**NOTE:** this is actually a hybrid and not all libraries will be linked
statically, specifically:
- libGL (OpenGL)
- libasound (ALSA)
- libdl (dynamic linking library)
- libpthread (POSIX Threads)
- libm (C math lib)
- libc (C stdlib)

To know the needed dynamic libraries:
```
$ readelf -d ibmulator | grep NEEDED
```

### Compilation

Instructions for Ubuntu 20.04 LTS (oldest supported version).

Current version of SDL2, all available features enabled:
```
sudo apt-get install build-essential git make autoconf automake libtool \
pkg-config cmake ninja-build gnome-desktop-testing libasound2-dev libpulse-dev \
libaudio-dev libjack-dev libsndio-dev libsamplerate0-dev libx11-dev libxext-dev \
libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libwayland-dev \
libxkbcommon-dev libdrm-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev \
libegl1-mesa-dev libdbus-1-dev libibus-1.0-dev libudev-dev fcitx-libs-dev
```
```
git clone https://github.com/libsdl-org/SDL.git
cd SDL && git switch SDL2
./configure --prefix=/home/marco/workspace/SDL/release
make -j`nproc`
make install
```

Compile GLEW:
```
$ wget https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0.tgz
$ tar -xzf glew-2.2.0.tgz
$ cd glew-2.2.0/build 
$ cmake cmake -G "Unix Makefiles" -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release \
-DCMAKE_INSTALL_PREFIX:PATH=/home/marco/workspace/glew-2.2.0/release
$ make -j`nproc`
$ make install
```

Compile RmlUi with the shared libs option disabled:
```
$ cd RmlUi/build/  
$ cmake .. -G "Unix Makefiles" -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release \
-DCMAKE_INSTALL_PREFIX:PATH=/home/marco/workspace/RmlUi/release
$ make -j`nproc`
$ make install
```

Configure IBMulator:
```
$ ./configure --prefix=/home/marco/workspace/IBMulator/release \
--enable-static --disable-sdltest \
--with-rmlui-prefix=/home/marco/workspace/RmlUi/release \
--with-sdl-prefix=/home/marco/workspace/SDL/release \
--with-glew-prefix=/home/marco/workspace/glew-2.2.0/release
```

## AppImage

Configure with the correct AppDir prefix: 
```
$ ./configure --prefix=/home/marco/workspace/IBMulator/AppDir/usr
$ make install
```
Run the `linuxdeploy` command:
```
$ ./linuxdeploy-x86_64.AppImage --appdir AppDir --output appimage
```
