# nvda.m4
# 22/08/2025
# Marco Bortolin

AC_DEFUN([AX_CHECK_NVDA],
[

HAVE_NVDA=0
NVDA_LIBS=""
NVDA_CFLAGS=""

AC_ARG_WITH([nvda-prefix],
            AS_HELP_STRING([--with-nvda-prefix=PFX], [Prefix where NVDA Controller lib is installed (optional)]),
            [nvda_prefix="$withval"],
            [nvda_prefix=""])

if test x$nvda_prefix != x ; then
	AC_CHECK_HEADER($nvda_prefix/x64/nvdaController.h,[HAVE_NVDA=1],[HAVE_NVDA=0])
else
	AC_CHECK_HEADER(nvdaController/nvdaController.h,[HAVE_NVDA=1],[HAVE_NVDA=0])
fi

AC_DEFINE_UNQUOTED([HAVE_NVDA],[$HAVE_NVDA],[Define to 1 if you have NVDA Controller lib installed])

if test "$HAVE_NVDA" = "1" ; then
	if test x$nvda_prefix != x ; then
		NVDA_CFLAGS="-I$nvda_prefix/x64"
		NVDA_LIBS="-L$nvda_prefix/x64 -lnvdaControllerClient"
	else
		NVDA_LIBS="-lnvdaControllerClient"
	fi
fi

AC_SUBST(NVDA_CFLAGS)
AC_SUBST(NVDA_LIBS)

])