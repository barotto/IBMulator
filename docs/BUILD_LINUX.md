# How to build on Linux


## Requirements

 * GNU Autotools
 * GCC 7.0+
 * SDL 2.0.18+
 * RmlUi (see instructions below)
 * GLEW
 * libsamplerate (optional)
 * libasound (optional)
 * libespeak-ng (optional)

Without libsamplerate the PC Speaker will not emit any sound and other audio sources will not be played unless they are at the same rate as the mixer. So while not technically required, it effectively is.

Without libasound there'll be no MIDI output support.

Without libespeak-ng there'll be no text-to-speech support.


## Ubuntu specific packages

Install the following packages:

```
sudo apt install build-essential git autoconf cmake libfreetype6-dev libsdl2-dev libasound2-dev libharfbuzz-dev libbz2-dev libglew-dev libsamplerate0-dev libespeak-ng-dev
```

Then follow the general instructions.


## General instructions

### RmlUi

[RmlUi](https://github.com/mikke89/RmlUi) is a C++ user interface library based on the HTML and CSS standards.  

To compile RmlUi you need **cmake** and **libfreetype**.

```
git clone https://github.com/mikke89/RmlUi
cd RmlUi && mkdir build && cd build
cmake .. -G"Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
```

If you don't want to install RmlUi as a system library, add the `CMAKE_INSTALL_PREFIX` option to cmake:

```
-DCMAKE_INSTALL_PREFIX:PATH=$PWD/workspace/RmlUi/build/release
```

Then compile and install:

```
make -j`nproc` && make install
```

### SDL2

If your distribution doesn't have the minimum required version of SDL2 (2.0.18), e.g. Ubuntu 20.04 LTS, you have to compile it from source, then use the `--with-sdl-prefix` IBMulator configuration option.

### IBMulator

Download the sources:

```
git clone https://github.com/barotto/IBMulator
```

Enter the directory and run `autoreconf`:

```
cd IBMulator
autoreconf --install
```

Run the `configure` script:

```
./configure
```

Use `./configure --help` to read the various compilation options.

You might want to use the `--prefix` option to install to a local directory instead of the default `/usr/local`.

If you did not install RmlUi as a system library use `--with-rmlui-prefix` to specify the full path of the library.

Any missing library will be pointed out; use your distributon's software manager to install them.

Here's an example configuration:

```
./configure --with-rmlui-prefix=$PWD/workspace/RmlUi/build/release --prefix=$PWD/workspace/IBMulator/build/release
```

Finally, compile and install:

```
make -j`nproc` && make install
```


## Statically linked version

Static linking is not recommended on Linux.

This procedure works only on my Ubuntu system with some library recompiled. These notes are for my reference and convenience only.

**NOTE:** this is actually a hybrid and not all libraries will be linked statically, specifically:

 - libGL (OpenGL)
 - libasound (ALSA)
 - libdl (dynamic linking library)
 - libpthread (POSIX Threads)
 - libm (C math lib)
 - libc (C stdlib)

To know the needed dynamic libraries:

```
readelf -d ibmulator | grep NEEDED
```

### Compilation

Instructions for Ubuntu 22.04 LTS (oldest supported version) and 24.04 LTS.

Compile **SDL2** with all the available features enabled:

```
sudo apt-get install build-essential git make autoconf automake libtool pkg-config cmake ninja-build gnome-desktop-testing libasound2-dev libpulse-dev libaudio-dev libjack-dev libsndio-dev libsamplerate0-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libwayland-dev libxkbcommon-dev libdrm-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev libdbus-1-dev libibus-1.0-dev libudev-dev fcitx-libs-dev
```

```
cd ~/workspace
git clone https://github.com/libsdl-org/SDL.git
cd SDL && git switch SDL2
./configure --prefix=$PWD/build/release
make -j`nproc` && make install
```

Compile **GLEW**:

```
cd ~/workspace
wget https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0.tgz
tar -xzf glew-2.2.0.tgz
cd glew-2.2.0/build
cmake cmake -G "Unix Makefiles" -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX:PATH=$PWD/release
make -j`nproc` && make install
```

Compile **RmlUi** with the shared libs option disabled:

```
cd ~/workspace
git clone https://github.com/mikke89/RmlUi
cd RmlUi && mkdir build && cd build
cmake .. -G "Unix Makefiles" -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX:PATH=$PWD/release
make -j`nproc` && make install
```

Compile **libsamplerate** (required for Ubuntu 24.04):

```
cd ~/workspace
git clone https://github.com/libsndfile/libsamplerate.git
cd libsamplerate
autoreconf -vif
./configure --enable-static --enable-sse2-lrint --prefix=$PWD/build/release
make -j`nproc` && make install
```

Configure **IBMulator**:

```
./configure \
 --prefix=$PWD/build/release \
 --enable-static \
 --disable-sdltest \
 --with-rmlui-prefix=$PWD/../RmlUi/build/release \
 --with-sdl-prefix=$PWD/../SDL/build/release \
 --with-glew-prefix=$PWD/../glew-2.2.0/build/release \
 --with-libsamplerate-prefix=$PWD/../libsamplerate/build/release
```


## AppImage

Configure IBMulator with the correct AppDir prefix:

```
./configure --prefix=$PWD/AppDir/usr
make install
```

Run the `linuxdeploy` command:

```
./linuxdeploy-x86_64.AppImage --appdir AppDir --output appimage
```
