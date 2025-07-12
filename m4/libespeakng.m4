# libespeak-ng.m4
# 02/02/2025
# Marco Bortolin

AC_DEFUN([AX_CHECK_LIBESPEAKNG],
[

HAVE_LIBESPEAKNG=1
LIBESPEAKNG_LIBS=""
LIBESPEAKNG_CFLAGS=""

AC_ARG_WITH([libespeakng-prefix],
            AS_HELP_STRING([--with-libespeakng-prefix=PFX], [Prefix where libespeak-ng is installed (optional)]),
            [libespeakng_prefix="$withval"],
            [libespeakng_prefix=""])

if test x$libespeakng_prefix != x ; then
	AC_CHECK_HEADER($libespeakng_prefix/include/speak_lib.h,, [HAVE_LIBESPEAKNG=0])
else
	AC_CHECK_HEADER(espeak-ng/speak_lib.h,, [HAVE_LIBESPEAKNG=0])
fi

AC_DEFINE_UNQUOTED([HAVE_LIBESPEAKNG],[$HAVE_LIBESPEAKNG],[Define to 1 if you have libespeak-ng installed])

if test "$HAVE_LIBESPEAKNG" = "1" ; then
	if test x$libespeakng_prefix != x ; then
		LIBESPEAKNG_CFLAGS="-I$libespeakng_prefix/include"
		LIBESPEAKNG_LIBS="$libespeakng_prefix/lib/libespeak-ng.a"
	else
		LIBESPEAKNG_LIBS="-lespeak-ng"
	fi
fi

AC_SUBST(LIBESPEAKNG_CFLAGS)
AC_SUBST(LIBESPEAKNG_LIBS)

])