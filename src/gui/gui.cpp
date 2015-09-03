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
#include "filesys.h"
#include "program.h"
#include "gui.h"
#include "machine.h"
#include "mixer.h"
#include "keys.h"
#include "keymap.h"
#include "utils.h"

#include <Rocket/Core.h>
#include <Rocket/Controls.h>
#include <Rocket/Debugger.h>
#include "gui/rocket/sys_interface.h"
#include "gui/rocket/rend_interface.h"
#include "gui/rocket/file_interface.h"
#include <SDL2/SDL_image.h>

#include "windows/desktop.h"
#include "windows/normal_interface.h"
#include "windows/realistic_interface.h"
#include "windows/status.h"
#include "windows/sysdebugger.h"
#include "windows/devstatus.h"
#include "windows/stats.h"

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
	{ "compact",   GUI_MODE_COMPACT },
	{ "normal",    GUI_MODE_NORMAL },
	{ "realistic", GUI_MODE_REALISTIC }
};

std::map<std::string, uint> GUI::ms_gui_sampler = {
	{ "nearest", DISPLAY_SAMPLER_NEAREST },
	{ "linear",  DISPLAY_SAMPLER_BILINEAR },
	{ "bilinear",DISPLAY_SAMPLER_BILINEAR },
	{ "bicubic", DISPLAY_SAMPLER_BICUBIC }
};

std::map<std::string, uint> GUI::ms_display_aspect = {
	{ "original", DISPLAY_ASPECT_ORIGINAL },
	{ "adaptive", DISPLAY_ASPECT_ADAPTIVE },
	{ "scaled", DISPLAY_ASPECT_SCALED }
};


GUI::GUI()
:
m_joystick0(JOY_NONE),
m_joystick1(JOY_NONE),
m_symspeed_factor(1.0),
m_rocket_renderer(NULL),
m_rocket_sys_interface(NULL),
m_rocket_file_interface(NULL)
{

}

GUI::~GUI()
{
	delete m_rocket_renderer;
	delete m_rocket_sys_interface;
	delete m_rocket_file_interface;
}

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

	m_mode = g_program.config().get_enum(GUI_SECTION, GUI_MODE, ms_gui_modes);

	/*** WINDOW CREATION ***/
	create_window(PACKAGE_STRING, 640, 480, SDL_WINDOW_RESIZABLE);

	glewExperimental = GL_FALSE;
	GLenum res = glewInit();
	if(res != GLEW_OK)	{
		PERRF(LOG_GUI,"GLEW ERROR: %s\n", glewGetErrorString(res));
		shutdown_SDL();
		throw std::exception();
	}
	PINFOF(LOG_V1, LOG_GUI, "Using GLEW %s\n", glewGetString(GLEW_VERSION));
	glGetError();

	try {
		check_device_caps();
		init_Rocket();
		m_windows.init(m_machine, this, m_mixer, m_mode);
	} catch(std::exception &e) {
		shutdown_SDL();
		throw;
	}

	vec2i wsize = m_windows.interface->get_size();
	resize_window(wsize.x, wsize.y);
	m_rocket_renderer->SetDimensions(wsize.x,wsize.y);

	SDL_SetWindowPosition(m_SDL_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	if(g_program.config().get_bool(GUI_SECTION, GUI_FULLSCREEN)) {
		toggle_fullscreen();
	}

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

	SDL_SetRenderDrawColor(m_SDL_renderer,
			g_program.config().get_int(GUI_SECTION, GUI_BG_R),
			g_program.config().get_int(GUI_SECTION, GUI_BG_G),
			g_program.config().get_int(GUI_SECTION, GUI_BG_B),
			255);

	m_second_timer = SDL_AddTimer(1000, GUI::every_second, NULL);

	if(SDL_InitSubSystem(SDL_INIT_JOYSTICK) != 0) {
		PWARNF(LOG_GUI, "Unable to init SDL Joystick subsystem: %s\n", SDL_GetError());
	} else {
		SDL_JoystickEventState(SDL_ENABLE);
		PDEBUGF(LOG_V2, LOG_GUI, "Joy evt state: %d\n", SDL_JoystickEventState(SDL_QUERY));
	}
}

void GUI::create_window(const char * _title, int _width, int _height, int _flags)
{
	int x, y;
	int display = 0;

	int ndisplays = SDL_GetNumVideoDisplays();
	if(display > ndisplays-1) {
		display = 0;
	}

	if(_flags & SDL_WINDOW_FULLSCREEN) {
		ASSERT(_flags & 0x00001000); //check the DESKTOP mode
		/* desktop mode is the only mode that really works. don't even think
		 * using the "real" fullscreen mode.
		 */
		SDL_DisplayMode desktop;
		SDL_GetDesktopDisplayMode(display, &desktop);
		x = 0;
		y = 0;
		m_width = desktop.w;
		m_height = desktop.h;
	} else {
		int x = SDL_WINDOWPOS_CENTERED;
		int y = SDL_WINDOWPOS_CENTERED;
		m_width = _width;
		m_height = _height;
	}

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	m_SDL_window = SDL_CreateWindow(_title, x, y, m_width, m_height, _flags | SDL_WINDOW_OPENGL);
	if(!m_SDL_window) {
		PERRF(LOG_GUI, "SDL_CreateWindow(): %s\n", SDL_GetError());
		throw std::exception();
	}

#ifndef _WIN32
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
#endif

	PINFOF(LOG_V0,LOG_GUI,"Selected video mode: %dx%d\n", m_width, m_height);

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

	SDL_ShowWindow(m_SDL_window);

	m_wnd_title = _title;
	m_curr_title = m_wnd_title;
}

vec2i GUI::resize_window(int _w, int _h)
{
	SDL_SetWindowSize(m_SDL_window, _w, _h);
	SDL_GetWindowSize(m_SDL_window, &m_width, &m_height);
	PINFOF(LOG_V0,LOG_GUI,"Window resized to %dx%d\n", m_width, m_height);
	update_window_size(m_width, m_height);
	return vec2i(m_width, m_height);
}

void GUI::toggle_fullscreen()
{
	Uint32 flags = (SDL_GetWindowFlags(m_SDL_window) ^ SDL_WINDOW_FULLSCREEN_DESKTOP);
    if(SDL_SetWindowFullscreen(m_SDL_window, flags) != 0) {
        PERRF(LOG_GUI, "Toggling fullscreen mode failed: %s\n", SDL_GetError());
        return;
    }
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
	GLCALL( glViewport(0,0,	m_width, m_height) );
	if(m_mode == GUI_MODE_REALISTIC) {
		//TODO move the rendering logic inside the Interface
		m_windows.interface->hide();
		m_windows.invert_visibility();
		m_rocket_context->Render();
		m_windows.interface->render_vga();
		m_windows.invert_visibility();
		m_rocket_context->Render();
		m_windows.interface->show();
	} else {
		m_windows.interface->render_vga();
		m_rocket_context->Render();
	}
	SDL_RenderPresent(m_SDL_renderer);
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

bool GUI::dispatch_special_keys(const SDL_Event &_event, SDL_Keycode &_discard_next_key)
{
	_discard_next_key = 0;
	SDL_Keycode modifier_key = 0;

	if(_event.type == SDL_KEYDOWN || _event.type == SDL_KEYUP) {
		if(_event.key.keysym.mod & KMOD_CTRL) {
			modifier_key = (_event.key.keysym.mod & KMOD_RCTRL)?SDLK_RCTRL:SDLK_LCTRL;
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
				case SDLK_F3: {
					//machine on/off
					if(_event.type == SDL_KEYUP) {
						_discard_next_key = modifier_key;
						return true;
					}
					m_machine->cmd_switch_power();
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
					std::string screenfile = FileSys::get_next_filename(path, "screenshot_", ".png");
					if(!screenfile.empty()) {
						std::string palfile;
						#ifndef NDEBUG
						palfile = path + FS_SEP + "palette.png";
						#endif
						try {
							save_framebuffer(screenfile, palfile);
							std::string mex = "screenshot saved to " + screenfile;
							PINFOF(LOG_V0, LOG_GUI, "%s\n", mex.c_str());
							show_message(mex.c_str());
						} catch(std::exception &e) { }
					}
					return true;
				}
				case SDLK_F6: {
					//start/stop audio capture
					if(_event.type == SDL_KEYUP) return true;
					m_mixer->cmd_toggle_capture();
					return true;
				}
				case SDLK_F7: {
					//save current machine state
					if(_event.type == SDL_KEYUP) return true;
					KeyEntry *entry = g_keymap.find_host_key(modifier_key);
					if(entry) {
						m_machine->send_key_to_kbctrl(entry->baseKey | KEY_RELEASED);
						_discard_next_key = modifier_key;
					}
					g_program.save_state("", [this]() {
						m_windows.interface->show_message("State saved");
					}, nullptr);
					return true;
				}
				case SDLK_F8: {
					//load last machine state
					if(_event.type == SDL_KEYUP) {
						_discard_next_key = modifier_key;
						return true;
					}
					g_program.restore_state("", [this]() {
						m_windows.interface->show_message("State restored");
					}, nullptr);
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
				case SDLK_DELETE: {
					//send CTRL+ALT+CANC
					if(_event.type == SDL_KEYUP) return true;
					//CTRL has been already sent
					m_machine->send_key_to_kbctrl(KEY_ALT_L);
					m_machine->send_key_to_kbctrl(KEY_DELETE);
					return true;
				}
				case SDLK_INSERT: {
					//send SysReq
					if(_event.type == SDL_KEYUP) return true;
					m_machine->send_key_to_kbctrl(KEY_ALT_SYSREQ);
					return true;
				}
			}
		} else if(_event.key.keysym.mod & KMOD_ALT) {
			modifier_key = (_event.key.keysym.mod & KMOD_LALT)?SDLK_LALT:SDLK_RALT;
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
	static bool special_key = false;
	static SDL_Keycode discard_next_key = 0;

	if(_event.type == SDL_WINDOWEVENT) {
		dispatch_window_event(_event.window);
	} else if(_event.type == SDL_USEREVENT) {
		//the 1-second timer
		uint expected, current = m_machine->get_bench().beat_count;
		static uint previous = UINT_MAX;
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
	} else if(_event.type == SDL_JOYDEVICEADDED) {
		SDL_Joystick *joy = SDL_JoystickOpen(_event.jdevice.which);
		if(joy) {
			m_SDL_joysticks.push_back(joy);
			int jinstance = m_SDL_joysticks.size()-1;
			int jid = JOY_NONE;
			if(m_joystick0 == JOY_NONE) {
				m_joystick0 = jinstance;
				jid = 0;
			} else if(m_joystick1 == JOY_NONE) {
				m_joystick1 = jinstance;
				jid = 1;
			}
			PINFOF(LOG_V0, LOG_GUI, "Joystick %d: %s (%d axes, %d buttons)\n",
				jid,
				SDL_JoystickName(joy),
				SDL_JoystickNumAxes(joy),
				SDL_JoystickNumButtons(joy)
			);
		} else {
			PWARNF(LOG_GUI, "Couldn't open Joystick %d\n", _event.jdevice.which);
		}
	} else if(_event.type == SDL_JOYDEVICEREMOVED) {
		PDEBUGF(LOG_V1, LOG_GUI, "Joystick id=%d has been removed\n", _event.jdevice.which);
		ASSERT(_event.jdevice.which <= m_SDL_joysticks.size());
		SDL_Joystick *joy = m_SDL_joysticks[_event.jdevice.which];
		if(SDL_JoystickGetAttached(joy)) {
			SDL_JoystickClose(joy);
		}
		m_SDL_joysticks[_event.jdevice.which] = nullptr;
		if(m_joystick0 == _event.jdevice.which) {
			PINFOF(LOG_V1, LOG_GUI, "Joystick 0 has been removed\n");
			m_joystick0 = m_joystick1;
			m_joystick1 = JOY_NONE;
		} else if(m_joystick1 == _event.jdevice.which) {
			PINFOF(LOG_V1, LOG_GUI, "Joystick 1 has been removed\n");
			m_joystick1 = JOY_NONE;
		}
		if(m_joystick1==JOY_NONE && m_joystick0!=JOY_NONE && SDL_NumJoysticks()>1) {
			for(int j=0; j<m_SDL_joysticks.size(); j++) {
				if(m_SDL_joysticks[j]!=nullptr && j!=m_joystick0) {
					m_joystick1 = j;
				}
			}
		}
		if(m_joystick0 != JOY_NONE) {
			PINFOF(LOG_V0, LOG_GUI, "Joystick 0: %s (%d axes, %d buttons)\n",
				SDL_JoystickName(m_SDL_joysticks[m_joystick0]),
				SDL_JoystickNumAxes(m_SDL_joysticks[m_joystick0]),
				SDL_JoystickNumButtons(m_SDL_joysticks[m_joystick0])
			);
		}
		if(m_joystick1 != JOY_NONE) {
			PINFOF(LOG_V0, LOG_GUI, "Joystick 1: %s (%d axes, %d buttons)\n",
				SDL_JoystickName(m_SDL_joysticks[m_joystick1]),
				SDL_JoystickNumAxes(m_SDL_joysticks[m_joystick1]),
				SDL_JoystickNumButtons(m_SDL_joysticks[m_joystick1])
			);
		}
	} else {
		if(discard_next_key && (_event.key.keysym.sym == discard_next_key)) {
			discard_next_key = 0;
			PDEBUGF(LOG_V2, LOG_GUI, "Discarded key: type=%d,sym=%d,mod=%d\n",
					_event.type, _event.key.keysym.sym, _event.key.keysym.mod);
			return;
		}
		if(dispatch_special_keys(_event,discard_next_key)) {
			PDEBUGF(LOG_V2, LOG_GUI, "Special key: type=%d,sym=%d,mod=%d\n",
					_event.type, _event.key.keysym.sym, _event.key.keysym.mod);
			special_key = true;
			return;
		}
		if(m_input_grab) {
			dispatch_hw_event(_event);
			return;
		}
		switch(_event.type) {
			case SDL_JOYAXISMOTION:
			case SDL_JOYBUTTONDOWN:
			case SDL_JOYBUTTONUP:
				dispatch_hw_event(_event);
				break;
			case SDL_KEYDOWN:
			case SDL_KEYUP:
				if(m_windows.needs_input() && !special_key) {
					dispatch_rocket_event(_event);
				} else {
					dispatch_hw_event(_event);
					special_key = false;
				}
				break;
			default:
				dispatch_rocket_event(_event);
				break;
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

	if(_event.type == SDL_KEYDOWN || _event.type == SDL_KEYUP) {
		PDEBUGF(LOG_V2, LOG_GUI, "HW key: type=%d,sym=%d,mod=%d\n",
				_event.type, _event.key.keysym.sym, _event.key.keysym.mod);
	}

	switch(_event.type)
	{
	case SDL_MOUSEMOTION:
		if(m_mouse.warped
			&& _event.motion.x == m_width/2
			&& _event.motion.y == m_height/2)
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

		SDL_WarpMouseInWindow(m_SDL_window, m_width/2, m_height/2);
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
	case SDL_JOYAXISMOTION: {
		ASSERT(_event.jaxis.which < m_SDL_joysticks.size());
		int jid = JOY_NONE;
		if(m_joystick0 == _event.jaxis.which) {
			jid = 0;
		} else if(m_joystick1 == _event.jaxis.which) {
			jid = 1;
		}
		if(jid <= 1 && _event.jaxis.axis <= 1) {
			PDEBUGF(LOG_V2, LOG_GUI, "Joy %d axis %d: %d\n", jid, _event.jaxis.axis, _event.jaxis.value);
			m_machine->joystick_motion(jid, _event.jaxis.axis, _event.jaxis.value);
		}
		break;
	}
	case SDL_JOYBUTTONDOWN:
	case SDL_JOYBUTTONUP: {
		ASSERT(_event.jbutton.which < m_SDL_joysticks.size());
		int jid = JOY_NONE;
		if(m_joystick0 == _event.jbutton.which) {
			jid = 0;
		} else if(m_joystick1 == _event.jbutton.which) {
			jid = 1;
		}
		if(jid <= 1 && _event.jbutton.button <= 1) {
			PDEBUGF(LOG_V2, LOG_GUI, "Joy %d btn %d: %d\n", jid, _event.jbutton.button, _event.jbutton.state);
			m_machine->joystick_button(jid, _event.jbutton.button, _event.jbutton.state);
		}
		break;
	}
	default:
		break;
	}
}

void GUI::update_window_size(int _w, int _h)
{
	m_width = _w;
	m_height = _h;
	m_rocket_context->SetDimensions(Rocket::Core::Vector2i(m_width,m_height));
	m_rocket_renderer->SetDimensions(m_width, m_height);
	m_windows.interface->container_size_changed(m_width, m_height);
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

std::string GUI::load_shader_file(const std::string &_path)
{
	std::string shdata;
	std::ifstream shstream(_path, std::ios::in);
	if(shstream.is_open()){
		std::string line = "";
		while(getline(shstream, line))
			shdata += "\n" + line;
		shstream.close();
	} else {
		PERRF(LOG_GUI, "Unable to open '%s'\n", _path.c_str());
		throw std::exception();
	}
	return shdata;
}

std::vector<GLuint> GUI::attach_shaders(const std::vector<std::string> &_sh_paths, GLuint _sh_type, GLuint _program)
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
		GLCALL( glShaderSource(shid, 1, &source , NULL) );
		GLCALL( glCompileShader(shid) );
		// Check Shader
		GLint result = GL_FALSE;
		int infologlen;
		GLCALL( glGetShaderiv(shid, GL_COMPILE_STATUS, &result) );
		GLCALL( glGetShaderiv(shid, GL_INFO_LOG_LENGTH, &infologlen) );
		if(!result && infologlen > 1) {
			std::vector<char> sherr(infologlen+1);
			GLCALL( glGetShaderInfoLog(shid, infologlen, NULL, &sherr[0]) );
			PERRF(LOG_GUI, "GLSL error in '%s'\n", sh.c_str());
			PERRF(LOG_GUI, "%s\n", &sherr[0]);
		}
		// Attach Vertex Shader to Program
		GLCALL( glAttachShader(_program, shid) );
		sh_ids.push_back(shid);
	}
	return sh_ids;
}

GLuint GUI::load_GLSL_program(const std::vector<std::string> &_vs_paths, std::vector<std::string> &_fs_paths)
{
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
			GLCALL( glGetProgramInfoLog(progid, infologlen, NULL, &progerr[0]) );
			PERRF(LOG_GUI, "Program error: '%s'\n", &progerr[0]);
		}
		throw std::exception();
	}

	return progid;
}

std::string GUI::get_shaders_dir()
{
	return g_program.config().get_assets_home() + FS_SEP "gui" FS_SEP "shaders" FS_SEP;
}

void GUI::save_framebuffer(std::string _screenfile, std::string _palfile)
{
	m_windows.interface->save_framebuffer(_screenfile, _palfile);
}

void GUI::show_message(const char* _mex)
{
	m_windows.interface->show_message(_mex);
}

Uint32 GUI::every_second(Uint32 interval, void */*param*/)
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

void GUI::Windows::init(Machine *_machine, GUI *_gui, Mixer *_mixer, uint _mode)
{
	desktop = new Desktop(_gui);
	desktop->show();

	if(_mode == GUI_MODE_REALISTIC) {
		interface = new RealisticInterface(_machine,_gui,_mixer);
	} else {
		interface = new NormalInterface(_machine,_gui,_mixer);
	}
	interface->show();

	if(g_program.config().get_bool(GUI_SECTION, GUI_SHOW_LEDS)) {
		status = new Status(_gui);
		status->show();
		status_wnd = true;
	} else {
		status_wnd = false;
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

void GUI::Windows::invert_visibility()
{
	if(debug_wnds) {
		if(debugger->is_visible()) {
			debugger->hide();
			devices->hide();
			stats->hide();
		} else {
			debugger->show();
			devices->show();
			stats->show();
		}
	}
	if(interface->is_visible()) {
		interface->hide();
	} else {
		interface->show();
	}

	if(status_wnd) {
		if(status->is_visible()) {
			status->hide();
		} else {
			status->show();
		}
	}
}

void GUI::Windows::update()
{
	interface->update();

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

