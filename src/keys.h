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

#ifndef IBMULATOR_KEYS_H
#define IBMULATOR_KEYS_H

#define KEY_PRESSED  0x00000000
#define KEY_RELEASED 0x80000000
#define KEY_UNHANDLED 0x10000000

enum class MouseButton {
	MOUSE_NOBTN  = 0,
	MOUSE_LEFT   = 1,
	MOUSE_RIGHT  = 2,
	MOUSE_CENTER = 3
};

enum Keys {
	KEY_NONE           = 0,
	
	KEY_CTRL_L         = 1,
	KEY_SHIFT_L        = 2,
	KEY_F1             = 3,
	KEY_F2             = 4,
	KEY_F3             = 5,
	KEY_F4             = 6,
	KEY_F5             = 7,
	KEY_F6             = 8,
	KEY_F7             = 9,
	KEY_F8             = 10,
	KEY_F9             = 11,
	KEY_F10            = 12,
	KEY_F11            = 13,
	KEY_F12            = 14,
	KEY_CTRL_R         = 15,
	KEY_SHIFT_R        = 16,
	KEY_CAPS_LOCK      = 17,
	KEY_NUM_LOCK       = 18,
	KEY_ALT_L          = 19,
	KEY_ALT_R          = 20,
	KEY_A              = 21,
	KEY_B              = 22,
	KEY_C              = 23,
	KEY_D              = 24,
	KEY_E              = 25,
	KEY_F              = 26,
	KEY_G              = 27,
	KEY_H              = 28,
	KEY_I              = 29,
	KEY_J              = 30,
	KEY_K              = 31,
	KEY_L              = 32,
	KEY_M              = 33,
	KEY_N              = 34,
	KEY_O              = 35,
	KEY_P              = 36,
	KEY_Q              = 37,
	KEY_R              = 38,
	KEY_S              = 39,
	KEY_T              = 40,
	KEY_U              = 41,
	KEY_V              = 42,
	KEY_W              = 43,
	KEY_X              = 44,
	KEY_Y              = 45,
	KEY_Z              = 46,
	KEY_0              = 47,
	KEY_1              = 48,
	KEY_2              = 49,
	KEY_3              = 50,
	KEY_4              = 51,
	KEY_5              = 52,
	KEY_6              = 53,
	KEY_7              = 54,
	KEY_8              = 55,
	KEY_9              = 56,
	KEY_ESC            = 57,
	KEY_SPACE          = 58,
	KEY_SINGLE_QUOTE   = 59,
	KEY_COMMA          = 60,
	KEY_PERIOD         = 61,
	KEY_SLASH          = 62,
	KEY_SEMICOLON      = 63,
	KEY_EQUALS         = 64,
	KEY_LEFT_BRACKET   = 65,
	KEY_BACKSLASH      = 66,
	KEY_RIGHT_BRACKET  = 67,
	KEY_MINUS          = 68,
	KEY_GRAVE          = 69,
	KEY_BACKSPACE      = 70,
	KEY_ENTER          = 71,
	KEY_TAB            = 72,
	KEY_LEFT_BACKSLASH = 73,
	KEY_PRINT          = 74,
	KEY_SCRL_LOCK      = 75,
	KEY_PAUSE          = 76,
	KEY_INSERT         = 77,
	KEY_DELETE         = 78,
	KEY_HOME           = 79,
	KEY_END            = 80,
	KEY_PAGE_UP        = 81,
	KEY_PAGE_DOWN      = 82,
	KEY_KP_ADD         = 83,
	KEY_KP_SUBTRACT    = 84,
	KEY_KP_END         = 85,
	KEY_KP_DOWN        = 86,
	KEY_KP_PAGE_DOWN   = 87,
	KEY_KP_LEFT        = 88,
	KEY_KP_RIGHT       = 89,
	KEY_KP_HOME        = 90,
	KEY_KP_UP          = 91,
	KEY_KP_PAGE_UP     = 92,
	KEY_KP_INSERT      = 93,
	KEY_KP_DELETE      = 94,
	KEY_KP_5           = 95,
	KEY_UP             = 96,
	KEY_DOWN           = 97,
	KEY_LEFT           = 98,
	KEY_RIGHT          = 99,
	KEY_KP_ENTER       = 100,
	KEY_KP_MULTIPLY    = 101,
	KEY_KP_DIVIDE      = 102,
	KEY_WIN_L          = 103,
	KEY_WIN_R          = 104,
	KEY_MENU           = 105,
	KEY_SYSREQ         = 106,
	KEY_BREAK          = 107,
	KEY_INT_BACK       = 108,
	KEY_INT_FORWARD    = 109,
	KEY_INT_STOP       = 110,
	KEY_INT_MAIL       = 111,
	KEY_INT_SEARCH     = 112,
	KEY_INT_FAV        = 113,
	KEY_INT_HOME       = 114,
	KEY_POWER_MYCOMP   = 115,
	KEY_POWER_CALC     = 116,
	KEY_POWER_SLEEP    = 117,
	KEY_POWER_POWER    = 118,
	KEY_POWER_WAKE     = 119,

	KEY_NBKEYS
};

#endif
