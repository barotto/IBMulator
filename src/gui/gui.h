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

#ifndef IBMULATOR_GUI_H
#define IBMULATOR_GUI_H

#include <SDL.h>
#include <Rocket/Core/Input.h>
#include <Rocket/Core/Types.h>
#include "rocket/rend_interface.h"
#include "matrix.h"
#include "timers.h"
#include <atomic>

enum GUIRenderer {
	GUI_RENDERER_OPENGL,
	GUI_RENDERER_SDL2D
};

enum GUIMode {
	GUI_MODE_NORMAL,
	GUI_MODE_COMPACT,
	GUI_MODE_REALISTIC
};

enum DisplaySampler {
	DISPLAY_SAMPLER_NEAREST,
	DISPLAY_SAMPLER_BILINEAR,
	DISPLAY_SAMPLER_BICUBIC,
};

enum DisplayAspect {
	DISPLAY_ASPECT_ORIGINAL,
	DISPLAY_ASPECT_ADAPTIVE,
	DISPLAY_ASPECT_SCALED
};

enum FrameCapMethods {
	GUI_FRAMECAP_VGA,
	GUI_FRAMECAP_VSYNC,
	GUI_FRAMECAP_OFF
};

#define JOY_NONE INT_MAX

class GUI;
class GUI_OpenGL;


class RocketSystemInterface;
class RocketFileInterface;

namespace Rocket {
namespace Core {
	class Context;
	class Element;
	class ElementDocument;
}
namespace Controls {
	class ElementFormControl;
}
}
#define RC Rocket::Core
#define RCN Rocket::Controls

#include "windows/interface.h"

class Machine;
class Mixer;
class Desktop;
class Interface;
class NormalInterface;
class Status;
class DebugTools;
class TextModeInfo;

#include "appconfig.h"

enum MouseTypes {
	MOUSE_TYPE_NONE,
	MOUSE_TYPE_PS2,
	MOUSE_TYPE_IMPS2, //Microsoft IntelliMouse
	MOUSE_TYPE_SERIAL,
	MOUSE_TYPE_SERIAL_WHEEL,
	MOUSE_TYPE_SERIAL_MSYS
};

extern ini_enum_map_t g_mouse_types;

#include "hardware/devices/vgadisplay.h"



class GUI
{
protected:
	Machine *m_machine;
	Mixer *m_mixer;

	std::string m_assets_path;
	int m_width;
	int m_height;
	SDL_Window *m_SDL_window;
	std::string m_wnd_title;
	std::string m_curr_prog;
	std::string m_curr_model;
	bool m_curr_model_changed;
	SDL_TimerID m_second_timer;
	std::vector<SDL_Joystick*> m_SDL_joysticks;
	int m_joystick0;
	int m_joystick1;
	bool m_gui_visible;
	bool m_input_grab;
	std::string m_grab_method;
	uint m_mode;
	unsigned m_framecap;
	bool m_vsync;
	bool m_vga_buffering;
	bool m_threads_sync;
	SDL_Color m_backcolor;

	struct Mouse {
		bool grab;
		Mouse() :
			grab(true)
		{}
	} m_mouse;

	double m_symspeed_factor;

	// mutex must be locked before any access to the libRocket's objects
	// TODO this mutex is currently used by 1 thread only (GUI). remove?
	static std::mutex ms_rocket_mutex;
	std::unique_ptr<RocketRenderer> m_rocket_renderer;
	RocketSystemInterface * m_rocket_sys_interface;
	RocketFileInterface * m_rocket_file_interface;
	Rocket::Core::Context * m_rocket_context;

	class Windows {
	public:
		static std::mutex s_interface_mutex;

		bool visible;
		bool debug_wnds;
		bool status_wnd;

		Desktop * desktop;
		Interface * interface;
		Status * status;
		DebugTools * dbgtools;

		std::string last_ifc_mex;
		std::string last_dbg_mex;

		EventTimers timers;
		unsigned dbgmex_timer;
		unsigned ifcmex_timer;

		Windows();
		void init(Machine *_machine, GUI *_gui, Mixer *_mixer, uint _mode);
		void config_changed();
		void update(uint64_t _current_time);
		void shutdown();
		void toggle_dbg();
		void show(bool _value);
		void toggle();
		bool needs_input();
		void show_ifc_message(const char* _mex);
		void show_dbg_message(const char* _mex);

	} m_windows;

	void init_Rocket();
	void set_window_icon();
	void show_welcome_screen();
	void render_vga();
	void update_window_size(int _w, int _h);
	void update_display_size();
	void update_display_size_realistic();
	void toggle_input_grab();
	void input_grab(bool _value);
	
	virtual void shutdown_SDL();
	
	void dispatch_hw_event(const SDL_Event &_event);
	void dispatch_rocket_event(const SDL_Event &event);
	void dispatch_window_event(const SDL_WindowEvent &_event);
	bool dispatch_special_keys(const SDL_Event &_event, SDL_Keycode &_discard_next_key);
	
	static Uint32 every_second(Uint32 interval, void *param);

	static std::string load_shader_file(const std::string &_path);

	virtual void create_window(int _flags) = 0;
	virtual void create_rocket_renderer() = 0;
	
public:
	static std::map<std::string, uint> ms_gui_modes;
	static std::map<std::string, uint> ms_gui_sampler;
	static std::map<std::string, uint> ms_display_aspect;

public:
	GUI();
	virtual ~GUI();

	static GUI * instance();
	
	virtual GUIRenderer renderer() const = 0;
	
	void init(Machine *_machine, Mixer *_mixer);
	void config_changed();
	void dispatch_event(const SDL_Event &_event);
	void update(uint64_t _time);
	void shutdown();
	
	RC::ElementDocument * load_document(const std::string &_filename);
	static std::string shaders_dir();
	static std::string images_dir();
	
	virtual uintptr_t load_texture(SDL_Surface *_surface) = 0;
	virtual uintptr_t load_texture(const std::string &_path, vec2i *_texdim=nullptr) = 0;
	virtual void render() = 0;
	
	void save_framebuffer(std::string _screenfile, std::string _palfile);
	void take_screenshot(bool _with_palette_file = false);
	void show_message(const char* _mex);
	void show_dbg_message(const char* _mex);

	vec2i resize_window(int _width, int _height);
	bool is_fullscreen();
	void toggle_fullscreen();
	
	inline VGADisplay * vga_display() const {
		assert(m_windows.interface); return m_windows.interface->vga_display();
	}
	inline int window_width() const { return m_width; }
	inline int window_height() const { return m_height; }
	inline uint32_t window_flags() const { return SDL_GetWindowFlags(m_SDL_window); }
	inline bool vsync_enabled() const { return m_vsync; }
	inline bool vga_buffering_enabled() const { return m_vga_buffering; }
	inline bool threads_sync_enabled() const { return m_threads_sync; }
	
	void sig_state_restored();
};


#endif
