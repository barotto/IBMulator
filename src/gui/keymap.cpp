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

#include "ibmulator.h"
#include "program.h"
#include "keys.h"
#include "keymap.h"
#include <SDL2/SDL.h>

Keymap g_keymap;

// Table of "KEY_*" symbols
// the table must be in KEY_* order
const char *g_key_symbol[KEY_NBKEYS] = {
  "KEY_CTRL_L",         "KEY_SHIFT_L",        "KEY_F1",
  "KEY_F2",             "KEY_F3",             "KEY_F4",
  "KEY_F5",             "KEY_F6",             "KEY_F7",
  "KEY_F8",             "KEY_F9",             "KEY_F10",
  "KEY_F11",            "KEY_F12",            "KEY_CTRL_R",
  "KEY_SHIFT_R",        "KEY_CAPS_LOCK",      "KEY_NUM_LOCK",
  "KEY_ALT_L",          "KEY_ALT_R",          "KEY_A",
  "KEY_B",              "KEY_C",              "KEY_D",
  "KEY_E",              "KEY_F",              "KEY_G",
  "KEY_H",              "KEY_I",              "KEY_J",
  "KEY_K",              "KEY_L",              "KEY_M",
  "KEY_N",              "KEY_O",              "KEY_P",
  "KEY_Q",              "KEY_R",              "KEY_S",
  "KEY_T",              "KEY_U",              "KEY_V",
  "KEY_W",              "KEY_X",              "KEY_Y",
  "KEY_Z",              "KEY_0",              "KEY_1",
  "KEY_2",              "KEY_3",              "KEY_4",
  "KEY_5",              "KEY_6",              "KEY_7",
  "KEY_8",              "KEY_9",              "KEY_ESC",
  "KEY_SPACE",          "KEY_SINGLE_QUOTE",   "KEY_COMMA",
  "KEY_PERIOD",         "KEY_SLASH",          "KEY_SEMICOLON",
  "KEY_EQUALS",         "KEY_LEFT_BRACKET",   "KEY_BACKSLASH",
  "KEY_RIGHT_BRACKET",  "KEY_MINUS",          "KEY_GRAVE",
  "KEY_BACKSPACE",      "KEY_ENTER",          "KEY_TAB",
  "KEY_LEFT_BACKSLASH", "KEY_PRINT",          "KEY_SCRL_LOCK",
  "KEY_PAUSE",          "KEY_INSERT",         "KEY_DELETE",
  "KEY_HOME",           "KEY_END",            "KEY_PAGE_UP",
  "KEY_PAGE_DOWN",      "KEY_KP_ADD",         "KEY_KP_SUBTRACT",
  "KEY_KP_END",         "KEY_KP_DOWN",        "KEY_KP_PAGE_DOWN",
  "KEY_KP_LEFT",        "KEY_KP_RIGHT",       "KEY_KP_HOME",
  "KEY_KP_UP",          "KEY_KP_PAGE_UP",     "KEY_KP_INSERT",
  "KEY_KP_DELETE",      "KEY_KP_5",           "KEY_UP",
  "KEY_DOWN",           "KEY_LEFT",           "KEY_RIGHT",
  "KEY_KP_ENTER",       "KEY_KP_MULTIPLY",    "KEY_KP_DIVIDE",
  "KEY_WIN_L",          "KEY_WIN_R",          "KEY_MENU",
  "KEY_ALT_SYSREQ",     "KEY_CTRL_BREAK",     "KEY_INT_BACK",
  "KEY_INT_FORWARD",    "KEY_INT_STOP",       "KEY_INT_MAIL",
  "KEY_INT_SEARCH",     "KEY_INT_FAV",        "KEY_INT_HOME",
  "KEY_POWER_MYCOMP",   "KEY_POWER_CALC",     "KEY_POWER_SLEEP",
  "KEY_POWER_POWER",    "KEY_POWER_WAKE",
};

/// key mapping code for SDL

#define DEF_SDL_KEY(key) \
  { #key, key },

std::map<std::string, uint32_t> sdl_keytable = {
	// this include provides all the entries.
	#include "sdlkeys.h"
};

// function to convert key names into SDLKey values.
static uint32_t convertStringToSDLKey(const char *string)
{
	auto key = sdl_keytable.find(string);
	if(key == sdl_keytable.end()) {
		return KEYMAP_UNKNOWN;
	}
	return key->second;
}


Keymap::Keymap(void)
:
m_keymapTable(nullptr),
m_keymapCount(0),
m_lineptr(nullptr)
{

}

Keymap::~Keymap(void)
{
	if(m_keymapTable != nullptr) {
		free(m_keymapTable);
		m_keymapTable = (KeyEntry *)nullptr;
	}
	m_keymapCount = 0;
}

bool Keymap::is_loaded()
{
	return (m_keymapCount > 0);
}

void Keymap::init_parse()
{
	m_lineCount = 0;
}

void Keymap::init_parse_line(char *_line_to_parse)
{
	// chop off newline
	m_lineptr = (unsigned char *)_line_to_parse;
	char *nl;
	if ((nl = strchr(_line_to_parse,'\n')) != nullptr) {
		*nl = 0;
	}
}

int Keymap::get_next_word(char *_output)
{
	char *copyp = _output;
	// find first nonspace
	while (*m_lineptr && isspace(*m_lineptr))
		m_lineptr++;
	if (!*m_lineptr)
		return -1;  // nothing but spaces until end of line
	if (*m_lineptr == '#')
		return -1;  // nothing but a comment
	// copy nonspaces into the _output
	while (*m_lineptr && !isspace(*m_lineptr))
		*copyp++ = *m_lineptr++;
	*copyp = 0;  // null terminate the copy
	// there must be at least one nonspace, since that's why we stopped the
	// first loop!
	assert(copyp != _output);
	return 0;
}

int Keymap::get_next_keymap_line(std::ifstream &_fp, char *_sym, char *_modsym,
		int32_t *_ascii, char *_hostsym)
{
	char line[256];
	char buf[256];
	line[0] = 0;
	while (1) {
		m_lineCount++;
		_fp.getline(line, sizeof(line)-1);
		if(!_fp.good())
			return -1;  // EOF
		init_parse_line(line);
		if(get_next_word(_sym) >= 0) {
			_modsym[0] = 0;
			char *p;
			p = strchr(_sym, '+');
			if(p != nullptr) {
				*p = 0;  // truncate _sym.
				p++;  // move one char beyond the +
				strcpy(_modsym, p);  // copy the rest to _modsym
			}
			if(get_next_word(buf) < 0) {
				PERRF(LOG_GUI, "keymap line %d: expected 3 columns\n", m_lineCount);
				throw std::exception();
			}
			if(buf[0] == '\'' && buf[2] == '\'' && buf[3]==0) {
				*_ascii = (uint8_t) buf[1];
			} else if (!strcmp(buf, "space")) {
				*_ascii = ' ';
			} else if (!strcmp(buf, "return")) {
				*_ascii = '\n';
			} else if (!strcmp(buf, "tab")) {
				*_ascii = '\t';
			} else if (!strcmp(buf, "backslash")) {
				*_ascii = '\\';
			} else if (!strcmp(buf, "apostrophe")) {
				*_ascii = '\'';
			} else if (!strcmp(buf, "none")) {
				*_ascii = -1;
			} else {
				PERRF(LOG_GUI,"keymap line %d: ascii equivalent is \"%s\" but it must be char constant like 'x', or one of space,tab,return,none\n", m_lineCount, buf);
				throw std::exception();
			}
			if(get_next_word(_hostsym) < 0) {
				PERRF(LOG_GUI,"keymap line %d: expected 3 columns\n", m_lineCount);
				throw std::exception();
			}
			return 0;
		}
		// no words on this line, keep reading.
	}
	return 0; //keep compiler happy;
}

void Keymap::load(const std::string &_filename)
{
	char baseSym[256], modSym[256], hostSym[256];
	int32_t ascii = 0;
	uint32_t baseKey, modKey, hostKey;

	std::ifstream keymapFile(_filename.c_str(), std::ios::in);
	if(!keymapFile.is_open()) {
		PERRF(LOG_MACHINE,"Unable to open keymap file '%s'\n", _filename.c_str());
		throw std::exception();
	}

	PINFOF(LOG_V0, LOG_GUI,"Loading keymap from '%s'\n",_filename.c_str());
	init_parse();

	// Read keymap file one line at a time
	while(true) {
		if(get_next_keymap_line(keymapFile, baseSym, modSym, &ascii, hostSym) < 0) {
			//EOF
			break;
		}

		// convert KEY_* symbols to values
		baseKey = convert_string_to_key(baseSym);
		modKey = convert_string_to_key(modSym);
		hostKey = convertStringToSDLKey(hostSym);

		if(hostKey == KEYMAP_UNKNOWN) {
			//maybe it's a hex value
			char* end;
			hostKey = strtol(hostSym, &end, 0);
			if(end <= hostSym) {
				hostKey = KEYMAP_UNKNOWN;
			}
		}

		PDEBUGF(LOG_V2, LOG_GUI,
				"baseKey='%s' (%d), modSym='%s' (%d), ascii=%d, guisym='%s' (%d)\n",
				baseSym, baseKey, modSym, modKey, ascii, hostSym, hostKey);

		// Check if data is valid
		if(baseKey == KEYMAP_UNKNOWN) {
			PERRF(LOG_GUI,"line %d: unknown KEY constant '%s'\n", m_lineCount, baseSym);
			throw std::exception();
		}

		if(hostKey == KEYMAP_UNKNOWN) {
			PERRF(LOG_GUI,"line %d: unknown host key name '%s' (wrong keymap ?)\n",
					m_lineCount, hostSym);
			throw std::exception();
		}

		m_keymapTable = (KeyEntry*)realloc(m_keymapTable,(m_keymapCount+1) * sizeof(KeyEntry));

		if(m_keymapTable == nullptr) {
			PERRF(LOG_GUI,"Can not allocate memory for keymap table\n");
			throw std::exception();
		}

		m_keymapTable[m_keymapCount].baseKey = baseKey;
		m_keymapTable[m_keymapCount].modKey = modKey;
		m_keymapTable[m_keymapCount].ascii = ascii;
		m_keymapTable[m_keymapCount].hostKey = hostKey;

		m_keymapCount++;
	}

	PINFOF(LOG_V1,LOG_GUI,"Loaded %d symbols\n",m_keymapCount);
	keymapFile.close();
}

uint32_t Keymap::convert_string_to_key(const char* _string)
{
	// We look through the g_key_symbol table to find the searched string
	for(uint i=0; i<KEY_NBKEYS; i++) {
		if(strcmp(_string, g_key_symbol[i])==0) {
			return i;
		}
	}
	// Key is not known
	return KEYMAP_UNKNOWN;
}

KeyEntry *Keymap::find_host_key(uint32_t _key)
{
	// We look through the keymap table to find the searched key
	for(uint i=0; i<m_keymapCount; i++) {
		if(m_keymapTable[i].hostKey == _key) {
			PDEBUGF(LOG_V2,LOG_GUI,"key 0x%02x matches hostKey for entry #%d\n", _key, i);
			return &m_keymapTable[i];
		}
	}
	PDEBUGF(LOG_V0,LOG_GUI,"key %02x matches no entries\n", _key);

	// Return default
	return nullptr;
}

KeyEntry *Keymap::find_ascii_char(uint8_t _ch)
{
	PDEBUGF(LOG_V2,LOG_GUI,"find_ascii_char (0x%02x)\n", _ch);

	// We look through the keymap table to find the searched key
	for(uint i=0; i<m_keymapCount; i++) {
		if(m_keymapTable[i].ascii == _ch) {
			PDEBUGF(LOG_V2,LOG_GUI,"key %02x matches ascii for entry #%d\n", _ch, i);
			return &m_keymapTable[i];
		}
	}
	PDEBUGF(LOG_V0,LOG_GUI,"key 0x%02x matches no entries\n", _ch);

	// Return default
	return nullptr;
}

const char *Keymap::get_key_name(uint32_t _key)
{
	return g_key_symbol[_key & 0x7fffffff];
}


