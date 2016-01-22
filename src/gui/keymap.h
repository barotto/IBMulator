/*
 * Copyright (C) 2015, 2016  Marco Bortolin
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
	uint32_t baseKey;  // base key
	uint32_t modKey;   // modifier key that must be held down
	int32_t ascii;     // ascii equivalent, if any
	uint32_t hostKey;  // value that the host's OS or library recognizes
};

class Keymap
{
private:
	uint32_t convert_string_to_key(const char *);

	KeyEntry *m_keymapTable;
	uint16_t  m_keymapCount;

	unsigned char *m_lineptr;
	int m_lineCount;

	void init_parse();
	void init_parse_line(char *_line_to_parse);
	int get_next_word(char *_output);
	int get_next_keymap_line(std::ifstream &_fp, char *_sym, char *_modsym,
			int32_t *_ascii, char *_hostsym);

public:
	Keymap();
	~Keymap();

	void load(const std::string &_filename);
	bool is_loaded();

	KeyEntry *find_host_key(uint32_t _hostkeynum);
	KeyEntry *find_ascii_char(uint8_t _ascii);
	const char *get_key_name(uint32_t _key);
};

extern Keymap g_keymap;

#endif
