# libpng.m4
# 17/ago/2009
# Marco Bortolin

AC_DEFUN([AX_CHECK_LIBPNG],
[
# Checking for the png library.

AC_ARG_WITH([libpng-prefix],
            AS_HELP_STRING([--with-libpng-prefix=PFX], [Prefix where libpng is installed (optional)]),
            [libpng_prefix="$withval"],
            [libpng_prefix=""])

AC_ARG_WITH([libpng-exec-prefix],
            AS_HELP_STRING([--with-libpng-exec-prefix=PFX], [Exec prefix where libpng is installed (optional)]),
            [libpng_exec_prefix="$withval"],
            [libpng_exec_prefix=""])

if test x$libpng_exec_prefix != x ; then
     if test x${LIBPNG_CONFIG+set} != xset ; then
        LIBPNG_CONFIG=$libpng_exec_prefix/bin/libpng-config
     fi
fi
if test x$libpng_prefix != x ; then
     if test x${LIBPNG_CONFIG+set} != xset ; then
        LIBPNG_CONFIG=$libpng_prefix/bin/libpng-config
     fi
fi
AC_PATH_PROG(LIBPNG_CONFIG, libpng-config, no)
no_libpng=""
if test "$LIBPNG_CONFIG" = "no" ; then
    AC_MSG_ERROR([
*** Unable to find libpng
])
else
    LIBPNG_CFLAGS="`$LIBPNG_CONFIG --cflags`"
    LIBPNG_LIBS=`$LIBPNG_CONFIG --libs`
fi

AC_SUBST([LIBPNG_CFLAGS])
AC_SUBST([LIBPNG_LIBS])
])