# libsamplerate.m4
# 22/05/2015
# Marco Bortolin

AC_DEFUN([AX_CHECK_LIBSAMPLERATE],
[
# Checking for the SRC library.

HAVE_LIBSAMPLERATE=1
LIBSAMPLERATE_LIBS=""
LIBSAMPLERATE_CFLAGS=""

AC_ARG_WITH([libsamplerate-prefix],
            AS_HELP_STRING([--with-libsamplerate-prefix=PFX], [Prefix where libsamplerate is installed (optional)]),
            [libsamplerate_prefix="$withval"],
            [libsamplerate_prefix=""])

if test x$libsamplerate_prefix != x ; then
	AC_CHECK_HEADER($libsamplerate_prefix/include/samplerate.h,, [HAVE_LIBSAMPLERATE=0])
else
	AC_CHECK_HEADER(samplerate.h,, [HAVE_LIBSAMPLERATE=0])
fi

AC_DEFINE_UNQUOTED([HAVE_LIBSAMPLERATE],[$HAVE_LIBSAMPLERATE],[Define to 1 if you have libsamplerate installed])

if test "$HAVE_LIBSAMPLERATE" = "1" ; then
	if test x$libsamplerate_prefix != x ; then
		LIBSAMPLERATE_CFLAGS="-I$libsamplerate_prefix/include"
		LIBSAMPLERATE_LIBS="$libsamplerate_prefix/lib/libsamplerate.a"
	else
		LIBSAMPLERATE_LIBS="-lsamplerate"
	fi
fi

AC_SUBST(LIBSAMPLERATE_CFLAGS)
AC_SUBST(LIBSAMPLERATE_LIBS)

])