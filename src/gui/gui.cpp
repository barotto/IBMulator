/*
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ibmulator.h"
#include "program.h"
#include "gui.h"
#include "machine.h"
#include "mixer.h"
#include "keys.h"
#include "keymap.h"
#include "hardware/devices/vga.h"
#include "hardware/devices/vgadisplay.h"

#include <Rocket/Core.h>
#include <Rocket/Controls.h>
#include <Rocket/Debugger.h>
#include "gui/rocket/sys_interface.h"
#include "gui/rocket/rend_interface.h"
#include "gui/rocket/file_interface.h"
#include <SDL_image.h>

#include <algorithm>


GUI g_gui;

ini_enum_map_t g_mouse_types = {
	{ "none", MOUSE_TYPE_NONE },
	{ "ps2", MOUSE_TYPE_PS2 },
	{ "imps2", MOUSE_TYPE_IMPS2 },
	{ "serial", MOUSE_TYPE_SERIAL },
	{ "serial-wheel", MOUSE_TYPE_SERIAL_WHEEL },
	{ "serial-msys", MOUSE_TYPE_SERIAL_MSYS }
};


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

std::map<std::string, uint> GUI::ms_gui_modes = {
	{ "compact", GUI_MODE_COMPACT },
	{ "normal", GUI_MODE_NORMAL }
};

std::map<std::string, uint> gui_sampler = {
	{ "nearest", DISPLAY_SAMPLER_NEAREST },
	{ "linear", DISPLAY_SAMPLER_LINEAR }
};

std::map<std::string, uint> display_aspect = {
	{ "original", DISPLAY_ASPECT_ORIGINAL },
	{ "adaptive", DISPLAY_ASPECT_ADAPTIVE },
	{ "scaled", DISPLAY_ASPECT_SCALED }
};

GUI::GUI()
:
m_rocket_renderer(NULL),
m_rocket_sys_interface(NULL),
m_rocket_file_interface(NULL),
m_symspeed_factor(1.0)
{

}

GUI::~GUI()
{
	delete m_rocket_renderer;
	delete m_rocket_sys_interface;
	delete m_rocket_file_interface;
}

GUI::Display::Display()
:
vb_data{
	-1.0f, -1.0f, 0.0f,
	 1.0f, -1.0f, 0.0f,
	-1.0f,  1.0f, 0.0f,
	-1.0f,  1.0f, 0.0f,
	 1.0f, -1.0f, 0.0f,
	 1.0f,  1.0f, 0.0f
}{}

void GUI::init(Machine *_machine, Mixer *_mixer)
{
	m_machine = _machine;
	m_mixer = _mixer;
	m_assets_path = g_program.config().get_assets_home() + FS_SEP "gui" FS_SEP;

	if(SDL_VideoInit(NULL) != 0) {
		PERRF(LOG_GUI, "unable to initialize SDL video: %s\n", SDL_GetError());
		throw std::exception();
	}

	const char* video_driver = SDL_GetVideoDriver(0);
	if(video_driver) {
		PINFOF(LOG_V1,LOG_GUI,"Video driver: %s\n", video_driver);
	} else {
		PERRF(LOG_GUI, "SDL_GetVideoDriver(): %s\n", SDL_GetError());
		throw std::exception();
	}

	int flags = SDL_WINDOW_RESIZABLE;
	if(g_program.config().get_bool(GUI_SECTION, GUI_FULLSCREEN)) {
		flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}
	int w,h;

	//try to parse the width as a scaling factor
	std::string widths = g_program.config().get_string(GUI_SECTION, GUI_WIDTH);
	if(widths.at(widths.length()-1) == 'x') {
		int scan = sscanf(widths.c_str(), "%ux", &m_display.scaling);
		if(scan == 1) {
			//sensible defaults
			w = 640;
			h = 480;
		} else {
			PERRF(LOG_GUI, "invalid scaling factor: '%s'\n", widths.c_str());
			throw std::exception();
		}
	} else {
		//try as a pixel int value
		w = g_program.config().get_int(GUI_SECTION, GUI_WIDTH);
		h = g_program.config().get_int(GUI_SECTION, GUI_HEIGHT);
		m_display.scaling = 0;
	}

	m_display.mvmat.load_identity();

	m_mode = g_program.config().get_enum(GUI_SECTION, GUI_MODE, ms_gui_modes);
	if(m_mode==GUI_MODE_NORMAL) {
		h += std::min(256, w/4); //the sysunit proportions are 4:1
	}
	//the projection matrix is used only for libRocket
	m_display.projmat = mat4_ortho<float>(0, w, h, 0, 0, 1);

	/*** WINDOW CREATION ***/
	create_window(PACKAGE_STRING, w, h, flags);


	glewExperimental = GL_TRUE;
	GLenum err = glewInit();
	if(GLEW_OK != err)	{
		PERRF(LOG_GUI,"GLEW ERROR: %s\n", glewGetErrorString(err));
		shutdown_SDL();
		throw std::exception();
	}
	PINFOF(LOG_V1, LOG_GUI, "Using GLEW %s\n", glewGetString(GLEW_VERSION));
	glGetError();

	try {
		check_device_caps();
		init_Rocket();
	} catch(std::exception &e) {
		shutdown_SDL();
		throw;
	}

	m_windows.init(m_machine, this, m_mixer);

	try {
		g_keymap.load(g_program.config().find_file(GUI_SECTION,GUI_KEYMAP));
	} catch(std::exception &e) {
		PERRF(LOG_GUI, "Unable to load the keymap!\n");
		shutdown_SDL();
		throw std::exception();
	}

	m_gui_visible = true;
	m_input_grab = false;
	m_grab_method = g_program.config().get_string(GUI_SECTION,GUI_GRAB_METHOD);
	std::transform(m_grab_method.begin(), m_grab_method.end(), m_grab_method.begin(), ::tolower);

	m_mouse.grab = g_program.config().get_bool(GUI_SECTION,GUI_MOUSE_GRAB);

	//at this point the VGA is already inited (see machine::init)
	g_vga.attach_display(&m_display.vga);
	load_splash_image();

	try {
		m_display.prog = load_GLSL_program(
				g_program.config().find_file(GUI_SECTION,GUI_FB_VERTEX_SHADER),
				g_program.config().find_file(GUI_SECTION,GUI_FB_FRAGMENT_SHADER)
		);
	} catch(std::exception &e) {
		PERRF(LOG_GUI, "Unable to create the shader program!\n");
		shutdown_SDL();
		throw std::exception();
	}
	//find the uniforms
	GLCALL( m_display.uniforms.ch0 = glGetUniformLocation(m_display.prog, "iChannel0") );
	if(m_display.uniforms.ch0 == -1) {
		PWARNF(LOG_GUI, "iChannel0 not found in shader program\n");
	}
	GLCALL( m_display.uniforms.ch0size = glGetUniformLocation(m_display.prog, "iCh0Size") );
	if(m_display.uniforms.ch0size == -1) {
		PWARNF(LOG_GUI, "iCh0Size not found in shader program\n");
	}
	GLCALL( m_display.uniforms.res = glGetUniformLocation(m_display.prog, "iResolution") );
	if(m_display.uniforms.res == -1) {
		PWARNF(LOG_GUI, "iResolution not found in shader program\n");
	}
	GLCALL( m_display.uniforms.mvmat = glGetUniformLocation(m_display.prog, "iModelView") );
	if(m_display.uniforms.mvmat == -1) {
		PWARNF(LOG_GUI, "iModelView not found in shader program\n");
	}

	m_display.glintf = GL_RGBA8;
	m_display.glf = GL_RGBA;
	m_display.gltype = GL_UNSIGNED_INT_8_8_8_8_REV;
	GLCALL( glGenTextures(1, &m_display.tex) );
	GLCALL( glBindTexture(GL_TEXTURE_2D, m_display.tex) );
	/* OpenGL 4.2+
	GLCALL( glTexStorage2D(GL_TEXTURE_2D, 0, m_display_glintf,
			m_display.get_fb_xsize(),
			m_display.get_fb_ysize())
	);
	*/

	GLCALL(
			glTexImage2D(GL_TEXTURE_2D, 0, m_display.glintf,
			m_display.vga.get_fb_xsize(), m_display.vga.get_fb_ysize(),
			0, m_display.glf, m_display.gltype, NULL)
	);

	GLCALL( glGenSamplers(1, &m_display.sampler) );
	GLCALL( glSamplerParameteri(m_display.sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE) );
	GLCALL( glSamplerParameteri(m_display.sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE) );
	uint sampler = g_program.config().get_enum(GUI_SECTION, GUI_SAMPLER, gui_sampler);
	if(sampler == DISPLAY_SAMPLER_NEAREST) {
		GLCALL( glSamplerParameteri(m_display.sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST) );
		GLCALL( glSamplerParameteri(m_display.sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST) );
	} else {
		GLCALL( glSamplerParameteri(m_display.sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR) );
		GLCALL( glSamplerParameteri(m_display.sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR) );
	}

	GLCALL( glGenBuffers(1, &m_display.vb) );
	GLCALL( glBindBuffer(GL_ARRAY_BUFFER, m_display.vb) );
	GLCALL( glBufferData(GL_ARRAY_BUFFER, sizeof(m_display.vb_data), m_display.vb_data, GL_DYNAMIC_DRAW) );

	GLCALL( glBindBuffer(GL_ARRAY_BUFFER, 0) );
	GLCALL( glBindTexture(GL_TEXTURE_2D, 0) );

	SDL_SetRenderDrawColor(m_SDL_renderer,
			g_program.config().get_int(GUI_SECTION, GUI_BG_R),
			g_program.config().get_int(GUI_SECTION, GUI_BG_G),
			g_program.config().get_int(GUI_SECTION, GUI_BG_B),
			255);

	m_display.aspect = g_program.config().get_enum(GUI_SECTION,GUI_ASPECT,display_aspect);
	update_window_size(w,h);

	m_second_timer = SDL_AddTimer(1000, GUI::every_second, NULL);
	m_display.vga_updated = true;
}

void GUI::create_window(const char * _title, int _width, int _height, int _flags)
{
	int x = SDL_WINDOWPOS_CENTERED;
	int y = SDL_WINDOWPOS_CENTERED;
	int display = 0;

	int ndisplays = SDL_GetNumVideoDisplays();
	if(display > ndisplays-1) {
		display = 0;
	}

	SDL_DisplayMode dm;

	/* currently useless info bc i'm using desktop fullscreen
	int modes = SDL_GetNumDisplayModes(display);
	PINFOF(LOG_V2,LOG_GUI,"Available video modes:\n");
	for(int i=0; i<modes; i++) {
		if(SDL_GetDisplayMode(display, i, &dm) == 0) {
			PINFOF(LOG_V2,LOG_GUI,"%dx%d @ %dHz\n", dm.w, dm.h, dm.refresh_rate);
		}
	}
	*/

	SDL_GetDesktopDisplayMode(display, &m_SDL_fullmode);
	SDL_GetCurrentDisplayMode(display, &m_SDL_windowmode);
	m_SDL_windowmode.w = _width;
	m_SDL_windowmode.h = _height;
	if(_flags & SDL_WINDOW_FULLSCREEN) {
		ASSERT(_flags & 0x00001000); //check the DESKTOP mode
		/* desktop mode is the only mode that really works. don't even think
		 * using the "real" fullscreen mode.
		 */
		dm = m_SDL_fullmode;
		x = 0;
		y = 0;
		PINFOF(LOG_V1,LOG_GUI,"Requested video mode: fullscreen (%dx%d)\n",
				dm.w, dm.h);
	} else {
		dm = m_SDL_windowmode;
		PINFOF(LOG_V1,LOG_GUI,"Requested video mode: %dx%d\n", dm.w, dm.h);
	}
	m_width = dm.w;
	m_height = dm.h;
	m_half_width = m_width/2;
	m_half_height = m_height/2;

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	m_SDL_window = SDL_CreateWindow(_title, x, y, m_width, m_height, _flags | SDL_WINDOW_OPENGL);
	if(!m_SDL_window) {
		PERRF(LOG_GUI, "SDL_CreateWindow(): %s\n", SDL_GetError());
		throw std::exception();
	}

	//the program icon
	std::string iconfile = g_program.config().get_assets_home() + FS_SEP "icon.png";
	SDL_Surface* icon = IMG_Load(iconfile.c_str());
	if(icon) {
		//this function must be called before SDL_CreateRenderer!
		SDL_SetWindowIcon(m_SDL_window, icon);
		SDL_FreeSurface(icon);
	} else {
		PERRF(LOG_GUI, "unable to load app icon '%s'\n", iconfile.c_str());
	}

	PINFOF(LOG_V0,LOG_GUI,"Selected video mode: %dx%d @ %dHz\n", m_width, m_height, dm.refresh_rate);

	m_SDL_glcontext = SDL_GL_CreateContext(m_SDL_window);
	if(!m_SDL_glcontext) {
		SDL_DestroyWindow(m_SDL_window);
		PERRF(LOG_GUI, "SDL_GL_CreateContext(): %s\n", SDL_GetError());
		throw std::exception();
	}

	int oglIdx = -1;
	int nRD = SDL_GetNumRenderDrivers();
	for(int i=0; i<nRD; i++) {
		SDL_RendererInfo info;
		if(SDL_GetRenderDriverInfo(i, &info) == 0) {
			if(strcmp(info.name, "opengl") == 0) {
				oglIdx = i;
			}
		}
	}

	m_SDL_renderer = SDL_CreateRenderer(m_SDL_window, oglIdx,
			SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    //SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

	SDL_ShowWindow(m_SDL_window);

	m_wnd_title = _title;
	m_curr_title = m_wnd_title;
}

void GUI::toggle_fullscreen()
{
	Uint32 flags = (SDL_GetWindowFlags(m_SDL_window) ^ SDL_WINDOW_FULLSCREEN_DESKTOP);
    if(SDL_SetWindowFullscreen(m_SDL_window, flags) != 0) {
        PERRF(LOG_GUI, "Toggling fullscreen mode failed: %s\n", SDL_GetError());
        return;
    }

    if(flags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
    	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    	SDL_RenderSetLogicalSize(m_SDL_renderer, m_SDL_fullmode.w, m_SDL_fullmode.h);
    }
    //SDL_SetWindowSize(m_SDL_window, m_SDL_windowmode.w, m_SDL_windowmode.h);
}

void GUI::check_device_caps()
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
		extension = glGetStringi( GL_EXTENSIONS, ext_count );
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
			NULL, //ids
			GL_TRUE //enabled
		) );
#endif
		m_gl_errors_count = 0;
	}
}

void GUI::init_Rocket()
{
	m_rocket_renderer = new RocketRenderer(m_SDL_renderer, m_SDL_window);
	m_rocket_sys_interface = new RocketSystemInterface();
	m_rocket_file_interface = new RocketFileInterface(m_assets_path.c_str());

	Rocket::Core::SetFileInterface(m_rocket_file_interface);
	Rocket::Core::SetRenderInterface(m_rocket_renderer);
    Rocket::Core::SetSystemInterface(m_rocket_sys_interface);

	if(!Rocket::Core::Initialise()) {
		PERRF(LOG_GUI, "Unable to initialise libRocket\n");
		throw std::exception();
	}
	Rocket::Core::FontDatabase::LoadFontFace("fonts/ProFontWindows.ttf");
	Rocket::Core::FontDatabase::LoadFontFace("fonts/Nouveau_IBM.ttf");
	m_rocket_context = Rocket::Core::CreateContext("default", Rocket::Core::Vector2i(m_width, m_height));
	Rocket::Debugger::Initialise(m_rocket_context);
	Rocket::Controls::Initialise();
}

Rocket::Core::ElementDocument * GUI::load_document(const std::string &_filename)
{
	Rocket::Core::ElementDocument * document = m_rocket_context->LoadDocument(_filename.c_str());

	if(document) {
		Rocket::Core::Element * title = document->GetElementById("title");
		if(title) {
			title->SetInnerRML(document->GetTitle());
		}
		PDEBUGF(LOG_V2,LOG_GUI,"Document \"%s\" loaded\n", _filename.c_str());
	} else {
		PERRF(LOG_GUI, "Document \"%s\" is NULL\n", _filename.c_str());
	}

	return document;
}

void GUI::GL_debug_output(
		GLenum _source,
		GLenum _type,
		GLuint /*_id*/,
		GLenum _severity,
		GLsizei /*_length*/,
		const GLchar *_message,
		GLvoid *_userParam
		)
{
	GUI* gui = (GUI*)_userParam;

	string source;
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

	int type;
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

	int verb;
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

void GUI::render()
{
	SDL_RenderClear(m_SDL_renderer);
	render_vga();
	m_rocket_context->Render();
	SDL_RenderPresent(m_SDL_renderer);
}

void GUI::render_vga()
{
	GLCALL( glActiveTexture(GL_TEXTURE0) );
	GLCALL( glBindTexture(GL_TEXTURE_2D, m_display.tex) );

	if(m_display.vga_updated) {
		m_display.vga_updated = false;
		m_display.vga.lock();
		m_display.vga_size = vec2i(m_display.vga.get_fb_xsize(),m_display.vga.get_fb_ysize());
		m_display.vga_res = vec2i(m_display.vga.get_screen_xres(),m_display.vga.get_screen_yres());

		GLCALL( glTexSubImage2D(GL_TEXTURE_2D, 0,
				0, 0,
				m_display.vga_size.x, m_display.vga_size.y,
				m_display.glf, m_display.gltype,
				m_display.vga.get_framebuffer()) );

		m_display.vga.unlock();
	}

	GLCALL( glBindSampler(0, m_display.sampler) );
	GLCALL( glUseProgram(m_display.prog) );

	GLCALL( glUniform1i(m_display.uniforms.ch0, 0) );
	GLCALL( glUniform2iv(m_display.uniforms.ch0size, 1, m_display.vga_size) );
	GLCALL( glUniform2iv(m_display.uniforms.res, 1, m_display.vga_res) );
	GLCALL( glUniformMatrix4fv(m_display.uniforms.mvmat, 1, GL_FALSE, m_display.mvmat.data()) );

	GLCALL( glDisable(GL_BLEND) );
	GLCALL( glDisable(GL_LIGHTING) );
	//GLCALL( glDisable(GL_DEPTH_TEST) );

	GLCALL( glViewport(0,0,	m_width, m_height) );

	GLCALL( glEnableVertexAttribArray(0) );
	GLCALL( glBindBuffer(GL_ARRAY_BUFFER, m_display.vb) );
	GLCALL( glVertexAttribPointer(
            0,        // attribute 0. must match the layout in the shader.
            3,        // size
            GL_FLOAT, // type
            GL_FALSE, // normalized?
            0,        // stride
            (void*)0  // array buffer offset
    ) );

	GLCALL( glDrawArrays(GL_TRIANGLES, 0, 6) ); // 2*3 indices starting at 0 -> 2 triangles

	GLCALL( glDisableVertexAttribArray(0) );
	GLCALL( glBindBuffer(GL_ARRAY_BUFFER, 0) );
	GLCALL( glBindTexture(GL_TEXTURE_2D, 0) );
}

void GUI::input_grab(bool _value)
{
	if(m_mouse.grab) {
		if(_value) {
			SDL_ShowCursor(0);
			SDL_SetWindowGrab(m_SDL_window, SDL_TRUE);
		} else {
			SDL_ShowCursor(1);
			SDL_SetWindowGrab(m_SDL_window, SDL_FALSE);
		}
	}
	m_input_grab = _value;
}

void GUI::toggle_input_grab()
{
	input_grab(!m_input_grab);
}

bool GUI::dispatch_special_keys(const SDL_Event &_event)
{
	if(_event.type == SDL_KEYDOWN || _event.type == SDL_KEYUP) {
		if(_event.key.keysym.mod & KMOD_CTRL) {
			switch(_event.key.keysym.sym) {
				case SDLK_F1: {
					//show/hide main interface
					if(_event.type == SDL_KEYUP || m_mode!=GUI_MODE_COMPACT) return true;
					m_windows.toggle();
					if(m_windows.visible) {
						input_grab(false);
					}
					return true;
				}
				case SDLK_F4: {
					//show/hide debug windows
					if(_event.type == SDL_KEYUP) return true;
					m_windows.toggle_dbg();
					return true;
				}
				case SDLK_F5: {
					//take screenshot
					if(_event.type == SDL_KEYUP) return true;
					std::string path =
							g_program.config().find_file(PROGRAM_SECTION, PROGRAM_CAPTURE_DIR);
					path = Program::get_next_filename(path, "screenshot_", ".png");
					if(!path.empty()) {
						save_framebuffer(path + ".png");
					}
					return true;
				}
				case SDLK_F6: {
					//start/stop audio capture
					if(_event.type == SDL_KEYUP) return true;
					g_mixer.cmd_toggle_capture();
					return true;
				}
				case SDLK_F10: {
					//mouse grab
					if(m_grab_method.compare("ctrl-f10") != 0) return false;
					if(_event.type == SDL_KEYUP) return true;
					toggle_input_grab();
					if(m_mode == GUI_MODE_COMPACT) {
						m_windows.show(!m_input_grab);
					}
					return true;
				}
				case SDLK_F11: {
					//emulation speed up
					if(_event.type == SDL_KEYUP) return true;
					m_symspeed_factor *= 0.9;
					if(m_symspeed_factor<1.0 && m_symspeed_factor>0.95) {
						m_symspeed_factor = 1.0;
					} else if(m_symspeed_factor<0.0000001) {
						m_symspeed_factor = 0.0;
						m_machine->cmd_pause();
					}
					m_machine->cmd_cycles_adjust(m_symspeed_factor);
					return true;
				}
				case SDLK_F12: {
					//emulation speed down
					if(_event.type == SDL_KEYUP) return true;
					m_symspeed_factor *= 1.1;
					if(m_symspeed_factor>1.0 && m_symspeed_factor<1.1) {
						m_symspeed_factor = 1.0;
					} else if(m_symspeed_factor>2.0) {
						m_symspeed_factor = 2.0;
					}
					m_machine->cmd_cycles_adjust(m_symspeed_factor);
					return true;
				}
			}
		} else if(_event.key.keysym.mod & KMOD_ALT) {
			switch(_event.key.keysym.sym) {
				case SDLK_RETURN: {
					if(_event.type == SDL_KEYUP) return true;
					toggle_fullscreen();
					return true;
				}
				case SDLK_PAUSE: {
					if(_event.type == SDL_KEYUP) return true;
					if(m_machine->is_paused()) {
						m_machine->cmd_resume();
					} else {
						m_machine->cmd_pause();
					}
					return true;
				}
			}
		}
	} else {
		switch(_event.type) {
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP: {
				if(_event.button.button == SDL_BUTTON_MIDDLE) {
					if(m_grab_method.compare("mouse3") != 0) return false;
					if(_event.type == SDL_MOUSEBUTTONUP) return true;
					toggle_input_grab();
					if(m_mode == GUI_MODE_COMPACT) {
						m_windows.show(!m_input_grab);
					}
					return true;
				}
				break;
			}
		}
	}
	return false;
}

void GUI::dispatch_event(const SDL_Event &_event)
{
	if(_event.type == SDL_WINDOWEVENT) {
		dispatch_window_event(_event.window);
	} else if(_event.type == SDL_USEREVENT) {
		//the 1-second timer
		uint expected, current = m_machine->get_bench().beat_count;
		static ulong previous = UINT_MAX;
		if(!MULTITHREADED) {
			expected = 1.0e6 / g_program.get_beat_time_usec();
		} else {
			expected = 1.0e6 / MACHINE_HEARTBEAT;
		}
		if(previous < expected && current < expected) {
			std::string title = m_curr_title + " !";
			SDL_SetWindowTitle(m_SDL_window, title.c_str());
			m_windows.interface->show_warning(true);
		} else {
			SDL_SetWindowTitle(m_SDL_window, m_curr_title.c_str());
			m_windows.interface->show_warning(false);
		}
		previous = current;
	} else {
		if(dispatch_special_keys(_event)) {
			return;
		}
		if(m_input_grab) {
			dispatch_hw_event(_event);
			return;
		}
		if( (_event.type==SDL_KEYDOWN || _event.type==SDL_KEYUP) && !m_windows.needs_input() )
		{
			dispatch_hw_event(_event);
		} else {
			dispatch_rocket_event(_event);
		}
	}
}

void GUI::dispatch_hw_event(const SDL_Event &_event)
{
	uint32_t key_event;
	uint8_t mouse_state;
	uint8_t buttons;


	if(!m_input_grab && (
			_event.type == SDL_MOUSEMOTION ||
			_event.type == SDL_MOUSEBUTTONDOWN ||
			_event.type == SDL_MOUSEBUTTONUP ||
			_event.type == SDL_MOUSEWHEEL)
	) {
		return;
	}

	switch(_event.type)
	{
	case SDL_MOUSEMOTION:
		if(m_mouse.warped
			&& _event.motion.x == m_half_width
			&& _event.motion.y == m_half_height)
		{
			// This event was generated as a side effect of the WarpMouse,
			// and it must be ignored.
			m_mouse.warped = false;
			break;
		}

		buttons  = bool(_event.motion.state & SDL_BUTTON(SDL_BUTTON_LEFT));
		buttons |= bool(_event.motion.state & SDL_BUTTON(SDL_BUTTON_RIGHT)) << 1;
		buttons |= bool(_event.motion.state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) << 2;

		m_machine->mouse_motion(_event.motion.xrel, -_event.motion.yrel, 0, buttons);

		SDL_WarpMouseInWindow(m_SDL_window, m_half_width, m_half_height);
		m_mouse.warped = true;

		break;

	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
		if(_event.button.button == SDL_BUTTON_MIDDLE) {
			break;
		}

		mouse_state = SDL_GetMouseState(NULL, NULL);

		buttons  = bool(mouse_state & SDL_BUTTON(SDL_BUTTON_LEFT));
		buttons |= bool(mouse_state & SDL_BUTTON(SDL_BUTTON_RIGHT)) << 1;
		buttons |= bool(mouse_state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) << 2;
		m_machine->mouse_motion(0,0,0, buttons);

		break;

	case SDL_MOUSEWHEEL:
		//no wheel on the ps/1. should I implement it for IM?
		break;

	case SDL_KEYDOWN: {
		KeyEntry *entry = g_keymap.find_host_key(_event.key.keysym.sym);
		if(!entry) {
			PERRF(LOG_GUI,"host key %d (0x%x) not mapped!\n",
					(uint) _event.key.keysym.sym,
					(uint) _event.key.keysym.sym);
			break;
		}
		key_event = entry->baseKey;
		m_machine->send_key_to_kbctrl(key_event);
		break;
	}
	case SDL_KEYUP: {
		KeyEntry *entry = g_keymap.find_host_key(_event.key.keysym.sym);
		if(!entry) {
			PERRF(LOG_GUI,"host key %d (0x%x) not mapped!\n",
					(uint) _event.key.keysym.sym,
					(uint) _event.key.keysym.sym);
			break;
		}
		key_event = entry->baseKey;
		if(key_event == KEY_UNHANDLED)
			break;
		m_machine->send_key_to_kbctrl(key_event | KEY_RELEASED);
		break;
	}
	default:
		break;
	}
}

void GUI::update_display_size()
{
	float xs = 1.f, ys = 1.f;
	float xt = 0.f, yt = 0.f;
	uint sysunit_h = m_width/4; //the sysunit proportions are 4:1
	sysunit_h = std::min(256u, sysunit_h);
	uint sysunit_w = sysunit_h*4;

	int disp_w, disp_h;
	int disp_area_w = m_width, disp_area_h = m_height;
	if(m_display.scaling>0) {
		disp_w = m_display.vga.get_screen_xres() * m_display.scaling;
		disp_h = m_display.vga.get_screen_yres() * m_display.scaling;
	} else {
		disp_w = disp_area_w;
		disp_h = disp_area_h;
	}

	if(m_mode==GUI_MODE_NORMAL) {
		disp_area_h = m_height - sysunit_h;
		m_windows.interface->update_size(sysunit_w, sysunit_h);
	}
	disp_w = std::min(disp_w,disp_area_w);
	disp_h = std::min(disp_h,disp_area_h);

	float ratio;
	if(m_display.aspect == DISPLAY_ASPECT_ORIGINAL) {
		ratio = 1.333333f; //4:3
	} else if(m_display.aspect == DISPLAY_ASPECT_ADAPTIVE) {
		ratio = float(m_display.vga.get_screen_xres()) / float(m_display.vga.get_screen_yres());
	} else {
		//SCALED
		ratio = float(disp_w) / float(disp_h);
	}
	disp_w = round(float(disp_h) * ratio);
	//xs = float(disp_w)/float(disp_area_w);
	xs = float(disp_w)/float(m_width);
	if(xs>1.0f) {
		disp_w = disp_area_w;
		xs = 1.0f;
		disp_h = round(float(disp_w) / ratio);
	}
	if(m_display.aspect == DISPLAY_ASPECT_SCALED) {
		ratio = float(disp_w) / float(disp_h);
	}
	//ys = float(disp_h)/float(disp_area_h);
	ys = float(disp_h)/float(m_height);
	if(m_mode==GUI_MODE_NORMAL) {
		yt = 1.f - ys; //aligned to top
	}
	if(ys>1.f) {
		disp_h = disp_area_h;
		//ys = 1.f;
		ys = float(disp_h)/float(m_height);
		yt = 0.f;
		if(m_display.aspect == DISPLAY_ASPECT_SCALED) {
			ratio = float(disp_w) / float(disp_h);
		}
		disp_w = round(float(disp_h) * ratio);
		//xs = float(disp_w)/float(disp_area_w);
		xs = float(disp_w)/float(m_width);
	}
	m_display.mvmat.load_scale(xs, ys, 1.0);
	m_display.mvmat.load_translation(xt, yt, 0.0);
}

void GUI::update_window_size(int _w, int _h)
{
	m_width = _w;
	m_height = _h;
	m_half_width = m_width/2;
	m_half_height = m_height/2;
	m_display.projmat = mat4_ortho<float>(0, m_width, m_height, 0, 0, 1);
	m_rocket_context->SetDimensions(Rocket::Core::Vector2i(m_width,m_height));

	update_display_size();
}

void GUI::dispatch_window_event(const SDL_WindowEvent &_event)
{
	switch(_event.event) {
		case SDL_WINDOWEVENT_RESIZED: {
			PDEBUGF(LOG_V1, LOG_GUI, "%ux%u\n", _event.data1, _event.data2);
			update_window_size(_event.data1, _event.data2);
			break;
		}
		case SDL_WINDOWEVENT_MINIMIZED:
			break;
		case SDL_WINDOWEVENT_MAXIMIZED:
			PDEBUGF(LOG_V1, LOG_GUI, "maximized\n");
			break;
		case SDL_WINDOWEVENT_RESTORED:
			break;
		case SDL_WINDOWEVENT_ENTER:
			//mouse enter the window
			break;
		case SDL_WINDOWEVENT_LEAVE:
			//Mouse left window
			break;
		case SDL_WINDOWEVENT_FOCUS_GAINED:
			break;
		case SDL_WINDOWEVENT_FOCUS_LOST:
			break;
		case SDL_WINDOWEVENT_CLOSE:
			break;
		default:
			break;
	}

}

void GUI::dispatch_rocket_event(const SDL_Event &event)
{
	int rockmod = m_rocket_sys_interface->GetKeyModifiers();

	switch(event.type)
	{
	case SDL_MOUSEMOTION:

		m_rocket_context->ProcessMouseMove(
				event.motion.x, event.motion.y,
				rockmod
				);
		break;

	case SDL_MOUSEBUTTONDOWN:
		m_rocket_context->ProcessMouseButtonDown(
				m_rocket_sys_interface->TranslateMouseButton(event.button.button),
				rockmod
				);
		break;

	case SDL_MOUSEBUTTONUP:
		m_rocket_context->ProcessMouseButtonUp(
				m_rocket_sys_interface->TranslateMouseButton(event.button.button),
				rockmod
				);
		break;

	case SDL_MOUSEWHEEL:
		m_rocket_context->ProcessMouseWheel(
				-event.wheel.y,
				rockmod
				);
		break;

	case SDL_KEYDOWN: {
		Rocket::Core::Input::KeyIdentifier key =
				m_rocket_sys_interface->TranslateKey(event.key.keysym.sym);
		if(key != Rocket::Core::Input::KI_UNKNOWN)
			m_rocket_context->ProcessKeyDown(key,rockmod);
		Rocket::Core::word w = RocketSystemInterface::GetCharacterCode(key, rockmod);
		if(w > 0)
			m_rocket_context->ProcessTextInput(w);
		break;
	}
	case SDL_KEYUP:
		m_rocket_context->ProcessKeyUp(
				m_rocket_sys_interface->TranslateKey(event.key.keysym.sym),
				rockmod
				);
		break;
	default:
		break;
	}
}

void GUI::update()
{
	//g_vga.refresh_display(true); just a test, don't use, not thread safe

	if(m_display.aspect == DISPLAY_ASPECT_ADAPTIVE || m_display.scaling>0) {
		m_display.vga.lock();
		if(m_display.vga.get_dimension_updated()) {
			Uint32 flags = SDL_GetWindowFlags(m_SDL_window);
			//WARNING in order for the MAXIMIZED case to work under X11 you need
			//SDL 2.0.4 with this patch:
			//https://bugzilla.libsdl.org/show_bug.cgi?id=2793
			if(!(flags & SDL_WINDOW_FULLSCREEN)&&
			   !(flags & SDL_WINDOW_MAXIMIZED) &&
			   m_display.scaling)
			{
				int w = m_display.vga.get_screen_xres() * m_display.scaling;
				int h = m_display.vga.get_screen_yres() * m_display.scaling;
				if(m_mode==GUI_MODE_NORMAL) {
					h += std::min(256, w/4); //the sysunit proportions are 4:1
				}
				SDL_SetWindowSize(m_SDL_window,w,h);
				int cw,ch;
				SDL_GetWindowSize(m_SDL_window,&cw,&ch);
				if(cw!=w || ch!=h) {
					//not enough space?
					//what TODO ?
					w = cw;
					h = ch;
				}
				update_window_size(w, h);
			} else {
				update_display_size();
			}
			m_display.vga.reset_dimension_updated();
		}
		m_display.vga.unlock();
	}

	m_windows.update();
	m_rocket_context->Update();

	m_machine->ms_gui_lock.lock();
	if(m_machine->is_current_program_name_changed()) {
		m_curr_title = m_machine->get_current_program_name();
		if(!m_curr_title.empty()) {
			m_curr_title = m_wnd_title + " - " + m_curr_title;
		} else {
			m_curr_title = m_wnd_title;
		}
		SDL_SetWindowTitle(m_SDL_window, m_curr_title.c_str());
	}
	m_machine->ms_gui_lock.unlock();
}

void GUI::shutdown_SDL()
{
    SDL_DestroyRenderer(m_SDL_renderer);
    SDL_DestroyWindow(m_SDL_window);
	SDL_VideoQuit();
}

void GUI::shutdown()
{
	SDL_RemoveTimer(m_second_timer);

	m_rocket_context->RemoveReference();
	m_windows.shutdown();
    Rocket::Core::Shutdown();

    shutdown_SDL();
}

GLuint GUI::load_GLSL_program(const std::string &_vs_path, const std::string &_fs_path)
{
	// Read the Vertex Shader code from the file
	std::string vscode;
	std::ifstream vsstream(_vs_path, std::ios::in);
	if(vsstream.is_open()){
		std::string Line = "";
		while(getline(vsstream, Line))
			vscode += "\n" + Line;
		vsstream.close();
	} else {
		PERRF(LOG_GUI, "Unable to open '%s'\n", _vs_path.c_str());
		throw std::exception();
	}

	// Read the Fragment Shader code from the file
	std::string fscode;
	std::ifstream fsstream(_fs_path, std::ios::in);
	if(fsstream.is_open()){
		std::string Line = "";
		while(getline(fsstream, Line))
			fscode += "\n" + Line;
		fsstream.close();
	} else {
		PERRF(LOG_GUI, "Unable to open '%s'\n", _fs_path.c_str());
		throw std::exception();
	}

	GLint result = GL_FALSE;
	int infologlen;

	// Create the shaders
	GLuint vsid,fsid;
	GLCALL( vsid = glCreateShader(GL_VERTEX_SHADER) );
	GLCALL( fsid = glCreateShader(GL_FRAGMENT_SHADER) );

	// Compile Vertex Shader
	char const * source = vscode.c_str();
	GLCALL( glShaderSource(vsid, 1, &source , NULL) );
	GLCALL( glCompileShader(vsid) );

	// Check Vertex Shader
	GLCALL( glGetShaderiv(vsid, GL_COMPILE_STATUS, &result) );
	GLCALL( glGetShaderiv(vsid, GL_INFO_LOG_LENGTH, &infologlen) );
	if(!result && infologlen > 1) {
		std::vector<char> vserr(infologlen+1);
		GLCALL( glGetShaderInfoLog(vsid, infologlen, NULL, &vserr[0]) );
		PERRF(LOG_GUI, "VS error: '%s'\n", &vserr[0]);
	}

	// Compile Fragment Shader
	source = fscode.c_str();
	GLCALL( glShaderSource(fsid, 1, &source , NULL) );
	GLCALL( glCompileShader(fsid) );

	// Check Fragment Shader
	GLCALL( glGetShaderiv(fsid, GL_COMPILE_STATUS, &result) );
	GLCALL( glGetShaderiv(fsid, GL_INFO_LOG_LENGTH, &infologlen) );
	if(!result && infologlen > 1) {
		std::vector<char> fserr(infologlen+1);
		GLCALL( glGetShaderInfoLog(fsid, infologlen, NULL, &fserr[0]) );
		PERRF(LOG_GUI, "FS error: '%s'\n", &fserr[0]);
	}

	// Link the program
	GLuint progid;
	GLCALL( progid = glCreateProgram() );
	GLCALL( glAttachShader(progid, vsid) );
	GLCALL( glAttachShader(progid, fsid) );
	GLCALL( glLinkProgram(progid) );
	GLCALL( glDeleteShader(vsid) );
	GLCALL( glDeleteShader(fsid) );

	// Check the program
	GLCALL( glGetProgramiv(progid, GL_LINK_STATUS, &result) );
	GLCALL( glGetProgramiv(progid, GL_INFO_LOG_LENGTH, &infologlen) );

	if(!result) {
		if(infologlen > 1) {
			std::vector<char> progerr(infologlen+1);
			GLCALL( glGetProgramInfoLog(progid, infologlen, NULL, &progerr[0]) );
			PERRF(LOG_GUI, "Program error: '%s'\n", &progerr[0]);
		}
		throw std::exception();
	}

	return progid;
}

void GUI::save_framebuffer(std::string _path)
{

	/*
	m_display.vga.lock();
	SDL_Surface * surface = SDL_CreateRGBSurfaceFrom(
		m_display.get_framebuffer(),
		m_display.get_fb_xsize(),
		m_display.get_fb_ysize(),
		32,
		m_display.get_fb_xsize()*4,
		PALETTE_RMASK,
		PALETTE_GMASK,
		PALETTE_BMASK,
		PALETTE_AMASK
	);
	*/
	SDL_Surface * surface = SDL_CreateRGBSurface(
		0,
		m_display.vga.get_screen_xres(),
		m_display.vga.get_screen_yres(),
		32,
		PALETTE_RMASK,
		PALETTE_GMASK,
		PALETTE_BMASK,
		PALETTE_AMASK
	);
	if(!surface) {
		PERRF(LOG_GUI, "save_framebuffer() : unable to create buffer surface\n");
		return;
	}
	m_display.vga.lock();
	SDL_LockSurface(surface);
	m_display.vga.copy_screen((uint8_t*)surface->pixels);
	SDL_UnlockSurface(surface);
	m_display.vga.unlock();

	IMG_SavePNG(surface, _path.c_str());
	SDL_FreeSurface(surface);
}

void GUI::load_splash_image()
{
	std::string file = g_program.config().find_file(GUI_SECTION,GUI_START_IMAGE);

	if(file.empty()) {
		return;
	}

	SDL_Surface* surface = IMG_Load(file.c_str());
	if(surface) {
		SDL_LockSurface(surface);
		m_display.vga.lock();
		uint32_t * fb = m_display.vga.get_framebuffer();
		uint fbw = m_display.vga.get_fb_xsize();
		uint bytespp = surface->format->BytesPerPixel;
		for(int y=0; y<surface->h; y++) {
			for(int x=0; x<surface->w; x++) {
				if(x>int(m_display.vga.get_fb_xsize())) {
					break;
				}
				uint32_t pixel;
				uint pixelidx = y*surface->w*bytespp + x*bytespp;
				memcpy(&pixel, &((uint8_t*)surface->pixels)[pixelidx], bytespp);
				Uint8 r,g,b,a;
				SDL_GetRGBA(pixel, surface->format, &r, &g, &b, &a);
				pixel = PALETTE_ENTRY(r,g,b);
				pixelidx = y*fbw + x;
				fb[pixelidx] = pixel;
			}
			if(y>int(m_display.vga.get_fb_ysize())) {
				break;
			}
		}
		m_display.vga.unlock();
		SDL_UnlockSurface(surface);
		SDL_FreeSurface(surface);
	}
}

Uint32 GUI::every_second(Uint32 interval, void *param)
{
	SDL_Event event;
	SDL_UserEvent userevent;

	userevent.type = SDL_USEREVENT;
	userevent.code = 0;
	userevent.data1 = NULL;
	userevent.data2 = NULL;

	event.type = SDL_USEREVENT;
	event.user = userevent;

	SDL_PushEvent(&event);

	return interval;
}

GUI::Windows::Windows()
:
visible(true),
debug_wnds(false),
desktop(NULL),
interface(NULL),
debugger(NULL),
stats(NULL),
status(NULL),
devices(NULL)
{

}

void GUI::Windows::init(Machine *_machine, GUI *_gui, Mixer *_mixer)
{
	desktop = new Desktop(_gui);
	desktop->show();

	interface = new Interface(_machine,_gui);
	interface->show();

	if(g_program.config().get_bool(GUI_SECTION, GUI_SHOW_LEDS)) {
		status = new Status(_gui);
		status->show();
	}

	//debug
	debugger = new SysDebugger(_machine, _gui);
	stats = new Stats(_machine, _gui, _mixer);
	devices = new DevStatus(_gui);
}

void GUI::Windows::toggle()
{
	show(!visible);
}

void GUI::Windows::show(bool _value)
{
	//show only the main interface
	if(!_value) {
		interface->hide();
	} else {
		interface->show();
	}
	visible = _value;
}

void GUI::Windows::update()
{
	if(debug_wnds) {
		//debug windows are autonomous
		debugger->update();
		devices->update();
		stats->update();
	}

	//always update the status
	if(status) {
		status->update();
	}

	if(visible) {
		interface->update();
	}
}

void GUI::Windows::toggle_dbg()
{
	debug_wnds = !debug_wnds;
	if(debug_wnds) {
		debugger->show();
		devices->show();
		stats->show();
	} else {
		debugger->hide();
		devices->hide();
		stats->hide();
	}
}

bool GUI::Windows::needs_input()
{
	//only debug windows has kb input at the moment
	return debug_wnds;
}

void GUI::Windows::shutdown()
{
	delete status;
	status = NULL;

	delete debugger;
	debugger = NULL;

	delete devices;
	devices = NULL;

	delete stats;
	stats = NULL;

	delete desktop;
	desktop = NULL;

	delete interface;
	interface = NULL;
}

