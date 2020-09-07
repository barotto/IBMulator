# librocket.m4
# 21/ago/2014
# Marco Bortolin

AC_DEFUN([AX_CHECK_LIBROCKET],
[
# Checking for the Rocket library.

HAVE_LIBROCKET=yes
LIBROCKET_LIBS=""
LIBROCKET_CFLAGS=""

AC_ARG_WITH([librocket-prefix],
            AS_HELP_STRING([--with-librocket-prefix=PFX], [Prefix where libRocket is installed (optional)]),
            [librocket_prefix="$withval"],
            [librocket_prefix=""])

AC_LANG_PUSH([C++])
if test x$librocket_prefix != x ; then
	LIBROCKET_CFLAGS="-I$librocket_prefix/include"
	LIBROCKET_LIBS="-L$librocket_prefix/lib"
	AC_CHECK_HEADERS([$librocket_prefix/include/Rocket/Core.h],, [HAVE_LIBROCKET=no])
else
	AC_CHECK_HEADERS([Rocket/Core.h],, [HAVE_LIBROCKET=no])
fi
AC_LANG_POP([C++])

if test "$HAVE_LIBROCKET" = "no" ; then
    AC_MSG_ERROR([
*** Unable to find libRocket
]) 
fi

#debug build of librocket is superslow, use only if required
#if test "x$debug" = "xyes"
#then
#LIBROCKET_LIBS="$LIBROCKET_LIBS -lRocketCore_d -lRocketControls_d -lRocketDebugger_d"
#else
LIBROCKET_LIBS="$LIBROCKET_LIBS -lRocketCore -lRocketControls -lRocketDebugger"
#fi

if test x$static != x ; then
	if test x$have_windows = xyes ; then
		LIBROCKET_LIBS="$LIBROCKET_LIBS -lfreetype -lharfbuzz -lgraphite2 -lglib-2.0 -lintl -liconv -lpcre -lpng -lz -lbz2 -lfreetype -lrpcrt4 -ldwrite -lbrotlidec-static -lbrotlicommon-static"
	else
		LIBROCKET_LIBS="$LIBROCKET_LIBS -lfreetype -lharfbuzz -lglib-2.0 -lpcre -lpng -lz -lbz2 -lfreetype"
	fi
	LIBROCKET_CFLAGS="$LIBROCKET_CFLAGS -DROCKET_STATIC_LIB"
else
	if test x$librocket_prefix != x ; then
		LIBROCKET_LIBS="$LIBROCKET_LIBS -Wl,-rpath,$librocket_prefix/lib"
	fi
fi

AC_SUBST(LIBROCKET_CFLAGS)
AC_SUBST(LIBROCKET_LIBS)

])