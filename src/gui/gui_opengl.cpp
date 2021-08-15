/*
 * Copyright (C) 2019-2021  Marco Bortolin
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
#include <SDL_image.h>
#include <GL/glew.h>


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
	GLCALL( glViewport(0,0, m_width, m_height) );
	GLCALL( glClearColor(
			float(m_backcolor.r)/255.f,
			float(m_backcolor.g)/255.f,
			float(m_backcolor.b)/255.f,
			float(m_backcolor.a)/255.f) );
	GLCALL( glClear(GL_COLOR_BUFFER_BIT) );
	
	// this is a rendering of the screen only (which includes the VGA image).
	// GUI controls are rendered later by the RmlUi context
	m_windows.interface->render_screen();

	ms_rml_mutex.lock();
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

	if(vendor) PINFOF(LOG_V2,LOG_GUI,"Vendor: %s\n", vendor);
	if(renderer) PINFOF(LOG_V1,LOG_GUI,"Renderer: %s\n", renderer);
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
		PINFOF(LOG_V1,LOG_GUI,"Version: %d.%d ", major,minor);
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

std::vector<GLuint> GUI_OpenGL::attach_shaders(
	const std::vector<std::string> _sh_paths, GLuint _sh_type, GLuint _program)
{
	std::vector<GLuint> sh_ids;
	for(auto sh : _sh_paths) {
		// Read Shader code from file
		std::string shcode = load_shader_file(sh);
		// Create the shader
		GLuint shid;
		GLCALL( shid = glCreateShader(_sh_type) );
		// Compile Vertex Shader
		char const * source = shcode.c_str();
		GLCALL( glShaderSource(shid, 1, &source , nullptr) );
		GLCALL( glCompileShader(shid) );
		// Check Shader
		GLint result = GL_FALSE;
		int infologlen;
		GLCALL( glGetShaderiv(shid, GL_COMPILE_STATUS, &result) );
		GLCALL( glGetShaderiv(shid, GL_INFO_LOG_LENGTH, &infologlen) );
		if(!result && infologlen > 1) {
			std::vector<char> sherr(infologlen+1);
			GLCALL( glGetShaderInfoLog(shid, infologlen, nullptr, &sherr[0]) );
			PERRF(LOG_GUI, "GLSL error in '%s'\n", sh.c_str());
			PERRF(LOG_GUI, "%s\n", &sherr[0]);
		}
		// Attach Vertex Shader to Program
		GLCALL( glAttachShader(_program, shid) );
		sh_ids.push_back(shid);
	}
	return sh_ids;
}

GLuint GUI_OpenGL::load_program(
	const std::vector<std::string> _vs_paths, std::vector<std::string> _fs_paths)
{
	PDEBUGF(LOG_V1, LOG_GUI, "Loading GLSL program:\n");
	for( auto s : _vs_paths ) {
		PDEBUGF(LOG_V1, LOG_GUI, " %s\n", s.c_str());
	}
	for( auto s : _fs_paths ) {
		PDEBUGF(LOG_V1, LOG_GUI, " %s\n", s.c_str());
	}
	
	// Create the Program
	GLuint progid;
	GLCALL( progid = glCreateProgram() );

	// Load and attach Shaders to Program
	std::vector<GLuint> vsids = attach_shaders(_vs_paths, GL_VERTEX_SHADER, progid);
	std::vector<GLuint> fsids = attach_shaders(_fs_paths, GL_FRAGMENT_SHADER, progid);

	// Link the Program
	GLCALL( glLinkProgram(progid) );

	// Delete useless Shaders
	for(auto shid : vsids) {
		//a shader won't actually be deleted by glDeleteShader until it's been detached
		GLCALL( glDetachShader(progid,shid) );
		GLCALL( glDeleteShader(shid) );
	}
	for(auto shid : fsids) {
		GLCALL( glDetachShader(progid,shid) );
		GLCALL( glDeleteShader(shid) );
	}

	// Check the program
	GLint result = GL_FALSE;
	int infologlen;
	GLCALL( glGetProgramiv(progid, GL_LINK_STATUS, &result) );
	GLCALL( glGetProgramiv(progid, GL_INFO_LOG_LENGTH, &infologlen) );
	if(!result) {
		if(infologlen > 1) {
			std::vector<char> progerr(infologlen+1);
			GLCALL( glGetProgramInfoLog(progid, infologlen, nullptr, &progerr[0]) );
			PERRF(LOG_GUI, "Program error: '%s'\n", &progerr[0]);
		}
		throw std::exception();
	}

	return progid;
}

uintptr_t GUI_OpenGL::load_texture(SDL_Surface *_surface)
{
	assert(_surface);
	if(_surface->format->BytesPerPixel != 4) {
		throw std::runtime_error("Unsupported image format");
	}
	SDL_LockSurface(_surface);
	GLuint gltex;
	GLCALL( glGenTextures(1, &gltex) );
	GLCALL( glBindTexture(GL_TEXTURE_2D, gltex) );
	GLCALL( glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
		_surface->w, _surface->h,
		0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV,
		_surface->pixels
		)
	);
	SDL_UnlockSurface(_surface);
	return gltex;
}

uintptr_t GUI_OpenGL::load_texture(const std::string &_path, vec2i *_texdim)
{
	SDL_Surface *surface = IMG_Load(_path.c_str());
	if(!surface) {
		throw std::runtime_error("Unable to load image file");
	}
	GLuint gltex;
	try {
		gltex = load_texture(surface);
	} catch(std::exception &e) {
		SDL_FreeSurface(surface);
		throw;
	}
	if(_texdim) {
		*_texdim = vec2i(surface->w, surface->h);
	}
	SDL_FreeSurface(surface);
	return gltex;
}

const char * GetGLErrorString(GLenum _error_code)
{
	switch(_error_code) {
		case GL_INVALID_ENUM:
			return "GL_INVALID_ENUM An unacceptable value is specified for an enumerated argument.";
		case GL_INVALID_VALUE:
			return "GL_INVALID_VALUE A numeric argument is out of range.";
		case GL_INVALID_OPERATION:
			return "GL_INVALID_OPERATION The specified operation is not allowed in the current state.";
		case GL_INVALID_FRAMEBUFFER_OPERATION:
			return "GL_INVALID_FRAMEBUFFER_OPERATION The framebuffer object is not complete.";
		case GL_OUT_OF_MEMORY:
			return "GL_OUT_OF_MEMORY There is not enough memory left to execute the command.";
		case GL_STACK_UNDERFLOW:
			return "GL_STACK_UNDERFLOW An attempt has been made to perform an operation that would cause an internal stack to underflow.";
		case GL_STACK_OVERFLOW:
			return "GL_STACK_OVERFLOW An attempt has been made to perform an operation that would cause an internal stack to overflow.";
		default:
			break;
	}
	return "unknown error";
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