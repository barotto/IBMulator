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
#include "program.h"
#include "keys.h"
#include "utils.h"
#include <SDL.h>
#include "keymap.h"
#include <algorithm>
#include <regex>

#include "keytables.cpp"

void InputEvent::parse_token(const std::string &_tok)
{
	auto uptok = str_to_upper(_tok);

	if(!name.empty()) {
		name += "+";
	}
	name += uptok;
	str_replace_all(name, "KMOD_", "");
	str_replace_all(name, "SDLK_", "");
	str_replace_all(name, "SDL_SCANCODE_", "");
	
	// is it an SDL scancode?
	key.scancode = Keymap::get_SDL_Scancode_from_name(uptok);
	if(key.scancode != SDL_SCANCODE_UNKNOWN) {
		type = Type::INPUT_KEY;
		key.sym = SDLK_UNKNOWN;
		PDEBUGF(LOG_V2, LOG_GUI, "  scancode: 0x%x (%s)\n", key.scancode, _tok.c_str());
		return;
	}
	// is it an SDL keycode?
	key.sym = Keymap::get_SDL_Keycode_from_name(uptok);
	if(key.sym != SDLK_UNKNOWN) {
		type = Type::INPUT_KEY;
		key.scancode = SDL_SCANCODE_UNKNOWN;
		PDEBUGF(LOG_V2, LOG_GUI, "  keycode: 0x%x (%s)\n", key.sym, _tok.c_str());
		return;
	}
	// is it an SDL keymod?
	auto mod = Keymap::ms_sdl_kmod_table.find(uptok);
	if(mod != Keymap::ms_sdl_kmod_table.end()) {
		type = Type::INPUT_KEY;
		key.mod |= mod->second;
		PDEBUGF(LOG_V2, LOG_GUI, "  modifier: 0x%x (%s)\n", mod->second, _tok.c_str());
		return;
	}
	// is it joystick?
	// using a regular expression as indices can be arbitrarily big numbers
	std::smatch match;
	std::regex jre("^JOY_([^_]+)_([^_]+)_([^_]+)$"); // don't match with \\d+, I want to give the user good feedback
	if(std::regex_match(uptok, match, jre)) {
		assert(match.size() == 4);
		try {
			pointing.which = std::stoi(match[1].str());
		} catch(std::exception &e) {
			throw std::runtime_error(std::string("invalid Joystick index number ") + match[1].str());
		}
		if(match[2].str() == "BUTTON") {
			try {
				pointing.button = std::stoi(match[3].str());
			} catch(std::exception &e) {
				throw std::runtime_error(std::string("invalid Joystick button number ") + match[3].str());
			}
			type = Type::INPUT_JOY_BUTTON;
		} else if(match[2].str() == "AXIS") {
			try {
				pointing.axis = std::stoi(match[3].str());
			} catch(std::exception &e) {
				throw std::runtime_error(std::string("invalid Joystick axis number ") + match[3].str());
			}
			type = Type::INPUT_JOY_AXIS;
		} else {
			throw std::runtime_error("invalid Joystick input event: must be JOY_j_BUTTON_n or JOY_j_AXIS_n");
		}
		pointing.type = ec_to_i(type);
		return;
	}
	// is it mouse?
	std::regex mre("^MOUSE_([^_]+)_([^_]+)$");
	if(std::regex_match(uptok, match, mre)) {
		assert(match.size() == 3);
		if(match[1].str() == "BUTTON") {
			try {
				pointing.button = std::stoi(match[2].str());
			} catch(std::exception &e) {
				throw std::runtime_error(std::string("invalid Mouse button number ") + match[2].str());
			}
			type = Type::INPUT_MOUSE_BUTTON;
		} else if(match[1].str() == "AXIS") {
			if(match[2].str() == "X") {
				pointing.axis = 0;
			} else if(match[2].str() == "Y") {
				pointing.axis = 1;
			} else {
				throw std::runtime_error(std::string("invalid Mouse axis letter ") + match[3].str() + ": must be X or Y");
			}
			type = Type::INPUT_MOUSE_AXIS;
		} else {
			throw std::runtime_error("invalid Mouse input event: must be MOUSE_BUTTON_n or MOUSE_AXIS_(X|Y)");
		}
		pointing.type = ec_to_i(type);
		return;
	}
	// oops
	throw std::runtime_error("unrecognized identifier");
}

bool InputEvent::Key::is_key_modifier() const
{
	if(scancode) {
		return (
			scancode == SDL_SCANCODE_LCTRL  ||
			scancode == SDL_SCANCODE_LSHIFT ||
			scancode == SDL_SCANCODE_LALT   ||
			scancode == SDL_SCANCODE_LGUI   ||
			scancode == SDL_SCANCODE_RCTRL  ||
			scancode == SDL_SCANCODE_RSHIFT ||
			scancode == SDL_SCANCODE_RALT   ||
			scancode == SDL_SCANCODE_RGUI
		);
	} else {
		// keycode
		return (
			sym == SDLK_LCTRL  ||
			sym == SDLK_LSHIFT ||
			sym == SDLK_LALT   ||
			sym == SDLK_LGUI   ||
			sym == SDLK_RCTRL  ||
			sym == SDLK_RSHIFT ||
			sym == SDLK_RALT   ||
			sym == SDLK_RGUI
		);
	}
}

ProgramEvent::ProgramEvent(const std::string &_tok)
{
	std::smatch match;
	auto uptok = str_to_upper(_tok);

	// is it a FUNC?
	if(std::regex_match(uptok, match, std::regex("^FUNC_.+$"))) {
		std::string func_name;
		// parse possible parameters
		if(std::regex_match(uptok, match, std::regex("^(FUNC_[^\\(]+)\\((.+)\\)$")) && match[2].matched) {
			func_name = match[1].str();
			auto params = str_parse_tokens(match[2].str(), ",");
			if(params.empty()) {
				// this should never happen
				throw std::runtime_error(std::string("invalid parameters for Function ") + _tok);
			}
			for(unsigned i=0; i<params.size() && i<2; i++) {
				func.params[i] = atoi(params[i].c_str());
			}
		} else {
			func_name = uptok;
		}
		auto funcp = Keymap::ms_prog_funcs_table.find(func_name);
		if(funcp != Keymap::ms_prog_funcs_table.end()) {
			type = Type::EVT_PROGRAM_FUNC;
			func.name = funcp->second;
			name = func_name;
			return;
		}
		throw std::runtime_error(std::string("invalid program Function ") + _tok);
	}
	// is it a guest KEY event?
	if(std::regex_match(uptok, match, std::regex("^KEY_.+$"))) {
		auto keyp = Keymap::ms_keycode_table.find(_tok);
		if(keyp != Keymap::ms_keycode_table.end()) {
			type = Type::EVT_KEY;
			key = keyp->second;
			name = uptok;
			return;
		}
		throw std::runtime_error(std::string("invalid emulated keyboard Key ") + _tok);
	}
	// is it a guest event of JOY?
	if(std::regex_match(uptok, match, std::regex("^JOY_([^_]+)_([^_]+)_([^\\(_]+)(\\((.+)\\))?$"))) {
		assert(match.size() == 6);
		if(match[1].str() == "A") {
			joy.which = 0;
		} else if(match[1].str() == "B") {
			joy.which = 1;
		} else {
			throw std::runtime_error(std::string("invalid emulated Joystick letter ") + match[1].str() + ": must be A or B");
		}
		if(match[2].str() == "BUTTON") {
			try {
				joy.button = std::stoi(match[3].str());
			} catch(std::exception &e) {
				throw std::runtime_error(std::string("invalid emulated Joystick button number ") + match[3].str());
			}
			if(joy.button != 1 && joy.button != 2) {
				throw std::runtime_error(str_format("invalid emulated Joystick button number %d: must be 1 or 2", joy.button));
			}
			joy.button--;
			type = Type::EVT_JOY_BUTTON;
			name = uptok;
		} else if(match[2].str() == "AXIS") {
			if(match[3].str() == "X") {
				joy.axis = 0;
			} else if(match[3].str() == "Y") {
				joy.axis = 1;
			} else {
				throw std::runtime_error(std::string("invalid emulated Joystick axis letter ") + match[3].str() + ": must be X or Y");
			}
			type = Type::EVT_JOY_AXIS;
			name = uptok;
		} else {
			throw std::runtime_error("invalid emulated Joystick event: must be JOY_[A|B]_BUTTON_[1|2] or JOY_[A|B]_AXIS_[X|Y]");
		}
		if(match[5].matched) {
			auto params = str_parse_tokens(match[5].str(), ",");
			if(params.empty()) {
				// if the regex is correct this should never happen
				throw std::runtime_error(std::string("invalid parameters for ") + _tok);
			}
			for(unsigned i=0; i<params.size() && i<3; i++) {
				if(params[i] == "MAX") {
					joy.params[i] = 32767;
				} else if(params[i] == "-MAX") {
					joy.params[i] = -32768;
				} else {
					joy.params[i] = atoi(params[i].c_str());
				}
			}
		}
		return;
	}
	// is it a guest MOUSE event?
	if(std::regex_match(uptok, match, std::regex("^MOUSE_([^_]+)_([^\\(]+)(\\((.+)\\))?$"))) {
		assert(match.size() == 5);
		if(match[1].str() == "BUTTON") {
			try {
				mouse.button = static_cast<MouseButton>(std::stoi(match[2].str()));
			} catch(std::exception &e) {
				throw std::runtime_error(std::string("invalid emulated Mouse button number ") + match[2].str());
			}
			type = Type::EVT_MOUSE_BUTTON;
			name = uptok;
		} else if(match[1].str() == "AXIS") {
			if(match[2].str() == "X") {
				mouse.axis = 0;
			} else if(match[2].str() == "Y") {
				mouse.axis = 1;
			} else {
				throw std::runtime_error(std::string("invalid emulated Mouse axis letter ") + match[2].str() + ": must be X or Y");
			}
			type = Type::EVT_MOUSE_AXIS;
			name = uptok;
		} else {
			throw std::runtime_error("invalid emulated Mouse event: must be MOUSE_BUTTON_n or MOUSE_AXIS_[X|Y]");
		}
		if(match[4].matched) {
			auto params = str_parse_tokens(match[4].str(), ",");
			if(params.empty()) {
				// this should never happen
				throw std::runtime_error(std::string("invalid parameters for ") + _tok);
			}
			for(unsigned i=0; i<params.size() && i<3; i++) {
				mouse.params[i] = atoi(params[i].c_str());
			}
		}
		return;
	}
	// maybe it's a CMD?
	// keep it the last check as commands don't have a prefix
	if(std::regex_match(uptok, match, std::regex("^([^\\(]+)(\\((.+)\\))?$"))) {
		auto cmdp = Keymap::ms_commands_table.find(str_to_upper(match[1].str()));
		if(cmdp != Keymap::ms_commands_table.end()) {
			type = Type::EVT_COMMAND;
			command.name = cmdp->second;
			name = uptok;
			if(match[3].matched) {
				auto params = str_parse_tokens(match[3].str(), ",");
				for(unsigned i=0; i<params.size() && i<2; i++) {
					auto const_value = Keymap::ms_constants_table.find(str_to_upper(params[i].c_str()));
					if(const_value != Keymap::ms_constants_table.end()) {
						command.params[i] = const_value->second;
					} else {
						command.params[i] = atoi(params[i].c_str());
					}
				}
			}
			return;
		}
	}
	// oops
	throw std::runtime_error("unrecognized identifier " + _tok);
}

ProgramEvent::ProgramEvent(const char *_tok)
: ProgramEvent(std::string(_tok))
{
}

void Keymap::Binding::parse_option(std::string _tok)
{
	std::smatch match;
	auto uptok = str_to_upper(_tok);

	if(std::regex_match(uptok, match, std::regex("^MODE:(.+)$"))) {
		if(match[1].str() == "1SHOT") {
			mode = Mode::ONE_SHOT;
		} else if(match[1].str() == "DEFAULT") {
			mode = Mode::DEFAULT;
		} else {
			throw std::runtime_error(std::string("invalid binding mode ") + _tok);
		}
		return;
	}
	if(std::regex_match(uptok, match, std::regex("^GROUP:(.+)$"))) {
		group = match[1].str();
		return;
	}

	// oops
	throw std::runtime_error(std::string("unrecognized identifier") + _tok);
}


bool Keymap::Binding::has_prg_event(const ProgramEvent &_evt) const 
{
	for(auto & pe : pevt) {
		if(pe == _evt) {
			return true;
		}
	}
	return false;
}

bool Keymap::Binding::has_cmd_event(ProgramEvent::CommandName _cmd_name, size_t _start_idx) const
{
	if(pevt.empty()) {
		return false;
	}
	size_t idx = std::min(pevt.size()-1, _start_idx);
	for(; idx<pevt.size(); idx++) {
		if(pevt[idx].type == ProgramEvent::Type::EVT_COMMAND && pevt[idx].command.name == _cmd_name) {
			return true;
		}
	}
	return false;
}

static constexpr bool is_key_modifier(Keys key)
{
	return (
		key == KEY_CTRL_L  ||
		key == KEY_SHIFT_L ||
		key == KEY_CTRL_R  ||
		key == KEY_SHIFT_R ||
		key == KEY_ALT_L   ||
		key == KEY_ALT_R   ||
		key == KEY_WIN_L   ||
		key == KEY_WIN_R
	);
}

bool ProgramEvent::is_key_modifier() const
{
	return (type == Type::EVT_KEY && ::is_key_modifier(key)); 
}

bool Keymap::Binding::is_ievt_keycombo() const
{
	return (
		(ievt.type == InputEvent::Type::INPUT_KEY) &&
		(ievt.key.mod)
	);
}

bool Keymap::Binding::is_pevt_keycombo() const
{
	bool mod = false;
	bool key = false;
	for(auto &evt : pevt) {
		if(evt.type == ProgramEvent::Type::EVT_KEY) {
			mod |= is_key_modifier(evt.key);
			key |= !is_key_modifier(evt.key);
		}
	}
	return (mod && key);
}

void Keymap::Binding::mask_pevt_kmods(bool _mask)
{
	for(auto &evt : pevt) {
		if(evt.is_key_modifier()) {
			evt.masked = _mask;
		}
	}
}

const Keymap::Binding *Keymap::find_sdl_binding(const SDL_KeyboardEvent &_event) const
{
	uint16_t kmodmask = 0;
	switch(_event.keysym.sym) {
		case SDLK_LCTRL:  kmodmask = KMOD_LCTRL; break;
		case SDLK_LSHIFT: kmodmask = KMOD_LSHIFT; break;
		case SDLK_LALT:   kmodmask = KMOD_LALT; break;
		case SDLK_LGUI:   kmodmask = KMOD_LGUI; break;
		case SDLK_RCTRL:  kmodmask = KMOD_RCTRL; break;
		case SDLK_RSHIFT: kmodmask = KMOD_RSHIFT; break;
		case SDLK_RALT:   kmodmask = KMOD_RALT; break;
		case SDLK_RGUI:   kmodmask = KMOD_RGUI; break;
			break;
		default:
			break;
	}

	auto find = [&](uint16_t _modmask)->const Binding * {
		// search the keycode first
		InputEvent::Key kevt;
		kevt.mod = _event.keysym.mod & _modmask;
		kevt.sym = _event.keysym.sym;
		const Binding * binding = find_input_binding(kevt);
		if(!binding) {
			// then the scancode
			kevt.sym = SDLK_UNKNOWN;
			kevt.scancode = _event.keysym.scancode;
			binding = find_input_binding(kevt);
		}
		return binding;
	};
	
	// search key w/ mod
	const Binding * binding = find( (KMOD_CTRL|KMOD_ALT|KMOD_SHIFT|KMOD_GUI) & ~kmodmask );
	
	if(!binding) {
		// search key w/o mod
		binding = find( 0 );
	}
	
	return binding;
}

const Keymap::Binding *Keymap::find_sdl_binding(const SDL_MouseMotionEvent &_event) const
{
	InputEvent::Pointing mevt;
	mevt.type = ec_to_i(InputEvent::Type::INPUT_MOUSE_AXIS);
	if(_event.xrel != 0) {
		mevt.axis = 0;
	} else if(_event.yrel != 0) {
		mevt.axis = 1;
	} else {
		PDEBUGF(LOG_V0, LOG_GUI, "mouse motion event with no motion?");
		return nullptr;
	}
	return find_input_binding(mevt);
}

const Keymap::Binding *Keymap::find_sdl_binding(const SDL_MouseButtonEvent &_event) const
{
	InputEvent::Pointing mevt;
	mevt.type = ec_to_i(InputEvent::Type::INPUT_MOUSE_BUTTON);
	if(_event.button == 2) {
		mevt.button = 3;
	} else if(_event.button == 3) {
		mevt.button = 2;
	} else {
		mevt.button = _event.button;
	}
	return find_input_binding(mevt);
}

const Keymap::Binding *Keymap::find_sdl_binding(uint8_t _joyid, const SDL_JoyAxisEvent &_event) const
{
	InputEvent::Pointing jevt;
	jevt.type = ec_to_i(InputEvent::Type::INPUT_JOY_AXIS);
	jevt.which = _joyid;
	jevt.axis = _event.axis;
	return find_input_binding(jevt);
}

const Keymap::Binding *Keymap::find_sdl_binding(uint8_t _joyid, const SDL_JoyButtonEvent &_event) const
{
	InputEvent::Pointing jevt;
	jevt.type = ec_to_i(InputEvent::Type::INPUT_JOY_BUTTON);
	jevt.which = _joyid;
	jevt.button = _event.button;
	return find_input_binding(jevt);
}

const Keymap::Binding *Keymap::find_input_binding(const InputEvent::Key &_kevt) const
{
	auto b = m_kbindings.find(_kevt.to_index());
	if(b != m_kbindings.end()) {
		return b->second;
	}
	return nullptr;
}

std::vector<const Keymap::Binding *> Keymap::find_prg_bindings(const ProgramEvent &_event) const
{
	std::vector<const Binding *> result;
	for(auto & binding : m_bindings) {
		for(auto & prgevt : binding.pevt) {
			if(prgevt == _event) {
				result.push_back(&binding);
			}
		}
	}
	return result;
}

const Keymap::Binding *Keymap::find_input_binding(const InputEvent::Pointing &_pevt) const
{
	auto b = m_pbindings.find(_pevt.to_index());
	if(b != m_pbindings.end()) {
		return b->second;
	}
	return nullptr;
}

SDL_Keycode Keymap::get_SDL_Keycode_from_name(const std::string &_name)
{
	auto k = Keymap::ms_sdl_keycode_table.find(_name);
	if(k != Keymap::ms_sdl_keycode_table.end()) {
		return k->second;
	}
	return SDLK_UNKNOWN;
}

SDL_Scancode Keymap::get_SDL_Scancode_from_name(const std::string &_name)
{
	auto s = Keymap::ms_sdl_scancode_table.find(_name);
	if(s != Keymap::ms_sdl_scancode_table.end()) {
		return s->second;
	}
	return SDL_SCANCODE_UNKNOWN;
}

std::string Keymap::get_name_from_SDL_Keycode(SDL_Keycode &_keycode)
{
	auto k = Keymap::ms_sdl_keycode_str_table.find(_keycode);
	if(k != Keymap::ms_sdl_keycode_str_table.end()) {
		return k->second;
	}
	return "";
}

std::string Keymap::get_name_from_SDL_Scancode(SDL_Scancode &_scancode)
{
	auto k = Keymap::ms_sdl_scancode_str_table.find(_scancode);
	if(k != Keymap::ms_sdl_scancode_str_table.end()) {
		return k->second;
	}
	return "";
}

void Keymap::load(const std::string &_filename)
{
	std::ifstream keymap_file(_filename.c_str(), std::ios::in);
	if(!keymap_file.is_open()) {
		PERRF(LOG_GUI, "Unable to open keymap file '%s'\n", _filename.c_str());
		throw std::exception();
	}

	PINFOF(LOG_V0, LOG_GUI, "Loading keymap from '%s':\n", _filename.c_str());

	// Read the keymap definition file one line at a time
	int linec = 0;
	int total = 0;
	while(true) {
		std::vector<std::string> itoks, ptoks, opt_toks;
		std::string line;
		try {
			line = parse_next_line(keymap_file, linec, itoks, ptoks, opt_toks);
			if(line.empty()) {
				// EOF
				break;
			}
		} catch(std::runtime_error &e) {
			PERRF(LOG_GUI, "  line %d: %s\n", linec, e.what());
			continue;
		}
		
		assert(itoks.size());
		assert(ptoks.size());
		
		InputEvent ievt;
		std::vector<ProgramEvent> pevts;
		
		for(auto &it : itoks) {
			try {
				ievt.parse_token(it);
			} catch(std::runtime_error &e) {
				PERRF(LOG_GUI, "  line %d: %s: %s\n", linec, it.c_str(), e.what());
				continue;
			}
		}
		for(auto &pt : ptoks) {
			try {
				pevts.emplace_back(pt);
			} catch(std::runtime_error &e) {
				PERRF(LOG_GUI, "  line %d: %s: %s\n", linec, pt.c_str(), e.what());
				continue;
			}
		}
		total++;
		auto binding = add_binding(ievt, pevts, line);
		for(auto &ot : opt_toks) {
			try {
				binding->parse_option(ot);
			} catch(std::runtime_error &e) {
				PERRF(LOG_GUI, "  line %d: %s: %s\n", linec, ot.c_str(), e.what());
				continue;
			}
		}
		if(binding->mode == Binding::Mode::DEFAULT) {
			bool tm = apply_typematic(binding->pevt);
			if(tm && binding->group.empty()) {
				binding->group = "typematic";
			}
		}
		PDEBUGF(LOG_V0, LOG_GUI, "  (%d) %s\n", total, line.c_str());
	}

	PINFOF(LOG_V0, LOG_GUI, "  loaded %d bindings.\n", total);
	keymap_file.close();
	m_name = _filename;
}

void Keymap::expand_macros(std::vector<ProgramEvent> &_pevts)
{
	for(size_t i=0; i<_pevts.size(); i++) {
		if(_pevts[i].type == ProgramEvent::Type::EVT_COMMAND) {
			switch(_pevts[i].command.name) {
				case ProgramEvent::CommandName::CMD_AUTOFIRE: {
					int time = _pevts[i].command.params[0];
					if(time <= 0) {
						time = 0;
					}
					// catching exception is not necessary as long as events are well formed
					// keep it for debugging tho
					try {
						std::string wait = "wait(" + std::to_string(time/2) + ")";
						_pevts.insert(_pevts.begin() + i, {
							wait,
							"release",
							wait,
							"repeat"
						});
						i += 4;
					} catch(std::runtime_error &e) {
						PDEBUGF(LOG_V2, LOG_GUI, "%s\n", e.what());
					}
					_pevts.erase(_pevts.begin() + i);
					break;
				}
				default: {
					break;
				}
			}
		}
	}
	
}

bool Keymap::apply_typematic(std::vector<ProgramEvent> &_pevts)
{
	bool is_typematic = true;
	int modifiers = 0;
	int key_idx = -1;
	for(size_t i=0; i<_pevts.size() && is_typematic; i++) {
		if(_pevts[i].type != ProgramEvent::Type::EVT_KEY) {
			is_typematic = false;
		} else {
			if(is_key_modifier(_pevts[i].key)) {
				if(key_idx >= 0) {
					// a modifier after the main key?
					is_typematic = false;
				} else {
					modifiers++;
				}
			} else {
				if(key_idx >= 0) {
					// this is a macro
					is_typematic = false;
				}
				key_idx = i;
			}
		}
	}
	
	if(key_idx >= 0 && is_typematic) {
		try {
			_pevts.insert(_pevts.end(), {
				"wait(tmd)",
				_pevts[key_idx],
				"wait(tmr)",
				"skip_to(" + std::to_string(key_idx+3) + ")"
			});
		} catch(std::runtime_error &e) {
			PDEBUGF(LOG_V2, LOG_GUI, "%s\n", e.what());
		}
		return true;
	}
	return false;
}

Keymap::Binding * Keymap::add_binding(InputEvent &_ievt, std::vector<ProgramEvent> &_pevts, std::string &_name)
{
	expand_macros(_pevts);

	m_bindings.emplace_back(_ievt, _pevts, _name);
	auto binding = &m_bindings.back();

	switch(_ievt.type) {
		case InputEvent::Type::INPUT_KEY:
			add_binding(_ievt.key, binding);
			return binding;
		case InputEvent::Type::INPUT_JOY_BUTTON:
		case InputEvent::Type::INPUT_JOY_AXIS:
		case InputEvent::Type::INPUT_MOUSE_BUTTON:
		case InputEvent::Type::INPUT_MOUSE_AXIS:
			m_pbindings[_ievt.pointing.to_index()] = binding;
			return binding;
		default:
			assert(false);
			return nullptr;
	}
}

void Keymap::add_binding(const InputEvent::Key &_kevt, const Binding *_binding)
{
	auto mod_split = [&](int _all, int _left, int _right) {
		PDEBUGF(LOG_V2, LOG_GUI, "  splitting modifier for binding 0x%llX\n", _kevt.to_index());
		InputEvent::Key k(_kevt);
		k.mod &= ~_all;
		k.mod |= _left;
		add_binding(k, _binding);
		k.mod &= ~_left;
		k.mod |= _right;
		add_binding(k, _binding);
	};
	if((_kevt.mod & KMOD_SHIFT) == KMOD_SHIFT) {
		// any shift key
		mod_split(KMOD_SHIFT, KMOD_LSHIFT, KMOD_RSHIFT);
		return;
	}
	if((_kevt.mod & KMOD_CTRL) == KMOD_CTRL) {
		// any control key
		mod_split(KMOD_CTRL, KMOD_LCTRL, KMOD_RCTRL);
		return;
	}
	if((_kevt.mod & KMOD_ALT) == KMOD_ALT) {
		// any alt key
		mod_split(KMOD_ALT, KMOD_LALT, KMOD_RALT);
		return;
	}
	if((_kevt.mod & KMOD_GUI) == KMOD_GUI) {
		// any gui key
		mod_split(KMOD_GUI, KMOD_LGUI, KMOD_RGUI);
		return;
	}
	// add
	PDEBUGF(LOG_V2, LOG_GUI, "  adding key binding 0x%llX\n", _kevt.to_index());
	m_kbindings[_kevt.to_index()] = _binding;
}

std::string Keymap::parse_next_line(std::ifstream &_fp, int &_linec,
		std::vector<std::string> &itoks_, std::vector<std::string> &ptoks_,
		std::vector<std::string> &opt_toks_)
{
	std::string line;
	
	while(true) {
		std::getline(_fp, line);
		if(!_fp.good()) {
			return "";  // EOF
		}
		_linec++;
		
		line = line.substr(0, line.find_first_of("#"));
		line = str_trim(str_compress_spaces(line));
		
		auto line_tokens = str_parse_tokens(line, "\\s*=\\s*");
		if(line_tokens.empty() || (line_tokens.size() == 1 && str_trim(line_tokens[0]).length() == 0)) {
			// nothing of value found, skip
			continue;
		}
		if(line_tokens.size() != 2) {
			throw std::runtime_error(str_format(
				"expected 'INPUT_EVENT = IBMULATOR_EVENT', found '%s' instead", line.c_str()
			));
		}
		
		itoks_ = str_parse_tokens(line_tokens[0], "\\+");
		auto out_toks = str_parse_tokens(line_tokens[1], ";");
		if(out_toks.size() > 1) {
			opt_toks_ = str_parse_tokens(out_toks[1], "\\s+");
		}
		ptoks_ = str_parse_tokens(out_toks[0], "\\+");
		
		break;
	}
	
	return line;
}
