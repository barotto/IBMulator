/*
 * Copyright (C) 2015-2020  Marco Bortolin
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
#include "rocket/sys_interface.h"
#include "rocket/file_interface.h"
#include <SDL_image.h>

#include "windows/desktop.h"
#include "windows/normal_interface.h"
#include "windows/realistic_interface.h"
#include "windows/debugtools.h"
#include "windows/status.h"

#include "hardware/devices/systemboard.h"

#include <algorithm>
#include <iomanip>


std::mutex GUI::ms_rocket_mutex;
std::mutex GUI::Windows::s_interface_mutex;

ini_enum_map_t g_mouse_types = {
	{ "none", MOUSE_TYPE_NONE },
	{ "ps2", MOUSE_TYPE_PS2 },
	{ "imps2", MOUSE_TYPE_IMPS2 },
	{ "serial", MOUSE_TYPE_SERIAL },
	{ "serial-wheel", MOUSE_TYPE_SERIAL_WHEEL },
	{ "serial-msys", MOUSE_TYPE_SERIAL_MSYS }
};

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
m_machine(nullptr),
m_mixer(nullptr),
m_width(640),
m_height(480),
m_SDL_window(nullptr),
m_curr_model_changed(false),
m_second_timer(0),
m_joystick0(JOY_NONE),
m_joystick1(JOY_NONE),
m_gui_visible(true),
m_input_grab(false),
m_mode(GUI_MODE_NORMAL),
m_framecap(GUI_FRAMECAP_VGA),
m_vsync(false),
m_vga_buffering(false),
m_threads_sync(false),
m_symspeed_factor(1.0),
m_rocket_sys_interface(nullptr),
m_rocket_file_interface(nullptr),
m_rocket_context(nullptr)
{
}

GUI::~GUI()
{
	delete m_rocket_sys_interface;
	delete m_rocket_file_interface;
}

void GUI::init(Machine *_machine, Mixer *_mixer)
{
	m_machine = _machine;
	m_mixer = _mixer;
	
	// configuration variables
	m_assets_path = g_program.config().get_assets_home() + FS_SEP "gui" FS_SEP;
	
	static std::map<std::string, unsigned> framecap = {
		{ "", GUI_FRAMECAP_VGA },
		{ "vga", GUI_FRAMECAP_VGA },
		{ "vsync", GUI_FRAMECAP_VSYNC },
		{ "no", GUI_FRAMECAP_OFF },
		{ "off", GUI_FRAMECAP_OFF }
	};
	m_framecap = g_program.config().get_enum(GUI_SECTION, GUI_FRAMECAP, framecap);
	switch(m_framecap) {
		case GUI_FRAMECAP_VGA:
			m_vsync = false;
			m_threads_sync = true;
			m_vga_buffering = false;
			PINFOF(LOG_V0, LOG_GUI, "Limiting FPS to the emulated VGA frequency\n");
			break;
		case GUI_FRAMECAP_VSYNC:
			m_vsync = true;
			m_threads_sync = false;
			m_vga_buffering = true;
			PINFOF(LOG_V0, LOG_GUI, "Enabling VSync\n");
			break;
		case GUI_FRAMECAP_OFF:
			m_vsync = false;
			m_threads_sync = false;
			m_vga_buffering = true;
			break;
		default:
			// errors should be detected by the get_enum()
			assert(false);
			break;
	}
	
	m_mode = g_program.config().get_enum(GUI_SECTION, GUI_MODE, ms_gui_modes);
	m_grab_method = str_to_lower(g_program.config().get_string(GUI_SECTION, GUI_GRAB_METHOD));
	m_mouse.grab = g_program.config().get_bool(GUI_SECTION,GUI_MOUSE_GRAB);
	m_backcolor = {
		Uint8(g_program.config().get_int(GUI_SECTION, GUI_BG_R)),
		Uint8(g_program.config().get_int(GUI_SECTION, GUI_BG_G)),
		Uint8(g_program.config().get_int(GUI_SECTION, GUI_BG_B)),
		255
	};
	
	// VIDEO INITIALIZATION
	if(SDL_VideoInit(nullptr) != 0) {
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
	
	// WINDOW CREATION
	// initial defaults, will be updated later on in the resize_window
	m_width = 640;
	m_height = 480;
	m_wnd_title = PACKAGE_STRING;
	try {
		create_window(SDL_WINDOW_RESIZABLE);
	} catch(std::exception &) {
		shutdown_SDL();
		throw;
	}

	PINFOF(LOG_V0,LOG_GUI, "Selected video mode: %dx%d\n", m_width, m_height);
	
	// INTERFACE INITIALIZATION
	try {
		init_Rocket();
		m_windows.init(m_machine, this, m_mixer, m_mode);
	} catch(std::exception &e) {
		shutdown_SDL();
		throw;
	}

	vec2i wsize = m_windows.interface->get_size();
	resize_window(wsize.x, wsize.y);

	SDL_ShowWindow(m_SDL_window);
	SDL_SetWindowPosition(m_SDL_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	
	if(g_program.config().get_bool(GUI_SECTION, GUI_FULLSCREEN)) {
		toggle_fullscreen();
	}

	auto keymap = g_program.config().find_file(GUI_SECTION, GUI_KEYMAP);
	if(keymap.empty()) {
		keymap = g_program.config().get_file_path("keymap.map", FILE_TYPE_ASSET);
	}
	try {
		g_keymap.load(keymap);
	} catch(std::exception &e) {
		PERRF(LOG_GUI, "Unable to load the keymap!\n");
		shutdown_SDL();
		throw std::exception();
	}

	m_gui_visible = true;
	m_input_grab = false;

	m_second_timer = SDL_AddTimer(1000, GUI::every_second, nullptr);

	// JOYSTICK SUPPORT
	if(SDL_InitSubSystem(SDL_INIT_JOYSTICK) != 0) {
		PWARNF(LOG_GUI, "Unable to init SDL Joystick subsystem: %s\n", SDL_GetError());
	} else {
		SDL_JoystickEventState(SDL_ENABLE);
		PDEBUGF(LOG_V2, LOG_GUI, "Joy evt state: %d\n", SDL_JoystickEventState(SDL_QUERY));
	}

	// DONE
	show_welcome_screen();
}

void GUI::config_changed()
{
	m_windows.config_changed();

	m_curr_model = m_machine->devices().sysboard()->model_string();
	m_curr_model += " (" + m_machine->cpu().model() + "@";
	std::stringstream ss;
	ss << std::fixed << std::setprecision(0) << m_machine->cpu().frequency();
	m_curr_model += ss.str() + "MHz)";
	m_curr_model_changed = true;
}

void GUI::set_window_icon()
{
	//this function must be called before SDL_CreateRenderer!
	
#ifndef _WIN32
	std::string iconfile = g_program.config().get_assets_home() + FS_SEP "icon.png";
	SDL_Surface* icon = IMG_Load(iconfile.c_str());
	if(icon) {
		SDL_SetWindowIcon(m_SDL_window, icon);
		SDL_FreeSurface(icon);
	} else {
		PERRF(LOG_GUI, "unable to load app icon '%s'\n", iconfile.c_str());
	}
#endif
}

vec2i GUI::resize_window(int _w, int _h)
{
	SDL_SetWindowSize(m_SDL_window, _w, _h);
	SDL_GetWindowSize(m_SDL_window, &m_width, &m_height);
	PINFOF(LOG_V0,LOG_GUI,"Window resized to %dx%d\n", m_width, m_height);
	
	update_window_size(m_width, m_height);
	
	return vec2i(m_width, m_height);
}

void GUI::sig_state_restored()
{
	m_windows.interface->sig_state_restored();
}

void GUI::toggle_fullscreen()
{
	Uint32 flags = (SDL_GetWindowFlags(m_SDL_window) ^ SDL_WINDOW_FULLSCREEN_DESKTOP);
	if(SDL_SetWindowFullscreen(m_SDL_window, flags) != 0) {
		PERRF(LOG_GUI, "Toggling fullscreen mode failed: %s\n", SDL_GetError());
	}
}

void GUI::init_Rocket()
{
	create_rocket_renderer();
	
	m_rocket_sys_interface = new RocketSystemInterface();
	m_rocket_file_interface = new RocketFileInterface(m_assets_path.c_str());

	Rocket::Core::SetFileInterface(m_rocket_file_interface);
	Rocket::Core::SetRenderInterface(m_rocket_renderer.get());
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
	RC::ElementDocument * document = m_rocket_context->LoadDocument(_filename.c_str());

	if(document) {
		RC::Element * title = document->GetElementById("title");
		if(title) {
			title->SetInnerRML(document->GetTitle());
		}
		PDEBUGF(LOG_V2,LOG_GUI,"Document \"%s\" loaded\n", _filename.c_str());
	} else {
		PERRF(LOG_GUI, "Document \"%s\" is nullptr\n", _filename.c_str());
	}

	return document;
}

void GUI::input_grab(bool _value)
{
	if(m_mouse.grab) {
		if(_value) {
			SDL_SetRelativeMouseMode(SDL_TRUE);
		} else {
			SDL_SetRelativeMouseMode(SDL_FALSE);
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
	SDL_Keycode modifier_key_code = 0;
	SDL_Scancode modifier_key_scan = SDL_SCANCODE_UNKNOWN;

	if(_event.type == SDL_KEYDOWN || _event.type == SDL_KEYUP) {
		if(_event.key.keysym.mod & KMOD_CTRL) {
			modifier_key_code = (_event.key.keysym.mod & KMOD_RCTRL)?SDLK_RCTRL:SDLK_LCTRL;
			modifier_key_scan = (_event.key.keysym.mod & KMOD_RCTRL)?SDL_SCANCODE_RCTRL:SDL_SCANCODE_LCTRL;
			KeyEntry *entry = g_keymap.find_host_key(modifier_key_code, modifier_key_scan);
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
				case SDLK_F2: {
					// screen recording
					if(ENABLE_VGA_SCREEN_RECORDING) {
						if(_event.type == SDL_KEYUP) return true;
						m_machine->cmd_toggle_VGA_rec();
						return true;
					}
					break;
				}
				case SDLK_F3: {
					//machine on/off
					if(_event.type == SDL_KEYUP) {
						if(entry) {
							_discard_next_key = modifier_key_code;
						}
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
					take_screenshot(
						#ifndef NDEBUG
						true
						#endif
					);
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
					KeyEntry *entry = g_keymap.find_host_key(modifier_key_code, modifier_key_scan);
					if(entry) {
						// send the CTRL key up event to the machine before saving 
						m_machine->send_key_to_kbctrl(entry->key, KEY_RELEASED);
						// then ignore it when it occurs
						_discard_next_key = modifier_key_code;
					}
					g_program.save_state("", [this]() {
						show_message("State saved");
					}, nullptr);
					return true;
				}
				case SDLK_F8: {
					//load last machine state
					if(_event.type == SDL_KEYUP) {
						if(entry) {
							_discard_next_key = modifier_key_code;
						}
						return true;
					}
					g_program.restore_state("", [this]() {
						show_message("State restored");
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
					//emulation speed down
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
					//emulation speed up
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
					PDEBUGF(LOG_V2, LOG_GUI, "CTRL+ALT+CANC sent to guest OS\n");
					if(_event.type == SDL_KEYUP) return true;
					//CTRL has been already sent
					m_machine->send_key_to_kbctrl(KEY_ALT_L, KEY_PRESSED);
					m_machine->send_key_to_kbctrl(KEY_DELETE, KEY_PRESSED);
					return true;
				}
				case SDLK_INSERT: {
					//send SysReq
					PDEBUGF(LOG_V2, LOG_GUI, "SysReq sent to guest OS\n");
					if(_event.type == SDL_KEYUP) return true;
					m_machine->send_key_to_kbctrl(KEY_SYSREQ, KEY_PRESSED);
					return true;
				}
				case SDLK_END: {
					//send Break
					PDEBUGF(LOG_V2, LOG_GUI, "Break sent to guest OS\n");
					if(_event.type == SDL_KEYUP) return true;
					m_machine->send_key_to_kbctrl(KEY_BREAK, KEY_PRESSED);
					return true;
				}
			}
		} else if(_event.key.keysym.mod & KMOD_ALT) {
			switch(_event.key.keysym.sym) {
				case SDLK_RETURN: {
					/* SDL fires multiple very fast key press events (key repeat
					 * without a pause after the first event, issue #38)
					 * use a static var to ignore all events after the first.
					 */
					static bool toggling = false;
					if(_event.type == SDL_KEYUP) {
						toggling = false;
						return true;
					}
					if(toggling) {
						return true;
					}
					toggling = true;
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

static void debug_key_print(const SDL_Event &_event, const KeyEntry *_kentry, const char *_handler)
{
	if(_event.type == SDL_KEYDOWN || _event.type == SDL_KEYUP) {
		std::string mod1 = bitfield_to_string(_event.key.keysym.mod&0xff,
		{ "KMOD_LSHIFT", "KMOD_RSHIFT", "", "", "", "", "KMOD_LCTRL", "KMOD_RCTRL" });
		
		std::string mod2 = bitfield_to_string((_event.key.keysym.mod>>8)&0xff,
		{ "KMOD_LALT", "KMOD_RALT", "KMOD_LGUI", "KMOD_RGUI", "KMOD_NUM", "KMOD_CAPS", "KMOD_MODE", "KMOD_RESERVED" });
		if(!mod1.empty()) {
			mod2 = " " + mod2;
		}
		
		PDEBUGF(LOG_V2, LOG_GUI, "%s: evt=%s,sym=%s,code=%s,mod=[%s%s]",
				_handler,
				(_event.type == SDL_KEYDOWN)?"SDL_KEYDOWN":"SDL_KEYUP",
				Keymap::ms_sdl_keycode_str_table[_event.key.keysym.sym].c_str(),
				Keymap::ms_sdl_scancode_str_table[_event.key.keysym.scancode].c_str(),
				mod1.c_str(), mod2.c_str());
		if(_kentry) {
			PDEBUGF(LOG_V2, LOG_GUI, " -> %s (%s)",
				Keymap::ms_keycode_str_table[_kentry->key].c_str(),
				_kentry->host_name.c_str());
		}
		PDEBUGF(LOG_V2, LOG_GUI, "\n");
	}
}

void GUI::dispatch_event(const SDL_Event &_event)
{
	static bool special_key = false;
	static SDL_Keycode discard_next_key = 0;

	if(_event.type == SDL_WINDOWEVENT) {
		dispatch_window_event(_event.window);
	} else if(_event.type == SDL_USEREVENT) {
		//the 1-second timer
		if(m_machine->get_bench().load >= 1.0) {
			m_windows.interface->show_warning(true);
		} else {
			m_windows.interface->show_warning(false);
		}
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
		assert(_event.jdevice.which <= Sint32(m_SDL_joysticks.size()));
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
			for(int j=0; j<int(m_SDL_joysticks.size()); j++) {
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
	} else if(_event.type == SDL_JOYAXISMOTION || 
			_event.type == SDL_JOYBUTTONDOWN ||
			_event.type == SDL_JOYBUTTONUP)
	{
		dispatch_hw_event(_event);
	} else {
		if(discard_next_key && (_event.key.keysym.sym == discard_next_key)) {
			discard_next_key = SDLK_UNKNOWN;
			debug_key_print(_event, nullptr, "Discarded");
			return;
		}
		if(dispatch_special_keys(_event, discard_next_key)) {
			debug_key_print(_event, nullptr, "Special");
			special_key = true;
			return;
		}
		if(m_input_grab) {
			dispatch_hw_event(_event);
			return;
		}
		switch(_event.type) {
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
	if(!m_input_grab && (
		_event.type == SDL_MOUSEMOTION ||
		_event.type == SDL_MOUSEBUTTONDOWN ||
		_event.type == SDL_MOUSEBUTTONUP ||
		_event.type == SDL_MOUSEWHEEL)
	) {
		return;
	}

	KeyEntry *entry = nullptr;
	if(_event.type == SDL_KEYDOWN || _event.type == SDL_KEYUP) {
		entry = g_keymap.find_host_key(_event.key.keysym.sym, _event.key.keysym.scancode);
		debug_key_print(_event, entry, "HW event");
	}

	switch(_event.type)
	{
	case SDL_MOUSEMOTION: {
		uint8_t buttons;
		buttons  = bool(_event.motion.state & SDL_BUTTON(SDL_BUTTON_LEFT));
		buttons |= bool(_event.motion.state & SDL_BUTTON(SDL_BUTTON_RIGHT)) << 1;
		buttons |= bool(_event.motion.state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) << 2;
		m_machine->mouse_motion(_event.motion.xrel, -_event.motion.yrel, 0, buttons);
		break;
	}
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP: {
		if(_event.button.button == SDL_BUTTON_MIDDLE) {
			break;
		}

		uint8_t mouse_state = SDL_GetMouseState(nullptr, nullptr);
		uint8_t buttons;
		buttons  = bool(mouse_state & SDL_BUTTON(SDL_BUTTON_LEFT));
		buttons |= bool(mouse_state & SDL_BUTTON(SDL_BUTTON_RIGHT)) << 1;
		buttons |= bool(mouse_state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) << 2;
		m_machine->mouse_motion(0,0,0, buttons);

		break;
	}
	case SDL_MOUSEWHEEL:
		//no wheel on the ps/1. should I implement it for IM?
		break;

	case SDL_KEYDOWN: {
		if(!entry) {
			PERRF(LOG_GUI,"host key %s [%s] not mapped\n",
					Keymap::ms_sdl_keycode_str_table[_event.key.keysym.sym].c_str(),
					Keymap::ms_sdl_scancode_str_table[_event.key.keysym.scancode].c_str());
			break;
		}
		if(entry->key == KEY_UNHANDLED) {
			PDEBUGF(LOG_GUI, LOG_V2, "host key '%s' ignored\n", 
					entry->host_name.c_str());
			break;
		}
		m_machine->send_key_to_kbctrl(entry->key, KEY_PRESSED);
		break;
	}
	case SDL_KEYUP: {
		if(!entry || entry->key == KEY_UNHANDLED) {
			break;
		}
		m_machine->send_key_to_kbctrl(entry->key, KEY_RELEASED);
		break;
	}
	case SDL_JOYAXISMOTION: {
		assert(_event.jaxis.which < Sint32(m_SDL_joysticks.size()));
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
		assert(_event.jbutton.which < Sint32(m_SDL_joysticks.size()));
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
	std::lock_guard<std::mutex> lock(ms_rocket_mutex);
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
	std::lock_guard<std::mutex> lock(ms_rocket_mutex);

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

	case SDL_MOUSEBUTTONUP: {
		m_rocket_context->ProcessMouseButtonUp(
			m_rocket_sys_interface->TranslateMouseButton(event.button.button),
			rockmod
			);
		// this is a hack to fix libRocket not updating events targets
		int x,y;
		SDL_GetMouseState(&x,&y);
		m_rocket_context->ProcessMouseMove(x, y, rockmod);
		break;
	}
	case SDL_MOUSEWHEEL:
		m_rocket_context->ProcessMouseWheel(
			-event.wheel.y,
			rockmod
			);
		break;

	case SDL_KEYDOWN: {
		Rocket::Core::Input::KeyIdentifier key =
			m_rocket_sys_interface->TranslateKey(event.key.keysym.sym);
		if(key != Rocket::Core::Input::KI_UNKNOWN) {
			m_rocket_context->ProcessKeyDown(key,rockmod);
		}
		Rocket::Core::word w = RocketSystemInterface::GetCharacterCode(key, rockmod);
		if(w > 0) {
			m_rocket_context->ProcessTextInput(w);
		}
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

void GUI::update(uint64_t _current_time)
{
	m_windows.update(_current_time);

	/* during a libRocket Context update no other thread can call any windows
	 * related functions
	 * TODO Interface messages are outside event handlers and SysLog doesn't
	 * write messages on the interface directly anymore, so this mutex is
	 * useless now because no other thread is accessing libRocket's context.
	 * Consider removing it?
	 */
	ms_rocket_mutex.lock();
	m_rocket_context->Update();
	ms_rocket_mutex.unlock();

	bool title_changed = m_curr_model_changed;
	if(SHOW_CURRENT_PROGRAM_NAME) {
		m_machine->ms_gui_lock.lock();
		if(m_machine->current_program_name_has_changed()) {
			title_changed = true;
			m_curr_prog = m_machine->get_current_program_name();
		}
		m_machine->ms_gui_lock.unlock();
	}
	if(title_changed) {
		m_curr_model_changed = false;
		std::string curr_title;
		if(!m_curr_prog.empty()) {
			curr_title = m_wnd_title + " - " + m_curr_model + " - " + m_curr_prog;
		} else {
			curr_title = m_wnd_title + " - " + m_curr_model;
		}
		SDL_SetWindowTitle(m_SDL_window, curr_title.c_str());
	}
}

void GUI::shutdown_SDL()
{
	if(m_SDL_window) {
		SDL_DestroyWindow(m_SDL_window);
		m_SDL_window = nullptr;
	}
	SDL_VideoQuit();
}

void GUI::shutdown()
{
	PDEBUGF(LOG_V1, LOG_GUI, "Shutting down the video subsystem\n");
	
	SDL_RemoveTimer(m_second_timer);

	m_windows.shutdown();

	ms_rocket_mutex.lock();
	m_rocket_context->RemoveReference();
	Rocket::Core::Shutdown();
	ms_rocket_mutex.unlock();

	shutdown_SDL();
}

std::string GUI::load_shader_file(const std::string &_path)
{
	std::string shdata;
	std::ifstream shstream(_path, std::ios::in);
	if(shstream.is_open()){
		std::string line = "";
		while(getline(shstream, line)) {
			shdata += "\n" + line;
		}
		shstream.close();
	} else {
		PERRF(LOG_GUI, "Unable to open '%s'\n", _path.c_str());
		throw std::exception();
	}
	return shdata;
}

GUI * GUI::instance()
{
	return g_program.gui_instance();
}

std::string GUI::shaders_dir()
{
	return g_program.config().get_assets_home() + FS_SEP "gui" FS_SEP "shaders" FS_SEP;
}

std::string GUI::images_dir()
{
	return g_program.config().get_assets_home() + FS_SEP "gui" FS_SEP "images" FS_SEP;
}

void GUI::save_framebuffer(std::string _screenfile, std::string _palfile)
{
	m_windows.interface->save_framebuffer(_screenfile, _palfile);
}

void GUI::take_screenshot(bool _with_palette_file)
{
	std::string path = g_program.config().find_file(PROGRAM_SECTION, PROGRAM_CAPTURE_DIR);
	std::string screenfile = FileSys::get_next_filename(path, "screenshot_", ".png");
	if(!screenfile.empty()) {
		std::string palfile;
		if(_with_palette_file) {
			palfile = path + FS_SEP + "palette.png";
		}
		try {
			save_framebuffer(screenfile, palfile);
			std::string mex = "screenshot saved to " + screenfile;
			PINFOF(LOG_V0, LOG_GUI, "%s\n", mex.c_str());
			show_message(mex.c_str());
		} catch(std::exception &e) { }
	}
}

void GUI::show_message(const char* _mex)
{
	std::lock_guard<std::mutex> lock(m_windows.s_interface_mutex);
	m_windows.last_ifc_mex = _mex;
}

void GUI::show_dbg_message(const char* _mex)
{
	std::lock_guard<std::mutex> lock(m_windows.s_interface_mutex);
	m_windows.last_dbg_mex = _mex;
}

Uint32 GUI::every_second(Uint32 interval, void */*param*/)
{
	SDL_Event event;
	SDL_UserEvent userevent;

	userevent.type = SDL_USEREVENT;
	userevent.code = 0;
	userevent.data1 = nullptr;
	userevent.data2 = nullptr;

	event.type = SDL_USEREVENT;
	event.user = userevent;

	SDL_PushEvent(&event);

	return interval;
}

void GUI::show_welcome_screen()
{
	std::vector<uint16_t> text(80*25,0x0000);
	int cx = 0, cy = 0;
	const int bg = 0x8;
	const int bd = 2;

	auto ps = [&](const char *_str, uint8_t _foreg, uint8_t _backg, int _border) {
		do {
			unsigned char c = *_str++;
			if(cx >= 80-_border || c == '\n') {
				cx = _border;
				cy++;
				if(c == '\n') {
					continue;
				}
			}
			if(cy >= 25) {
				cy = 0;
			}
			uint16_t attrib = (_backg << 4) | (_foreg & 0x0F);
			text[cy*80 + cx++] = (attrib << 8) | c;

		} while(*_str);
	};

	ps(
"\xC9"
"\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
"\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
"\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
"\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
"\xCD\xCD\xBB",
0xf, bg, 0);
	for(int i=1; i<=((m_mode==GUI_MODE_COMPACT)?17:23); i++) {
		ps("\xBA                                                                              \xBA",
				0xf, bg, 0);
	}
	ps(
"\xC8"
"\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
"\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
"\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
"\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
"\xCD\xCD\xBC",
0xf, bg, 0);

	cx = bd; cy = 1;
	ps("Welcome to ", 0xf, bg, bd); ps(PACKAGE_STRING "\n\n", 0xa, 0x9, bd);
	ps(PACKAGE_NAME " is free software, you can redistribute it and/or modify it"
			" under\nthe terms of the GNU GPL v.3+\n\n", 0x7, bg, bd);

	cx = 0;
	ps(
"\xCC"
"\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
"\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
"\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
"\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
"\xCD\xCD\xB9",
0xf, bg, 0);

	cx = bd;
	if(m_mode == GUI_MODE_COMPACT) {
		ps("\nTo show/hide the interface press ", 0xf, bg, bd); ps("CTRL+F1", 0xe, bg, bd);
		ps(" or grab the mouse\n", 0xf, bg, bd);
	} else {
		cy++;
	}
	ps("To grab the mouse press ", 0xf, bg, bd);
	if(m_grab_method.compare("ctrl-f10") == 0) {
		ps("CTRL+F10\n", 0xe, bg, bd);
	} else {
		ps("the middle mouse button\n", 0xe, bg, bd);
	}
	ps("To start/stop the machine press ", 0xf, bg, bd); ps("CTRL+F3\n", 0xe, bg, bd);
	ps("To toggle fullscreen mode press ", 0xf, bg, bd); ps("ALT+ENTER", 0xe, bg, bd);
	ps(" and to close the emulator ", 0xf, bg, bd); ps("ALT+F4\n", 0xe, bg, bd);
	ps("\nYou can find the configuration file here:\n", 0xf, bg, bd);
	ps(g_program.config().get_parsed_file().c_str(), 0xe, bg, bd);
	ps("\n\nFor more information read the README file and visit the project page at\n", 0xf, bg, bd);
	ps("http://barotto.github.io/IBMulator/\n", 0xe, bg, bd);

	m_machine->cmd_print_VGA_text(text);
}


/*******************************************************************************
 * Windows
 */

class SysDbgMessage : public Logdev
{
private:
	GUI *m_gui;
public:
	SysDbgMessage(GUI * _gui, bool _delete) : Logdev(_delete), m_gui(_gui) {}
	void log_put(const std::string & /*_prefix*/, const std::string &_message)
	{
		// This function is called by the Syslog thread
		m_gui->show_dbg_message(_message.c_str());
	}
};

class IfaceMessage : public Logdev
{
private:
	GUI *m_gui;
public:
	IfaceMessage(GUI * _gui, bool _delete) : Logdev(_delete), m_gui(_gui) {}
	void log_put(const std::string & /*_prefix*/, const std::string &_message)
	{
		// This function is called by the Syslog thread
		m_gui->show_message(_message.c_str());
	}
};

GUI::Windows::Windows()
:
visible(true),
debug_wnds(false),
status_wnd(false),
desktop(nullptr),
interface(nullptr),
status(nullptr),
dbgtools(nullptr),
dbgmex_timer(NULL_TIMER_HANDLE),
ifcmex_timer(NULL_TIMER_HANDLE)

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
		status = new Status(_gui, _machine);
		status->show();
		status_wnd = true;
	} else {
		status_wnd = false;
	}

	//debug
	dbgtools = new DebugTools(_gui, _machine, _mixer);

	timers.init();
	ifcmex_timer = timers.register_timer([this](uint64_t){
		if(interface) {
			interface->show_message("");
		}
	}, "main interface messages");
	dbgmex_timer = timers.register_timer([this](uint64_t){
		if(dbgtools) {
			dbgtools->show_message("");
		}
	}, "debug messages");

	static SysDbgMessage sysdbgmsg(_gui, false);
	static IfaceMessage ifacemsg(_gui, false);
	g_syslog.add_device(LOG_INFO, LOG_MACHINE, &sysdbgmsg);
	g_syslog.add_device(LOG_ERROR, LOG_ALL_FACILITIES, &ifacemsg);
}

void GUI::Windows::config_changed()
{
	std::lock_guard<std::mutex> lock(ms_rocket_mutex);

	desktop->config_changed();
	interface->config_changed();
	if(status) {
		status->config_changed();
	}
	dbgtools->config_changed();
}

void GUI::Windows::show_ifc_message(const char* _mex)
{
	std::lock_guard<std::mutex> lock(ms_rocket_mutex);
	if(interface != nullptr) {
		interface->show_message(_mex);
		timers.activate_timer(ifcmex_timer, 3e9, false);
	}
}

void GUI::Windows::show_dbg_message(const char* _mex)
{
	std::lock_guard<std::mutex> lock(ms_rocket_mutex);
	if(dbgtools != nullptr) {
		dbgtools->show_message(_mex);
		timers.activate_timer(dbgmex_timer, 3e9, false);
	}
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

void GUI::Windows::update(uint64_t _current_time)
{
	s_interface_mutex.lock();
	if(!last_ifc_mex.empty()) {
		show_ifc_message(last_ifc_mex.c_str());
		last_ifc_mex.clear();
	}
	if(!last_dbg_mex.empty()) {
		show_dbg_message(last_dbg_mex.c_str());
		last_dbg_mex.clear();
	}
	s_interface_mutex.unlock();

	// updates can't call any function that needs a lock on ms_rocket_mutex
	std::lock_guard<std::mutex> lock(ms_rocket_mutex);

	interface->update();

	if(debug_wnds) {
		dbgtools->update();
	}

	if(status) {
		status->update();
	}

	// timers are where the timed windows events take place
	// like the interface messages clears
	while(!timers.update(_current_time));
}

void GUI::Windows::toggle_dbg()
{
	debug_wnds = !debug_wnds;
	if(debug_wnds) {
		dbgtools->show();
	} else {
		dbgtools->hide();
	}
}

bool GUI::Windows::needs_input()
{
	//only debug windows have kb input at the moment
	return debug_wnds;
}

void GUI::Windows::shutdown()
{
	std::lock_guard<std::mutex> lock(ms_rocket_mutex);

	if(status) {
		status->close();
		delete status;
		status = nullptr;
	}

	if(dbgtools) {
		dbgtools->close();
		delete dbgtools;
		dbgtools = nullptr;
	}

	if(desktop) {
		desktop->close();
		delete desktop;
		desktop = nullptr;
	}

	if(interface) {
		interface->close();
		delete interface;
		interface = nullptr;
	}
}

