/*
 * Copyright (C) 2019-2023  Marco Bortolin
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

#include "ibmulator.h"
#include "gui_opengl.h"
#include "rml/rend_interface_opengl.h"
#include <RmlUi/Core.h>
#include <SDL.h>
#include "stb/stb.h"


GUI_OpenGL::GUI_OpenGL()
:
m_SDL_glcontext(nullptr),
m_gl_errors_count(0)
{
}

GUI_OpenGL::~GUI_OpenGL()
{
}

void GUI_OpenGL::render()
{
	GLCALL( glBindFramebuffer(GL_FRAMEBUFFER, 0) );
	GLCALL( glViewport(0,0, m_width, m_height) );
	GLCALL( glClearColor(
			float(m_backcolor.r)/255.f,
			float(m_backcolor.g)/255.f,
			float(m_backcolor.b)/255.f,
			1.f) );
	GLCALL( glClear(GL_COLOR_BUFFER_BIT) );

	// this is the rendering of the viewport area (which includes the VGA image).
	// GUI controls are rendered later by the RmlUi context
	m_windows.interface->render_screen();

	ms_rml_mutex.lock();
	GLCALL( glEnable(GL_BLEND) );
	GLCALL( glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) );
	m_rml_context->Render();
	ms_rml_mutex.unlock();

	SDL_GL_SwapWindow(m_SDL_window);
}

void GUI_OpenGL::create_window(int _flags)
{
	PINFOF(LOG_V0, LOG_GUI, "Using the OpenGL renderer\n");
	
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	m_SDL_window = SDL_CreateWindow(m_wnd_title.c_str(), 
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 
		m_width, m_height, _flags | SDL_WINDOW_OPENGL);

	if(!m_SDL_window) {
		PERRF(LOG_GUI, "SDL_CreateWindow(): %s\n", SDL_GetError());
		throw std::exception();
	}
	
	set_window_icon();

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, GUI_OPENGL_MAJOR_VER);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, GUI_OPENGL_MINOR_VER);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	m_SDL_glcontext = SDL_GL_CreateContext(m_SDL_window);
	if(!m_SDL_glcontext) {
		PERRF(LOG_GUI, "SDL_GL_CreateContext(): %s\n", SDL_GetError());
		throw std::exception();
	}

	glewExperimental = GL_FALSE;
	GLenum res = glewInit();
	if(res != GLEW_OK) {
		PERRF(LOG_GUI,"GLEW ERROR: %s\n", glewGetErrorString(res));
		throw std::exception();
	}
	PINFOF(LOG_V1, LOG_GUI, "Using GLEW %s\n", glewGetString(GLEW_VERSION));
	glGetError();
	
	check_device_GL_caps();
	
	// TODO SDL supports adaptive sync passing -1 to this function.
	// Acquire an adaptive sync monitor and test the possibility of
	// refreshing the screen at VGA native refresh rates.
	SDL_GL_SetSwapInterval(m_vsync);
}

void GUI_OpenGL::check_device_GL_caps()
{
	const GLubyte* vendor;
	GLCALL( vendor = glGetString(GL_VENDOR) );
	const GLubyte* renderer;
	GLCALL( renderer = glGetString(GL_RENDERER) );
	const GLubyte* version;
	GLCALL( version = glGetString(GL_VERSION) );

	if(vendor) PINFOF(LOG_V2,LOG_GUI,"OpenGL Vendor: %s\n", vendor);
	if(renderer) PINFOF(LOG_V1,LOG_GUI,"OpenGL Renderer: %s\n", renderer);
	if(!version) {
		PERRF(LOG_GUI, "Unable to determine OpenGL driver version\n");
		throw std::exception();
	}

	int major,minor;
	GLCALL( glGetIntegerv(GL_MAJOR_VERSION, &major) );
	GLCALL( glGetIntegerv(GL_MINOR_VERSION, &minor) );

	if(major<GUI_OPENGL_MAJOR_VER || (major==GUI_OPENGL_MAJOR_VER && minor<GUI_OPENGL_MINOR_VER)) {
		PERRF(LOG_GUI, "OpenGL version: %s (%d.%d)\n", version, major,minor);
		PERRF(LOG_GUI, "This OpenGL version is not supported: minimum %d.%d required\n", GUI_OPENGL_MAJOR_VER, GUI_OPENGL_MINOR_VER);
		throw std::exception();
	} else {
		PINFOF(LOG_V1,LOG_GUI,"OpenGL Version: %d.%d ", major,minor);
	}

	int context_mask=0;
	GLCALL( glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &context_mask) );
	if(context_mask & GL_CONTEXT_CORE_PROFILE_BIT)
		PINFOF(LOG_V1,LOG_GUI,"core");
	if(context_mask & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT)
		PINFOF(LOG_V1,LOG_GUI,"compatibility");
	PINFOF(LOG_V1,LOG_GUI," (%s)\n", version);

	const GLubyte* glsl_version = glGetString( GL_SHADING_LANGUAGE_VERSION );
	if(glsl_version) PINFOF(LOG_V1,LOG_GUI,"GLSL version: %s\n", glsl_version);

	PINFOF(LOG_V2,LOG_GUI,"Extensions:");
	int num_extensions=0;
	GLCALL( glGetIntegerv( GL_NUM_EXTENSIONS, &num_extensions) );
	PINFOF(LOG_V2,LOG_GUI," %d\n",num_extensions);

	bool debug_output = false;

	const GLubyte* extension;
	for(int ext_count=0; ext_count<num_extensions; ext_count++) {
		GLCALL( extension = glGetStringi( GL_EXTENSIONS, ext_count ) );
		if(!extension) break;
		if(strcmp("GL_ARB_debug_output",(const char*)extension) == 0) {
			debug_output = true;
		}
		PINFOF(LOG_V2,LOG_GUI,"%d) %s\n",ext_count,extension);
	}

	GLfloat texture_max_anisotropy;
	GLCALL( glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &texture_max_anisotropy) );
	PINFOF(LOG_V2,LOG_GUI,"Texture max anisotropy: %.1f\n", texture_max_anisotropy);

	if(debug_output) {
#if !defined(NDEBUG) && (OGL_DEBUG_TYPE == OGL_ARB_DEBUG_OUTPUT)
		GLCALL( glDebugMessageCallbackARB(&GUI::GL_debug_output,(void*)this) );
		GLCALL( glDebugMessageControlARB(
			GL_DONT_CARE, //source
			GL_DONT_CARE , //type
			GL_DONT_CARE, //severity
			0, //count
			nullptr, //ids
			GL_TRUE //enabled
		) );
#endif
		m_gl_errors_count = 0;
	}
}

void GUI_OpenGL::create_renderer()
{
	m_rml_renderer = std::make_unique<RmlRenderer_OpenGL>(nullptr, m_SDL_window);
}

void GUI_OpenGL::update_texture(uintptr_t _texture, SDL_Surface *_data)
{
	assert(_data && _texture);
	GLuint gltex = static_cast<GLuint>(_texture);
	int w, h;
	GLCALL( glActiveTexture(GL_TEXTURE0) );
	GLCALL( glBindTexture(GL_TEXTURE_2D, gltex) );
	GLCALL( glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w) );
	GLCALL( glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h) );
	if(w != _data->w || h != _data->h) {
		throw std::runtime_error(str_format("Cannot update texture: invalid size %dx%d, must be %dx%d", _data->w, _data->h, w, h));
	}
	SDL_LockSurface(_data);
	GLCALL( glTexSubImage2D(
		GL_TEXTURE_2D, 0,       // target, level
		0, 0,                   // xoffset, yoffset
		_data->w, _data->h,     // width, height
		GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV,
		_data->pixels
	) );
	SDL_UnlockSurface(_data);
}

void GUI_OpenGL::GL_debug_output(
		GLenum _source,
		GLenum _type,
		GLuint /*_id*/,
		GLenum _severity,
		GLsizei /*_length*/,
		const GLchar *_message,
		GLvoid *_userParam
		)
{
	GUI_OpenGL* gui = (GUI_OpenGL*)_userParam;

	std::string source;
	switch(_source) {
		case GL_DEBUG_SOURCE_API_ARB:
			source = "API";
			break;
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM_ARB:
			source = "window system";
			break;
		case GL_DEBUG_SOURCE_SHADER_COMPILER_ARB:
			source = "shader compiler";
			break;
		case GL_DEBUG_SOURCE_THIRD_PARTY_ARB:
			source = "third party";
			break;
		case GL_DEBUG_SOURCE_APPLICATION_ARB:
			source = "application";
			break;
		case GL_DEBUG_SOURCE_OTHER_ARB:
		default:
			source = "other";
			break;
	}

	int type = LOG_ERROR;
	switch(_type) {
		case GL_DEBUG_TYPE_ERROR_ARB:
			type = LOG_ERROR;
			break;
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB:
			type = LOG_WARNING;
			source += " deprecated behavior";
			break;
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB:
			type = LOG_WARNING;
			source += " undefined behavior";
			break;
		case GL_DEBUG_TYPE_PORTABILITY_ARB:
			type = LOG_DEBUG;
			source += " portability";
			break;
		case GL_DEBUG_TYPE_PERFORMANCE_ARB:
			type = LOG_DEBUG;
			source += " performance";
			break;
		case GL_DEBUG_TYPE_OTHER_ARB:
		default:
			type = LOG_DEBUG;
			source += " other";
			break;
	}

	int verb = LOG_V0;
	bool stop = false;
	switch(_severity) {
		case GL_DEBUG_SEVERITY_HIGH_ARB:
			verb = LOG_V0;
			stop = true;
			break;
		case GL_DEBUG_SEVERITY_MEDIUM_ARB:
			verb = LOG_V1;
			break;
		case GL_DEBUG_SEVERITY_LOW_ARB:
		default:
			verb = LOG_V2;
			break;
	}

	bool logged = LOG(type, LOG_GUI, verb, "%d: GL %s: %s\n", gui->m_gl_errors_count+1, source.c_str(), _message);

	if(logged) {
		gui->m_gl_errors_count++;
		if(gui->m_gl_errors_count == GUI_ARB_DEBUG_OUTPUT_LIMIT && GUI_STOP_ON_ERRORS)
			PERRF_ABORT(LOG_GUI, "maximum number of GL debug log lines (%d) reached.\n",gui->m_gl_errors_count);
	}

	if(stop && GUI_STOP_ON_ERRORS)
		PERRF_ABORT(LOG_GUI, "stop condition met.\n");
}