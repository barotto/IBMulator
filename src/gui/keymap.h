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

#ifndef IBMULATOR_KEYMAP_H
#define IBMULATOR_KEYMAP_H

// In case of unknown symbol
#define KEYMAP_UNKNOWN   0xFFFFFFFF

// Structure of an element of the keymap table
struct KeyEntry
{
	uint32_t key;          // ibmulator key code
	uint32_t host_key;     // host key code
	std::string host_name; // host key name
};

class Keymap
{
private:
	std::map<uint32_t, KeyEntry> m_keys_by_keycode;
	std::map<uint32_t, KeyEntry> m_keys_by_scancode;

public:
	Keymap();
	~Keymap();

	void load(const std::string &_filename);
	KeyEntry *find_host_key(uint32_t _key_code, uint32_t _scan_code);

public:
	static std::map<std::string, uint32_t> ms_keycode_table;
	static std::map<std::string, uint32_t> ms_sdl_keycode_table;
	static std::map<std::string, uint32_t> ms_sdl_scancode_table;
	static std::map<uint32_t, std::string> ms_keycode_str_table;
	static std::map<uint32_t, std::string> ms_sdl_keycode_str_table;
	static std::map<uint32_t, std::string> ms_sdl_scancode_str_table;
	
private:
	int parse_next_line(std::ifstream &_fp, int &_linec, std::string &basesym_, std::string  &hostsym_);
	uint32_t convert_string_to_key(std::map<std::string, uint32_t> &_dictionary,
			const std::string &_string);
};

extern Keymap g_keymap;

#endif
