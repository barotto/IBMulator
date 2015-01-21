# Created on: 6-nov-2008
# Author: marco

AC_DEFUN([AX_CHECK_FREETYPE],
[
# Checking for the freetype library.

dnl Get the cflags and libraries from the freetype-config script

AC_ARG_WITH([freetype-prefix],
            AS_HELP_STRING([--with-freetype-prefix=PFX], [Prefix where FREETYPE is installed (optional)]),
            [freetype_prefix="$withval"],
            [freetype_prefix=""])

AC_ARG_WITH([freetype-exec-prefix],
            AS_HELP_STRING([--with-freetype-exec-prefix=PFX], [Exec prefix where FREETYPE is installed (optional)]),
            [freetype_exec_prefix="$withval"],
            [freetype_exec_prefix=""])

if test x$freetype_exec_prefix != x ; then
     freetype_args="$freetype_args --exec-prefix=$freetype_exec_prefix"
     if test x${FREETYPE_CONFIG+set} != xset ; then
        FREETYPE_CONFIG=$freetype_exec_prefix/bin/freetype-config
     fi
fi
if test x$freetype_prefix != x ; then
     freetype_args="$freetype_args --prefix=$freetype_prefix"
     if test x${FREETYPE_CONFIG+set} != xset ; then
        FREETYPE_CONFIG=$freetype_prefix/bin/freetype-config
     fi
fi
AC_PATH_PROG(FREETYPE_CONFIG, freetype-config, no)
no_freetype=""
if test "$FREETYPE_CONFIG" = "no" ; then
    AC_MSG_ERROR([
*** Unable to find FreeType2 library (http://www.freetype.org/)
])
else
    FREETYPE_CFLAGS="`$FREETYPE_CONFIG $freetypeconf_args --cflags`"
    FREETYPE_LIBS=`$FREETYPE_CONFIG $freetypeconf_args --libs`
fi

AC_SUBST([FREETYPE_CFLAGS])
AC_SUBST([FREETYPE_LIBS])
])

