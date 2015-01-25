# Created on: 25-jan-2015
# Author: marco

AC_DEFUN([AX_CHECK_LIBARCHIVE],
[
HAVE_LIBARCHIVE=yes
LIBARCHIVE_LIBS=""
LIBARCHIVE_CFLAGS=""

AC_ARG_WITH(libarchive-prefix,[  --with-libarchive-prefix=PFX   Prefix where libarchive is installed (optional)],
            libarchive_prefix="$withval", libarchive_prefix="")

if test x$libarchive_prefix != x ; then
	LIBARCHIVE_CFLAGS="-I$libarchive_prefix/include"
	AC_CHECK_HEADER($libarchive_prefix/include/archive.h,, [HAVE_LIBARCHIVE=no])
	LIBARCHIVE_LIBS="$libarchive_prefix/lib/libarchive.a"
else
	AC_CHECK_HEADER(archive.h,, [HAVE_LIBARCHIVE=no])
	LIBARCHIVE_LIBS="-larchive"
fi


if test "$HAVE_LIBARCHIVE" = "no" ; then
    AC_MSG_ERROR([
*** Unable to find libarchive
]) 
fi

AC_SUBST(LIBARCHIVE_CFLAGS)
AC_SUBST(LIBARCHIVE_LIBS)

])