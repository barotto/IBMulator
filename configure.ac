#######################################################################
# Initial configuration                                               #
#######################################################################
define([IBMULATOR_VERSION],[m4_esyscmd_s([git describe --always --dirty | tr -d '\n'])])

AC_INIT([IBMulator], IBMULATOR_VERSION, [https://github.com/barotto/IBMulator/issues], [ibmulator], [https://barotto.github.io/IBMulator/])
AC_PREREQ([2.59])
AC_CANONICAL_HOST
AC_CANONICAL_BUILD
AM_INIT_AUTOMAKE([subdir-objects])
AC_CONFIG_HEADERS([config.h])

#AM_MAINTAINER_MODE

AC_CONFIG_MACRO_DIR([m4])

: ${CXXFLAGS=""}

AC_PROG_MAKE_SET
AC_PROG_CC
AC_PROG_CPP
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_RANLIB
#AC_CHECK_TOOLS([AR], [gcc-ar ar], [:])
#AC_CHECK_TOOLS([NM], [gcc-nm nm], [:])
#AC_CHECK_TOOLS([RANLIB], [gcc-ranlib ranlib], [:])

AM_CONDITIONAL([GCC], [test x$GXX = xyes])

AX_CXX_COMPILE_STDCXX_17

have_windows="no"
case "$host_os" in
	msys*|mingw*)
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

AC_ARG_ENABLE([static],
	AS_HELP_STRING([--enable-static], [enable static linking]),
	[static=$enableval],
	[static=no])

if test "x$debug" = "xyes"
then
	CXX_DEFAULTS="-O0 -g3 -Wextra -Wall -pedantic -pthread"
	if test x$have_windows = xno ; then
		CXX_DEFAULTS="$CXX_DEFAULTS -rdynamic"
	fi
else
	CXX_DEFAULTS="-O3 -pthread -fomit-frame-pointer -finline-functions -DNDEBUG" 
fi

if test x$have_windows = xno ; then
	separator="/"
	SDL_VERSION=2.0.18
else
	separator="\\\\"
	#-mno-ms-bitfields is required for the __attribute__(packed) to work
	CXX_DEFAULTS="$CXX_DEFAULTS -mno-ms-bitfields"
	SDL_VERSION=2.0.18
fi

if test x$static = xyes ; then
	if test x$have_windows = xno ; then
		# the no-pie flag serves two purposes: allows to link with a couple of non PIE
		# compiled libraries and allows to launch the executable in nautilus by double clicking
		# (the latter is due to a nautilus bug)
		CXX_DEFAULTS="$CXX_DEFAULTS -static-libgcc -static-libstdc++ -no-pie"
		static_sdl="yes"
	else
		CXX_DEFAULTS=`echo $CXX_DEFAULTS | sed 's/-pthread//'`
		CXX_DEFAULTS="$CXX_DEFAULTS -static-libgcc -static-libstdc++ -static -pthread"
		static_sdl="yes"
	fi
	static="-Wl,-Bstatic"
else
	static=""
fi

CC_DEFAULTS="$CXX_DEFAULTS"

AC_SUBST(CC_DEFAULTS)
AC_SUBST(CXX_DEFAULTS)
AC_SUBST(separator)
AC_DEFINE_UNQUOTED([FS_SEP],["$separator"],[File system separator])
	

#######################################################################
# Checks for libraries and headers                                    #
#######################################################################
AM_PATH_SDL2($SDL_VERSION, :, AC_MSG_ERROR([*** SDL $SDL_VERSION not found!]))
AX_CHECK_OPENGL
AX_CHECK_GLEW
AX_CHECK_RMLUI
AX_CHECK_LIBSAMPLERATE
#AX_CHECK_ZLIB

if test x$have_windows = xno ; then
	# ALSA
	PKG_CHECK_MODULES(ALSA, alsa, HAVE_ALSA="yes", HAVE_ALSA="no")
	AC_SUBST(ALSA_CFLAGS)
	AC_SUBST(ALSA_LIBS)
	AC_DEFINE(HAVE_WINMM, [0], [whether to build in Windows Multimedia support])
else
	# Windows Multimedia
	AC_DEFINE(HAVE_WINMM, [1], [whether to build in Windows Multimedia support])
fi

AM_CONDITIONAL(HAVE_ALSA, test x$HAVE_ALSA = xyes)

if test "x$HAVE_ALSA" == xyes ; then
	AC_DEFINE(HAVE_ALSA, [1], [whether to build in Alsa support])
else
	AC_DEFINE(HAVE_ALSA, [0], [whether to build in Alsa support])
fi

BASELIBS="-L. $GLEW_LIBS $OPENGL_LIBS $ALSA_LIBS"
SDL_LIBSs=$SDL_LIBS

if test x$static_sdl = xno ; then
	BASELIBS="$BASELIBS $SDL_LIBS"
	SDL_LIBSs=""
fi

BASELIBS="$BASELIBS $static $RMLUI_LIBS $LIBSAMPLERATE_LIBS $ZLIB_LIBS $SDL_LIBSs"

if test x$have_windows = xno ; then
	BASELIBS="$BASELIBS -lrt"
else
	BASELIBS="$BASELIBS -lws2_32 -liconv ibmulator.res"
fi
AC_SUBST([BASELIBS])

BASECFLAGS="$ALSA_CFLAGS $FREETYPE_CFLAGS $RMLUI_CFLAGS $GLEW_CFLAGS $SDL_CFLAGS $LIBSAMPLERATE_CFLAGS $ZLIB_CFLAGS"
AC_SUBST([BASECFLAGS])

AC_DEFINE_UNQUOTED([SDL_MAJ_VER],[$sdl_major_version],[The SDL maj version you are compiling with])
AC_DEFINE_UNQUOTED([SDL_MIN_VER],[$sdl_minor_version],[The SDL min version you are compiling with])

AC_HEADER_DIRENT
AC_CHECK_HEADERS([stdlib.h unistd.h poll.h sys/poll.h sys/select.h])
AC_CHECK_HEADERS([execinfo.h])
AC_CHECK_HEADERS([sys/mman.h])

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
AC_CHECK_TYPE(ushort, unsigned short)
AC_CHECK_TYPE(uint, unsigned int)


AC_CONFIG_FILES([
	Makefile 
	src/Makefile
	src/audio/Makefile
	src/hardware/Makefile
	src/gui/Makefile
])

AC_OUTPUT

echo ""
echo "${PACKAGE_NAME} build summary"
echo "------------------------"
if test x$have_windows = xno ; then
	echo "- system       : Unix"
else
	echo "- system       : Windows"
fi
if test "x$debug" = "xyes"; then
	echo "- configuration: Debug"
else
	echo "- configuration: Release"
fi
if test "x$static" != "x"; then
	echo "- link type    : Static"
else
	echo "- link type    : Dynamic"
fi
echo "- OpenGL       : Yes ($OPENGL_LIBS $GLEW_LIBS)"
echo "- SDL          : $sdl_major_version.$sdl_minor_version.$sdl_micro_version ($SDL_LIBS)"
echo "- RmlUi        : Yes ($RMLUI_LIBS)"
if test "x$LIBSAMPLERATE_LIBS" = "x"; then
	echo "- libsamplerate: NO (samples playback disabled)"
else
	echo "- libsamplerate: Yes ($LIBSAMPLERATE_LIBS)"
fi
if test x$have_windows = xno ; then
	if test "x$ALSA_LIBS" != "x"; then
		echo "- ALSA         : Yes ($ALSA_LIBS)"
	else
		echo "- ALSA         : NO (MIDI output disabled)"
	fi
fi

echo "- prefix       : ${prefix}"
