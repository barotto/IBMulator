# Created on: 25-jan-2015
# Author: marco

AC_DEFUN([AX_CHECK_LIBARCHIVE],
[
HAVE_LIBARCHIVE=1
LIBARCHIVE_LIBS=""
LIBARCHIVE_CFLAGS=""

AC_ARG_WITH(libarchive-prefix,[  --with-libarchive-prefix=PFX   Prefix where libarchive is installed (optional)],
            libarchive_prefix="$withval", libarchive_prefix="")

if test x$libarchive_prefix != x ; then
	AC_CHECK_HEADER($libarchive_prefix/include/archive.h,, [HAVE_LIBARCHIVE=0])
else
	AC_CHECK_HEADER(archive.h,, [HAVE_LIBARCHIVE=0])
fi

AC_DEFINE_UNQUOTED([HAVE_LIBARCHIVE],[$HAVE_LIBARCHIVE],[Define to 1 if you have libarchive installed])

if test "$HAVE_LIBARCHIVE" = "1" ; then
	if test x$libarchive_prefix != x ; then
		LIBARCHIVE_CFLAGS="-I$libarchive_prefix/include"
		LIBARCHIVE_LIBS="$libarchive_prefix/lib/libarchive.a"
	else
		LIBARCHIVE_LIBS="-larchive"
	fi
	if test x$static != x ; then
		if test x$have_windows = xno ; then
			# this linker definition depends on a libarchive version with almost everything disabled
			LIBARCHIVE_LIBS="$LIBARCHIVE_LIBS -llzo2"
		else
			LIBARCHIVE_LIBS="$LIBARCHIVE_LIBS -lzstd -lbz2 -llz4 -llzma -llzo2 -lnettle -lexpat -lbcrypt -lb2 -lcrypto"
		fi
		LIBARCHIVE_CFLAGS="-DLIBARCHIVE_STATIC"
	fi
fi

AC_SUBST(LIBARCHIVE_CFLAGS)
AC_SUBST(LIBARCHIVE_LIBS)

])