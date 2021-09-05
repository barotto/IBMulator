# How to build on Linux

## Requirements

* Autotools
* GCC 7.0+
* SDL 2.0.8+
* SDL_image 2.0.0+
* RmlUi (see notes below)
* GLEW
* libarchive (optional)
* libsamplerate (optional)
* zlib (optional)

You need libarchive if you want to use zipped ROM sets.

Without libsamplerate the PC Speaker will not emit any sound and other audio 
sources will not be played unless they are at the same rate as the mixer.

Without zlib the ZMBV compressed video capture format will not work.

## General instructions
First of all run:  
```
$ autoreconf --install
```

Then configure, build, and install:  
```
$ ./configure --with-rmlui-prefix=PATH/TO/RMLUI  
$ make  
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
$ mkdir RmlUi/build && cd RmlUi/build
$ cmake .. -G"Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
$ make
```

## Statically linked version

Static linking is totally unsupported on Linux.  
Just follow the previous instructions and compile with dynamic linking.

This procedure works only on my Ubuntu system with some library recompiled.
I'll keep these notes here only for my reference and convenience.

**NOTE:** libSDL2 will always be linked dynamically.

Needed libs:

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
libzstd-dev libharfbuzz-dev libbz2-dev liblzo2-dev libfreetype6-dev
```

Compile libarchive with lot of stuff removed:
```
$ git clone https://github.com/libarchive/libarchive.git  
$ cd libarchive
$ ./configure --without-bz2lib --without-libb2 --without-iconv --without-lz4 \
--without-zstd --without-lzma --with-lzo2 --without-cng --without-nettle \
--without-openssl --without-xml2 --without-expat \
--prefix=/home/marco/workspace/libarchive/release
$ make -j24  
$ make install
```

Compile GLEW:
```
$ git clone https://github.com/nigels-com/glew.git  
$ cd glew/build/cmake  
$ ccmake  
$ make -j24  
$ make install
```

Compile RmlUi with the shared libs option disabled:
```
$ cd RmlUi/build/  
$ cmake .. -G "Unix Makefiles" -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release \
-DCMAKE_INSTALL_PREFIX:PATH=/home/marco/workspace/RmlUi/release
$ make -j24  
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
