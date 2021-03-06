/*
 * Copyright (C) 2015-2021  Marco Bortolin
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
#include "wincompat.h"

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

#include "capture/capture.h"

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

const std::map<ProgramEvent::Func, std::function<void(GUI&,GUI::EventPhase)>> GUI::ms_event_funcs = {
	{ ProgramEvent::Func::FUNC_NONE,                 &GUI::pevt_func_none                 },
	{ ProgramEvent::Func::FUNC_GUI_MODE_ACTION,      &GUI::pevt_func_gui_mode_action      },
	{ ProgramEvent::Func::FUNC_TOGGLE_POWER,         &GUI::pevt_func_toggle_power         },
	{ ProgramEvent::Func::FUNC_TOGGLE_PAUSE,         &GUI::pevt_func_toggle_pause         },
	{ ProgramEvent::Func::FUNC_TOGGLE_DBG_WND,       &GUI::pevt_func_toggle_dbg_wnd       },
	{ ProgramEvent::Func::FUNC_TAKE_SCREENSHOT,      &GUI::pevt_func_take_screenshot      },
	{ ProgramEvent::Func::FUNC_TOGGLE_AUDIO_CAPTURE, &GUI::pevt_func_toggle_audio_capture },
	{ ProgramEvent::Func::FUNC_TOGGLE_VIDEO_CAPTURE, &GUI::pevt_func_toggle_video_capture },
	{ ProgramEvent::Func::FUNC_QUICK_SAVE_STATE,     &GUI::pevt_func_quick_save_state     },
	{ ProgramEvent::Func::FUNC_QUICK_LOAD_STATE,     &GUI::pevt_func_quick_load_state     },
	{ ProgramEvent::Func::FUNC_GRAB_MOUSE,           &GUI::pevt_func_grab_mouse           },
	{ ProgramEvent::Func::FUNC_SYS_SPEED_UP,         &GUI::pevt_func_sys_speed_up         },
	{ ProgramEvent::Func::FUNC_SYS_SPEED_DOWN,       &GUI::pevt_func_sys_speed_down       },
	{ ProgramEvent::Func::FUNC_TOGGLE_FULLSCREEN,    &GUI::pevt_func_toggle_fullscreen    },
	{ ProgramEvent::Func::FUNC_EXIT,                 &GUI::pevt_func_exit                 }
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
	m_joystick[0].id = JOY_NONE;
	m_joystick[1].id = JOY_NONE;
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

	std::string keymap_file;
	auto keymap_default = g_program.config().get_file_path("keymap.map", FILE_TYPE_ASSET);
	
	try {
		keymap_file = g_program.config().get_file(GUI_SECTION, GUI_KEYMAP, FILE_TYPE_USER);
		if(keymap_file.empty()) {
			keymap_file = keymap_default;
		} else {
			if(!FileSys::file_exists(keymap_file.c_str())) {
				PWARNF(LOG_V0, LOG_GUI, "The kemap file '%s' doesn't exists, creating...\n", keymap_file.c_str());
				try {
					FileSys::copy_file(keymap_default.c_str(), keymap_file.c_str());
				} catch(std::exception &e) {
					PWARNF(LOG_V0, LOG_GUI, "Unable to create the keymap file, using the default one\n");
					keymap_file = keymap_default;
				}
			}
		}
	} catch(std::exception &e) {
		PWARNF(LOG_V0, LOG_GUI, "The keymap file name is undefined, using the default one\n");
		keymap_file = keymap_default;
	}
	
	try {
		m_keymap.load(keymap_file);
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
		PWARNF(LOG_V0, LOG_GUI, "Unable to init SDL Joystick subsystem: %s\n", SDL_GetError());
	} else {
		SDL_JoystickEventState(SDL_ENABLE);
		PDEBUGF(LOG_V2, LOG_GUI, "Joy evt state: %d\n", SDL_JoystickEventState(SDL_QUERY));
		int connected = SDL_NumJoysticks();
		m_joystick[0].show_message = (connected < 1);
		m_joystick[1].show_message = (connected < 2);
	}

	// CAPTURE THREAD
	m_capture = std::make_unique<Capture>(vga_display(), m_mixer);
	m_capture_thread = std::thread(&Capture::thread_start, m_capture.get());
	
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
	
	std::mutex m;
	std::condition_variable cv;
	std::unique_lock<std::mutex> lock(m);
	m_capture->sig_config_changed(m, cv);
	cv.wait(lock);
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

bool GUI::is_fullscreen()
{
	return (SDL_GetWindowFlags(m_SDL_window) & SDL_WINDOW_FULLSCREEN);
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

void GUI::send_key_to_machine(Keys _key, uint32_t _keystate)
{
	if(m_machine->is_on()) {
		if(_keystate == KEY_PRESSED && m_key_state[_key]) {
			PDEBUGF(LOG_V2, LOG_GUI, "key %s already pressed\n", 
					Keymap::ms_keycode_str_table.at(_key).c_str());
		} else if(_keystate == KEY_RELEASED && !m_key_state[_key]) {
			PDEBUGF(LOG_V2, LOG_GUI, "key %s already released\n", 
					Keymap::ms_keycode_str_table.at(_key).c_str());
		} else {
			m_machine->send_key_to_kbctrl(_key, _keystate);
		}
	}
	m_key_state[_key] = (_keystate == KEY_PRESSED);
}

void GUI::pevt_key(Keys _key, EventPhase _phase)
{
	auto sendk = [=](EventPhase ph) {
		if(m_machine->is_on()) {
			PDEBUGF(LOG_V2, LOG_GUI, "  pevt: keyboard key %s: %s\n", 
					Keymap::ms_keycode_str_table.at(_key).c_str(),
					ph==EventPhase::EVT_START?"KEY_PRESSED":"KEY_RELEASED");
		}
		send_key_to_machine(_key, ph==EventPhase::EVT_START?KEY_PRESSED:KEY_RELEASED);
	};
	if(_phase == EventPhase::EVT_REPEAT) {
		// key repeats are managed by the guest OS
		return;
	}
	if(_phase == EventPhase::EVT_ONESHOT) {
		sendk(EventPhase::EVT_START);
		sendk(EventPhase::EVT_END);
	} else {
		sendk(_phase);
	}
}

void GUI::pevt_mouse_axis(uint8_t _axis, const SDL_Event &_event)
{
	int amount = 0;
	switch(_event.type) {
		case SDL_MOUSEMOTION: {
			// direct translation, value is the non zero relative amount
			if(_event.motion.xrel != 0) {
				amount = _event.motion.xrel;
			} else {
				amount = _event.motion.yrel;
			}
			break;
		}
		case SDL_JOYAXISMOTION: {
			// TODO: this is useless
			amount = _event.jaxis.value>0 ? 1 : -1;
			break;
		}
		case SDL_KEYDOWN:
		case SDL_MOUSEBUTTONDOWN:
		case SDL_JOYBUTTONDOWN:
		default:
			return;
	}
	int x_amount = 0;
	int y_amount = 0;
	if(_axis == 0) {
		x_amount = amount;
	} else {
		y_amount = -amount;
	}
	PDEBUGF(LOG_V2, LOG_GUI, "  pevt: mouse %s axis: %d pixels\n", _axis?"Y":"X", _axis?y_amount:x_amount);
	m_machine->mouse_motion(x_amount, y_amount, 0);
}

void GUI::pevt_mouse_button(MouseButton _button, EventPhase _phase)
{
	PDEBUGF(LOG_V2, LOG_GUI, "  pevt: mouse button %d: \n", ec_to_i(_button), _phase==EventPhase::EVT_START?"pressed":"released");
	m_machine->mouse_button(_button, _phase==EventPhase::EVT_START);
}

void GUI::pevt_joy_axis(uint8_t _jid, uint8_t _axis, const SDL_Event &_event)
{
	int value = 0;
	switch(_event.type) {
		case SDL_MOUSEMOTION: {
			double amount = .0;
			if(_event.motion.x != 0) {
				amount = _event.motion.x - (m_width / 2.0);
				amount /= (m_width / 2.0);
			} else {
				amount = _event.motion.y - (m_height / 2.0);
				amount /= (m_height / 2.0);
			}
			value = amount * 32768.0;
			break;
		}
		case SDL_JOYAXISMOTION: {
			value = _event.jaxis.value;
			break;
		}
		case SDL_KEYDOWN:
		case SDL_KEYUP:
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
		default:
			break;
	}
	PDEBUGF(LOG_V2, LOG_GUI, "  pevt: joystick %s axis %d: %d\n", _jid?"B":"A", _axis, value);
	m_machine->joystick_motion(_jid, _axis, value);
}

void GUI::pevt_joy_button(uint8_t _jid, uint8_t _button, EventPhase _phase)
{
	int state = 0;
	if(_phase == EventPhase::EVT_START) {
		state = 1;
	}
	PDEBUGF(LOG_V2, LOG_GUI, "  pevt: joystick %s btn %d: %d\n", _jid?"B":"A", _button, state);
	m_machine->joystick_button(_jid, _button, state);
}

bool GUI::on_event_binding(const SDL_Event &_event, const Keymap::Binding &_binding, bool _guest_input, uint32_t _type_mask)
{
	bool gui_input = !_guest_input;
	
	for(auto const & pevt : _binding.pevt) {
		
		PDEBUGF(LOG_V2, LOG_GUI, "  evt=%x,mask=%x\n", ec_to_i(pevt.type), _type_mask);
		
		if(ec_to_i(pevt.type) & _type_mask) {
			PDEBUGF(LOG_V2, LOG_GUI, "  masked event\n");
			continue;
		}
		
		EventPhase phase;
		if(_event.type == SDL_KEYDOWN || _event.type == SDL_MOUSEBUTTONDOWN || _event.type == SDL_JOYBUTTONDOWN) {
			if(_event.type == SDL_KEYDOWN && _event.key.repeat) {
				phase = EventPhase::EVT_REPEAT;
			} else {
				phase = EventPhase::EVT_START;
			}
		} else if(_event.type == SDL_KEYUP || _event.type == SDL_MOUSEBUTTONUP || _event.type == SDL_JOYBUTTONUP) {
			phase = EventPhase::EVT_END;
		} else if(_event.type == SDL_MOUSEMOTION || _event.type == SDL_JOYAXISMOTION) {
			phase = EventPhase::EVT_ONESHOT;
		} else {
			// ???
			PDEBUGF(LOG_V0, LOG_GUI, "  unexpected SDL event type %d\n", _event.type);
			phase = EventPhase::EVT_END;
		}

		PDEBUGF(LOG_V2, LOG_GUI, "  pevt: %s%s\n", pevt.name.c_str(),
				phase==EventPhase::EVT_REPEAT?" (repeat)":"");

		switch(pevt.type) {
			case ProgramEvent::Type::EVT_KEY:
				if(_guest_input) {
					pevt_key(pevt.key, phase);
				}
				break;
			case ProgramEvent::Type::EVT_PROGRAM_FUNC:
				gui_input = false;
				ms_event_funcs.at(pevt.func)(*this, phase);
				break;
			case ProgramEvent::Type::EVT_MOUSE_AXIS:
				if(_guest_input) {
					pevt_mouse_axis(pevt.mouse.axis, _event);
				}
				break;
			case ProgramEvent::Type::EVT_MOUSE_BUTTON:
				if(_guest_input) {
					pevt_mouse_button(pevt.mouse.button, phase);
				}
				break;
			case ProgramEvent::Type::EVT_JOY_AXIS:
				if(_guest_input) {
					pevt_joy_axis(pevt.joy.which, pevt.joy.axis, _event);
				}
				break;
			case ProgramEvent::Type::EVT_JOY_BUTTON:
				if(_guest_input) {
					pevt_joy_button(pevt.joy.which, pevt.joy.button, phase);
				}
				break;
			default:
				break;
		}
	}
	
	return gui_input;
}

void GUI::on_keyboard_event(const SDL_Event &_event)
{
	std::string mod1 = bitfield_to_string(_event.key.keysym.mod & 0xff,
	{ "KMOD_LSHIFT", "KMOD_RSHIFT", "", "", "", "", "KMOD_LCTRL", "KMOD_RCTRL" });
	
	std::string mod2 = bitfield_to_string((_event.key.keysym.mod >> 8) & 0xff,
	{ "KMOD_LALT", "KMOD_RALT", "KMOD_LGUI", "KMOD_RGUI", "KMOD_NUM", "KMOD_CAPS", "KMOD_MODE", "KMOD_RESERVED" });
	if(!mod1.empty()) {
		mod2 = " " + mod2;
	}
	
	PDEBUGF(LOG_V2, LOG_GUI, "Key: evt=%s, sym=%s, code=%s, mod=[%s%s]\n",
			(_event.type == SDL_KEYDOWN)?"SDL_KEYDOWN":"SDL_KEYUP",
			Keymap::ms_sdl_keycode_str_table.find(_event.key.keysym.sym)->second.c_str(),
			Keymap::ms_sdl_scancode_str_table.find(_event.key.keysym.scancode)->second.c_str(),
			mod1.c_str(), mod2.c_str());
	
	bool guest_input = m_input_grab || !m_windows.need_input();
	bool gui_input = !guest_input;
	
	static const Keymap::Binding *combo = nullptr;
	const Keymap::Binding *binding_ptr = m_keymap.find_sdl_binding(_event.key);
	
	if(binding_ptr) {
		PDEBUGF(LOG_V2, LOG_GUI, "  match: %s\n", binding_ptr->name.c_str());
		Keymap::Binding binding = *binding_ptr;
		if(binding.is_pevt_keycombo()) {
			// a key combo is always modifier(s) + key
			if(_event.type == SDL_KEYDOWN) {
				if(binding_ptr == combo) {
					binding.remove_pevt_kmods();
				} else {
					// release all key modifiers
					for(auto k : {
						KEY_CTRL_L  ,
						KEY_SHIFT_L ,
						KEY_CTRL_R  ,
						KEY_SHIFT_R ,
						KEY_ALT_L   ,
						KEY_ALT_R   ,
						KEY_WIN_L   ,
						KEY_WIN_R
					}) {
						if(m_key_state[k]) {
							send_key_to_machine(k, KEY_RELEASED);
						}
					}
					combo = binding_ptr;
				}
			} else if(_event.type == SDL_KEYUP && combo) {
				if(binding_ptr == combo) {
					// release everything except the modifiers
					binding.remove_pevt_kmods();
				}
			}
		} else if(_event.type == SDL_KEYUP && combo) {
			if(binding.ievt.key.is_key_modifier()) {
				// release the combo
				binding = *combo;
				combo = nullptr;
			}
			//else {
			//	release this key
			//	keep combo modifiers pressed
			//}
		}
		//else if(_event.type == SDL_KEYDOWN && combo) {
		//	just execute the binding while keeping the prev combo modifiers pressed
		//}
		gui_input = on_event_binding(_event, binding, guest_input);
	} else {
		PDEBUGF(LOG_V2, LOG_GUI, "  no match\n");
	}
	if(gui_input) {
		dispatch_rocket_event(_event);
	}
}

void GUI::on_mouse_motion_event(const SDL_Event &_event)
{
	// mouse motion events are 2-in-1 (X and Y axes)
	
	PDEBUGF(LOG_V2, LOG_GUI, "Mouse motion: x:%d,y:%d\n", _event.motion.xrel, _event.motion.yrel);
	
	bool guest_input = m_input_grab;
	
	const Keymap::Binding *binding_x=nullptr, *binding_y=nullptr;
	SDL_Event mevent;
	
	memcpy(&mevent, &_event, sizeof(SDL_Event));
	
	uint32_t evtmask = 0;
	bool bind_x_map_x = false;
	bool bind_x_map_y = false;
	bool bind_y_map_x = false;
	bool bind_y_map_y = false;
	ProgramEvent x_mapping, y_mapping;
	
	x_mapping.type = y_mapping.type = ProgramEvent::Type::EVT_MOUSE_AXIS;
	y_mapping.mouse.axis = 1;
	
	if(_event.motion.xrel) {
		mevent.motion.yrel = 0;
		binding_x = m_keymap.find_sdl_binding(mevent.motion);
		if(binding_x) {
			bind_x_map_x = binding_x->has_prg_event(x_mapping);
			bind_x_map_y = binding_x->has_prg_event(y_mapping);
			PDEBUGF(LOG_V2, LOG_GUI, "  match for axis X: %s\n", binding_x->name.c_str());
		} else {
			PDEBUGF(LOG_V2, LOG_GUI, "  no match for axis X\n");
		}
	}
	if(_event.motion.yrel) {
		mevent.motion.xrel = 0;
		mevent.motion.yrel = _event.motion.yrel;
		binding_y = m_keymap.find_sdl_binding(mevent.motion);
		if(binding_y) {
			bind_y_map_x = binding_y->has_prg_event(x_mapping);
			bind_y_map_y = binding_y->has_prg_event(y_mapping);
			PDEBUGF(LOG_V2, LOG_GUI, "  match for axis Y: %s\n", binding_y->name.c_str());
		} else {
			PDEBUGF(LOG_V2, LOG_GUI, "  no match for axis Y\n");
		}
	}
	
	// check if bindings have mouse mappings to X and Y
	if(guest_input && (
	   (bind_x_map_x && bind_y_map_y) ||
	   (bind_x_map_y && bind_y_map_x) // inverted mouse? lol ok.
	))
	{
		// if so translate mouse to mouse as a special case (combine in a single event)
		// otherwise pointer motion would not work on the guest
		int xrel, yrel;
		if(bind_x_map_x) {
			xrel = _event.motion.xrel;
		} else if(bind_y_map_x) {
			xrel = -_event.motion.yrel;
		}
		if(bind_y_map_y) {
			yrel = _event.motion.yrel;
		} else  if(bind_x_map_y) {
			yrel = -_event.motion.xrel;
		}
		PDEBUGF(LOG_V2, LOG_GUI, "  pevt: mouse X,Y axis: %d,%d pixels\n", xrel,yrel);
		m_machine->mouse_motion(xrel, -yrel, 0);
		evtmask = ec_to_i(ProgramEvent::Type::EVT_MOUSE_AXIS);
	}

	// even if input is not grabbed, bindings could still be mapped to functions
	if(binding_x) {
		mevent.motion.x = _event.motion.x;
		mevent.motion.xrel = _event.motion.xrel;
		mevent.motion.y = 0;
		mevent.motion.yrel = 0;
		on_event_binding(mevent, *binding_x, guest_input, evtmask);
		if(bind_x_map_x || bind_x_map_y) {
			// don't send double mouse events to the guest! 
			evtmask = ec_to_i(ProgramEvent::Type::EVT_MOUSE_AXIS);
		}
	}
	if(binding_y) {
		mevent.motion.x = 0;
		mevent.motion.xrel = 0;
		mevent.motion.y = _event.motion.y;
		mevent.motion.yrel = _event.motion.yrel;
		on_event_binding(mevent, *binding_y, guest_input, evtmask);
	}
	
	if(!guest_input) {
		dispatch_rocket_event(_event);
	}
}

void GUI::on_mouse_button_event(const SDL_Event &_event)
{
	PDEBUGF(LOG_V2, LOG_GUI, "Mouse button: %d\n", _event.button.button);
	
	bool guest_input = m_input_grab;
	bool gui_input = !guest_input;
	
	const Keymap::Binding *binding = m_keymap.find_sdl_binding(_event.button);
	
	if(binding) {
		PDEBUGF(LOG_V2, LOG_GUI, "  match: %s\n", binding->name.c_str());
		gui_input = on_event_binding(_event, *binding, guest_input);
	} else {
		PDEBUGF(LOG_V2, LOG_GUI, "  no match\n");
	}
	if(gui_input) {
		dispatch_rocket_event(_event);
	}
}

void GUI::on_joystick_motion_event(const SDL_Event &_event)
{
	PDEBUGF(LOG_V2, LOG_GUI, "Joystick motion: joy:%d, axis:%d\n", _event.jaxis.which, _event.jaxis.axis);
	
	assert(_event.jaxis.which < Sint32(m_SDL_joysticks.size()));
	
	int jid = JOY_NONE;
	if(m_joystick[0].id == _event.jaxis.which) {
		jid = 0;
	} else if(m_joystick[1].id == _event.jaxis.which) {
		jid = 1;
	} else {
		return;
	}
	
	const Keymap::Binding *binding = m_keymap.find_sdl_binding(jid, _event.jaxis);
	if(binding) {
		PDEBUGF(LOG_V2, LOG_GUI, "  match: %s\n", binding->name.c_str());
		on_event_binding(_event, *binding, true);
	} else {
		PDEBUGF(LOG_V2, LOG_GUI, "  no match\n");
	}
}

void GUI::on_joystick_button_event(const SDL_Event &_event)
{
	PDEBUGF(LOG_V2, LOG_GUI, "Joystick button: joy:%d, button:%d, state:%d\n",
			_event.jbutton.which, _event.jbutton.button, _event.jbutton.state);
	
	assert(_event.jbutton.which < Sint32(m_SDL_joysticks.size()));
	
	int jid = JOY_NONE;
	if(m_joystick[0].id == _event.jbutton.which) {
		jid = 0;
	} else if(m_joystick[1].id == _event.jbutton.which) {
		jid = 1;
	} else {
		return;
	}
	
	const Keymap::Binding *binding = m_keymap.find_sdl_binding(jid, _event.jbutton);
	if(binding) {
		PDEBUGF(LOG_V2, LOG_GUI, "  match: %s\n", binding->name.c_str());
		on_event_binding(_event, *binding, true);
	} else {
		PDEBUGF(LOG_V2, LOG_GUI, "  no match\n");
	}
}

void GUI::on_joystick_event(const SDL_Event &_event)
{
	auto print_joy_message = [this](SDL_Joystick *joy, int jid, bool forced = false) {
		char *mex;
		if(asprintf(&mex,
				"Joystick %s: %s (%d axes, %d buttons)",
				jid?"B":"A",
				SDL_JoystickName(joy),
				SDL_JoystickNumAxes(joy),
				SDL_JoystickNumButtons(joy)) > 0)
		{
			PINFOF(LOG_V0, LOG_GUI, "%s\n", mex);
			if(m_joystick[jid].show_message || forced) {
				show_message(mex);
			}
			free(mex);
		}
		m_joystick[jid].show_message = true;
	};
	
	if(_event.type == SDL_JOYDEVICEADDED) {
		SDL_Joystick *joy = SDL_JoystickOpen(_event.jdevice.which);
		if(joy) {
			m_SDL_joysticks.push_back(joy);
			int jinstance = m_SDL_joysticks.size()-1;
			PDEBUGF(LOG_V1, LOG_GUI, "Joystick SDL index %d / instance id %d has been added\n",
					_event.jdevice.which, jinstance);
			int jid = JOY_NONE;
			if(m_joystick[0].id == JOY_NONE) {
				m_joystick[0].id = jinstance;
				jid = 0;
			} else if(m_joystick[1].id == JOY_NONE) {
				m_joystick[1].id = jinstance;
				jid = 1;
			} else {
				jid = 2;
			}
			if(jid < 2) {
				print_joy_message(joy, jid);
			}
		} else {
			PWARNF(LOG_V0, LOG_GUI, "Couldn't open Joystick index %d\n", _event.jdevice.which);
		}
	} else if(_event.type == SDL_JOYDEVICEREMOVED) {
		PDEBUGF(LOG_V1, LOG_GUI, "Joystick SDL instance id %d has been removed\n", _event.jdevice.which);
		assert(_event.jdevice.which <= Sint32(m_SDL_joysticks.size()));
		
		SDL_Joystick *joy = m_SDL_joysticks[_event.jdevice.which];
		if(SDL_JoystickGetAttached(joy)) {
			SDL_JoystickClose(joy);
		}
		bool notify_user = false;
		m_SDL_joysticks[_event.jdevice.which] = nullptr;
		if(m_joystick[0].id == _event.jdevice.which) {
			PINFOF(LOG_V1, LOG_GUI, "Joystick A has been removed\n");
			m_joystick[0].id = m_joystick[1].id;
			m_joystick[1].id = JOY_NONE;
			notify_user = (m_joystick[0].id != JOY_NONE);
		} else if(m_joystick[1].id == _event.jdevice.which) {
			PINFOF(LOG_V1, LOG_GUI, "Joystick B has been removed\n");
			m_joystick[1].id = JOY_NONE;
		}
		if(m_joystick[1].id==JOY_NONE && m_joystick[0].id!=JOY_NONE && SDL_NumJoysticks()>1) {
			for(int j=0; j<int(m_SDL_joysticks.size()); j++) {
				if(m_SDL_joysticks[j]!=nullptr && j!=m_joystick[0].id) {
					m_joystick[1].id = j;
					notify_user = true;
				}
			}
		}
		if(notify_user && m_joystick[0].id != JOY_NONE) {
			print_joy_message(m_SDL_joysticks[m_joystick[0].id], 0, true);
		}
		if(notify_user && m_joystick[1].id != JOY_NONE) {
			print_joy_message(m_SDL_joysticks[m_joystick[1].id], 1, true);
		}
	}
}

void GUI::dispatch_event(const SDL_Event &_event)
{
	switch(_event.type) {
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			on_keyboard_event(_event);
			break;
		case SDL_MOUSEMOTION:
			on_mouse_motion_event(_event);
			break;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			on_mouse_button_event(_event);
			break;
		case SDL_JOYAXISMOTION:
			on_joystick_motion_event(_event);
			break;
		case SDL_JOYBUTTONDOWN:
		case SDL_JOYBUTTONUP:
			on_joystick_button_event(_event);
			break;
		case SDL_JOYDEVICEADDED:
		case SDL_JOYDEVICEREMOVED:
			on_joystick_event(_event);
			break;
		case SDL_WINDOWEVENT:
			dispatch_window_event(_event.window);
			break;
		case SDL_USEREVENT:
			//the 1-second timer
			break;
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
		case SDL_WINDOWEVENT_SIZE_CHANGED: {
			PDEBUGF(LOG_V1, LOG_GUI, "Window resized to %ux%u\n", _event.data1, _event.data2);
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
			PDEBUGF(LOG_V2, LOG_GUI, "Unhandled SDL window event: %d\n", _event.event);
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

void GUI::cmd_stop_capture_and_signal(std::mutex &_mutex, std::condition_variable &_cv)
{
	m_capture->cmd_stop_capture_and_signal(_mutex, _cv);
}

void GUI::shutdown()
{
	m_capture->cmd_quit();
	m_capture_thread.join();
	PDEBUGF(LOG_V1, LOG_GUI, "Capture thread stopped\n");
	
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
	std::string path = g_program.config().find_file(CAPTURE_SECTION, CAPTURE_DIR);
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

void GUI::toggle_dbg_windows()
{
	m_windows.toggle_dbg();
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

	ProgramEvent evt;
	evt.type = ProgramEvent::Type::EVT_PROGRAM_FUNC;
	std::vector<const Keymap::Binding *> bindings;

	cx = bd;
	evt.func = ProgramEvent::Func::FUNC_GUI_MODE_ACTION;
	bindings = m_keymap.find_prg_bindings(evt);
	if(!bindings.empty()) {
		if(m_mode == GUI_MODE_COMPACT) {
			ps("\nTo show/hide the interface press ", 0xf, bg, bd);
			ps(bindings[0]->ievt.name.c_str(), 0xe, bg, bd);
			ps(" or grab the mouse.\n", 0xf, bg, bd);
		} else if(m_mode == GUI_MODE_REALISTIC) {
			ps("\nTo zoom in on the monitor press ", 0xf, bg, bd);
			ps(bindings[0]->ievt.name.c_str(), 0xe, bg, bd);
			ps("\nTo switch between the interface styles keep ", 0xf, bg, bd);
			ps(bindings[0]->ievt.name.c_str(), 0xe, bg, bd);
			ps(" pressed.\n", 0xf, bg, bd);
		} else {
			cy++;
		}
	}

	evt.func = ProgramEvent::Func::FUNC_TOGGLE_POWER;
	bindings = m_keymap.find_prg_bindings(evt);
	if(!bindings.empty()) {
		ps("To start/stop the machine press ", 0xf, bg, bd);
		ps(bindings[0]->ievt.name.c_str(), 0xe, bg, bd);
		ps("\n", 0xe, bg, bd);
	}
	
	evt.func = ProgramEvent::Func::FUNC_GRAB_MOUSE;
	bindings = m_keymap.find_prg_bindings(evt);
	if(!bindings.empty()) {
		ps("To grab the mouse press ", 0xf, bg, bd);
		ps(bindings[0]->ievt.name.c_str(), 0xe, bg, bd);
		ps("\n", 0xe, bg, bd);
	}
	if(m_mode == GUI_MODE_REALISTIC) {
		evt.func = ProgramEvent::Func::FUNC_TOGGLE_PAUSE;
		bindings = m_keymap.find_prg_bindings(evt);
		if(!bindings.empty()) {
			ps("To pause the machine press ", 0xf, bg, bd);
			ps(bindings[0]->ievt.name.c_str(), 0xe, bg, bd);
			ps("\n", 0xe, bg, bd);
		}
		evt.func = ProgramEvent::Func::FUNC_QUICK_SAVE_STATE;
		bindings = m_keymap.find_prg_bindings(evt);
		if(!bindings.empty()) {
			ps("To save the machine's state press ", 0xf, bg, bd);
			ps(bindings[0]->ievt.name.c_str(), 0xe, bg, bd);
			evt.func = ProgramEvent::Func::FUNC_QUICK_LOAD_STATE;
			bindings = m_keymap.find_prg_bindings(evt);
			if(!bindings.empty()) {
				ps("\nTo load the last saved state press ", 0xf, bg, bd);
				ps(bindings[0]->ievt.name.c_str(), 0xe, bg, bd);
				ps("\n", 0xe, bg, bd);
			}
		}
	}
	evt.func = ProgramEvent::Func::FUNC_TOGGLE_FULLSCREEN;
	bindings = m_keymap.find_prg_bindings(evt);
	if(!bindings.empty()) {
		ps("To toggle fullscreen mode press ", 0xf, bg, bd);
		ps(bindings[0]->ievt.name.c_str(), 0xe, bg, bd);
		ps("\n", 0xe, bg, bd);
	}
	evt.func = ProgramEvent::Func::FUNC_EXIT;
	bindings = m_keymap.find_prg_bindings(evt);
	if(!bindings.empty()) {
		ps("To close the emulator press ", 0xf, bg, bd);
		ps(bindings[0]->ievt.name.c_str(), 0xe, bg, bd);
		ps("\n", 0xe, bg, bd);
	}
	ps("\nYou can find the configuration file here:\n", 0xf, bg, bd);
	ps(g_program.config().get_parsed_file().c_str(), 0xe, bg, bd);
	ps("\n\nFor more information read the README file and visit the project page at\n", 0xf, bg, bd);
	ps("https://barotto.github.io/IBMulator/\n", 0xe, bg, bd);

	m_machine->cmd_print_VGA_text(text);
}

void GUI::pevt_func_none(EventPhase)
{
	PDEBUGF(LOG_V0, LOG_GUI, "Unknown func event!\n");
}

void GUI::pevt_func_gui_mode_action(EventPhase _phase)
{
	PDEBUGF(LOG_V1, LOG_GUI, "GUI mode action func event\n");
	
	static int repeat = 0;
	if(_phase == EventPhase::EVT_REPEAT) {
		if(++repeat == 1) {
			std::lock_guard<std::mutex> lock(ms_rocket_mutex);
			m_windows.interface->action(1);
			m_windows.interface->container_size_changed(m_width, m_height);
		}
	} else if(_phase == EventPhase::EVT_END) {
		if(!repeat) {
			std::lock_guard<std::mutex> lock(ms_rocket_mutex);
			m_windows.interface->action(0);
			m_windows.interface->container_size_changed(m_width, m_height);
			if(m_mode == GUI_MODE_COMPACT &&
			  dynamic_cast<NormalInterface*>(m_windows.interface)->is_system_visible())
			{
				input_grab(false);
			}
		}
		repeat = 0;
	}
}

void GUI::pevt_func_toggle_power(EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Toggle machine power button func event\n");
	
	m_windows.interface->switch_power();
}

void GUI::pevt_func_toggle_pause(EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Toggle pause func event\n");
	
	if(m_machine->is_paused()) {
		m_machine->cmd_resume();
	} else {
		m_machine->cmd_pause();
	}
}

void GUI::pevt_func_toggle_dbg_wnd(EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Toggle debugging windows func event\n");
	toggle_dbg_windows();
}

void GUI::pevt_func_take_screenshot(EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Take screenshot func event\n");
	
	take_screenshot(
		#ifndef NDEBUG
		true
		#endif
	);
}

void GUI::pevt_func_toggle_audio_capture(EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Toggle audio capture func event\n");
	
	m_mixer->cmd_toggle_capture();
}

void GUI::pevt_func_toggle_video_capture(EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Toggle video capture func event\n");
	
	m_capture->cmd_toggle_capture();
}

void GUI::pevt_func_quick_save_state(EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Quick save func event\n");
	
	// don't save key presses in a machine state.
	// every keypress must be neutralized by a release before save.
	for(const auto & [key, state] : m_key_state) {
		if(state) {
			// the eventual physical key releases will not be sent to the guest again,
			// as the keys' states are checked in send_key_to_machine()
			send_key_to_machine(key, KEY_RELEASED);
		}
	}
	
	g_program.save_state("", [this](){
		show_message("State saved");
	}, nullptr);
}

void GUI::pevt_func_quick_load_state(EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Quick load func event\n");
	
	for(const auto & [key, state] : m_key_state) {
		if(state) {
			send_key_to_machine(key, KEY_RELEASED);
		}
	}
	
	g_program.restore_state("", [this](){
		show_message("State restored");
	}, nullptr);
}

void GUI::pevt_func_grab_mouse(EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Grab mouse func event\n");
	
	toggle_input_grab();
	
	if(m_mode == GUI_MODE_COMPACT) {
		if(m_input_grab) {
			dynamic_cast<NormalInterface*>(m_windows.interface)->hide_system();
		} else {
			dynamic_cast<NormalInterface*>(m_windows.interface)->show_system();
		}
	}
}

void GUI::pevt_func_sys_speed_up(EventPhase _phase)
{
	if(_phase == EventPhase::EVT_END) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "System speed up func event\n");
	
	double factor = m_symspeed_factor * 1.1;
	if(factor > 0.91 && factor < 1.09) {
		factor = 1.0;
		if(_phase == EventPhase::EVT_REPEAT) {
			m_machine->cmd_cycles_adjust(factor);
			return;
		}
	}
	if(factor < 5.0) {
		m_symspeed_factor = factor;
		m_machine->cmd_cycles_adjust(m_symspeed_factor);
	} else {
		m_machine->cmd_cycles_adjust(5.0);
	}
}

void GUI::pevt_func_sys_speed_down(EventPhase _phase)
{
	if(_phase == EventPhase::EVT_END) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "System speed down func event\n");
	
	double factor = m_symspeed_factor / 1.1;
	if(factor > 0.91 && factor < 1.09) {
		factor = 1.0;
		if(_phase == EventPhase::EVT_REPEAT) {
			m_machine->cmd_cycles_adjust(factor);
			return;
		}
	}
	if(factor < 0.00002) {
		m_machine->cmd_pause();
	} else {
		m_symspeed_factor = factor;
		m_machine->cmd_cycles_adjust(m_symspeed_factor);
	}
}

void GUI::pevt_func_toggle_fullscreen(EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Toggle fullscreen func event\n");

	toggle_fullscreen();
}

void GUI::pevt_func_exit(EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Quit func event\n");
	SDL_Event sdlevent;
	sdlevent.type = SDL_QUIT;
	SDL_PushEvent(&sdlevent);
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
		timers.activate_timer(ifcmex_timer, g_program.pacer().chrono().get_nsec(), 3e9, false);
	}
}

void GUI::Windows::show_dbg_message(const char* _mex)
{
	std::lock_guard<std::mutex> lock(ms_rocket_mutex);
	if(dbgtools != nullptr) {
		dbgtools->show_message(_mex);
		timers.activate_timer(dbgmex_timer, g_program.pacer().chrono().get_nsec(), 3e9, false);
	}
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

bool GUI::Windows::need_input()
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

