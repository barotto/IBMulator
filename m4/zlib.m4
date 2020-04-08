# zlib.m4
# 1/04/2020
# Marco Bortolin

AC_DEFUN([AX_CHECK_ZLIB],
[
HAVE_ZLIB=1
ZLIB_LIBS=""
ZLIB_CFLAGS=""

AC_ARG_WITH([zlib-prefix],
            AS_HELP_STRING([--with-zlib-prefix=PFX], [Prefix where zlib is installed (optional)]),
            [zlib_prefix="$withval"],
            [zlib_prefix=""])

if test x$zlib_prefix != x ; then
	AC_CHECK_HEADER($zlib_prefix/include/zlib.h,, [HAVE_ZLIB=0])
else
	AC_CHECK_HEADER(zlib.h,, [HAVE_ZLIB=0])
fi

AC_DEFINE_UNQUOTED([HAVE_ZLIB],[$HAVE_ZLIB],[Define to 1 if you have zlib installed])

if test "$HAVE_ZLIB" = "1" ; then
	if test x$zlib_prefix != x ; then
		ZLIB_CFLAGS="-I$zlib_prefix/include"
		ZLIB_LIBS="$zlib_prefix/lib/libz.a"
	else
		ZLIB_LIBS="-lz"
	fi
fi

AC_SUBST(ZLIB_CFLAGS)
AC_SUBST(ZLIB_LIBS)

])