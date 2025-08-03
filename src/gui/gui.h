/*
 * Copyright (C) 2015-2025  Marco Bortolin
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
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Types.h>
#include "rml/rend_interface.h"
#include "windows/interface.h"
#include "matrix.h"
#include "timers.h"
#include "keymap.h"
#include "state_record.h"
#include "tts.h"
#include <atomic>
#include <climits>

enum GUIRenderer {
	GUI_RENDERER_OPENGL,
	GUI_RENDERER_SDL2D
};

enum GUIMode {
	GUI_MODE_NORMAL,
	GUI_MODE_REALISTIC
};

enum FrameCapMethods {
	GUI_FRAMECAP_VGA,
	GUI_FRAMECAP_VSYNC,
	GUI_FRAMECAP_OFF
};

#define JOY_NONE INT_MAX

class GUI;
class GUI_OpenGL;

class Capture;

class RmlSystemInterface;
class RmlFileInterface;

namespace Rml {
	class Context;
	class Element;
	class ElementDocument;
	class ElementFormControl;
}

class Machine;
class Mixer;
class Desktop;
class Status;
class ShaderParameters;
class MixerControl;
class PrinterControl;
class DebugTools;
class TextModeInfo;
class AudioOSD;

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
#include "windows/message_wnd.h"


class GUI
{
protected:
	Machine *m_machine;
	Mixer *m_mixer;

	std::string m_assets_path;
	int m_width;
	int m_height;
	double m_scaling_factor;
	SDL_Window *m_SDL_window;
	std::string m_wnd_title;
	std::string m_curr_prog;
	std::string m_curr_model;
	bool m_curr_model_changed;
	std::vector<SDL_Joystick*> m_SDL_joysticks;
	bool m_gui_visible;
	uint m_mode;
	unsigned m_framecap;
	bool m_vsync;
	bool m_vga_buffering;
	bool m_threads_sync;
	SDL_Color m_backcolor;
	std::vector<Keymap> m_keymaps;
	unsigned m_current_keymap;
	bool m_fullscreen_toggled = false;
	
	enum TimedEvents {
		GUI_TEVT_PEVT = 0,
		GUI_TEVT_MOUSE,
		GUI_TEVT_JOYSTICK,

		GUI_TEVT_COUNT
	};
	inline static Uint32 ms_sdl_user_evt_id;
	
	struct Mouse {
		enum Axis {
			X,Y
		};
		bool grab = true;
		double speed[2] = {.0,.0};
		double maxspeed[2] = {.0,.0};
		double rel[2] = {.0,.0};
		double accel[2] = {.0,.0};
		SDL_TimerID events_timer = 0;
		void enable_timer();
		void disable_timer();
		static Uint32 s_sdl_timer_callback(Uint32 interval, void *param);
		void generate_sdl_event();
		void update(double _time);
		void stop(Axis _axis);
		void send(Machine *_machine);
	} m_mouse;

	enum class EventPhase {
		EVT_START, EVT_REPEAT, EVT_END
	};

	struct InputSystem {
		struct Event {
			Sint32 code = 0;
			SDL_Event sdl_evt;
			Keymap::Binding binding; // copy!
			unsigned pevt_idx = 0;
			SDL_TimerID events_timer = 0;
			std::shared_ptr<Event> combo_link;
			bool linked = false;
			bool remove = true;

			Event(Sint32, const SDL_Event &, const Keymap::Binding *);
			~Event();

			void enable_timer(Uint32 _interval_ms);
			void disable_timer();
			void restart();
			bool is_running() const { return events_timer != 0; }
			static Uint32 s_sdl_timer_callback(Uint32 interval, void *param);
			void generate_sdl_event();
			void link_to(std::shared_ptr<Event> _evt) { combo_link = _evt; };
		};

		GUI * gui;
		std::map<Sint32, std::shared_ptr<Event>> events;
		Sint32 evt_count = 0;
		bool grab = false;

		std::shared_ptr<Event> start_evt(const SDL_Event &, const Keymap::Binding *);
		std::shared_ptr<Event> find(SDL_Event _sdl_evt);
		std::shared_ptr<Event> find(Sint32 _code);
		std::vector<std::shared_ptr<Event>> find_mods(unsigned _sdl_mod);
		void stop_group(InputSystem::Event &_event);
		bool is_active(std::shared_ptr<GUI::InputSystem::Event> _evt);
		void remove(std::shared_ptr<Event>);
		void reset();
	} m_input;


	struct Joystick {
		// host side
		// there's no direct mapping to VM joysticks. that's done via a keymap.
		int sdl_id;
		bool show_message;

		// vm side
		// sharing the structure with the host side.
		enum Axis {
			X,Y, MAX_AXIS
		};
		enum Joy {
			A,B, MAX_JOY
		};
		Joy which;
		int value[2] = {0,0};
		int maxvalue[2] = {0,0};
		int speed[2] = {0,0};
		SDL_TimerID events_timer = 0;

		void enable_timer();
		void disable_timer();
		static Uint32 s_sdl_timer_callback(Uint32 interval, void *param);
		void generate_sdl_event();
		void update(int _time_ms);
		void stop(Axis _axis);
		void send(Machine *_machine);
	} m_joystick[2];
	
	double m_symspeed_factor;

	// mutex must be locked before any access to the RmlUi's objects
	// TODO this mutex is currently used by 1 thread only (GUI). remove?
	inline static std::mutex ms_rml_mutex;
	std::unique_ptr<RmlRenderer> m_rml_renderer;
	RmlSystemInterface * m_rml_sys_interface;
	RmlFileInterface * m_rml_file_interface;
	Rml::Context * m_rml_context;

	class WindowManager : public Rml::EventListener {
	public:
		GUI *m_gui = nullptr;
		int windows_count = 0; // debug
		std::list<std::pair<Rml::ElementDocument*, Window*>> m_docs; // ElementDocument objects are property of RmlUI
		Rml::ElementDocument *last_focus_doc = nullptr;
		Rml::ElementDocument *revert_focus = nullptr;

		std::unique_ptr<Desktop> root;
		Interface *interface = nullptr;

		std::string last_ifc_mex;
		std::string last_dbg_mex;

		EventTimers timers;
		TimerID dbgmex_timer = NULL_TIMER_ID;
		TimerID ifcmex_timer = NULL_TIMER_ID;

		WindowManager();

		void init(Machine *_machine, GUI *_gui, Mixer *_mixer, uint _mode);
		void config_changing();
		void config_changed(bool _startup);
		void update(uint64_t _current_time);
		void update_after();
		void update_window_size(int _w, int _h);
		void shutdown();
		template <class WinClass> WinClass* get_window();
		template <class WinClass> void toggle_window();
		template <class WinClass> void show_window();
		void show_ifc_message(const char* _mex);
		void show_dbg_message(const char* _mex);
		Rml::ElementDocument * current_doc();
		Window * current_window();
		bool need_input();
		void ProcessEvent(Rml::Event &);
		bool are_visible();
		void register_document(Rml::ElementDocument *_doc, Window *_owner_wnd);
		void reload_rcss();
		static constexpr Rml::EventId listening_evts[] = {
			Rml::EventId::Show,
			Rml::EventId::Hide,
			Rml::EventId::Unload,
			Rml::EventId::Focus
		};
	} m_windows;

	typedef std::function<void()> GUI_fun_t;
	shared_queue<GUI_fun_t> m_cmd_queue;
	
	std::unique_ptr<Capture> m_capture;
	std::thread m_capture_thread;

	TTS m_tts;

	void init_rmlui();
	void set_window_icon();
	void update_window_size(int _w, int _h);
	
	virtual void shutdown_SDL();
	
	void dispatch_rml_event(const SDL_Event &event);
	void dispatch_window_event(const SDL_WindowEvent &_event);
	void dispatch_user_event(const SDL_UserEvent &_event);
	
	static std::list<std::string> load_shader_file(const std::string &_path);

	virtual void create_window(int _flags) = 0;
	virtual void create_renderer() = 0;
	
public:
	static std::map<std::string, uint> ms_gui_modes;
	static std::map<std::string, uint> ms_gui_sampler;
	static std::map<std::string, uint> ms_display_aspect;
	static std::map<std::string, uint> ms_display_scale;
	static std::vector<std::string> ms_themes;

public:
	GUI();
	virtual ~GUI();

	static GUI * instance();
	
	virtual GUIRenderer renderer() const = 0;
	
	void init(Machine *_machine, Mixer *_mixer);
	void config_changing() noexcept;
	void config_changed(bool _startup) noexcept;
	void dispatch_event(const SDL_Event &_event);
	void update(uint64_t _time);
	void shutdown();
	void cmd_stop_capture_and_signal(std::mutex &_mutex, std::condition_variable &_cv);

	Rml::DataModelConstructor create_data_model(const std::string &_model_name);
	void remove_data_model(const std::string &_model_name);
	Rml::ElementDocument * load_document(const std::string &_filename, Window *_owner_wnd);
	void unload_document(Rml::ElementDocument *);

	SDL_Surface *load_surface(const std::string &_name);
	void update_surface(const std::string &_name, SDL_Surface *_data);

	virtual void update_texture(uintptr_t _texture, SDL_Surface *_data) = 0;
	virtual void render() = 0;

	void save_framebuffer(std::string _screenfile, std::string _palfile);
	SDL_Surface * copy_framebuffer();
	void take_screenshot(bool _with_palette_file = false);
	void show_message(std::string _mex);
	void show_dbg_message(std::string _mex);
	void show_message_box(const std::string &_title, const std::string &_message, MessageWnd::Type _type, 
			std::function<void()> _on_action1 = nullptr, std::function<void()> _on_action2 = nullptr);
	void show_error_message_box(const std::string &_message);
	bool are_windows_visible();
	void show_options_window();
	void toggle_dbg_windows();
	void toggle_mixer_control();
	void toggle_printer_control();
	void grab_input(bool _value);
	bool is_input_grabbed();
	bool is_video_recording() const;
	bool is_audio_recording() const;

	vec2i resize_window(int _width, int _height);
	bool is_fullscreen();
	void toggle_fullscreen();
	
	inline VGADisplay * vga_display() const {
		assert(m_windows.interface);
		return m_windows.interface->vga_display();
	}
	inline int window_width() const { return m_width; }
	inline int window_height() const { return m_height; }
	inline double scaling_factor() const { return m_scaling_factor; }
	inline uint32_t window_flags() const { return SDL_GetWindowFlags(m_SDL_window); }
	inline bool vsync_enabled() const { return m_vsync; }
	inline bool vga_buffering_enabled() const { return m_vga_buffering; }
	inline bool threads_sync_enabled() const { return m_threads_sync; }
	
	void restore_state(StateRecord::Info _info);
	void sig_state_restored();
	
	EventTimers & timers() { return m_windows.timers; }

	TTS & tts() { return m_tts; }

private:
	void load_keymap(const std::string &_filename);

	void on_keyboard_event(const SDL_Event &_event);
	void on_mouse_motion_event(const SDL_Event &_event);
	void on_mouse_button_event(const SDL_Event &_event);
	void on_mouse_wheel_event(const SDL_Event &_event);
	void on_joystick_motion_event(const SDL_Event &_event);
	void on_joystick_button_event(const SDL_Event &_event);
	void on_joystick_event(const SDL_Event &_event);
	void on_button_event(const SDL_Event &, const Keymap::Binding *, EventPhase);

	void run_event_binding(InputSystem::Event &, EventPhase, uint32_t _mask = 0);
	void run_event_functions(const Keymap::Binding *_binding, EventPhase _phase);
	
	void pevt_key(Keys _key, EventPhase);
	void pevt_mouse_axis(const ProgramEvent::Mouse &, const SDL_Event &, EventPhase, Keymap::Binding::Mode);
	void pevt_mouse_button(const ProgramEvent::Mouse &, EventPhase);
	void pevt_joy_axis(const ProgramEvent::Joy &, const SDL_Event &, EventPhase);
	void pevt_joy_button(const ProgramEvent::Joy &, EventPhase);

	static const std::map<ProgramEvent::FuncName, std::function<void(GUI&, const ProgramEvent::Func&, EventPhase)>> ms_event_funcs;
	std::map<Keys, bool> m_key_state;
	void send_key_to_machine(Keys _key, uint32_t _event);
	
	void pevt_func_none(const ProgramEvent::Func&, EventPhase);
	void pevt_func_show_options(const ProgramEvent::Func&, EventPhase);
	void pevt_func_toggle_mixer(const ProgramEvent::Func&, EventPhase);
	void pevt_func_set_audio_ch(const ProgramEvent::Func&, EventPhase);
	void pevt_func_set_next_audio_ch(const ProgramEvent::Func&, EventPhase);
	void pevt_func_set_prev_audio_ch(const ProgramEvent::Func&, EventPhase);
	void pevt_func_set_audio_volume(const ProgramEvent::Func&, EventPhase);
	void pevt_func_gui_mode_action(const ProgramEvent::Func&, EventPhase);
	void pevt_func_toggle_power(const ProgramEvent::Func&, EventPhase);
	void pevt_func_toggle_pause(const ProgramEvent::Func&, EventPhase);
	void pevt_func_toggle_status_ind(const ProgramEvent::Func&, EventPhase);
	void pevt_func_toggle_dbg_wnd(const ProgramEvent::Func&, EventPhase);
	void pevt_func_toggle_printer(const ProgramEvent::Func&, EventPhase);
	void pevt_func_take_screenshot(const ProgramEvent::Func&, EventPhase);
	void pevt_func_toggle_audio_capture(const ProgramEvent::Func&, EventPhase);
	void pevt_func_toggle_video_capture(const ProgramEvent::Func&, EventPhase);
	void pevt_func_insert_medium(const ProgramEvent::Func&, EventPhase);
	void pevt_func_eject_medium(const ProgramEvent::Func&, EventPhase);
	void pevt_func_change_drive(const ProgramEvent::Func&, EventPhase);
	void pevt_func_save_state(const ProgramEvent::Func&, EventPhase);
	void pevt_func_load_state(const ProgramEvent::Func&, EventPhase);
	void pevt_func_quick_save_state(const ProgramEvent::Func&, EventPhase);
	void pevt_func_quick_load_state(const ProgramEvent::Func&, EventPhase);
	void pevt_func_grab_mouse(const ProgramEvent::Func&, EventPhase);
	void pevt_func_sys_speed(const ProgramEvent::Func&, EventPhase);
	void pevt_func_sys_speed_up(const ProgramEvent::Func&, EventPhase);
	void pevt_func_sys_speed_down(const ProgramEvent::Func&, EventPhase);
	void pevt_func_toggle_fullscreen(const ProgramEvent::Func&, EventPhase);
	void pevt_func_switch_keymaps(const ProgramEvent::Func&, EventPhase);
	void pevt_func_tts_adj_rate(const ProgramEvent::Func&, EventPhase);
	void pevt_func_tts_adj_volume(const ProgramEvent::Func&, EventPhase);
	void pevt_func_tts_describe(const ProgramEvent::Func&, EventPhase);
	void pevt_func_tts_read_chars(const ProgramEvent::Func&, EventPhase);
	void pevt_func_tts_read_words(const ProgramEvent::Func&, EventPhase);
	void pevt_func_tts_stop(const ProgramEvent::Func&, EventPhase);
	void pevt_func_tts_window_title(const ProgramEvent::Func&, EventPhase);
	void pevt_func_tts_gui_toggle(const ProgramEvent::Func&, EventPhase);
	void pevt_func_tts_guest_toggle(const ProgramEvent::Func&, EventPhase);
	void pevt_func_set_color_mode(const ProgramEvent::Func&, EventPhase);
	void pevt_func_set_prev_color_mode(const ProgramEvent::Func&, EventPhase);
	void pevt_func_set_next_color_mode(const ProgramEvent::Func&, EventPhase);
	void pevt_func_exit(const ProgramEvent::Func&, EventPhase);
	void pevt_func_reload_rcss(const ProgramEvent::Func&, EventPhase);
	
	void rmlui_mouse_hack();
};


#endif
