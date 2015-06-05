# libsamplerate.m4
# 22/05/2015
# Marco Bortolin

AC_DEFUN([AX_CHECK_LIBSAMPLERATE],
[
# Checking for the SRC library.

HAVE_LIBSAMPLERATE=yes
LIBSAMPLERATE_LIBS=""
LIBSAMPLERATE_CFLAGS=""

AC_ARG_WITH([libsamplerate-prefix],
            AS_HELP_STRING([--with-libsamplerate-prefix=PFX], [Prefix where libsamplerate is installed (optional)]),
            [libsamplerate_prefix="$withval"],
            [libsamplerate_prefix=""])

if test x$libsamplerate_prefix != x ; then
	LIBARCHIVE_CFLAGS="-I$libsamplerate_prefix/include"
	AC_CHECK_HEADER($libsamplerate_prefix/include/samplerate.h,, [HAVE_LIBSAMPLERATE=no])
	LIBSAMPLERATE_LIBS="$libsamplerate_prefix/lib/libsamplerate.a"
else
	AC_CHECK_HEADER(samplerate.h,, [HAVE_LIBSAMPLERATE=no])
	LIBSAMPLERATE_LIBS="-lsamplerate"
fi

if test "$LIBSAMPLERATE_LIBS" = "no" ; then
    AC_MSG_ERROR([
*** Unable to find libsamplerate
]) 
fi

AC_SUBST(LIBSAMPLERATE_CFLAGS)
AC_SUBST(LIBSAMPLERATE_LIBS)

])