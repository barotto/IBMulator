# How to build on Linux

## Requirements

* Autotools
* GCC 7.0+
* SDL 2.0.8+
* RmlUi (see notes below)
* GLEW
* libsamplerate (optional)
* libasound (optional)

Without libsamplerate the PC Speaker will not emit any sound and other audio 
sources will not be played unless they are at the same rate as the mixer.

Without libasound there'll be no MIDI output support.

## General instructions

First of all run:  
```
$ autoreconf --install
```

Then configure, build, and install:  
```
$ ./configure --with-rmlui-prefix=PATH/TO/RMLUI  
$ make -j`nproc`
$ make install
```  
Use `./configure --help` to read the various compilation options.

You can omit the `--with-rmlui-prefix` if you installed RmlUi has a
system library.

Any missing library will be pointed out; use your distributon's software
manager to install them (except RmlUi, see notes below).

### RmlUi

RmlUi is a C++ user interface library based on the HTML and CSS standards.

To compile RmlUi you'll need cmake and libfreetype.

```
$ git clone https://github.com/mikke89/RmlUi
$ cd RmlUi && git checkout 5.1
$ mkdir build && cd build
$ cmake .. -G"Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
$ make -j`nproc`
```

## Statically linked version

Static linking is totally unsupported on Linux.  
Just follow the previous instructions and compile with dynamic linking.

But if you insist on building a "static" version, here's my personal recipe.

This procedure works only on my Ubuntu system with some library recompiled.
These notes are for my reference and convenience only.

**NOTE:** this is actually a hybrid and not all libraries will be linked
statically, specifically:
- libSDL2-2.0 (SDL2)
- libasound (ALSA)
- libGL (OpenGL)
- libc (C stdlib)
- libm (C math lib)
- libpthread (POSIX Threads)

To know the needed dynamic libraries:
```
$ readelf -d ibmulator | grep NEEDED
```

### Required libraries

* libsdl2
* libasound
* libtiff
* libjpeg
* libwebp
* liblzma
* libjbig
* libzstd
* libharfbuzz
* libbz2
* liblzo2
* libfreetype6

```
$ sudo apt install libtiff-dev libjpeg-dev libwebp-dev liblzma-dev libjbig-dev \
libzstd-dev libharfbuzz-dev libbz2-dev liblzo2-dev libfreetype6-dev \
libsdl2-dev libasound2-dev
```

### Compilation

Compile libarchive with lot of stuff removed (not required unless you want
support for other types of archives other than zip):
```
$ git clone https://github.com/libarchive/libarchive.git  
$ cd libarchive
$ ./configure --without-bz2lib --without-libb2 --without-iconv --without-lz4 \
--without-zstd --without-lzma --with-lzo2 --without-cng --without-nettle \
--without-openssl --without-xml2 --without-expat \
--prefix=/home/marco/workspace/libarchive/release
$ make -j`nproc` 
$ make install
```

Compile GLEW:
```
$ git clone https://github.com/nigels-com/glew.git  
$ cd glew/build/cmake  
$ ccmake  
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
--with-glew-prefix=/home/marco/workspace/glew/release \
--with-libarchive-prefix=/home/marco/workspace/libarchive/release
```
