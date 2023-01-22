/*
 * Copyright (C) 2023  Marco Bortolin
 *
 * This file is part of IBMulator.
 *
 * IBMulator is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * IBMulator is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IBMULATOR_OPENGL_H
#define IBMULATOR_OPENGL_H

#include <GL/glew.h>

const char * GL_get_error_string(GLenum _error_code);
const char * GL_get_uniform_type_string(GLenum _uniform_type);

#define GUI_OPENGL_MAJOR_VER 3
#define GUI_OPENGL_MINOR_VER 3

#define OGL_NO_ERROR_CHECKING 0
#define OGL_ARB_DEBUG_OUTPUT 1
#define OGL_GET_ERROR 2

#define GUI_STOP_ON_ERRORS 1
#define GUI_ARB_DEBUG_OUTPUT_LIMIT 1000
#define GUI_GL_GHOSTHUNTING true

//#define OGL_DEBUG_TYPE OGL_NO_ERROR_CHECKING
//#define OGL_DEBUG_TYPE OGL_ARB_DEBUG_OUTPUT
#define OGL_DEBUG_TYPE OGL_GET_ERROR


#if GUI_STOP_ON_ERRORS
	#define GUI_PRINT_ERROR_FUNC PERRFEX_ABORT
#else
	#define GUI_PRINT_ERROR_FUNC PERRFEX
#endif
#if !defined(NDEBUG) && (OGL_DEBUG_TYPE == OGL_GET_ERROR)
	#if defined(GUI_GL_GHOSTHUNTING)
	#define GLCALL(X) \
	{\
		GLenum glerrcode = glGetError();\
		if(glerrcode != GL_NO_ERROR)\
			GUI_PRINT_ERROR_FUNC(LOG_OGL, "ghost GL Error: %d (%s)\n",glerrcode,GL_get_error_string(glerrcode));\
		X;\
		glerrcode = glGetError();\
		if(glerrcode != GL_NO_ERROR)\
			GUI_PRINT_ERROR_FUNC(LOG_OGL, #X"\nGL Error: %d (%s)\n",glerrcode,GL_get_error_string(glerrcode));\
	}
	#define GLCALL_NOGHOST(X) \
	{\
		X;\
		GLenum glerrcode = glGetError();\
		if(glerrcode != GL_NO_ERROR)\
			GUI_PRINT_ERROR_FUNC(LOG_OGL, #X"\nGL Error: %d (%s)\n",glerrcode,GL_get_error_string(glerrcode));\
	}
	#else
	#define GLCALL(X) \
	{\
		X;\
		GLenum glerrcode = glGetError();\
		if(glerrcode != GL_NO_ERROR)\
			GUI_PRINT_ERROR_FUNC(LOG_OGL, #X"\nGL Error: %d (%s)\n",glerrcode,GL_get_error_string(glerrcode));\
	}
	#define GLCALL_NOGHOST(X) GLCALL(X)
	#endif
#else
	#define GLCALL(X) X
	#define GLCALL_NOGHOST(X) X
#endif

#endif