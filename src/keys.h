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

#ifndef IBMULATOR_KEYS_H
#define IBMULATOR_KEYS_H

#define KEY_PRESSED  0x00000000
#define KEY_RELEASED 0x80000000
#define KEY_UNHANDLED 0x10000000

/* same as Bochs' BX_KEY_* but without the BX_ */
enum Keys {
	KEY_CTRL_L         = 0,
	KEY_SHIFT_L        = 1,
	KEY_F1             = 2,
	KEY_F2             = 3,
	KEY_F3             = 4,
	KEY_F4             = 5,
	KEY_F5             = 6,
	KEY_F6             = 7,
	KEY_F7             = 8,
	KEY_F8             = 9,
	KEY_F9             = 10,
	KEY_F10            = 11,
	KEY_F11            = 12,
	KEY_F12            = 13,
	KEY_CTRL_R         = 14,
	KEY_SHIFT_R        = 15,
	KEY_CAPS_LOCK      = 16,
	KEY_NUM_LOCK       = 17,
	KEY_ALT_L          = 18,
	KEY_ALT_R          = 19,
	KEY_A              = 20,
	KEY_B              = 21,
	KEY_C              = 22,
	KEY_D              = 23,
	KEY_E              = 24,
	KEY_F              = 25,
	KEY_G              = 26,
	KEY_H              = 27,
	KEY_I              = 28,
	KEY_J              = 29,
	KEY_K              = 30,
	KEY_L              = 31,
	KEY_M              = 32,
	KEY_N              = 33,
	KEY_O              = 34,
	KEY_P              = 35,
	KEY_Q              = 36,
	KEY_R              = 37,
	KEY_S              = 38,
	KEY_T              = 39,
	KEY_U              = 40,
	KEY_V              = 41,
	KEY_W              = 42,
	KEY_X              = 43,
	KEY_Y              = 44,
	KEY_Z              = 45,
	KEY_0              = 46,
	KEY_1              = 47,
	KEY_2              = 48,
	KEY_3              = 49,
	KEY_4              = 50,
	KEY_5              = 51,
	KEY_6              = 52,
	KEY_7              = 53,
	KEY_8              = 54,
	KEY_9              = 55,
	KEY_ESC            = 56,
	KEY_SPACE          = 57,
	KEY_SINGLE_QUOTE   = 58,
	KEY_COMMA          = 59,
	KEY_PERIOD         = 60,
	KEY_SLASH          = 61,
	KEY_SEMICOLON      = 62,
	KEY_EQUALS         = 63,
	KEY_LEFT_BRACKET   = 64,
	KEY_BACKSLASH      = 65,
	KEY_RIGHT_BRACKET  = 66,
	KEY_MINUS          = 67,
	KEY_GRAVE          = 68,
	KEY_BACKSPACE      = 69,
	KEY_ENTER          = 70,
	KEY_TAB            = 71,
	KEY_LEFT_BACKSLASH = 72,
	KEY_PRINT          = 73,
	KEY_SCRL_LOCK      = 74,
	KEY_PAUSE          = 75,
	KEY_INSERT         = 76,
	KEY_DELETE         = 77,
	KEY_HOME           = 78,
	KEY_END            = 79,
	KEY_PAGE_UP        = 80,
	KEY_PAGE_DOWN      = 81,
	KEY_KP_ADD         = 82,
	KEY_KP_SUBTRACT    = 83,
	KEY_KP_END         = 84,
	KEY_KP_DOWN        = 85,
	KEY_KP_PAGE_DOWN   = 86,
	KEY_KP_LEFT        = 87,
	KEY_KP_RIGHT       = 88,
	KEY_KP_HOME        = 89,
	KEY_KP_UP          = 90,
	KEY_KP_PAGE_UP     = 91,
	KEY_KP_INSERT      = 92,
	KEY_KP_DELETE      = 93,
	KEY_KP_5           = 94,
	KEY_UP             = 95,
	KEY_DOWN           = 96,
	KEY_LEFT           = 97,
	KEY_RIGHT          = 98,
	KEY_KP_ENTER       = 99,
	KEY_KP_MULTIPLY    = 100,
	KEY_KP_DIVIDE      = 101,
	KEY_WIN_L          = 102,
	KEY_WIN_R          = 103,
	KEY_MENU           = 104,
	KEY_SYSREQ         = 105,
	KEY_BREAK          = 106,
	KEY_INT_BACK       = 107,
	KEY_INT_FORWARD    = 108,
	KEY_INT_STOP       = 109,
	KEY_INT_MAIL       = 110,
	KEY_INT_SEARCH     = 111,
	KEY_INT_FAV        = 112,
	KEY_INT_HOME       = 113,
	KEY_POWER_MYCOMP   = 114,
	KEY_POWER_CALC     = 115,
	KEY_POWER_SLEEP    = 116,
	KEY_POWER_POWER    = 117,
	KEY_POWER_WAKE     = 118,

	KEY_NBKEYS
};

#endif
