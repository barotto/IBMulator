# Created on: 6-nov-2008
# Author: marco

AC_DEFUN([AX_CHECK_OPENGL],
[
# Checking for OpenGL
HAVE_OPENGL=yes
OPENGL_LIBS=""
AC_CHECK_HEADER(GL/gl.h,, [HAVE_OPENGL=no])
#AC_CHECK_HEADER(GL/glu.h,, [HAVE_OPENGL=no])	

if test "$HAVE_OPENGL" = "no" ; then
    AC_MSG_ERROR([
*** Unable to find OpenGL
])
else
	if test x$have_windows = xyes ; then
		OPENGL_LIBS="-lopengl32"
	else
    	OPENGL_LIBS="-lGL"
    fi
    if test x$static != xno ; then
    	OPENGL_LIBS="-Wl,-Bdynamic $OPENGL_LIBS $static"
    fi
fi

AC_SUBST(OPENGL_LIBS)
])

AC_DEFUN([AX_CHECK_GLEW],
[
# Checking for GLEW
HAVE_GLEW=yes
GLEW_LIBS=""
GLEW_CFLAGS=""

AC_ARG_WITH(glew-prefix,[  --with-glew-prefix=PFX   Prefix where GLEW is installed (optional)],
            glew_prefix="$withval", glew_prefix="")

if test x$glew_prefix != x ; then
	GLEW_CFLAGS="-I$glew_prefix/include"
	AC_CHECK_HEADER($glew_prefix/include/GL/glew.h,, [HAVE_GLEW=no])
	if test x$have_windows = xyes ; then
		GLEW_LIBS="$glew_prefix/lib/libglew32.a"
	else
    	GLEW_LIBS="$glew_prefix/lib/libGLEW.a"
    fi
	
else
	AC_CHECK_HEADER(GL/glew.h,, [HAVE_GLEW=no])
	if test x$have_windows = xyes ; then
		GLEW_LIBS="-lglew32"
	else
    	GLEW_LIBS="-lGLEW"
    fi
	
fi

if test "$HAVE_GLEW" = "no" ; then
    AC_MSG_ERROR([
*** Unable to find GLEW
]) 
fi

if test x$static != xno ; then
	GLEW_CFLAGS="-DGLEW_STATIC"
fi

AC_SUBST(GLEW_CFLAGS)
AC_SUBST(GLEW_LIBS)

])