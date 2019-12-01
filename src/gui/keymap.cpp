/*
 * Copyright (C) 2015-2019  Marco Bortolin
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
#include "keymap.h"
#include "utils.h"
#include <SDL.h>

#include "keytables.cpp"

Keymap g_keymap;


Keymap::Keymap()
{
}

Keymap::~Keymap()
{
}

void Keymap::load(const std::string &_filename)
{
	std::ifstream keymapFile(_filename.c_str(), std::ios::in);
	if(!keymapFile.is_open()) {
		PERRF(LOG_MACHINE,"Unable to open keymap file '%s'\n", _filename.c_str());
		throw std::exception();
	}

	PINFOF(LOG_V0, LOG_GUI, "Loading keymap from '%s'\n", _filename.c_str());

	// Read keymap file one line at a time
	int linec = 0;
	while(true) {
		std::string base_sym, host_sym;
		if(parse_next_line(keymapFile, linec, base_sym, host_sym) < 0) {
			//EOF
			break;
		}
		
		// convert KEY_* symbols to values
		uint32_t base_key = convert_string_to_key(Keymap::ms_keycode_table, base_sym);
		if(base_key == KEYMAP_UNKNOWN) {
			PERRF(LOG_GUI,"line %d: unknown KEY constant '%s'\n", linec, base_sym.c_str());
			continue;
		}

		// convert SDL_* symbols to values
		uint32_t host_key = convert_string_to_key(Keymap::ms_sdl_keycode_table, host_sym);
		uint32_t host_scan = convert_string_to_key(Keymap::ms_sdl_scancode_table, host_sym);
		if(host_key == KEYMAP_UNKNOWN && host_scan == KEYMAP_UNKNOWN) {
			// check if it's a hex value
			try {
				host_key = std::stol(host_sym);
			} catch(...) {
				host_key = KEYMAP_UNKNOWN;
			}
		}
		
		if(host_key != KEYMAP_UNKNOWN) {
			m_keys_by_keycode[host_key] = KeyEntry{base_key, host_key, host_sym};
			PINFOF(LOG_V2, LOG_GUI, "base_key='%s' (%d), keycode='%s' (0x%x)\n",
					base_sym.c_str(), base_key, host_sym.c_str(), host_key);
		} else if(host_scan != KEYMAP_UNKNOWN){
			m_keys_by_scancode[host_scan] = KeyEntry{base_key, host_scan, host_sym};
			PINFOF(LOG_V2, LOG_GUI, "base_key='%s' (%d), scancode='%s' (%d)\n",
					base_sym.c_str(), base_key, host_sym.c_str(), host_scan);
		} else {
			PERRF(LOG_GUI, "line %d: unknown host key name '%s'\n", linec, host_sym.c_str());
			continue;
		}
	}

	PINFOF(LOG_V1, LOG_GUI, "Loaded %d symbols\n",
			m_keys_by_keycode.size() + m_keys_by_scancode.size());
	keymapFile.close();
}

KeyEntry *Keymap::find_host_key(uint32_t _key_code, uint32_t _scan_code)
{
	// keycode table takes precedence
	auto code = m_keys_by_keycode.find(_key_code);
	if(code == m_keys_by_keycode.end()) {
		auto scan = m_keys_by_scancode.find(_scan_code);
		if(scan != m_keys_by_scancode.end()) {
			PDEBUGF(LOG_V2, LOG_GUI, "key 0x%x matched scancodes table\n", _scan_code);
			return &scan->second;
		}
	} else {
		PDEBUGF(LOG_V2, LOG_GUI, "key 0x%x matched keycodes table\n", _key_code);
		return &code->second;
	}
	PDEBUGF(LOG_V0, LOG_GUI, "key 0x%x / 0x%x is unmapped\n", _key_code, _scan_code);
	return nullptr;
}

int Keymap::parse_next_line(std::ifstream &_fp, int &_linec,
		std::string &basesym_, std::string  &hostsym_)
{
	std::vector<std::string> tokens;
	
	while(true) {
		std::string line;
		std::getline(_fp, line);
		if(!_fp.good()) {
			return -1;  // EOF
		}
		_linec++;
		
		tokens = str_parse_tokens(line, "\\s+");
		if(tokens.empty() || (tokens.size() == 1 && tokens[0].empty())) {
			// nothing but spaces until end of line
			continue;
		}
		if(tokens[0][0] == '#') {
			// nothing but a comment
			continue;
		}
		if(tokens.size() != 2) {
			PERRF(LOG_GUI, "keymap line %d: expected 2 columns, found %d instead\n",
					_linec, tokens.size());
			throw std::exception();
		}
		
		break;
	}
	
	basesym_ = tokens[0];
	hostsym_ = tokens[1];
	
	return 0;
}

uint32_t Keymap::convert_string_to_key(std::map<std::string, uint32_t> &_dictionary,
		const std::string &_string)
{
	auto key = _dictionary.find(_string);
	if(key == _dictionary.end()) {
		return KEYMAP_UNKNOWN;
	}
	return key->second;
}
