# rmlui.m4
# 7/ago/2021
# Marco Bortolin

AC_DEFUN([AX_CHECK_RMLUI],
[
# Checking for the RmlUi library.

HAVE_RMLUI=yes
RMLUI_LIBS=""
RMLUI_CFLAGS=""

AC_ARG_WITH([rmlui-prefix],
            AS_HELP_STRING([--with-rmlui-prefix=PFX], [Prefix where the RmlUi lib is installed (optional)]),
            [rmlui_prefix="$withval"],
            [rmlui_prefix=""])

AC_LANG_PUSH([C++])
if test x$rmlui_prefix != x ; then
	RMLUI_CFLAGS="-I$rmlui_prefix/include"
	RMLUI_LIBS="-L$rmlui_prefix/lib"
	AC_CHECK_HEADER($rmlui_prefix/include/RmlUi/Core.h,, [HAVE_RMLUI=no])
else
	AC_CHECK_HEADER(RmlUi/Core.h,, [HAVE_RMLUI=no])
fi
AC_LANG_POP([C++])

if test "$HAVE_RMLUI" = "no" ; then
    AC_MSG_ERROR([
*** Unable to find RmlUi
]) 
fi

RMLUI_LIBS="$RMLUI_LIBS -lrmlui_debugger -lrmlui"

if test x$static != x ; then
	if test x$have_windows = xyes ; then
		RMLUI_LIBS="$RMLUI_LIBS -lfreetype -lharfbuzz -ldwrite -lgraphite2 -lglib-2.0 -lpng -lz -lbz2 -lfreetype -lrpcrt4 -lbrotlidec -lbrotlicommon"
	else
		RMLUI_LIBS="$RMLUI_LIBS -lfreetype -lharfbuzz -lglib-2.0 -lpcre -lpng -lz -lbz2 -lfreetype -lbrotlidec -lbrotlicommon"
	fi
	RMLUI_CFLAGS="$RMLUI_CFLAGS -DRMLUI_STATIC_LIB"
else
	if test x$rmlui_prefix != x ; then
		RMLUI_LIBS="$RMLUI_LIBS -Wl,-rpath,$rmlui_prefix/lib"
	fi
fi

AC_SUBST(RMLUI_CFLAGS)
AC_SUBST(RMLUI_LIBS)

])