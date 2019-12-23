# Created on: 6-nov-2008
# Author: marco

###############################################################################
# Configure paths for SDL
# Sam Lantinga 9/21/99
# stolen from Manish Singh
# stolen back from Frank Belew
# stolen from Manish Singh
# Shamelessly stolen from Owen Taylor

dnl AM_PATH_SDL2([MINIMUM-VERSION, [ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]]])
dnl Test for SDL, and define SDL_CFLAGS and SDL_LIBS
dnl
AC_DEFUN([AM_PATH_SDL2],
[dnl 
dnl Get the cflags and libraries from the sdl2-config script
dnl
AC_ARG_WITH(sdl-prefix,[  --with-sdl-prefix=PFX   Prefix where SDL is installed (optional)],
            sdl_prefix="$withval", sdl_prefix="")
AC_ARG_WITH(sdl-exec-prefix,[  --with-sdl-exec-prefix=PFX Exec prefix where SDL is installed (optional)],
            sdl_exec_prefix="$withval", sdl_exec_prefix="")
AC_ARG_ENABLE(sdltest, [  --disable-sdltest       Do not try to compile and run a test SDL program],
		    , enable_sdltest=yes)

	if test x$static_sdl != xno; then
		sdl_config_libs="--static-libs"
	else
		sdl_config_libs="--libs"
	fi
	
	min_sdl_version=ifelse([$1], ,2.0.0,$1)

	if test x$sdl_exec_prefix != x ; then
	  sdl_config_args="$sdl_config_args --exec-prefix=$sdl_exec_prefix"
	  if test x${SDL2_CONFIG+set} != xset ; then
	    SDL2_CONFIG=$sdl_exec_prefix/bin/sdl2-config
	  fi
	fi
	if test x$sdl_prefix != x ; then
	  sdl_config_args="$sdl_config_args --prefix=$sdl_prefix"
	  if test x${SDL2_CONFIG+set} != xset ; then
	    SDL2_CONFIG=$sdl_prefix/bin/sdl2-config
	  fi
	fi

    as_save_PATH="$PATH"
    if test "x$prefix" != xNONE && test "$cross_compiling" != yes; then
      PATH="$prefix/bin:$prefix/usr/bin:$PATH"
    fi
    AC_CHECK_TOOL(SDL2_CONFIG, sdl2-config, no, [$PATH])
    PATH="$as_save_PATH"
    AC_MSG_CHECKING(for SDL - version >= $min_sdl_version)
    no_sdl=""

    if test "$SDL2_CONFIG" = "no" ; then
      no_sdl=yes
    else
      SDL_CFLAGS=`$SDL2_CONFIG $sdl_config_args --cflags`
      SDL_LIBS=`$SDL2_CONFIG $sdl_config_args $sdl_config_libs`
		
      sdl_major_version=`$SDL2_CONFIG --version | \
             sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\1/'`
      sdl_minor_version=`$SDL2_CONFIG --version | \
             sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\2/'`
      sdl_micro_version=`$SDL2_CONFIG --version | \
             sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\3/'`
      if test "x$enable_sdltest" = "xyes" ; then
        ac_save_CFLAGS="$CFLAGS"
        ac_save_CXXFLAGS="$CXXFLAGS"
        ac_save_LIBS="$LIBS"
        CFLAGS="$CFLAGS $SDL_CFLAGS"
        CXXFLAGS="$CXXFLAGS $SDL_CFLAGS"
        LIBS="$LIBS $SDL_LIBS"
dnl
dnl Now check if the installed SDL is sufficiently new. (Also sanity
dnl checks the results of sdl2-config to some extent
dnl
      rm -f conf.sdltest
      AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SDL.h"

char*
my_strdup (char *str)
{
  char *new_str;
  
  if (str)
    {
      new_str = (char *)malloc ((strlen (str) + 1) * sizeof(char));
      strcpy (new_str, str);
    }
  else
    new_str = NULL;
  
  return new_str;
}

int main (int argc, char *argv[])
{
  int major, minor, micro;
  char *tmp_version;

  /* This hangs on some systems (?)
  system ("touch conf.sdltest");
  */
  { FILE *fp = fopen("conf.sdltest", "a"); if ( fp ) fclose(fp); }

  /* HP/UX 9 (%@#!) writes to sscanf strings */
  tmp_version = my_strdup("$min_sdl_version");
  if (sscanf(tmp_version, "%d.%d.%d", &major, &minor, &micro) != 3) {
     printf("%s, bad version string\n", "$min_sdl_version");
     exit(1);
   }

   if (($sdl_major_version > major) ||
      (($sdl_major_version == major) && ($sdl_minor_version > minor)) ||
      (($sdl_major_version == major) && ($sdl_minor_version == minor) && ($sdl_micro_version >= micro)))
    {
      return 0;
    }
  else
    {
      printf("\n*** 'sdl2-config --version' returned %d.%d.%d, but the minimum version\n", $sdl_major_version, $sdl_minor_version, $sdl_micro_version);
      printf("*** of SDL required is %d.%d.%d. If sdl2-config is correct, then it is\n", major, minor, micro);
      printf("*** best to upgrade to the required version.\n");
      printf("*** If sdl2-config was wrong, set the environment variable SDL2_CONFIG\n");
      printf("*** to point to the correct copy of sdl2-config, and remove the file\n");
      printf("*** config.cache before re-running configure\n");
      return 1;
    }
}

],, no_sdl=yes,[echo $ac_n "cross compiling; assumed OK... $ac_c"])
        CFLAGS="$ac_save_CFLAGS"
        CXXFLAGS="$ac_save_CXXFLAGS"
        LIBS="$ac_save_LIBS"
      fi
    fi
    if test "x$no_sdl" = x ; then
      AC_MSG_RESULT(yes)
    else
      AC_MSG_RESULT(no)
    fi

  if test "x$no_sdl" = x ; then
     ifelse([$2], , :, [$2])
  else
     if test "$SDL2_CONFIG" = "no" ; then
       echo "*** The sdl2-config script installed by SDL could not be found"
       echo "*** If SDL was installed in PREFIX, make sure PREFIX/bin is in"
       echo "*** your path, or set the SDL2_CONFIG environment variable to the"
       echo "*** full path to sdl2-config."
     else
       if test -f conf.sdltest ; then
        :
       else
          echo "*** Could not run SDL test program, checking why..."
          CFLAGS="$CFLAGS $SDL_CFLAGS"
          CXXFLAGS="$CXXFLAGS $SDL_CFLAGS"
          LIBS="$LIBS $SDL_LIBS"
          AC_TRY_LINK([
#include <stdio.h>
#include "SDL.h"

int main(int argc, char *argv[])
{ return 0; }
#undef  main
#define main K_and_R_C_main
],      [ return 0; ],
        [ echo "*** The test program compiled, but did not run. This usually means"
          echo "*** that the run-time linker is not finding SDL or finding the wrong"
          echo "*** version of SDL. If it is not finding SDL, you'll need to set your"
          echo "*** LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf to point"
          echo "*** to the installed location  Also, make sure you have run ldconfig if that"
          echo "*** is required on your system"
	  echo "***"
          echo "*** If you have an old version installed, it is best to remove it, although"
          echo "*** you may also be able to get things to work by modifying LD_LIBRARY_PATH"],
        [ echo "*** The test program failed to compile or link. See the file config.log for the"
          echo "*** exact error that occured. This usually means SDL was incorrectly installed"
          echo "*** or that you have moved SDL since it was installed. In the latter case, you"
          echo "*** may want to edit the sdl2-config script: $SDL2_CONFIG" ])
          CFLAGS="$ac_save_CFLAGS"
          CXXFLAGS="$ac_save_CXXFLAGS"
          LIBS="$ac_save_LIBS"
       fi
     fi
     SDL_CFLAGS=""
     SDL_LIBS=""
     ifelse([$3], , :, [$3])
  fi
  AC_SUBST(SDL_CFLAGS)
 # if test x$static_sdl = xyes; then
 # 	SDL_LIBS=`echo $SDL_LIBS | sed 's/-lSDL2//'`
 # 	SDL_LIBS="-Wl,-Bstatic -lSDL2 -Wl,-Bdynamic $SDL_LIBS"
 # fi
 # SDL_LIBS=`echo $SDL_LIBS | sed 's/-lSDL2//'`
 # SDL_LIBS="-lSDL2 $SDL_LIBS"
  AC_SUBST(SDL_LIBS)
  rm -f conf.sdltest
])



AC_DEFUN([AX_CHECK_SDLIMAGE],
[
HAVE_SDLIMAGE=yes
SDLIMAGE_LIBS=""
SDLIMAGE_CFLAGS=""

AC_ARG_WITH([sdlimage-prefix],
            AS_HELP_STRING([--with-sdlimage-prefix=PFX], [Prefix where SDL_image is installed (optional)]),
            [sdlimage_prefix="$withval"],
            [sdlimage_prefix=""])

if test x$sdlimage_prefix != x ; then
	SDLIMAGE_CFLAGS="-I$sdlimage_prefix/include/SDL2"
	SDLIMAGE_LIBS="-L$sdlimage_prefix/lib -Wl,-rpath,$sdlimage_prefix/lib"
	#AC_CHECK_HEADERS([$sdlimage_prefix/include/SDL2/SDL_image.h],, [HAVE_SDLIMAGE=no])
else
	AC_CHECK_HEADERS([SDL2/SDL_image.h],, [HAVE_SDLIMAGE=no])
fi

if test "$HAVE_SDLIMAGE" = "no" ; then
    AC_MSG_ERROR([*** Unable to find SDL_image])
fi

SDLIMAGE_LIBS="$SDLIMAGE_LIBS -lSDL2_image"

if test x$static != x ; then
	SDLIMAGE_LIBS="$SDLIMAGE_LIBS -lpng -ltiff -ljpeg -lwebp -lz -llzma"
fi

AC_SUBST([SDLIMAGE_CFLAGS])
AC_SUBST([SDLIMAGE_LIBS])
])


AC_DEFUN([AX_CHECK_SDLMIXER],
[
HAVE_SDLMIXER=yes
SDLMIXER_LIBS=""
SDLMIXER_CFLAGS=""

AC_ARG_WITH([sdlmixer-prefix],
            AS_HELP_STRING([--with-sdlmixer-prefix=PFX], [Prefix where SDL_mixer is installed (optional)]),
            [sdlmixer_prefix="$withval"],
            [sdlmixer_prefix=""])

if test x$sdlmixer_prefix != x ; then
	SDLMIXER_CFLAGS="-I$sdlmixer_prefix/include/SDL2"
	SDLMIXER_LIBS="-L$sdlmixer_prefix/lib -Wl,-rpath,$sdlmixer_prefix/lib"
	AC_CHECK_HEADERS([$sdlmixer_prefix/include/SDL2/SDL_mixer.h],, [HAVE_SDLMIXER=no])
else
	AC_CHECK_HEADERS([SDL2/SDL_mixer.h],, [HAVE_SDLMIXER=no])
fi

if test "$HAVE_SDLMIXER" = "no" ; then
    AC_MSG_ERROR([
*** Unable to find SDL_mixer
]) 
fi

SDLMIXER_LIBS="$SDLMIXER_LIBS -lSDL2_mixer"

AC_SUBST([SDLMIXER_CFLAGS])
AC_SUBST([SDLMIXER_LIBS])
])