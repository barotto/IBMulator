#######################################################################
# Initial configuration                                               #
#######################################################################
define([IBMULATOR_VERSION],[0.4])

AC_INIT([IBMulator], IBMULATOR_VERSION, [github.com/barotto/IBMulator])
AC_PREREQ([2.59])
AC_CANONICAL_HOST
AC_CANONICAL_BUILD
AM_INIT_AUTOMAKE([subdir-objects])
AC_CONFIG_HEADER([config.h])

#AM_MAINTAINER_MODE

AC_CONFIG_MACRO_DIR([m4])

: ${CXXFLAGS=""}

AC_PROG_MAKE_SET
AC_PROG_CC
AC_PROG_CPP
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_RANLIB

AM_CONDITIONAL([GCC], [test x$GXX = xyes])

have_windows="no"
case "$host_os" in
   mingw*)
      have_windows="yes"
   ;;
esac 
AC_SUBST(have_windows)

#######################################################################
# Some configuration options                                          #
#######################################################################
AC_ARG_ENABLE([debug],
	      AS_HELP_STRING([--enable-debug], [default CXXFLAGS suitable for debugging]),
	      [debug=$enableval],
	      [debug=no])

if test "x$debug" = "xyes"
then
	CXX_DEFAULTS="-O0 -g -Wextra -Wall -ansi -pthread -std=c++11"
	if test x$have_windows = xno ; then
		CXX_DEFAULTS="$CXX_DEFAULTS -rdynamic"
	fi
else
	CXX_DEFAULTS="-O3 -pthread -std=c++11 -fomit-frame-pointer -finline-functions -DNDEBUG" 
fi

AC_SUBST(CXX_DEFAULTS)

if test x$have_windows = xno ; then
	separator="/"
else
	separator="\\\\"
fi
AC_SUBST(separator)
AC_DEFINE_UNQUOTED([FS_SEP],["$separator"],[File system separator])
	

#######################################################################
# Checks for libraries and headers                                    #
#######################################################################
SDL_VERSION=2.0.0
AM_PATH_SDL2($SDL_VERSION, :, AC_MSG_ERROR([*** SDL $SDL_VERSION not found!]))
AX_CHECK_SDLIMAGE
AX_CHECK_OPENGL
AX_CHECK_GLEW
AX_CHECK_LIBROCKET
AX_CHECK_LIBARCHIVE
AX_CHECK_LIBSAMPLERATE


BASELIBS="-L. $SDL_LIBS $SDLIMAGE_LIBS $OPENGL_LIBS $GLEW_LIBS $LIBROCKET_LIBS $LIBARCHIVE_LIBS $LIBSAMPLERATE_LIBS"
if test x$have_windows = xno ; then
	BASELIBS="$BASELIBS -lrt"
else
	BASELIBS="$BASELIBS -lws2_32 -liconv"
fi
AC_SUBST([BASELIBS])

BASECFLAGS="$FREETYPE_CFLAGS $LIBROCKET_CFLAGS $GLEW_CFLAGS $SDL_CFLAGS $SDLIMAGE_CFLAGS $LIBARCHIVE_CFLAGS $LIBSAMPLERATE_CFLAGS"
AC_SUBST([BASECFLAGS])

AC_DEFINE_UNQUOTED([SDL_MAJ_VER],[$sdl_major_version],[The SDL maj version you are compiling with])
AC_DEFINE_UNQUOTED([SDL_MIN_VER],[$sdl_minor_version],[The SDL min version you are compiling with])

AC_HEADER_DIRENT
AC_HEADER_STDC
AC_CHECK_HEADERS([stdlib.h unistd.h poll.h sys/poll.h sys/select.h])
AC_CHECK_HEADERS([execinfo.h])
AC_CHECK_HEADERS([sys/mman.h])
AC_CHECK_HEADERS([sys/socket.h])
AC_CHECK_HEADERS([netinet/in.h])

# GNU C++ demangling support
AC_CHECK_HEADERS(cxxabi.h)
AC_CHECK_FUNCS([__cxa_demangle])

AC_CHECK_FUNCS(unsetenv)
AC_CHECK_FUNCS(timegm)


#################################################################
# Check for typedefs, structures, and compiler characteristics. #
#################################################################
AC_C_BIGENDIAN([AC_MSG_ERROR([*** This byte sex is not supported.])])
AC_TYPE_SIZE_T
AC_CHECK_MEMBER([struct sockaddr_in.sin_len],
	[AC_DEFINE(HAVE_SOCKADDR_IN_SIN_LEN,1,[whether sockaddr_in struct has sin_len])],
	[],
	[#include <netinet/in.h>])
AC_CHECK_SIZEOF(char)
AC_CHECK_SIZEOF(wchar_t)
AC_CHECK_SIZEOF(short)
AC_CHECK_SIZEOF(int)
AC_CHECK_SIZEOF(long)
AC_CHECK_SIZEOF(long long)
AC_CHECK_SIZEOF(void*)
AC_CHECK_TYPE(ushort, unsigned short)
AC_CHECK_TYPE(uint, unsigned int)


AC_CONFIG_FILES([
	Makefile 
	src/Makefile
	src/hardware/Makefile
	src/gui/Makefile
	src/gui/rocket/Makefile
])
		 
AC_OUTPUT

echo ""
echo "${PACKAGE_NAME} build summary"
echo "------------------------"
if test "x$debug" = "xyes"; then
	echo "- configured for debugging"
fi
echo "- SDL          : $sdl_major_version.$sdl_minor_version.$sdl_micro_version ($SDL_LIBS)"
echo "- SDL_image    : $SDLIMAGE_LIBS"
echo "- libRocket    : $LIBROCKET_LIBS"
echo "- libarchive   : $LIBARCHIVE_LIBS"
echo "- libsamplerate: $LIBSAMPLERATE_LIBS"
echo "- OpenGL       : $OPENGL_LIBS $GLEW_LIBS"
echo "- prefix       : ${prefix}"
