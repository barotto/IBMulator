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

#ifndef IBMULATOR_KEYMAP_H
#define IBMULATOR_KEYMAP_H

#include "keys.h"

struct InputEvent {
	enum class Type {
		INPUT_NONE,
		INPUT_KEY,
		INPUT_JOY_BUTTON,
		INPUT_JOY_AXIS,
		INPUT_MOUSE_BUTTON,
		INPUT_MOUSE_AXIS
	};
	enum class State {
		INPUT_NONE,
		INPUT_PRESS,
		INPUT_RELEASE,
		INPUT_MOTION
	};
	
	Type type = Type::INPUT_NONE;
	State state = State::INPUT_NONE;
	
	struct Key {
		SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;  // SDL physical key code
		SDL_Keycode  sym = SDLK_UNKNOWN;               // SDL virtual key code
		uint16_t     mod = 0;                          // key modifiers
		
		uint64_t to_index() const {
			// scancode value is always limited to 16bit
			return (uint64_t(sym)<<32 | scancode<<16 | mod);
		}
		bool operator<(const Key &_rhs) const {
			return (to_index() < _rhs.to_index());
		}
		bool is_key_modifier() const;
	} key;
	
	struct Pointing {
		uint8_t type = 0;
		uint8_t which = 0;
		uint8_t button = 0;
		uint8_t axis = 0;
		
		uint32_t to_index() const {
			return (type<<24 | which<<16 | button<<8 | axis);
		}
		bool operator<(const Pointing &_rhs) const {
			return (to_index() < _rhs.to_index());
		}
	} pointing;
	
	std::string name;
	
	void parse_token(const std::string &_tok);
};

struct ProgramEvent {
	// FIXME how about using oop principles? (how about no?)
	enum class Type {
		EVT_NONE = 0,
		EVT_PROGRAM_FUNC = 1,
		EVT_KEY = 2,
		EVT_JOY_BUTTON = 4,
		EVT_JOY_AXIS = 8,
		EVT_MOUSE_BUTTON = 16,
		EVT_MOUSE_AXIS = 32,
		EVT_COMMAND = 64
	};
	
	enum class FuncName {
		FUNC_NONE,
		FUNC_GUI_MODE_ACTION,      // GUI Mode action (see README); can be binded to keyboard events only!
		FUNC_TOGGLE_POWER,         // toggle the machine's power button
		FUNC_TOGGLE_PAUSE,         // pause / resume emulation
		FUNC_TOGGLE_DBG_WND,       // show / hide the debug windows
		FUNC_TAKE_SCREENSHOT,      // take a screenshot
		FUNC_TOGGLE_AUDIO_CAPTURE, // start / stop audio capture
		FUNC_TOGGLE_VIDEO_CAPTURE, // start / stop video capture
		FUNC_QUICK_SAVE_STATE,     // quick save the current emulator's state
		FUNC_QUICK_LOAD_STATE,     // quick load the last saved state
		FUNC_GRAB_MOUSE,           // lock / unlock mouse to emulator
		FUNC_SYS_SPEED_UP,         // increase emulation speed (whole system)
		FUNC_SYS_SPEED_DOWN,       // decrease emulation speed (whole system)
		FUNC_SYS_SPEED,            // set emulation speed to specified % (2 params: speed,momentary?)
		FUNC_TOGGLE_FULLSCREEN,    // toggle fullscreen mode
		FUNC_SWITCH_KEYMAPS,       // change the active keymap to the next in the keymaps list
		FUNC_EXIT                  // close program
	};
	
	enum class CommandName {
		CMD_NONE,
		CMD_WAIT,
		CMD_RELEASE,
		CMD_SKIP_TO,
		CMD_AUTOFIRE
	};
	
	enum Constant {
		CONST_TYPEMATIC_DELAY = -1,
		CONST_TYPEMATIC_RATE = -2
	};
	
	Type type = Type::EVT_NONE;
	bool masked = false;
	
	Keys key = KEY_NONE;
	
	struct Func {
		FuncName name = FuncName::FUNC_NONE;
		int params[2] = {0,0};
		bool operator==(const ProgramEvent::Func &_rhs) const {
			return (name == _rhs.name);
		}
	} func;
	
	struct Joy {
		uint8_t which = 0;
		uint8_t button = 0;
		uint8_t axis = 0;
		int params[3] = {0,0,0};
		bool operator==(const ProgramEvent::Joy &_rhs) const {
			return (which == _rhs.which && button == _rhs.button && axis == _rhs.axis);
		}
	} joy;
	
	struct Mouse {
		MouseButton button = MouseButton::MOUSE_NOBTN;
		uint8_t axis = 0;
		int params[3] = {0,0,0};
		bool operator==(const ProgramEvent::Mouse &_rhs) const {
			return (button == _rhs.button && axis == _rhs.axis);
		}
	} mouse;
	
	struct Command {
		CommandName name = CommandName::CMD_NONE;
		int params[2] = {0,0};
		bool operator==(const ProgramEvent::Command &_rhs) const {
			return (name == _rhs.name);
		}
	} command;
	
	std::string name;
	
	ProgramEvent() {}
	ProgramEvent(const std::string &_tok);
	ProgramEvent(const char *_tok);
	
	bool operator==(const ProgramEvent &_rhs) const {
		return (type == _rhs.type && func == _rhs.func && key == _rhs.key &&
				joy == _rhs.joy && mouse == _rhs.mouse && command == _rhs.command);
	}
	
	bool is_key_modifier() const;
};

class Keymap
{
public:
	struct Binding {
		InputEvent ievt;
		std::vector<ProgramEvent> pevt;
		enum class Mode {
			DEFAULT, ONE_SHOT, LATCHED
		} mode = Mode::DEFAULT;
		std::string name;
		std::string group;
		bool typematic = true;

		Binding() {}
		Binding(InputEvent _ie, std::vector<ProgramEvent> _pe, std::string _n) : 
			ievt(_ie), pevt(_pe), name(_n) {}

		bool operator!=(const Binding &_rhs) const {
			return (name != _rhs.name);
		}
		void parse_option(std::string);
		bool has_prg_event(const ProgramEvent&) const;
		bool has_cmd_event(ProgramEvent::CommandName _cmd, size_t _idx_from = 0) const;
		bool is_ievt_keycombo() const;
		bool is_pevt_keycombo() const;
		void mask_pevt_kmods(bool _mask = true);
	};

private:
	std::list<Binding> m_bindings;
	std::map<uint64_t, const Binding*> m_kbindings;
	std::map<uint32_t, const Binding*> m_pbindings;
	std::string m_name;
	
public:
	void load(const std::string &_filename);
	const char *name() const { return m_name.c_str(); }

	const Binding *find_sdl_binding(const SDL_KeyboardEvent &_event) const;
	const Binding *find_sdl_binding(const SDL_MouseMotionEvent &_event) const;
	const Binding *find_sdl_binding(const SDL_MouseButtonEvent &_event) const;
	const Binding *find_sdl_binding(uint8_t _joyid, const SDL_JoyAxisEvent &_event) const;
	const Binding *find_sdl_binding(uint8_t _joyid, const SDL_JoyButtonEvent &_event) const;
	
	std::vector<const Keymap::Binding *> find_prg_bindings(const ProgramEvent &_event) const;
	
	static SDL_Keycode get_SDL_Keycode_from_name(const std::string &);
	static SDL_Scancode get_SDL_Scancode_from_name(const std::string &);
	static std::string get_name_from_SDL_Keycode(SDL_Keycode &);
	static std::string get_name_from_SDL_Scancode(SDL_Scancode &_scancode);

	static const std::map<std::string, uint32_t> ms_sdl_kmod_table;
	static const std::map<std::string, Keys> ms_keycode_table;
	static const std::map<std::string, SDL_Keycode> ms_sdl_keycode_table;
	static const std::map<std::string, SDL_Scancode> ms_sdl_scancode_table;
	static const std::map<Keys, std::string> ms_keycode_str_table;
	static const std::map<SDL_Keycode, std::string> ms_sdl_keycode_str_table;
	static const std::map<SDL_Scancode, std::string> ms_sdl_scancode_str_table;
	static const std::map<std::string, ProgramEvent::FuncName> ms_prog_funcs_table;
	static const std::map<std::string, ProgramEvent::CommandName> ms_commands_table;
	static const std::map<std::string, ProgramEvent::Constant> ms_constants_table;

private:
	Binding * add_binding(InputEvent &_ievt, std::vector<ProgramEvent> &_pevts, std::string &_name);
	void add_binding(const InputEvent::Key &_kevt, const Binding *_binding);
	
	const Binding *find_input_binding(const InputEvent::Key &_kevt) const;
	const Binding *find_input_binding(const InputEvent::Pointing &_pevt) const;

	std::string parse_next_line(std::ifstream &_fp, int &_linec,
			std::vector<std::string> &itoks_, std::vector<std::string> &ptoks_,
			std::vector<std::string> &opt_toks_);
	
	void expand_macros(std::vector<ProgramEvent> &_pevts);
	bool apply_typematic(std::vector<ProgramEvent> &_pevts);
};

#endif
