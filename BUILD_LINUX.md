# How to build on Linux

## Requirements

* GCC 4.9+
* SDL 2.0.4+
* SDL_image 2.0.0+
* libRocket (see notes below)
* GLEW
* libarchive (optional)
* libsamplerate (optional)

If you cloned the code from the GitHub repo you also need the GNU Autotools.

You need libarchive if you want to use zipped ROM sets.

Without libsamplerate the PC Speaker will not emit any sound and other audio 
sources will not be played unless they are at the same rate as the mixer. 

## General instructions
If you cloned the code from GitHub run:  
```
$ autoreconf --install
```

Then configure, build, and install:  
```
$ ./configure --with-librocket-prefix=PATH/TO/LIBROCKET  
$ make  
$ make install
```  
Use './configure --help' to read the various compilation options.

You can omit the --with-librocket-prefix if you installed libRocket has a
system library.

Any missing library will be pointed out; use your distributon's software
manager to install them (except libRocket, see notes below).

If you are going to compile using your own version of SDL2, you may want to
compile SDL2 with xinerama and xrandr support, otherwise you could experience
problems switching to fullscreen.

### libRocket

libRocket is a C++ user interface package based on the HTML and CSS standards.  
IBMulator needs various fixes so use the version from my repository.  
```
$ git clone https://github.com/barotto/libRocket.git  
$ cd libRocket/Build  
$ cmake -G "Unix Makefiles"  
$ make
```
