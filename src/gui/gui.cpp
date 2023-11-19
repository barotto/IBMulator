/*
 * Copyright (C) 2015-2023  Marco Bortolin
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

#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>
#include "rml/sys_interface.h"
#include "rml/file_interface.h"
#include "stb/stb.h"

#include "windows/desktop.h"
#include "windows/normal_interface.h"
#include "windows/realistic_interface.h"
#include "windows/debugtools.h"
#include "windows/status.h"
#include "windows/printer_control.h"
#include "windows/mixer_control.h"
#include "windows/shader_parameters.h"

#include "capture/capture.h"

#include "hardware/devices/systemboard.h"

#include <algorithm>
#include <iomanip>

ini_enum_map_t g_mouse_types = {
	{ "none", MOUSE_TYPE_NONE },
	{ "ps2", MOUSE_TYPE_PS2 },
	{ "imps2", MOUSE_TYPE_IMPS2 },
	{ "serial", MOUSE_TYPE_SERIAL },
	{ "serial-wheel", MOUSE_TYPE_SERIAL_WHEEL },
	{ "serial-msys", MOUSE_TYPE_SERIAL_MSYS }
};

std::map<std::string, uint> GUI::ms_gui_modes = {
	{ "compact",   GUI_MODE_NORMAL },
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
	{ "vga",      DISPLAY_ASPECT_VGA },
	{ "area",     DISPLAY_ASPECT_AREA },
	{ "original", DISPLAY_ASPECT_ORIGINAL },
};

std::map<std::string, uint> GUI::ms_display_scale = {
	{ "1x",      DISPLAY_SCALE_1X },
	{ "fill",    DISPLAY_SCALE_FILL },
	{ "integer", DISPLAY_SCALE_INTEGER }
};

const std::map<ProgramEvent::FuncName, std::function<void(GUI&,const ProgramEvent::Func&,GUI::EventPhase)>>
	GUI::ms_event_funcs = {
	{ ProgramEvent::FuncName::FUNC_NONE,                 &GUI::pevt_func_none                 },
	{ ProgramEvent::FuncName::FUNC_SHOW_OPTIONS,         &GUI::pevt_func_show_options         },
	{ ProgramEvent::FuncName::FUNC_TOGGLE_MIXER,         &GUI::pevt_func_toggle_mixer         },
	{ ProgramEvent::FuncName::FUNC_GUI_MODE_ACTION,      &GUI::pevt_func_gui_mode_action      },
	{ ProgramEvent::FuncName::FUNC_TOGGLE_POWER,         &GUI::pevt_func_toggle_power         },
	{ ProgramEvent::FuncName::FUNC_TOGGLE_PAUSE,         &GUI::pevt_func_toggle_pause         },
	{ ProgramEvent::FuncName::FUNC_TOGGLE_STATUS_IND,    &GUI::pevt_func_toggle_status_ind    },
	{ ProgramEvent::FuncName::FUNC_TOGGLE_DBG_WND,       &GUI::pevt_func_toggle_dbg_wnd       },
	{ ProgramEvent::FuncName::FUNC_TOGGLE_PRINTER,       &GUI::pevt_func_toggle_printer       },
	{ ProgramEvent::FuncName::FUNC_TAKE_SCREENSHOT,      &GUI::pevt_func_take_screenshot      },
	{ ProgramEvent::FuncName::FUNC_TOGGLE_AUDIO_CAPTURE, &GUI::pevt_func_toggle_audio_capture },
	{ ProgramEvent::FuncName::FUNC_TOGGLE_VIDEO_CAPTURE, &GUI::pevt_func_toggle_video_capture },
	{ ProgramEvent::FuncName::FUNC_INSERT_FLOPPY,        &GUI::pevt_func_insert_floppy        },
	{ ProgramEvent::FuncName::FUNC_EJECT_FLOPPY,         &GUI::pevt_func_eject_floppy         },
	{ ProgramEvent::FuncName::FUNC_CHANGE_FLOPPY_DRIVE,  &GUI::pevt_func_change_floppy_drive  },
	{ ProgramEvent::FuncName::FUNC_SAVE_STATE,           &GUI::pevt_func_save_state           },
	{ ProgramEvent::FuncName::FUNC_LOAD_STATE,           &GUI::pevt_func_load_state           },
	{ ProgramEvent::FuncName::FUNC_QUICK_SAVE_STATE,     &GUI::pevt_func_quick_save_state     },
	{ ProgramEvent::FuncName::FUNC_QUICK_LOAD_STATE,     &GUI::pevt_func_quick_load_state     },
	{ ProgramEvent::FuncName::FUNC_GRAB_MOUSE,           &GUI::pevt_func_grab_mouse           },
	{ ProgramEvent::FuncName::FUNC_SYS_SPEED_UP,         &GUI::pevt_func_sys_speed_up         },
	{ ProgramEvent::FuncName::FUNC_SYS_SPEED_DOWN,       &GUI::pevt_func_sys_speed_down       },
	{ ProgramEvent::FuncName::FUNC_SYS_SPEED,            &GUI::pevt_func_sys_speed            },
	{ ProgramEvent::FuncName::FUNC_TOGGLE_FULLSCREEN,    &GUI::pevt_func_toggle_fullscreen    },
	{ ProgramEvent::FuncName::FUNC_SWITCH_KEYMAPS,       &GUI::pevt_func_switch_keymaps       },
	{ ProgramEvent::FuncName::FUNC_EXIT,                 &GUI::pevt_func_exit                 },
	{ ProgramEvent::FuncName::FUNC_RELOAD_RCSS,          &GUI::pevt_func_reload_rcss          },
};

GUI::GUI()
:
m_machine(nullptr),
m_mixer(nullptr),
m_width(640),
m_height(480),
m_SDL_window(nullptr),
m_curr_model_changed(false),
m_gui_visible(true),
m_mode(GUI_MODE_NORMAL),
m_framecap(GUI_FRAMECAP_VGA),
m_vsync(false),
m_vga_buffering(false),
m_threads_sync(false),
m_current_keymap(0),
m_symspeed_factor(1.0),
m_rml_sys_interface(nullptr),
m_rml_file_interface(nullptr),
m_rml_context(nullptr)
{
	m_input.gui = this;
	m_joystick[0].sdl_id = JOY_NONE;
	m_joystick[1].sdl_id = JOY_NONE;
	m_joystick[0].which = Joystick::Joy::A;
	m_joystick[1].which = Joystick::Joy::B;
}

GUI::~GUI()
{
	if(m_rml_sys_interface) {
		delete m_rml_sys_interface;
	}
	if(m_rml_file_interface) {
		delete m_rml_file_interface;
	}
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
		Uint8(g_program.config().get_int(GUI_SECTION, GUI_BG_R, 0)),
		Uint8(g_program.config().get_int(GUI_SECTION, GUI_BG_G, 0)),
		Uint8(g_program.config().get_int(GUI_SECTION, GUI_BG_B, 0)),
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
	m_scaling_factor = g_program.config().get_int(GUI_SECTION, GUI_UI_SCALING, 100);
	m_scaling_factor = std::max(m_scaling_factor, 100.0);
	m_scaling_factor = std::min(m_scaling_factor, 500.0);
	m_scaling_factor = std::floor((m_scaling_factor / 100.0) * 4.0) / 4.0;
	PINFOF(LOG_V0, LOG_GUI, "UI scaling: %.0f%%\n", m_scaling_factor * 100.0);

	try {
		init_rmlui();
	} catch(std::exception &) {
		shutdown_SDL();
		throw;
	}

	try {
		m_windows.init(m_machine, this, m_mixer, m_mode);
	} catch(std::exception &e) {
		shutdown();
		throw;
	}

	vec2i wsize = m_windows.interface->get_size();
	resize_window(wsize.x, wsize.y);

	SDL_ShowWindow(m_SDL_window);
	SDL_SetWindowPosition(m_SDL_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	
	if(g_program.config().get_bool(GUI_SECTION, GUI_FULLSCREEN)) {
		toggle_fullscreen();
	}

	auto keymap_value = g_program.config().get_string(GUI_SECTION, GUI_KEYMAP);
	auto keymap_default = g_program.config().get_file_path("keymap.map", FILE_TYPE_ASSET);
	if(keymap_value.empty()) {
		keymap_value = keymap_default;
	}
	auto keymaps = str_parse_tokens(keymap_value, "\\s*\\|\\s*");
	for(auto &keymap : keymaps) {
		if(keymap.empty()) {
			continue;
		}
		auto path = g_program.config().get_file_path(keymap, FILE_TYPE_USER);
		if(!FileSys::file_exists(path.c_str())) {
			PWARNF(LOG_V0, LOG_GUI, "The keymap file '%s' doesn't exists, creating...\n", keymap.c_str());
			try {
				FileSys::copy_file(keymap_default.c_str(), path.c_str());
			} catch(std::exception &e) {
				PWARNF(LOG_V0, LOG_GUI, "  cannot create the keymap file!\n");
				continue;
			}
		}
		try {
			load_keymap(path);
		} catch(std::exception &) {}
	}
	if(m_keymaps.empty()) {
		shutdown();
		PERRF(LOG_GUI, "No available valid keymaps!\n");
		throw std::exception();
	}

	m_gui_visible = true;

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
	
	// User defined events
	if(!ms_sdl_user_evt_id) {
		ms_sdl_user_evt_id = SDL_RegisterEvents(TimedEvents::GUI_TEVT_COUNT);
		PDEBUGF(LOG_V2, LOG_GUI, "Registered %d SDL user events\n", TimedEvents::GUI_TEVT_COUNT);
	}
	
	// DONE
	show_welcome_screen();
}

void GUI::load_keymap(const std::string &_filename)
{
	m_keymaps.emplace_back();
	try {
		m_keymaps.back().load(_filename);
	} catch(std::exception &e) {
		m_keymaps.pop_back();
		PERRF(LOG_GUI, "Unable to load keymap '%s'\n", _filename.c_str());
		throw std::exception();
	}
}

void GUI::config_changed(bool _startup) noexcept
{
	m_windows.config_changed(_startup);

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
	try {
		SDL_Surface *icon = stbi_load(iconfile.c_str());
		SDL_SetWindowIcon(m_SDL_window, icon);
		SDL_FreeSurface(icon);
	} catch(std::runtime_error &err) {
		PERRF(LOG_GUI, "Cannot load app icon '%s'\n", iconfile.c_str());
	}
#endif
}

vec2i GUI::resize_window(int _w, int _h)
{
	SDL_SetWindowSize(m_SDL_window, _w, _h);
	SDL_GetWindowSize(m_SDL_window, &m_width, &m_height);
	update_window_size(m_width, m_height);
	PINFOF(LOG_V0,LOG_GUI,"Window resized to %dx%d\n", m_width, m_height);
	return vec2i(m_width, m_height);
}

void GUI::restore_state(StateRecord::Info _info)
{
	// Main thread here
	m_machine->cmd_pause(false);
	m_machine->cmd_commit_media([=](){
		// Machine or FloppyLoader threads here
		m_cmd_queue.push([=]() {
			// Main thread here
			g_program.restore_state(_info, [this]() {
				show_message("State restored");
			}, nullptr);
		});
	});
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
	if(SDL_SetWindowFullscreen(m_SDL_window, flags) == 0) {
		m_fullscreen_toggled = true;
	} else {
		PERRF(LOG_GUI, "Toggling fullscreen mode failed: %s\n", SDL_GetError());
	}
}

void GUI::init_rmlui()
{
	create_renderer();
	
	m_rml_sys_interface = new RmlSystemInterface();
	m_rml_file_interface = new RmlFileInterface(m_assets_path.c_str());

	Rml::SetFileInterface(m_rml_file_interface);
	Rml::SetRenderInterface(m_rml_renderer.get());
	Rml::SetSystemInterface(m_rml_sys_interface);

	if(!Rml::Initialise()) {
		PERRF(LOG_GUI, "Unable to initialise RmlUi\n");
		throw std::exception();
	}
	Rml::LoadFontFace("fonts/ProFontWindows.ttf");
	Rml::LoadFontFace("fonts/Nouveau_IBM.ttf");
	Rml::LoadFontFace("fonts/Ubuntu-Regular.ttf");
	Rml::LoadFontFace("fonts/Ubuntu-Bold.ttf");
	m_rml_context = Rml::CreateContext("default", Rml::Vector2i(m_width, m_height));
	Rml::Debugger::Initialise(m_rml_context);

	m_rml_context->SetDensityIndependentPixelRatio(m_scaling_factor);
}

Rml::ElementDocument * GUI::load_document(const std::string &_filename)
{
	Rml::ElementDocument * document = m_rml_context->LoadDocument(_filename.c_str());

	if(document) {
		Rml::Element * title = document->GetElementById("title");
		if(title) {
			title->SetInnerRML(document->GetTitle());
		}
		PDEBUGF(LOG_V1, LOG_GUI, "Document \"%s\" loaded\n", _filename.c_str());
		m_windows.register_document(document);
	} else {
		PERRF(LOG_GUI, "Cannot load document file: '%s'\n", _filename.c_str());
	}

	return document;
}

void GUI::unload_document(Rml::ElementDocument *)
{
	// *after* the ElementDocument::Close() has been called, a context update is
	// necessary to avoid event listeners lifetime problems
	m_rml_context->Update();
}

void GUI::Mouse::enable_timer()
{
	if(!events_timer) {
		events_timer = SDL_AddTimer(10, GUI::Mouse::s_sdl_timer_callback, this);
		PDEBUGF(LOG_V2, LOG_GUI, "  mouse motion events generator enabled\n");
	}
}

void GUI::Mouse::disable_timer()
{
	if(events_timer) {
		SDL_RemoveTimer(events_timer);
		events_timer = 0;
		PDEBUGF(LOG_V2, LOG_GUI, "  mouse motion events generator disabled\n");
	}
}

Uint32 GUI::Mouse::s_sdl_timer_callback(Uint32 interval, void *obj)
{
	static_cast<GUI::Mouse*>(obj)->generate_sdl_event();
	return interval;
}

void GUI::Mouse::generate_sdl_event()
{
	// this function is called by a different thread
	SDL_Event event{};
	SDL_UserEvent userevent{};

	userevent.type = GUI::ms_sdl_user_evt_id + GUI::TimedEvents::GUI_TEVT_MOUSE;
	userevent.timestamp = SDL_GetTicks();
	userevent.code = 0;
	userevent.data1 = nullptr;
	userevent.data2 = nullptr;

	event.type = SDL_USEREVENT;
	event.user = userevent;

	SDL_PushEvent(&event);
}

void GUI::Mouse::update(double _time)
{
	speed[Axis::X] += accel[Axis::X] * _time;
	speed[Axis::Y] += accel[Axis::Y] * _time;

	if((maxspeed[Axis::X] > 0 && speed[Axis::X] > maxspeed[Axis::X]) || 
	   (maxspeed[Axis::X] < 0 && speed[Axis::X] < maxspeed[Axis::X]))
	{
		speed[Axis::X] = maxspeed[Axis::X];
	}
	if((maxspeed[Axis::Y] > 0 && speed[Axis::Y] > maxspeed[Axis::Y]) || 
	   (maxspeed[Axis::Y] < 0 && speed[Axis::Y] < maxspeed[Axis::Y]))
	{
		speed[Axis::Y] = maxspeed[Axis::Y];
	}

	rel[Axis::X] += speed[Axis::X];
	rel[Axis::Y] += speed[Axis::Y];
}

void GUI::Mouse::stop(Axis _axis)
{
	speed[_axis] = .0;
	accel[_axis] = .0;
	if(speed[Axis::X] == 0.0 && speed[Axis::Y] == 0.0) {
		disable_timer();
	}
}

void GUI::Mouse::send(Machine *_machine)
{
	int x_amount = int(rel[Axis::X]);
	rel[Axis::X] -= double(x_amount);

	int y_amount = int(rel[Axis::Y]);
	rel[Axis::Y] -= double(y_amount);

	if(x_amount != 0 || y_amount != 0) {
		_machine->mouse_motion(x_amount, y_amount, 0);
	}
}

void GUI::Joystick::enable_timer()
{
	if(!events_timer) {
		events_timer = SDL_AddTimer(10, GUI::Joystick::s_sdl_timer_callback, this);
		PDEBUGF(LOG_V2, LOG_GUI,
			"  joystick motion events generator enabled: X=s:%d,v:%d,m:%d/Y=s:%d,v:%d,m:%d\n",
			speed[Axis::X], value[Axis::X], maxvalue[Axis::X],
			speed[Axis::Y], value[Axis::Y], maxvalue[Axis::Y]
		);
	}
}

void GUI::Joystick::disable_timer()
{
	if(events_timer) {
		SDL_RemoveTimer(events_timer);
		events_timer = 0;
		PDEBUGF(LOG_V2, LOG_GUI,
			"  joystick motion events generator disabled: X=s:%d,v:%d,m:%d/Y=s:%d,v:%d,m:%d\n",
			speed[Axis::X], value[Axis::X], maxvalue[Axis::X],
			speed[Axis::Y], value[Axis::Y], maxvalue[Axis::Y]
		);
	}
}

Uint32 GUI::Joystick::s_sdl_timer_callback(Uint32 interval, void *obj)
{
	static_cast<GUI::Joystick*>(obj)->generate_sdl_event();
	return interval;
}

void GUI::Joystick::generate_sdl_event()
{
	// this function is called by a different thread
	SDL_Event event{};
	SDL_UserEvent userevent{};

	userevent.type = GUI::ms_sdl_user_evt_id + GUI::TimedEvents::GUI_TEVT_JOYSTICK;
	userevent.timestamp = SDL_GetTicks();
	userevent.code = which;
	userevent.data1 = nullptr;
	userevent.data2 = nullptr;

	event.type = SDL_USEREVENT;
	event.user = userevent;

	SDL_PushEvent(&event);
}

void GUI::Joystick::update(int _time_ms)
{
	for(int axis=Axis::X; axis<Axis::MAX_AXIS; axis++) {
		int oldvalue = value[axis];
		value[axis] += speed[axis] * _time_ms;

		if((maxvalue[axis] > 0 && value[axis] > maxvalue[axis]) || 
		   (maxvalue[axis] < 0 && value[axis] < maxvalue[axis]) ||
		   (maxvalue[axis] == 0 && value[axis] < 0 && oldvalue > 0) ||
		   (maxvalue[axis] == 0 && value[axis] > 0 && oldvalue < 0)
		   )
		{
			value[axis] = maxvalue[axis];
		}
	}
}

void GUI::Joystick::stop(Axis _axis)
{
	speed[_axis] = 0;
	if(speed[Axis::X] == 0 && speed[Axis::Y] == 0) {
		disable_timer();
	}
}

void GUI::Joystick::send(Machine *_machine)
{
	for(int axis=Axis::X; axis<Axis::MAX_AXIS; axis++) {
		if(speed[axis] != 0) {
			_machine->joystick_motion(which, axis, value[axis]);
			if((maxvalue[axis] > 0 && value[axis] >= maxvalue[axis]) ||
			   (maxvalue[axis] < 0 && value[axis] <= maxvalue[axis]) ||
			   (maxvalue[axis] == 0 && value[axis] == 0))
			{
				speed[axis] = 0;
				//stop(static_cast<Axis>(axis));
			}
		}
	}
	if(speed[Axis::X] == 0 && speed[Axis::Y] == 0) {
		disable_timer();
	}
}

void GUI::InputSystem::Event::enable_timer(Uint32 _interval_ms)
{
	disable_timer();
	events_timer = SDL_AddTimer(_interval_ms,
		&GUI::InputSystem::Event::s_sdl_timer_callback,
		(void*)(intptr_t(code))
	);
}

void GUI::InputSystem::Event::disable_timer()
{
	if(events_timer) {
		SDL_RemoveTimer(events_timer);
		events_timer = 0;
	}
}

void GUI::InputSystem::Event::restart()
{
	disable_timer();
	pevt_idx = 0;
}

Uint32 GUI::InputSystem::Event::s_sdl_timer_callback(Uint32 _interval, void *_obj)
{
	UNUSED(_interval);

	auto evt_code = intptr_t(_obj);
	auto event = g_program.gui_instance()->m_input.find(evt_code);
	if(event) {
		event->generate_sdl_event();
	} else {
		PDEBUGF(LOG_V2, LOG_GUI, "Event code %llu expired\n", uint64_t(evt_code));
	}
	// returning 0 will disable the timer
	return 0;
}

void GUI::InputSystem::Event::generate_sdl_event()
{
	SDL_Event event{};
	SDL_UserEvent userevent{};

	userevent.type = GUI::ms_sdl_user_evt_id + GUI::TimedEvents::GUI_TEVT_PEVT;
	userevent.timestamp = SDL_GetTicks();
	userevent.code = code;
	userevent.data1 = nullptr;
	userevent.data2 = nullptr;

	event.type = SDL_USEREVENT;
	event.user = userevent;

	SDL_PushEvent(&event);

	disable_timer();
}

void GUI::grab_input(bool _value)
{
	if(m_mouse.grab) {
		if(_value) {
			SDL_SetRelativeMouseMode(SDL_TRUE);
		} else {
			SDL_SetRelativeMouseMode(SDL_FALSE);
		}
	}
	m_input.grab = _value;
}

bool GUI::is_input_grabbed()
{
	return m_input.grab;
}

void GUI::send_key_to_machine(Keys _key, uint32_t _keystate)
{
	if(m_machine->is_on()) {
		if(_keystate == KEY_RELEASED && !m_key_state[_key]) {
			PDEBUGF(LOG_V2, LOG_GUI, "key %s not pressed\n", Keymap::ms_keycode_str_table.at(_key).c_str());
		} else {
			m_machine->send_key_to_kbctrl(_key, _keystate);
		}
	}
	m_key_state[_key] = (_keystate == KEY_PRESSED);
}

void GUI::pevt_key(Keys _key, EventPhase _phase)
{
	if(m_machine->is_on()) {
		PDEBUGF(LOG_V2, LOG_GUI, "  pevt: keyboard key %s: %s\n", 
				Keymap::ms_keycode_str_table.at(_key).c_str(),
				_phase==EventPhase::EVT_START?"KEY_PRESSED":"KEY_RELEASED");
	}
	send_key_to_machine(_key, _phase==EventPhase::EVT_START?KEY_PRESSED:KEY_RELEASED);
}

void GUI::pevt_mouse_axis(const ProgramEvent::Mouse &_mouse, const SDL_Event &_sdl_evt,
		EventPhase _phase, Keymap::Binding::Mode _mode)
{
	int pixper10ms = _mouse.params[0]==0 ? 10 : _mouse.params[0];

	switch(_sdl_evt.type) {
		case SDL_MOUSEMOTION: {
			if(_phase == EventPhase::EVT_END) {
				return;
			}
			// direct translation, value is the non zero relative amount
			// param 0 sets the direction
			int amount = 0;
			int sign = pixper10ms < 0 ? -1 : 1;
			if(_sdl_evt.motion.xrel != 0) {
				amount = sign * _sdl_evt.motion.xrel;
			} else {
				amount = -1 * sign * _sdl_evt.motion.yrel;
			}
			m_mouse.rel[_mouse.axis] += amount;
			PDEBUGF(LOG_V2, LOG_GUI, "  pevt: mouse %s axis: %d pixels\n", _mouse.axis?"Y":"X", amount);
			break;
		}
		case SDL_JOYAXISMOTION: {
			if(_phase == EventPhase::EVT_END || _sdl_evt.jaxis.value == 0) {
				PDEBUGF(LOG_V2, LOG_GUI, "  pevt: mouse %s axis: stop\n", _mouse.axis?"Y":"X");
				m_mouse.stop(static_cast<Mouse::Axis>(_mouse.axis));
			} else {
				if(_mouse.params[1] == 2) {
					// single shot
					int amount = 0;
					if(_sdl_evt.jaxis.value < 0) {
						amount = -1.0 * pixper10ms;
					} else {
						amount = pixper10ms;
					}
					m_mouse.rel[_mouse.axis] += amount;
					PDEBUGF(LOG_V2, LOG_GUI, "  pevt: mouse %s axis: single shot %d pixels\n", _mouse.axis?"Y":"X", amount);
					return;
				}
				// accelerated and continuous/proportional
				double value = double(_sdl_evt.jaxis.value) / 32768.0;
				double amount = smoothstep(0.0, 1.0, std::fabs(value));
				m_mouse.maxspeed[_mouse.axis] = pixper10ms;
				if(value < .0) {
					m_mouse.maxspeed[_mouse.axis] *= -1.0;
				}
				m_mouse.maxspeed[_mouse.axis] = amount * m_mouse.maxspeed[_mouse.axis];
				double accel = double(_mouse.params[2]) / 1000.0;
				if(accel == .0) {
					accel = 0.005;
				}
				switch(_mouse.params[1]) {
					case 1:
						// accelerated
						if(m_mouse.maxspeed[_mouse.axis] < .0) {
							m_mouse.accel[_mouse.axis] = -accel;
						} else {
							m_mouse.accel[_mouse.axis] = accel;
						}
						break;
					case 0:
					default:
						// continuous/proportional
						m_mouse.accel[_mouse.axis] = m_mouse.maxspeed[_mouse.axis];
						break;
				}
				PDEBUGF(LOG_V2, LOG_GUI, "  pevt: mouse %s axis: %.3f->%.3f pixels/10ms^2, max:%.3f\n",
						_mouse.axis?"Y":"X", value, m_mouse.accel[_mouse.axis], m_mouse.maxspeed[_mouse.axis]);
				m_mouse.enable_timer();
			}
			return;
		}
		case SDL_KEYDOWN:
		case SDL_MOUSEBUTTONDOWN:
		case SDL_JOYBUTTONDOWN:
			if(_mode == Keymap::Binding::Mode::ONE_SHOT) {
				m_mouse.rel[_mouse.axis] += pixper10ms;
				PDEBUGF(LOG_V2, LOG_GUI, "  pevt: mouse %s axis: %d pixels\n", _mouse.axis?"Y":"X", pixper10ms);
			} else if(_phase == EventPhase::EVT_END) {
				PDEBUGF(LOG_V2, LOG_GUI, "  pevt: mouse %s axis: 0.00 pixels/10ms\n", _mouse.axis?"Y":"X");
				m_mouse.stop(static_cast<Mouse::Axis>(_mouse.axis));
			} else {
				m_mouse.maxspeed[_mouse.axis] = pixper10ms;
				double accel = double(_mouse.params[2]) / 1000.0;
				if(accel == .0) {
					accel = 0.005;
				}
				switch(_mouse.params[1]) {
					case 1:
						// accelerated
						if(m_mouse.maxspeed[_mouse.axis] < .0) {
							m_mouse.accel[_mouse.axis] = -accel;
						} else {
							m_mouse.accel[_mouse.axis] = accel;
						}
						break;
					case 0:
					default:
						// continuous
						m_mouse.accel[_mouse.axis] = m_mouse.maxspeed[_mouse.axis];
						break;
				}
				PDEBUGF(LOG_V2, LOG_GUI, "  pevt: mouse %s axis: %.3f pixels/10ms^2, max:%.3f\n",
						_mouse.axis?"Y":"X", m_mouse.accel[_mouse.axis], m_mouse.maxspeed[_mouse.axis]);
				m_mouse.enable_timer();
			}
			return;
		default:
			return;
	}
}

void GUI::pevt_mouse_button(const ProgramEvent::Mouse &_mouse, EventPhase _phase)
{
	PDEBUGF(LOG_V2, LOG_GUI, "  pevt: mouse button %d: %s\n", ec_to_i(_mouse.button), _phase==EventPhase::EVT_START?"pressed":"released");
	m_machine->mouse_button(_mouse.button, _phase==EventPhase::EVT_START);
}

void GUI::pevt_joy_axis(const ProgramEvent::Joy &_joy, const SDL_Event &_event, EventPhase _phase)
{
	switch(_event.type) {
		case SDL_MOUSEMOTION: {
			double amount = .0;
			double maxvalue = _joy.params[0];
			if(maxvalue == 0.0) {
				maxvalue = 32768.0;
			}
			if(_event.motion.x != 0) {
				amount = _event.motion.x - (m_width / 2.0);
				amount /= (m_width / 2.0);
			} else {
				amount = _event.motion.y - (m_height / 2.0);
				amount /= (m_height / 2.0);
			}
			m_joystick[_joy.which].speed[_joy.axis] = 0;
			m_joystick[_joy.which].value[_joy.axis] = amount * maxvalue;
			m_joystick[_joy.which].maxvalue[_joy.axis] = m_joystick[_joy.which].value[_joy.axis];
			break;
		}
		case SDL_JOYAXISMOTION: {
			double maxvalue = _joy.params[0];
			if(maxvalue == 0.0) {
				maxvalue = 32768.0;
			}
			m_joystick[_joy.which].speed[_joy.axis] = 0;
			m_joystick[_joy.which].value[_joy.axis] = maxvalue * (_event.jaxis.value / 32768.0);
			m_joystick[_joy.which].maxvalue[_joy.axis] = m_joystick[_joy.which].value[_joy.axis];
			break;
		}
		case SDL_KEYDOWN:
		case SDL_KEYUP:
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
		case SDL_JOYBUTTONDOWN:
		case SDL_JOYBUTTONUP: {
			int maxvalue = _joy.params[0];
			int speed = _joy.params[2]==0 ? 500 : _joy.params[2];
			if(_phase == EventPhase::EVT_END) {
				int curmax = m_joystick[_joy.which].maxvalue[_joy.axis];
				if(std::signbit(maxvalue) != std::signbit(curmax)) {
					PDEBUGF(LOG_V2, LOG_GUI, "  pevt: joystick %s axis %d: skip end\n",
							_joy.which?"B":"A", _joy.axis);
					return;
				}
				m_joystick[_joy.which].maxvalue[_joy.axis] = 0;
				if(m_joystick[_joy.which].value[_joy.axis] == 0) {
					PDEBUGF(LOG_V2, LOG_GUI, "  pevt: joystick %s axis %d: value already 0\n",
							_joy.which?"B":"A", _joy.axis);
					return;
				}
				switch(_joy.params[1]) {
					case 1: // constant speed
						if(m_joystick[_joy.which].value[_joy.axis] < 0) {
							m_joystick[_joy.which].speed[_joy.axis] = speed;
						} else {
							m_joystick[_joy.which].speed[_joy.axis] = -speed;
						}
						if(m_joystick[_joy.which].value[_joy.axis] != 0) {
							m_joystick[_joy.which].enable_timer();
						}
						return;
					case 0:
					default: // immediate
						m_joystick[_joy.which].speed[_joy.axis] = 0;
						m_joystick[_joy.which].value[_joy.axis] = 0;
						m_joystick[_joy.which].maxvalue[_joy.axis] = 0;
						break;
				}
			} else if(_phase == EventPhase::EVT_START) {
				switch(_joy.params[1]) {
					case 1: // constant speed
						m_joystick[_joy.which].maxvalue[_joy.axis] = maxvalue;
						if(maxvalue < 0) {
							m_joystick[_joy.which].speed[_joy.axis] = -speed;
						} else {
							m_joystick[_joy.which].speed[_joy.axis] = speed;
						}
						if(m_joystick[_joy.which].value[_joy.axis] != maxvalue) {
							m_joystick[_joy.which].enable_timer();
						}
						return;
					case 0:
					default: // immediate
						m_joystick[_joy.which].speed[_joy.axis] = 0;
						m_joystick[_joy.which].value[_joy.axis] = maxvalue;
						m_joystick[_joy.which].maxvalue[_joy.axis] = maxvalue;
						break;
				}
			}
			break;
		}
		default:
			break;
	}

	PDEBUGF(LOG_V2, LOG_GUI, "  pevt: joystick %s axis %d: %d\n",
			_joy.which?"B":"A", _joy.axis, m_joystick[_joy.which].value[_joy.axis]);

	m_machine->joystick_motion(_joy.which, _joy.axis, m_joystick[_joy.which].value[_joy.axis]);
}

void GUI::pevt_joy_button(const ProgramEvent::Joy &_joy, EventPhase _phase)
{
	int state = _phase == EventPhase::EVT_START;
	PDEBUGF(LOG_V2, LOG_GUI, "  pevt: joystick %s btn %d: %d\n", _joy.which?"B":"A", _joy.button, state);
	m_machine->joystick_button(_joy.which, _joy.button, state);
}

void GUI::run_event_functions(const Keymap::Binding *_binding, EventPhase _phase)
{
	for(auto & pevt : _binding->pevt) {
		if(pevt.type != ProgramEvent::Type::EVT_PROGRAM_FUNC) {
			continue;
		}
		PDEBUGF(LOG_V2, LOG_GUI, "function %d\n", ec_to_i(pevt.func.name));
		ms_event_funcs.at(pevt.func.name)(*this, pevt.func, _phase);
	}
}

void GUI::run_event_binding(InputSystem::Event &_event, EventPhase _phase, uint32_t _type_mask)
{
	if(!_event.binding.pevt.size()) {
		PDEBUGF(LOG_V2, LOG_GUI, "  nothing to do\n");
		return;
	}
	if(_phase == EventPhase::EVT_END) {
		_event.pevt_idx = 0;
	} else {
		// search for running events of the same group and stop them
		if(!_event.binding.group.empty()) {
			m_input.stop_group(_event);
		}
	}

	static uint64_t sd_last_event = 0;
	PDEBUGF(LOG_V2, LOG_GUI, "  %llu event exec: \"%s\", phase:%d\n",
		get_curtime_ms()-sd_last_event, _event.binding.name.c_str(), ec_to_i(_phase)
	);
	sd_last_event = get_curtime_ms();

	if(_event.pevt_idx >= _event.binding.pevt.size()) {
		PDEBUGF(LOG_V2, LOG_GUI, "  nothing to do\n");
		return;
	}

	while(_event.pevt_idx < _event.binding.pevt.size()) {
		auto pevt = &_event.binding.pevt[_event.pevt_idx];

		PDEBUGF(LOG_V2, LOG_GUI, "  evt[%d] = type:%d, \"%s\", ",
			_event.pevt_idx+1, ec_to_i(pevt->type), pevt->name.c_str()
		);

		if((ec_to_i(pevt->type) & _type_mask) || pevt->masked) {
			PDEBUGF(LOG_V2, LOG_GUI, "masked\n");
			_event.pevt_idx++;
			continue;
		}

		bool skipped = false;
		switch(pevt->type) {
			case ProgramEvent::Type::EVT_KEY:
				PDEBUGF(LOG_V2, LOG_GUI, "keyboard key\n");
				pevt_key(pevt->key, _phase);
				break;
			case ProgramEvent::Type::EVT_PROGRAM_FUNC:
				PDEBUGF(LOG_V2, LOG_GUI, "function %d\n", ec_to_i(pevt->func.name));
				ms_event_funcs.at(pevt->func.name)(*this, pevt->func, _phase);
				break;
			case ProgramEvent::Type::EVT_MOUSE_AXIS:
				PDEBUGF(LOG_V2, LOG_GUI, "mouse axis\n");
				pevt_mouse_axis(pevt->mouse, _event.sdl_evt, _phase, _event.binding.mode);
				break;
			case ProgramEvent::Type::EVT_MOUSE_BUTTON:
				PDEBUGF(LOG_V2, LOG_GUI, "mouse button\n");
				pevt_mouse_button(pevt->mouse, _phase);
				break;
			case ProgramEvent::Type::EVT_JOY_AXIS:
				PDEBUGF(LOG_V2, LOG_GUI, "joy axis\n");
				pevt_joy_axis(pevt->joy, _event.sdl_evt, _phase);
				break;
			case ProgramEvent::Type::EVT_JOY_BUTTON:
				PDEBUGF(LOG_V2, LOG_GUI, "joy button\n");
				pevt_joy_button(pevt->joy, _phase);
				break;
			case ProgramEvent::Type::EVT_COMMAND: {
				if(_phase == EventPhase::EVT_END) {
					skipped = true;
					break;
				}
				switch(pevt->command.name) {
					case ProgramEvent::CommandName::CMD_WAIT: {
						// set a timer and exit this function
						int ms = pevt->command.params[0];
						if(ms < 0) {
							Keyboard::Typematic tm;
							if(m_machine->is_on()) {
								tm = m_machine->devices().device<Keyboard>()->typematic();
							}
							// special cases
							switch(ms) {
								case ProgramEvent::Constant::CONST_TYPEMATIC_DELAY:
									ms = tm.delay_ms;
									break;
								case ProgramEvent::Constant::CONST_TYPEMATIC_RATE:
									ms = std::round(1000.0 / tm.cps);
									break;
								default:
									break;
							}
						}
						PDEBUGF(LOG_V2, LOG_GUI, "wait %d ms\n", ms);
						if(ms < 0) {
							ms = 0;
						}
						_event.pevt_idx++;
						_event.enable_timer(Uint32(ms));
						return;
					}
					case ProgramEvent::CommandName::CMD_RELEASE:
					{
						// user provided indices are 1-based
						unsigned idx = unsigned(pevt->command.params[0]);
						unsigned end_idx;
						if(idx == 0) {
							// release all
							end_idx = _event.binding.pevt.size() - 1;
						} else if(idx > _event.binding.pevt.size()) {
							PDEBUGF(LOG_V2, LOG_GUI, "invalid index\n");
							_event.pevt_idx++;
							continue;
						} else {
							idx--;
							end_idx = idx;
						}
						PDEBUGF(LOG_V2, LOG_GUI, "release %u-%u\n", idx, end_idx);
						for(; idx<=end_idx; idx++) {
							switch(_event.binding.pevt[idx].type) {
								case ProgramEvent::Type::EVT_KEY:
									pevt_key(_event.binding.pevt[idx].key, EventPhase::EVT_END);
									break;
								case ProgramEvent::Type::EVT_MOUSE_BUTTON:
									pevt_mouse_button(_event.binding.pevt[idx].mouse, EventPhase::EVT_END);
									break;
								case ProgramEvent::Type::EVT_JOY_BUTTON:
									pevt_joy_button(_event.binding.pevt[idx].joy, EventPhase::EVT_END);
									break;
								default:{
									//PDEBUGF(LOG_V2, LOG_GUI, "  not a button (%d=>%s)\n", idx, _binding.pevt[idx].name.c_str());
									break;
								}
							}
						}
						break;
					}
					case ProgramEvent::CommandName::CMD_SKIP_TO:
					{
						unsigned idx = unsigned(pevt->command.params[0]);
						if(idx > 0) {
							idx--;
						}
						if(idx < _event.binding.pevt.size()) {
							_event.pevt_idx = idx;
							if(!_event.binding.has_cmd_event(ProgramEvent::CommandName::CMD_WAIT, idx)) {
								PDEBUGF(LOG_V2, LOG_GUI, "repeating from %d (wait)\n", idx+1);
								_event.enable_timer(0);
								return;
							}
							PDEBUGF(LOG_V2, LOG_GUI, "repeat from %d\n", idx+1);
							continue;
						} else {
							PDEBUGF(LOG_V2, LOG_GUI, "repeat invalid index %d\n", idx+1);
						}
						break;
					}
					default:
					{
						PDEBUGF(LOG_V2, LOG_GUI, "command ignored\n");
						break;
					}
				}
				break;
			}
			default:
				PDEBUGF(LOG_V2, LOG_GUI, "unknown event!\n");
				break;
		}
		if(skipped) {
			PDEBUGF(LOG_V2, LOG_GUI, "skipped\n");
		}
		_event.pevt_idx++;
	}
}

GUI::InputSystem::Event::Event(Sint32 _code, const SDL_Event &_sdl_evt,
		const Keymap::Binding *_binding)
: code(_code)
{
	std::memcpy(&sdl_evt, &_sdl_evt, sizeof(SDL_Event));
	if(_binding) {
		binding = *_binding;
	}
	PDEBUGF(LOG_V2, LOG_GUI, "  Event created: %d\n", code);
}

GUI::InputSystem::Event::~Event()
{
	PDEBUGF(LOG_V2, LOG_GUI, "  Event destroyed: %d\n", code);
}

std::shared_ptr<GUI::InputSystem::Event> GUI::InputSystem::start_evt(
		const SDL_Event &_sdl_evt, const Keymap::Binding *_binding)
{
	auto running_evt = find(_sdl_evt);
	if(running_evt) {
		remove(running_evt);
	}
	running_evt = std::make_shared<InputSystem::Event>(evt_count, _sdl_evt, _binding);
	events[evt_count] = running_evt;
	evt_count++;
	return running_evt;
}

std::shared_ptr<GUI::InputSystem::Event> GUI::InputSystem::find(SDL_Event _sdl_evt)
{
	for(auto [k, evt] : events) {
		switch(evt->sdl_evt.type) {
			case SDL_KEYDOWN:
				if((_sdl_evt.type == SDL_KEYDOWN || _sdl_evt.type == SDL_KEYUP) &&
					(evt->sdl_evt.key.keysym.sym == _sdl_evt.key.keysym.sym))
				{
					return evt;
				}
				break;
			case SDL_MOUSEMOTION:
				if(_sdl_evt.type == SDL_MOUSEMOTION) {
					if((evt->sdl_evt.motion.xrel && _sdl_evt.motion.xrel) ||
					   (evt->sdl_evt.motion.yrel && _sdl_evt.motion.yrel))
					{
						return evt;
					}
				}
				break;
			case SDL_MOUSEBUTTONDOWN:
				if((_sdl_evt.type == SDL_MOUSEBUTTONDOWN || _sdl_evt.type == SDL_MOUSEBUTTONUP) &&
				   (evt->sdl_evt.button.button == _sdl_evt.button.button))
				{
					return evt;
				}
				break;
			case SDL_JOYAXISMOTION:
				if(_sdl_evt.type == SDL_JOYAXISMOTION) {
					if(evt->sdl_evt.jaxis.which == _sdl_evt.jaxis.which && 
					   evt->sdl_evt.jaxis.axis == _sdl_evt.jaxis.axis)
					{
						return evt;
					}
				}
				break;
			case SDL_JOYBUTTONDOWN:
				if(_sdl_evt.type == SDL_JOYBUTTONDOWN || _sdl_evt.type == SDL_JOYBUTTONUP) {
					if(evt->sdl_evt.jbutton.which == _sdl_evt.jbutton.which && 
					   evt->sdl_evt.jbutton.button == _sdl_evt.jbutton.button)
					{
						return evt;
					}
				}
				break;
			default:
				break;
		}
	}
	return nullptr;
}

std::vector<std::shared_ptr<GUI::InputSystem::Event>> GUI::InputSystem::find_mods(unsigned _sdl_mod)
{
	std::vector<std::shared_ptr<GUI::InputSystem::Event>> running_evts;
	for(auto [k, evt] : events) {
		if(evt->sdl_evt.type != SDL_KEYDOWN) {
			continue;
		}
		if((_sdl_mod & KMOD_LSHIFT) && evt->sdl_evt.key.keysym.sym == SDLK_LSHIFT) {
			running_evts.push_back(evt);
		}
		if((_sdl_mod & KMOD_RSHIFT) && evt->sdl_evt.key.keysym.sym == SDLK_RSHIFT) {
			running_evts.push_back(evt);
		}
		if((_sdl_mod & KMOD_LALT) && evt->sdl_evt.key.keysym.sym == SDLK_LALT) {
			running_evts.push_back(evt);
		}
		if((_sdl_mod & KMOD_RALT) && evt->sdl_evt.key.keysym.sym == SDLK_RALT) {
			running_evts.push_back(evt);
		}
		if((_sdl_mod & KMOD_LCTRL) && evt->sdl_evt.key.keysym.sym == SDLK_LCTRL) {
			running_evts.push_back(evt);
		}
		if((_sdl_mod & KMOD_RCTRL) && evt->sdl_evt.key.keysym.sym == SDLK_RCTRL) {
			running_evts.push_back(evt);
		}
		if((_sdl_mod & KMOD_LGUI) && evt->sdl_evt.key.keysym.sym == SDLK_LGUI) {
			running_evts.push_back(evt);
		}
		if((_sdl_mod & KMOD_RGUI) && evt->sdl_evt.key.keysym.sym == SDLK_RGUI) {
			running_evts.push_back(evt);
		}
	}
	return running_evts;
}

void GUI::InputSystem::stop_group(InputSystem::Event &_event)
{
	for(auto it = events.begin(); it != events.end(); ) {
		if(it->second->binding.group == _event.binding.group &&
		   it->second->code != _event.code
		) {
			it->second->disable_timer();
			if(it->second->binding.mode ==  Keymap::Binding::Mode::LATCHED) {
				gui->run_event_binding(*(it->second), EventPhase::EVT_END);
				it = events.erase(it);
			} else {
				it++;
			}
		} else {
			it++;
		}
	}
}

std::shared_ptr<GUI::InputSystem::Event> GUI::InputSystem::find(Sint32 _code)
{
	auto it = events.find(_code);
	if(it != events.end()) {
		return it->second;
	}
	return nullptr;
}

bool GUI::InputSystem::is_active(std::shared_ptr<GUI::InputSystem::Event> _evt)
{
	auto it = events.find(_evt->code);
	return(it != events.end());
}

void GUI::InputSystem::remove(std::shared_ptr<Event> _evt)
{
	if(_evt) {
		_evt->disable_timer();
		if(is_active(_evt)) {
			events.erase(_evt->code);
			PDEBUGF(LOG_V2, LOG_GUI, "running events: %u\n", static_cast<unsigned>(events.size()));
		}
	}
}

void GUI::InputSystem::reset()
{
	for(auto & evt : events) {
		evt.second->disable_timer();
	}
	events.clear();
}

void GUI::on_keyboard_event(const SDL_Event &_sdl_event)
{
	bool gui_input = !(m_input.grab || !m_windows.need_input());

	EventPhase phase;
	if(_sdl_event.type == SDL_KEYDOWN) {
		if(_sdl_event.key.repeat) {
			if(!gui_input) {
				// repeats go to the GUI only
				return;
			}
			phase = EventPhase::EVT_REPEAT;
		} else {
			phase = EventPhase::EVT_START;
		}
	} else if(_sdl_event.type == SDL_KEYUP) {
		phase = EventPhase::EVT_END;
	} else  {
		assert(false);
		return;
	}

	auto binding_ptr = m_keymaps[m_current_keymap].find_sdl_binding(_sdl_event.key);
	auto running_evt = m_input.find(_sdl_event);

	std::string mod1 = bitfield_to_string(_sdl_event.key.keysym.mod & 0xff,
	{ "KMOD_LSHIFT", "KMOD_RSHIFT", "", "", "", "", "KMOD_LCTRL", "KMOD_RCTRL" });

	std::string mod2 = bitfield_to_string((_sdl_event.key.keysym.mod >> 8) & 0xff,
	{ "KMOD_LALT", "KMOD_RALT", "KMOD_LGUI", "KMOD_RGUI", "KMOD_NUM", "KMOD_CAPS", "KMOD_MODE", "KMOD_RESERVED" });
	if(!mod1.empty()) {
		mod2 = " " + mod2;
	}

	PDEBUGF(LOG_V2, LOG_GUI, "SDL Key: evt=%s, sym=%s, code=%s, mod=[%s%s]\n",
			(_sdl_event.type == SDL_KEYDOWN)?"SDL_KEYDOWN":"SDL_KEYUP",
			Keymap::ms_sdl_keycode_str_table.find(_sdl_event.key.keysym.sym)->second.c_str(),
			Keymap::ms_sdl_scancode_str_table.find(_sdl_event.key.keysym.scancode)->second.c_str(),
			mod1.c_str(), mod2.c_str());

	// events are not created when gui is active, if one is present skip this
	if(!running_evt && gui_input) {
		// do gui stuff
		if(binding_ptr && !m_windows.current_doc()->IsModal()) {
			// but first run any FUNC_*
			run_event_functions(binding_ptr, phase);
		}
		dispatch_rml_event(_sdl_event);
		return;
	}

	// keyboard events need to account for the special case of key combos triggered by key combos,
	// where a key combo is modifier + key

	if(binding_ptr) {
		PDEBUGF(LOG_V2, LOG_GUI, "  match: \"%s\"\n", binding_ptr->name.c_str());
		if((running_evt && running_evt->binding.mode == Keymap::Binding::Mode::LATCHED) || 
		   (!running_evt && binding_ptr->mode == Keymap::Binding::Mode::LATCHED)
		) {
			if(phase != EventPhase::EVT_START) {
				return;
			}
			if(!running_evt) {
				running_evt = m_input.start_evt(_sdl_event, binding_ptr);
				if(binding_ptr->is_ievt_keycombo()) {
					// this is true only for the main combo key.
					// terminate any events belonging to modifiers
					PDEBUGF(LOG_V2, LOG_GUI, "  input combo: %s\n", binding_ptr->ievt.name.c_str());
					auto modifier_evts = m_input.find_mods(binding_ptr->ievt.key.mod);
					for(auto & modifier_evt : modifier_evts) {
						run_event_binding(*modifier_evt, EventPhase::EVT_END);
						m_input.remove(modifier_evt);
					}
				}
				run_event_binding(*running_evt, EventPhase::EVT_START);
			} else {
				run_event_binding(*running_evt, EventPhase::EVT_END);
				m_input.remove(running_evt);
			}
			return;
		}
	} else {
		PDEBUGF(LOG_V2, LOG_GUI, "  no match\n");
	}

	// DEFAULT and ONE_SHOT modes

	bool finish = (binding_ptr && binding_ptr->mode == Keymap::Binding::Mode::ONE_SHOT);
	if(phase == EventPhase::EVT_START) {
		if(binding_ptr && binding_ptr->is_ievt_keycombo()) {
			// this is true only for the main combo key.
			if(!running_evt) {
				running_evt = m_input.start_evt(_sdl_event, binding_ptr);
				// we need to neutralize any key modifiers sent to the machine by the
				// binding of the key modifiers of this input combo event (re-read if unclear).
				// find the starting combo key(s), which are always modifiers
				auto modifier_evts = m_input.find_mods(binding_ptr->ievt.key.mod);
				PDEBUGF(LOG_V2, LOG_GUI, "  input combo: %s\n", binding_ptr->ievt.name.c_str());
				for(auto & modifier_evt : modifier_evts) {
					// 1. link this event
					if(binding_ptr->mode != Keymap::Binding::Mode::ONE_SHOT) {
						modifier_evt->link_to(running_evt);
						running_evt->linked = true;
					}
					// 2. mask any key modifier for the machine, so they won't be sent anymore by timed commands
					modifier_evt->binding.mask_pevt_kmods();
					// 3. release any key modifier sent to the machine by this binding
					for(auto &pevt : modifier_evt->binding.pevt) {
						if(pevt.is_key_modifier() && m_key_state[pevt.key]) {
							PDEBUGF(LOG_V2, LOG_GUI, "  releasing mod %s\n",
									Keymap::ms_keycode_str_table.at(pevt.key).c_str());
							send_key_to_machine(pevt.key, KEY_RELEASED);
						}
					}
					modifier_evt->binding.mode = Keymap::Binding::Mode::DEFAULT;
				}
			} else {
				running_evt->restart();
			}
		} else {
			running_evt = m_input.start_evt(_sdl_event, binding_ptr);
		}
	} else if(phase == EventPhase::EVT_END) {
		if(binding_ptr && binding_ptr->mode == Keymap::Binding::Mode::ONE_SHOT) {
			return;
		}
		if(!running_evt) {
			PDEBUGF(LOG_V2, LOG_GUI, "  no running event found\n");
			return;
		}
		if(running_evt->binding.is_ievt_keycombo() && running_evt->binding.is_pevt_keycombo()) {
			// this is true only for the main combo key.
			// release everything on the guest except the modifiers.
			if(running_evt->linked) {
				running_evt->binding.mask_pevt_kmods();
			}
			running_evt->disable_timer();
		} else {
			finish = true;
		}
	} else {
		return;
	}

	run_event_binding(*running_evt, phase);

	if(finish) {
		if(running_evt->binding.mode == Keymap::Binding::Mode::ONE_SHOT) {
			if(running_evt->is_running()) {
				return;
			}
			run_event_binding(*running_evt, EventPhase::EVT_END);
		}
		if(running_evt->combo_link && m_input.is_active(running_evt->combo_link)) {
			// release combo modifiers
			PDEBUGF(LOG_V2, LOG_GUI, "  output combo finish\n");
			running_evt->combo_link->binding.mask_pevt_kmods(false);
			run_event_binding(*running_evt->combo_link, EventPhase::EVT_END);
			m_input.remove(running_evt->combo_link);
		}
		m_input.remove(running_evt);
	}
}

void GUI::on_mouse_motion_event(const SDL_Event &_sdl_event)
{
	PDEBUGF(LOG_V2, LOG_GUI, "SDL Mouse motion: x:%d,y:%d\n", _sdl_event.motion.xrel, _sdl_event.motion.yrel);

	if(!m_input.grab) {
		dispatch_rml_event(_sdl_event);
		return;
	}

	// mouse motion events are 2-in-1 (X and Y axes)

	auto on_mouse_axis_event = [&](const char *_axis_name, SDL_Event _axis_evt) {
		auto binding = m_keymaps[m_current_keymap].find_sdl_binding(_axis_evt.motion);
		if(binding) {
			// events are ONE SHOT only because mouse motion is relative and there's no END condition
			PDEBUGF(LOG_V2, LOG_GUI, "  match for axis %s: %s\n", _axis_name, binding->name.c_str());
			auto running_evt = m_input.start_evt(_axis_evt, binding);
			run_event_binding(*running_evt, EventPhase::EVT_START);
			if(!running_evt->is_running()) {
				run_event_binding(*running_evt, EventPhase::EVT_END);
				m_input.remove(running_evt);
			}
		} else {
			PDEBUGF(LOG_V2, LOG_GUI, "  no match for axis %s\n", _axis_name);
		}
	};

	SDL_Event axis_event;
	if(_sdl_event.motion.xrel) {
		memcpy(&axis_event, &_sdl_event, sizeof(SDL_Event));
		//axis_event.motion.x *= m_scaling_factor;
		//axis_event.motion.xrel *= m_scaling_factor;
		axis_event.motion.y = 0;
		axis_event.motion.yrel = 0;
		on_mouse_axis_event("X", axis_event);
	}
	if(_sdl_event.motion.yrel) {
		memcpy(&axis_event, &_sdl_event, sizeof(SDL_Event));
		//axis_event.motion.y *= m_scaling_factor;
		//axis_event.motion.yrel *= m_scaling_factor;
		axis_event.motion.x = 0;
		axis_event.motion.xrel = 0;
		on_mouse_axis_event("Y", axis_event);
	}
}

void GUI::on_mouse_button_event(const SDL_Event &_sdl_event)
{
	EventPhase phase;
	if(_sdl_event.type == SDL_MOUSEBUTTONDOWN) {
		phase = EventPhase::EVT_START;
	} else if(_sdl_event.type == SDL_MOUSEBUTTONUP) {
		phase = EventPhase::EVT_END;
	} else  {
		assert(false);
		return;
	}

	auto binding_ptr = m_keymaps[m_current_keymap].find_sdl_binding(_sdl_event.button);
	auto running_evt = m_input.find(_sdl_event);

	PDEBUGF(LOG_V2, LOG_GUI, "SDL Mouse button: %d %s\n", _sdl_event.button.button,
			_sdl_event.type == SDL_MOUSEBUTTONDOWN ? "down" : "up");

	if(!running_evt && !m_input.grab) {
		// do gui stuff
		if(binding_ptr && !m_windows.current_doc()->IsModal()) {
			// but first run any FUNC_*
			run_event_functions(binding_ptr, phase);
		}
		dispatch_rml_event(_sdl_event);
		return;
	}

	if(binding_ptr) {
		PDEBUGF(LOG_V2, LOG_GUI, "  match: %s\n", binding_ptr->name.c_str());
	} else {
		PDEBUGF(LOG_V2, LOG_GUI, "  no match\n");
		return;
	}

	on_button_event(_sdl_event, binding_ptr, phase);
}

void GUI::on_mouse_wheel_event(const SDL_Event &_sdl_event)
{
	// TODO mouse wheel events are not mappable and used by GUI only
	if(m_input.grab) {
		return;
	}

	// TODO GUI scaling is hardcoded to CTRL + wheel up/down
	SDL_Keymod mod = SDL_GetModState();
	if(mod & KMOD_CTRL) {
		if(_sdl_event.wheel.y > 0) {
			// scroll up
			if(m_scaling_factor < 5.0) {
				m_scaling_factor += 0.25;
			}
		} else if(_sdl_event.wheel.y < 0) {
			// scroll down
			if(m_scaling_factor > 1.0) {
				m_scaling_factor -= 0.25;
			}
		}
		m_rml_context->SetDensityIndependentPixelRatio(m_scaling_factor);
		m_windows.interface->container_size_changed(m_width, m_height);
		show_message(str_format("UI scaling: %.0f%%", m_scaling_factor*100.0));
	} else {
		dispatch_rml_event(_sdl_event);
	}
}

void GUI::on_joystick_motion_event(const SDL_Event &_sdl_evt)
{
	// joystick events are for the machine only
	// TODO add support for GUI navigation with gamepads

	PDEBUGF(LOG_V2, LOG_GUI, "SDL Joystick motion: joy:%d, axis:%d, value:%d\n",
			_sdl_evt.jaxis.which, _sdl_evt.jaxis.axis, _sdl_evt.jaxis.value);

	assert(_sdl_evt.jaxis.which < Sint32(m_SDL_joysticks.size()));

	int jid = JOY_NONE;
	if(m_joystick[0].sdl_id == _sdl_evt.jaxis.which) {
		jid = 0;
	} else if(m_joystick[1].sdl_id == _sdl_evt.jaxis.which) {
		jid = 1;
	} else {
		return;
	}

	EventPhase phase;
	if(_sdl_evt.jaxis.value == 0) {
		phase = EventPhase::EVT_END;
	} else {
		phase = EventPhase::EVT_START;
	}

	auto binding_ptr = m_keymaps[m_current_keymap].find_sdl_binding(jid, _sdl_evt.jaxis);

	if(!binding_ptr) {
		PDEBUGF(LOG_V2, LOG_GUI, "  no match\n");
		return;
	}
	PDEBUGF(LOG_V2, LOG_GUI, "  match: %s\n", binding_ptr->name.c_str());

	std::shared_ptr<GUI::InputSystem::Event> running_evt;
	switch(binding_ptr->mode) {
		case Keymap::Binding::Mode::ONE_SHOT:
			PDEBUGF(LOG_V2, LOG_GUI, "  1shot mode\n");
			running_evt = m_input.find(_sdl_evt);
			if(running_evt) {
				if(phase == EventPhase::EVT_END) {
					if(!running_evt->is_running()) {
						m_input.remove(running_evt);
					} else {
						running_evt->remove = true;
					}
				}
				// start, old event
				// do nothing
			} else {
				if(phase == EventPhase::EVT_END) {
					PDEBUGF(LOG_V2, LOG_GUI, "  event end without a start\n");
					return;
				}
				// start, new event
				running_evt = m_input.start_evt(_sdl_evt, binding_ptr);
				run_event_binding(*running_evt, EventPhase::EVT_START);
				if(running_evt->is_running()) {
					// event will be removed at the end
					running_evt->remove = false;
				} else {
					run_event_binding(*running_evt, EventPhase::EVT_END);
					// dont remove now, it will at the end or when timer stops
				}
			}
			break;
		case Keymap::Binding::Mode::LATCHED:
			running_evt = m_input.find(_sdl_evt);
			if(running_evt) {
				if(phase == EventPhase::EVT_END) {
					running_evt->remove = true;
					return;
				}
				// start
				if(running_evt->remove) {
					run_event_binding(*running_evt, EventPhase::EVT_END);
					m_input.remove(running_evt);
				}
			} else {
				if(phase == EventPhase::EVT_END) {
					PDEBUGF(LOG_V2, LOG_GUI, "  event end without a start\n");
					return;
				}
				// start
				running_evt = m_input.start_evt(_sdl_evt, binding_ptr);
				run_event_binding(*running_evt, EventPhase::EVT_START);
				running_evt->remove = false;
			}
			break;
		default:
			if(phase == EventPhase::EVT_START) {
				running_evt = m_input.start_evt(_sdl_evt, binding_ptr);
			} else if(phase == EventPhase::EVT_END) {
				running_evt = m_input.find(_sdl_evt);
				if(!running_evt) {
					PDEBUGF(LOG_V2, LOG_GUI, "  no running event found\n");
					return;
				}
				running_evt->sdl_evt.jaxis.value = _sdl_evt.jaxis.value;
			}
			run_event_binding(*running_evt, phase);
			if(phase == EventPhase::EVT_END) {
				m_input.remove(running_evt);
			}
		break;
	}
}

void GUI::on_joystick_button_event(const SDL_Event &_sdl_event)
{
	// joystick events are for the machine only
	// TODO add support for GUI navigation with gamepads

	PDEBUGF(LOG_V2, LOG_GUI, "SDL Joystick button: joy:%d, button:%d, state:%d\n",
			_sdl_event.jbutton.which, _sdl_event.jbutton.button, _sdl_event.jbutton.state);

	assert(_sdl_event.jbutton.which < Sint32(m_SDL_joysticks.size()));

	int jid = JOY_NONE;
	if(m_joystick[0].sdl_id == _sdl_event.jbutton.which) {
		jid = 0;
	} else if(m_joystick[1].sdl_id == _sdl_event.jbutton.which) {
		jid = 1;
	} else {
		return;
	}

	auto binding_ptr = m_keymaps[m_current_keymap].find_sdl_binding(jid, _sdl_event.jbutton);

	if(binding_ptr) {
		PDEBUGF(LOG_V2, LOG_GUI, "  match: %s\n", binding_ptr->name.c_str());
	} else {
		PDEBUGF(LOG_V2, LOG_GUI, "  no match\n");
		return;
	}

	EventPhase phase;
	if(_sdl_event.type == SDL_JOYBUTTONDOWN) {
		phase = EventPhase::EVT_START;
	} else if(_sdl_event.type == SDL_JOYBUTTONUP) {
		phase = EventPhase::EVT_END;
		if(binding_ptr->mode == Keymap::Binding::Mode::ONE_SHOT) {
			return;
		}
	} else  {
		assert(false);
		return;
	}

	on_button_event(_sdl_event, binding_ptr, phase);
}

void GUI::on_button_event(const SDL_Event &_sdl_event, const Keymap::Binding *_binding_ptr, EventPhase _phase)
{
	if(!_binding_ptr) {
		return;
	}

	std::shared_ptr<InputSystem::Event> running_evt;

	switch(_binding_ptr->mode) {
		case Keymap::Binding::Mode::ONE_SHOT:
			if(_phase == EventPhase::EVT_START) {
				running_evt = m_input.start_evt(_sdl_event, _binding_ptr);
				run_event_binding(*running_evt, _phase);
				if(!running_evt->is_running()) {
					run_event_binding(*running_evt, EventPhase::EVT_END);
					m_input.remove(running_evt);
				}
			}
			break;
		case Keymap::Binding::Mode::LATCHED:
			if(_phase == EventPhase::EVT_START) {
				running_evt = m_input.find(_sdl_event);
				if(running_evt) {
					run_event_binding(*running_evt, EventPhase::EVT_END);
					m_input.remove(running_evt);
				} else {
					running_evt = m_input.start_evt(_sdl_event, _binding_ptr);
					run_event_binding(*running_evt, EventPhase::EVT_START);
				}
			}
			break;
		default:
			if(_phase == EventPhase::EVT_START) {
				running_evt = m_input.start_evt(_sdl_event, _binding_ptr);
				run_event_binding(*running_evt, _phase);
			} else {
				running_evt = m_input.find(_sdl_event);
				if(!running_evt) {
					// this happens when events are run with GUI input active
					PDEBUGF(LOG_V2, LOG_GUI, "  no running event\n");
					return;
				}
				run_event_binding(*running_evt, _phase);
				m_input.remove(running_evt);
			}
			break;
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
			if(m_joystick[0].sdl_id == JOY_NONE) {
				m_joystick[0].sdl_id = jinstance;
				jid = 0;
			} else if(m_joystick[1].sdl_id == JOY_NONE) {
				m_joystick[1].sdl_id = jinstance;
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
		if(m_joystick[0].sdl_id == _event.jdevice.which) {
			PINFOF(LOG_V1, LOG_GUI, "Joystick A has been removed\n");
			m_joystick[0].sdl_id = m_joystick[1].sdl_id;
			m_joystick[1].sdl_id = JOY_NONE;
			notify_user = (m_joystick[0].sdl_id != JOY_NONE);
		} else if(m_joystick[1].sdl_id == _event.jdevice.which) {
			PINFOF(LOG_V1, LOG_GUI, "Joystick B has been removed\n");
			m_joystick[1].sdl_id = JOY_NONE;
		}
		if(m_joystick[1].sdl_id==JOY_NONE && m_joystick[0].sdl_id!=JOY_NONE && SDL_NumJoysticks()>1) {
			for(int j=0; j<int(m_SDL_joysticks.size()); j++) {
				if(m_SDL_joysticks[j]!=nullptr && j!=m_joystick[0].sdl_id) {
					m_joystick[1].sdl_id = j;
					notify_user = true;
				}
			}
		}
		if(notify_user && m_joystick[0].sdl_id != JOY_NONE) {
			print_joy_message(m_SDL_joysticks[m_joystick[0].sdl_id], 0, true);
		}
		if(notify_user && m_joystick[1].sdl_id != JOY_NONE) {
			print_joy_message(m_SDL_joysticks[m_joystick[1].sdl_id], 1, true);
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
		case SDL_MOUSEWHEEL:
			on_mouse_wheel_event(_event);
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
		default:
			dispatch_user_event(_event.user);
			break;
	}
}

void GUI::update_window_size(int _w, int _h)
{
	// the caller should acquire a lock on ms_rml_mutex
	m_width = _w;
	m_height = _h;
	m_rml_context->SetDimensions(Rml::Vector2i(m_width,m_height));
	m_rml_renderer->SetDimensions(m_width, m_height);
	m_windows.update_window_size(m_width, m_height);
}

void GUI::dispatch_window_event(const SDL_WindowEvent &_event)
{
	switch(_event.event) {
		case SDL_WINDOWEVENT_SIZE_CHANGED: {
			{
			std::lock_guard<std::mutex> lock(ms_rml_mutex);
			update_window_size(_event.data1, _event.data2);
			}
			PDEBUGF(LOG_V1, LOG_GUI, "Window resized to %ux%u\n", _event.data1, _event.data2);
			if(m_fullscreen_toggled) {
				m_fullscreen_toggled = false;
				if(!m_input.grab) {
					int x = m_width / 2, y = m_height / 2;
					// double warp so that RmlUi can always receive a mouse event
					SDL_WarpMouseInWindow(m_SDL_window, x, y);
					SDL_WarpMouseInWindow(m_SDL_window, x+1, y);
				}
			}
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

void GUI::dispatch_user_event(const SDL_UserEvent &_event)
{
	if(_event.type == ms_sdl_user_evt_id + GUI::TimedEvents::GUI_TEVT_MOUSE) {
		if(m_mouse.events_timer) {
			m_mouse.update(10.0);
			// mouse state is sent to the machine once per frame in GUI::update()
		}
	} else if(_event.type == ms_sdl_user_evt_id + GUI::TimedEvents::GUI_TEVT_JOYSTICK) {
		if(m_joystick[_event.code].events_timer) {
			m_joystick[_event.code].update(10.0);
			m_joystick[_event.code].send(m_machine);
		}
	} else if(_event.type == ms_sdl_user_evt_id + GUI::TimedEvents::GUI_TEVT_PEVT) {
		auto running_event = m_input.find(_event.code);
		if(running_event) {
			PDEBUGF(LOG_V2, LOG_GUI, "Timed event\n");
			run_event_binding(*running_event, EventPhase::EVT_START);
			if(!running_event->is_running() && running_event->binding.mode == Keymap::Binding::Mode::ONE_SHOT) {
				run_event_binding(*running_event, EventPhase::EVT_END);
				if(running_event->remove) {
					m_input.remove(running_event);
				}
			}
		}
	}
}

void GUI::rmlui_mouse_hack()
{
	// TODO this is a hack to solve a RmlUi issue where it won't
	// update the element below the pointer
	// https://github.com/mikke89/RmlUi/issues/220
	int x, y;
	SDL_GetMouseState(&x, &y);
	m_rml_context->ProcessMouseMove(x, y, m_rml_sys_interface->GetKeyModifiers());
}

void GUI::dispatch_rml_event(const SDL_Event &event)
{
	std::lock_guard<std::mutex> lock(ms_rml_mutex);

	int rmlmod = m_rml_sys_interface->GetKeyModifiers();

	switch(event.type)
	{
	case SDL_MOUSEMOTION:
		m_rml_context->ProcessMouseMove(
			event.motion.x, event.motion.y,
			rmlmod
			);
		break;

	case SDL_MOUSEBUTTONDOWN:
		m_rml_context->ProcessMouseButtonDown(
			m_rml_sys_interface->TranslateMouseButton(event.button.button),
			rmlmod
			);
		break;

	case SDL_MOUSEBUTTONUP: {
		m_rml_context->ProcessMouseButtonUp(
			m_rml_sys_interface->TranslateMouseButton(event.button.button),
			rmlmod
			);
		rmlui_mouse_hack();
		break;
	}
	case SDL_MOUSEWHEEL: {
		m_rml_context->ProcessMouseWheel(
			-event.wheel.y,
			rmlmod
			);
		rmlui_mouse_hack();
		break;
	}
	case SDL_KEYDOWN: {
		Rml::Input::KeyIdentifier key =
			m_rml_sys_interface->TranslateKey(event.key.keysym.sym);
		if(key != Rml::Input::KI_UNKNOWN) {
			m_rml_context->ProcessKeyDown(key,rmlmod);
		}
		if(!(rmlmod & Rml::Input::KM_CTRL)) {
			char c = RmlSystemInterface::GetCharacterCode(key, rmlmod);
			if(c > 0) {
				m_rml_context->ProcessTextInput(c);
			}
		}
		break;
	}
	case SDL_KEYUP:
		m_rml_context->ProcessKeyUp(
			m_rml_sys_interface->TranslateKey(event.key.keysym.sym),
			rmlmod
			);
		break;
	default:
		break;
	}
}

void GUI::update(uint64_t _current_time)
{
	GUI_fun_t fn;
	while(m_cmd_queue.try_and_pop(fn)) {
		fn();
	}

	m_mouse.send(m_machine);

	m_windows.update(_current_time);

	/* during a RmlUi Context update no other thread can call any windows
	 * related functions
	 * TODO Interface messages are outside event handlers and SysLog doesn't
	 * write messages on the interface directly anymore, so this mutex is
	 * useless now because no other thread is accessing RmlUi's context.
	 * Consider removing it?
	 */
	ms_rml_mutex.lock();
	m_rml_context->Update();
	ms_rml_mutex.unlock();

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

	m_windows.update_after();
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
	if(m_capture) {
		m_capture->cmd_quit();
		m_capture_thread.join();
		PDEBUGF(LOG_V1, LOG_GUI, "Capture thread stopped\n");
	}

	PDEBUGF(LOG_V1, LOG_GUI, "Shutting down the video subsystem\n");

	m_mouse.disable_timer();

	m_windows.shutdown();

	ms_rml_mutex.lock();
	Rml::Shutdown();
	ms_rml_mutex.unlock();

	shutdown_SDL();
}

std::list<std::string> GUI::load_shader_file(const std::string &_path)
{
	std::string path = g_program.config().find_file(_path);
	if(path.empty() || !FileSys::file_exists(path.c_str())) {
		throw std::runtime_error("file not found");
	}
	std::list<std::string> shdata;
	std::ifstream shstream = FileSys::make_ifstream(path.c_str());
	if(shstream.is_open()){
		std::string line = "";
		while(std::getline(shstream, line)) {
			// trimming will force '#' to first place
			line = str_trim(line);
			// don't skip empty lines (for shader debugging)
			shdata.emplace_back(line + "\n");
		}
		shstream.close();
	} else {
		throw std::runtime_error("unable to open file for reading");
	}
	return shdata;
}

GUI * GUI::instance()
{
	return g_program.gui_instance();
}

SDL_Surface * GUI::load_surface(const std::string &_name)
{
	if(_name == "gui:printer_preview") {
		if(!m_windows.printer_ctrl) {
			throw std::runtime_error("Printer not present");
		}
		return m_windows.printer_ctrl->get_preview_surface();
	}
	throw std::runtime_error(str_format("Invalid internal surface name: %s\n", _name.c_str()));
}

void GUI::update_surface(const std::string &_name, SDL_Surface *_data)
{
	assert(_data);
	auto texture = m_rml_renderer->GetNamedTexture(_name);
	if(texture) {
		try {
			this->update_texture(texture, _data);
		} catch(std::exception &e) {
			PDEBUGF(LOG_V0, LOG_GUI, "Error updating texture data: %s\n", e.what());
		}
	}
}

void GUI::save_framebuffer(std::string _screenfile, std::string _palfile)
{
	m_windows.interface->save_framebuffer(_screenfile, _palfile);
}

SDL_Surface * GUI::copy_framebuffer()
{
	return m_windows.interface->copy_framebuffer();
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

void GUI::show_message(std::string _mex)
{
	m_cmd_queue.push([=]() {
		m_windows.last_ifc_mex = _mex;
	});
}

void GUI::show_dbg_message(std::string _mex)
{
	m_cmd_queue.push([=]() {
		m_windows.last_dbg_mex = _mex;
	});
}

void GUI::show_message_box(const std::string &_title, const std::string &_message,
		MessageWnd::Type _type, 
		std::function<void()> _on_action1, std::function<void()> _on_action2)
{
	m_cmd_queue.push([=]() {
		if(m_windows.message_wnd) {
			m_windows.message_wnd->close();
		}
		m_windows.message_wnd = std::make_unique<MessageWnd>(this);
		m_windows.message_wnd->create();
		m_windows.message_wnd->set_modal(true);
		m_windows.message_wnd->set_title(_title);
		m_windows.message_wnd->set_message(_message);
		m_windows.message_wnd->set_type(_type);
		m_windows.message_wnd->set_callbacks(_on_action1, _on_action2);
		m_windows.message_wnd->show();
	});
}

void GUI::show_error_message_box(const std::string &_message)
{
	show_message_box("Error", _message, MessageWnd::Type::MSGW_OK, nullptr, nullptr);
}

bool GUI::are_windows_visible()
{
	return m_windows.are_visible();
}

void GUI::show_options_window()
{
	m_windows.show_options();
}

void GUI::toggle_mixer_control()
{
	m_windows.toggle_mixer();
}

void GUI::toggle_dbg_windows()
{
	m_windows.toggle_dbg();
}

void GUI::toggle_printer_control()
{
	m_windows.toggle_printer();
}

bool GUI::is_video_recording() const
{
	return m_capture->is_recording();
}

bool GUI::is_audio_recording() const
{
	return m_mixer->is_recording();
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
	constexpr int height = 23;
	for(int i=1; i<=height; i++) {
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
	evt.func.name = ProgramEvent::FuncName::FUNC_GUI_MODE_ACTION;
	bindings = m_keymaps[m_current_keymap].find_prg_bindings(evt);
	if(!bindings.empty()) {
		if(m_mode == GUI_MODE_REALISTIC) {
			evt.func.params[0] = 1;
			bindings = m_keymaps[m_current_keymap].find_prg_bindings(evt, true);
			if(!bindings.empty()) {
				ps("\nTo zoom in on the monitor press ", 0xf, bg, bd);
				ps(bindings[0]->ievt.name.c_str(), 0xe, bg, bd);
			}
			evt.func.params[0] = 2;
			bindings = m_keymaps[m_current_keymap].find_prg_bindings(evt, true);
			if(!bindings.empty()) {
				ps("\nTo switch between the interface styles press ", 0xf, bg, bd);
				ps(bindings[0]->ievt.name.c_str(), 0xe, bg, bd);
			}
			ps("\n", 0xf, bg, bd);
		} else {
			cy++;
		}
	} else {
		cy++;
	}

	evt.func.name = ProgramEvent::FuncName::FUNC_TOGGLE_POWER;
	bindings = m_keymaps[m_current_keymap].find_prg_bindings(evt);
	if(!bindings.empty()) {
		ps("To start/stop the machine press ", 0xf, bg, bd);
		ps(bindings[0]->ievt.name.c_str(), 0xe, bg, bd);
		ps("\n", 0xe, bg, bd);
	}
	
	evt.func.name = ProgramEvent::FuncName::FUNC_GRAB_MOUSE;
	bindings = m_keymaps[m_current_keymap].find_prg_bindings(evt);
	if(!bindings.empty()) {
		ps("To grab the mouse press ", 0xf, bg, bd);
		ps(bindings[0]->ievt.name.c_str(), 0xe, bg, bd);
		ps("\n", 0xe, bg, bd);
	}
	if(m_mode == GUI_MODE_REALISTIC) {
		evt.func.name = ProgramEvent::FuncName::FUNC_TOGGLE_PAUSE;
		bindings = m_keymaps[m_current_keymap].find_prg_bindings(evt);
		if(!bindings.empty()) {
			ps("To pause the machine press ", 0xf, bg, bd);
			ps(bindings[0]->ievt.name.c_str(), 0xe, bg, bd);
			ps("\n", 0xe, bg, bd);
		}
		evt.func.name = ProgramEvent::FuncName::FUNC_SAVE_STATE;
		bindings = m_keymaps[m_current_keymap].find_prg_bindings(evt);
		if(!bindings.empty()) {
			ps("To save the machine's state press ", 0xf, bg, bd);
			ps(bindings[0]->ievt.name.c_str(), 0xe, bg, bd);
			evt.func.name = ProgramEvent::FuncName::FUNC_LOAD_STATE;
			bindings = m_keymaps[m_current_keymap].find_prg_bindings(evt);
			if(!bindings.empty()) {
				ps("\nTo load a saved state press ", 0xf, bg, bd);
				ps(bindings[0]->ievt.name.c_str(), 0xe, bg, bd);
				ps("\n", 0xe, bg, bd);
			}
		}
	}
	evt.func.name = ProgramEvent::FuncName::FUNC_TOGGLE_FULLSCREEN;
	bindings = m_keymaps[m_current_keymap].find_prg_bindings(evt);
	if(!bindings.empty()) {
		ps("To toggle fullscreen mode press ", 0xf, bg, bd);
		ps(bindings[0]->ievt.name.c_str(), 0xe, bg, bd);
		ps("\n", 0xe, bg, bd);
	}
	evt.func.name = ProgramEvent::FuncName::FUNC_EXIT;
	bindings = m_keymaps[m_current_keymap].find_prg_bindings(evt);
	if(!bindings.empty()) {
		ps("To close the emulator press ", 0xf, bg, bd);
		ps(bindings[0]->ievt.name.c_str(), 0xe, bg, bd);
		ps("\n", 0xe, bg, bd);
	}
	ps("\nYou can find the configuration file here:\n", 0xf, bg, bd);
	ps(g_program.config().get_path().c_str(), 0xe, bg, bd);
	ps("\n\nFor more information read the README file and visit the home page at\n", 0xf, bg, bd);
	ps(PACKAGE_URL "\n", 0xe, bg, bd);

	m_machine->cmd_print_VGA_text(text);
}

void GUI::pevt_func_none(const ProgramEvent::Func&, EventPhase)
{
	PDEBUGF(LOG_V0, LOG_GUI, "Unknown func event!\n");
}

void GUI::pevt_func_show_options(const ProgramEvent::Func&, EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Show options window func event\n");
	show_options_window();
}

void GUI::pevt_func_toggle_mixer(const ProgramEvent::Func&, EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Toggle mixer control func event\n");
	toggle_mixer_control();
}

void GUI::pevt_func_gui_mode_action(const ProgramEvent::Func &_func, EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	
	PDEBUGF(LOG_V1, LOG_GUI, "GUI mode action func event\n");
	
	int action = _func.params[0] - 1;
	std::lock_guard<std::mutex> lock(ms_rml_mutex);
	bool was_visible = m_windows.interface->is_system_visible();
	m_windows.interface->action(action);
	m_windows.interface->container_size_changed(m_width, m_height);
	if(!was_visible && m_windows.interface->is_system_visible()) {
		grab_input(false);
	}
}

void GUI::pevt_func_toggle_power(const ProgramEvent::Func&, EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Toggle machine power button func event\n");
	
	m_windows.interface->switch_power();
}

void GUI::pevt_func_toggle_pause(const ProgramEvent::Func&, EventPhase _phase)
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

void GUI::pevt_func_toggle_status_ind(const ProgramEvent::Func&, EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Toggle status indicators func event\n");
	m_windows.toggle_status();
}

void GUI::pevt_func_toggle_dbg_wnd(const ProgramEvent::Func&, EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Toggle debugging windows func event\n");
	toggle_dbg_windows();
}

void GUI::pevt_func_toggle_printer(const ProgramEvent::Func&, EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Toggle printer control func event\n");
	toggle_printer_control();
}

void GUI::pevt_func_take_screenshot(const ProgramEvent::Func&, EventPhase _phase)
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

void GUI::pevt_func_toggle_audio_capture(const ProgramEvent::Func&, EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Toggle audio capture func event\n");
	
	m_mixer->cmd_toggle_capture();
}

void GUI::pevt_func_toggle_video_capture(const ProgramEvent::Func&, EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Toggle video capture func event\n");
	
	m_capture->cmd_toggle_capture();
}

void GUI::pevt_func_insert_floppy(const ProgramEvent::Func&, EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Insert floppy func event\n");

	Rml::Event e;
	m_windows.interface->on_fdd_mount(e);
}

void GUI::pevt_func_eject_floppy(const ProgramEvent::Func&, EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Eject floppy func event\n");

	Rml::Event e;
	m_windows.interface->on_fdd_eject(e);
}

void GUI::pevt_func_change_floppy_drive(const ProgramEvent::Func&, EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Change floppy drive func event\n");

	Rml::Event e;
	m_windows.interface->on_fdd_select(e);
}

void GUI::pevt_func_save_state(const ProgramEvent::Func&, EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Save func event\n");
	
	// don't save key presses in a machine state.
	// every keypress must be neutralized by a release before opening the save dialog.
	// key release events will be intercepted by gui elements
	for(const auto & [key, state] : m_key_state) {
		if(state) {
			// the eventual physical key releases will not be sent to the guest again,
			// as the keys' states are checked in send_key_to_machine()
			send_key_to_machine(key, KEY_RELEASED);
		}
	}
	Rml::Event e;
	m_windows.interface->on_save_state(e);
}

void GUI::pevt_func_load_state(const ProgramEvent::Func&, EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Load func event\n");
	
	for(const auto & [key, state] : m_key_state) {
		if(state) {
			send_key_to_machine(key, KEY_RELEASED);
		}
	}
	Rml::Event e;
	m_windows.interface->on_load_state(e);
}

void GUI::pevt_func_quick_save_state(const ProgramEvent::Func&, EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Quick save func event\n");

	for(const auto & [key, state] : m_key_state) {
		if(state) {
			send_key_to_machine(key, KEY_RELEASED);
		}
	}

	m_windows.interface->save_state({QUICKSAVE_RECORD, QUICKSAVE_DESC, "", 0, 0});
}

void GUI::pevt_func_quick_load_state(const ProgramEvent::Func&, EventPhase _phase)
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

	restore_state({QUICKSAVE_RECORD, QUICKSAVE_DESC, "", 0, 0});
}

void GUI::pevt_func_grab_mouse(const ProgramEvent::Func&, EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Grab mouse func event\n");
	
	grab_input(!m_input.grab);
	
	if(m_mode == GUI_MODE_NORMAL) {
		NormalInterface *interface = dynamic_cast<NormalInterface*>(m_windows.interface.get());
		if(interface) {
			interface->grab_input(m_input.grab);
		}
	}
}

void GUI::pevt_func_sys_speed_up(const ProgramEvent::Func&, EventPhase _phase)
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

void GUI::pevt_func_sys_speed_down(const ProgramEvent::Func&, EventPhase _phase)
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

void GUI::pevt_func_sys_speed(const ProgramEvent::Func &_func, EventPhase _phase)
{
	if(_phase == EventPhase::EVT_REPEAT) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "System speed func event, speed=%d%%\n", _func.params[0]);
	if(_phase == EventPhase::EVT_START) {
		m_symspeed_factor = double(_func.params[0]) / 100.0;
	} else {
		m_symspeed_factor = 1.0;
	}

	m_machine->cmd_cycles_adjust(m_symspeed_factor);
}

void GUI::pevt_func_toggle_fullscreen(const ProgramEvent::Func&, EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Toggle fullscreen func event\n");

	toggle_fullscreen();
}

void GUI::pevt_func_switch_keymaps(const ProgramEvent::Func&, EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}

	PDEBUGF(LOG_V1, LOG_GUI, "Switch keymap func event\n");
	if(m_keymaps.size() <= 1) {
		PDEBUGF(LOG_V1, LOG_GUI, "  only 1 keymap installed\n");
		return;
	}

	m_input.reset();
	m_current_keymap = (m_current_keymap + 1) % m_keymaps.size();

	std::string mex = "Current keymap: ";
	mex += m_keymaps[m_current_keymap].name();
	show_message(mex.c_str());
}

void GUI::pevt_func_exit(const ProgramEvent::Func&, EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Quit func event\n");
	SDL_Event sdlevent;
	sdlevent.type = SDL_QUIT;
	SDL_PushEvent(&sdlevent);
}

void GUI::pevt_func_reload_rcss(const ProgramEvent::Func&, EventPhase _phase)
{
	if(_phase != EventPhase::EVT_START) {
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Reload RCSS func event\n");
	m_windows.reload_rcss();
}

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

void GUI::WindowManager::init(Machine *_machine, GUI *_gui, Mixer *_mixer, uint _mode)
{
	m_gui = _gui;
	timers.set_log_facility(LOG_GUI);
	timers.init();

	desktop = std::make_unique<Desktop>(_gui);
	desktop->show();

	if(_mode == GUI_MODE_REALISTIC) {
		interface = std::make_unique<RealisticInterface>(_machine, _gui, _mixer);
	} else {
		interface = std::make_unique<NormalInterface>(_machine, _gui, _mixer, &timers);
	}
	interface->show();

	status = std::make_unique<Status>(_gui, _machine);
	status->create();
	status_wnd = false;
	if(g_program.config().get_bool(GUI_SECTION, GUI_SHOW_INDICATORS)) {
		status->show();
		status_wnd = true;
	}

	auto printer = _machine->get_printer();
	if(printer) {
		printer_ctrl = std::make_unique<PrinterControl>(_gui, printer);
	}
	
	auto renderer = interface->screen_renderer();
	if(renderer->get_shader_params()) {
		options_wnd = std::make_unique<ShaderParameters>(_gui, renderer);
	}

	mixer_ctrl = std::make_unique<MixerControl>(_gui, _mixer);

	//debug
	dbgtools = std::make_unique<DebugTools>(_gui, _machine, _mixer);

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
	
	interface->focus();
}

void GUI::WindowManager::config_changed(bool _startup)
{
	std::lock_guard<std::mutex> lock(ms_rml_mutex);

	desktop->config_changed(_startup);
	interface->config_changed(_startup);
	if(status) {
		status->config_changed(_startup);
	}
	dbgtools->config_changed(_startup);
	mixer_ctrl->config_changed(_startup);
}

void GUI::WindowManager::show_ifc_message(const char* _mex)
{
	std::lock_guard<std::mutex> lock(ms_rml_mutex);
	if(interface != nullptr) {
		interface->show_message(_mex);
		timers.activate_timer(ifcmex_timer, 3_s, false);
	}
}

void GUI::WindowManager::show_dbg_message(const char* _mex)
{
	std::lock_guard<std::mutex> lock(ms_rml_mutex);
	if(dbgtools != nullptr) {
		dbgtools->show_message(_mex);
		timers.activate_timer(dbgmex_timer, 3_s, false);
	}
}

void GUI::WindowManager::update(uint64_t _current_time)
{
	if(!last_ifc_mex.empty()) {
		show_ifc_message(last_ifc_mex.c_str());
		last_ifc_mex.clear();
	}
	if(!last_dbg_mex.empty()) {
		show_dbg_message(last_dbg_mex.c_str());
		last_dbg_mex.clear();
	}

	// updates can't call any function that needs a lock on ms_rml_mutex
	std::lock_guard<std::mutex> lock(ms_rml_mutex);

	interface->update();

	if(options_wnd && options_wnd->is_visible()) {
		options_wnd->update();
	}

	if(mixer_ctrl->is_visible()) {
		mixer_ctrl->update();
	}

	if(debug_wnds) {
		dbgtools->update();
	}

	if(status) {
		status->update();
	}

	if(printer_ctrl && printer_ctrl->is_visible()) {
		printer_ctrl->update();
	}

	// timers are where the timed windows events take place
	// like the interface messages clears
	while(!timers.update(_current_time));
	
	if(revert_focus) {
		if(revert_focus->IsVisible()) {
			PDEBUGF(LOG_V1, LOG_GUI, "Switching focus to '%s'\n", revert_focus->GetTitle().c_str());
			revert_focus->Focus();
		} else {
			interface->focus();
		}
	}
	revert_focus = nullptr;
}

void GUI::WindowManager::update_after()
{
	// called after the Rml::Context::Update()
	// should be used to update the DOM during event callbacks

	interface->update_after();

	if(options_wnd) {
		options_wnd->update_after();
	}

	if(mixer_ctrl) {
		mixer_ctrl->update_after();
	}

	if(debug_wnds) {
		dbgtools->update_after();
	}

	if(status) {
		status->update_after();
	}

	if(printer_ctrl) {
		printer_ctrl->update_after();
	}
}

void GUI::WindowManager::update_window_size(int _w, int _h)
{
	interface->container_size_changed(_w, _h);

	for(auto doc : m_docs) {
		if(!doc->IsClassSet("window")) {
			continue;
		}
		auto size = doc->GetBox().GetSize();
		auto offset = doc->GetAbsoluteOffset();
		if(offset.x < 0 || offset.x + size.x > _w || 
		   offset.y < 0 || offset.y + size.y > _h)
		{
			// this document is out of view.
			PDEBUGF(LOG_V2, LOG_GUI, "Reset '%s' to initial properties\n", doc->GetTitle().c_str());
			for(auto p : {
				Rml::PropertyId::Width,
				Rml::PropertyId::Height,
				Rml::PropertyId::MarginTop,
				Rml::PropertyId::MarginRight,
				Rml::PropertyId::MarginBottom,
				Rml::PropertyId::MarginLeft,
				Rml::PropertyId::Top,
				Rml::PropertyId::Right,
				Rml::PropertyId::Bottom,
				Rml::PropertyId::Left})
			{
				doc->RemoveProperty(p);
			}
		}
	}
}

void GUI::WindowManager::show_options()
{
	if(options_wnd && !options_wnd->is_visible()) {
		options_wnd->show();
	}
}

void GUI::WindowManager::toggle_mixer()
{
	if(!mixer_ctrl->is_visible()) {
		mixer_ctrl->show();
	} else {
		mixer_ctrl->hide();
	}
}

void GUI::WindowManager::toggle_dbg()
{
	debug_wnds = !debug_wnds;
	if(debug_wnds) {
		dbgtools->show();
	} else {
		dbgtools->hide();
	}
}

void GUI::WindowManager::toggle_printer()
{
	if(!printer_ctrl) {
		return;
	}

	if(!printer_ctrl->is_visible()) {
		printer_ctrl->show();
	} else {
		printer_ctrl->hide();
	}
}

void GUI::WindowManager::toggle_status()
{
	status_wnd = !status_wnd;
	if(status_wnd) {
		status->show();
	} else {
		status->hide();
	}
}

Rml::ElementDocument * GUI::WindowManager::current_doc()
{
	return m_gui->m_rml_context->GetFocusElement()->GetOwnerDocument();
}

bool GUI::WindowManager::need_input()
{
	return (current_doc() != interface->m_wnd);
}

void GUI::WindowManager::ProcessEvent(Rml::Event &_ev)
{
	Rml::ElementDocument *doc = dynamic_cast<Rml::ElementDocument *>(_ev.GetTargetElement());
	std::string title;
	if(doc) {
		title = doc->GetTitle();
	}
	bool is_window = _ev.GetTargetElement()->IsClassSet("window");
	PDEBUGF(LOG_V1, LOG_GUI, "Event '%s' on '%s'\n", _ev.GetType().c_str(), title.c_str());
	switch(_ev.GetId()) {
		case Rml::EventId::Show:
			if(is_window) {
				windows_count++;
				PDEBUGF(LOG_V1, LOG_GUI, "  window '%s' shown (%d)\n", 
						title.c_str(), windows_count);
			}
			break;
		case Rml::EventId::Hide:
			if(is_window) {
				windows_count--;
				PDEBUGF(LOG_V1, LOG_GUI, " window '%s' hidden (%d)\n", 
						title.c_str(), windows_count);
			}
			break;
		case Rml::EventId::Unload:
			if(doc) {
				auto doc_it = std::find(m_docs.begin(), m_docs.end(), doc);
				if(doc_it != m_docs.end()) {
					m_docs.erase(doc_it);
				}
				PDEBUGF(LOG_V1, LOG_GUI, "  registered docs: %u\n", static_cast<unsigned>(m_docs.size()));
			}
			break;
		case Rml::EventId::Focus:
			if(!is_window && title != "Interface") {
				if(last_focus_doc) {
					revert_focus = last_focus_doc;
				}
			} else if(doc) {
				revert_focus = nullptr;
				last_focus_doc = doc;
			}
			break;
		default:
			break;
	}

}

bool GUI::WindowManager::are_visible()
{
	return windows_count;
}

void GUI::WindowManager::register_document(Rml::ElementDocument *_doc)
{
	for(auto id : listening_evts) {
		_doc->AddEventListener(id, this);
	}
	m_docs.push_back(_doc);
	PDEBUGF(LOG_V1, LOG_GUI, "Registered documents: %u\n", static_cast<unsigned>(m_docs.size()));
}

void GUI::WindowManager::reload_rcss()
{
	for(auto doc : m_docs) {
		doc->ReloadStyleSheet();
	}
}

void GUI::WindowManager::shutdown()
{
	std::lock_guard<std::mutex> lock(ms_rml_mutex);

	PDEBUGF(LOG_V1, LOG_GUI, "Window manager shutting down, registered docs: %u\n", static_cast<unsigned>(m_docs.size()));

	if(status) {
		status->close();
		status.reset(nullptr);
	}

	if(dbgtools) {
		dbgtools->close();
		dbgtools.reset(nullptr);
	}

	if(desktop) {
		desktop->close();
		desktop.reset(nullptr);
	}

	if(interface) {
		interface->close();
		interface.reset(nullptr);
	}

	PDEBUGF(LOG_V1, LOG_GUI, "Window manager shut down: registered docs: %u\n", static_cast<unsigned>(m_docs.size()));
}

