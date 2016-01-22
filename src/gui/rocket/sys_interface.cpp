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
#include "gui/gui.h"
#include <Rocket/Core.h>
#include "sys_interface.h"

Rocket::Core::Input::KeyIdentifier RocketSystemInterface::TranslateKey(SDL_Keycode sdlkey)
{
    using namespace Rocket::Core::Input;

    switch(sdlkey) {
        case SDLK_UNKNOWN:
            return KI_UNKNOWN;
            break;
        case SDLK_SPACE:
            return KI_SPACE;
            break;
        case SDLK_0:
            return KI_0;
            break;
        case SDLK_1:
            return KI_1;
            break;
        case SDLK_2:
            return KI_2;
            break;
        case SDLK_3:
            return KI_3;
            break;
        case SDLK_4:
            return KI_4;
            break;
        case SDLK_5:
            return KI_5;
            break;
        case SDLK_6:
            return KI_6;
            break;
        case SDLK_7:
            return KI_7;
            break;
        case SDLK_8:
            return KI_8;
            break;
        case SDLK_9:
            return KI_9;
            break;
        case SDLK_a:
            return KI_A;
            break;
        case SDLK_b:
            return KI_B;
            break;
        case SDLK_c:
            return KI_C;
            break;
        case SDLK_d:
            return KI_D;
            break;
        case SDLK_e:
            return KI_E;
            break;
        case SDLK_f:
            return KI_F;
            break;
        case SDLK_g:
            return KI_G;
            break;
        case SDLK_h:
            return KI_H;
            break;
        case SDLK_i:
            return KI_I;
            break;
        case SDLK_j:
            return KI_J;
            break;
        case SDLK_k:
            return KI_K;
            break;
        case SDLK_l:
            return KI_L;
            break;
        case SDLK_m:
            return KI_M;
            break;
        case SDLK_n:
            return KI_N;
            break;
        case SDLK_o:
            return KI_O;
            break;
        case SDLK_p:
            return KI_P;
            break;
        case SDLK_q:
            return KI_Q;
            break;
        case SDLK_r:
            return KI_R;
            break;
        case SDLK_s:
            return KI_S;
            break;
        case SDLK_t:
            return KI_T;
            break;
        case SDLK_u:
            return KI_U;
            break;
        case SDLK_v:
            return KI_V;
            break;
        case SDLK_w:
            return KI_W;
            break;
        case SDLK_x:
            return KI_X;
            break;
        case SDLK_y:
            return KI_Y;
            break;
        case SDLK_z:
            return KI_Z;
            break;
        case SDLK_SEMICOLON:
            return KI_OEM_1;
            break;
        case SDLK_PLUS:
            return KI_OEM_PLUS;
            break;
        case SDLK_COMMA:
            return KI_OEM_COMMA;
            break;
        case SDLK_MINUS:
            return KI_OEM_MINUS;
            break;
        case SDLK_PERIOD:
            return KI_OEM_PERIOD;
            break;
        case SDLK_SLASH:
            return KI_OEM_2;
            break;
        case SDLK_BACKQUOTE:
            return KI_OEM_3;
            break;
        case SDLK_LEFTBRACKET:
            return KI_OEM_4;
            break;
        case SDLK_BACKSLASH:
            return KI_OEM_5;
            break;
        case SDLK_RIGHTBRACKET:
            return KI_OEM_6;
            break;
        case SDLK_QUOTEDBL:
            return KI_OEM_7;
            break;
        case SDLK_KP_0:
            return KI_NUMPAD0;
            break;
        case SDLK_KP_1:
            return KI_NUMPAD1;
            break;
        case SDLK_KP_2:
            return KI_NUMPAD2;
            break;
        case SDLK_KP_3:
            return KI_NUMPAD3;
            break;
        case SDLK_KP_4:
            return KI_NUMPAD4;
            break;
        case SDLK_KP_5:
            return KI_NUMPAD5;
            break;
        case SDLK_KP_6:
            return KI_NUMPAD6;
            break;
        case SDLK_KP_7:
            return KI_NUMPAD7;
            break;
        case SDLK_KP_8:
            return KI_NUMPAD8;
            break;
        case SDLK_KP_9:
            return KI_NUMPAD9;
            break;
        case SDLK_KP_ENTER:
            return KI_NUMPADENTER;
            break;
        case SDLK_KP_MULTIPLY:
            return KI_MULTIPLY;
            break;
        case SDLK_KP_PLUS:
            return KI_ADD;
            break;
        case SDLK_KP_MINUS:
            return KI_SUBTRACT;
            break;
        case SDLK_KP_PERIOD:
            return KI_DECIMAL;
            break;
        case SDLK_KP_DIVIDE:
            return KI_DIVIDE;
            break;
        case SDLK_KP_EQUALS:
            return KI_OEM_NEC_EQUAL;
            break;
        case SDLK_BACKSPACE:
            return KI_BACK;
            break;
        case SDLK_TAB:
            return KI_TAB;
            break;
        case SDLK_CLEAR:
            return KI_CLEAR;
            break;
        case SDLK_RETURN:
            return KI_RETURN;
            break;
        case SDLK_PAUSE:
            return KI_PAUSE;
            break;
        case SDLK_CAPSLOCK:
            return KI_CAPITAL;
            break;
        case SDLK_PAGEUP:
            return KI_PRIOR;
            break;
        case SDLK_PAGEDOWN:
            return KI_NEXT;
            break;
        case SDLK_END:
            return KI_END;
            break;
        case SDLK_HOME:
            return KI_HOME;
            break;
        case SDLK_LEFT:
            return KI_LEFT;
            break;
        case SDLK_UP:
            return KI_UP;
            break;
        case SDLK_RIGHT:
            return KI_RIGHT;
            break;
        case SDLK_DOWN:
            return KI_DOWN;
            break;
        case SDLK_INSERT:
            return KI_INSERT;
            break;
        case SDLK_DELETE:
            return KI_DELETE;
            break;
        case SDLK_HELP:
            return KI_HELP;
            break;
        case SDLK_F1:
            return KI_F1;
            break;
        case SDLK_F2:
            return KI_F2;
            break;
        case SDLK_F3:
            return KI_F3;
            break;
        case SDLK_F4:
            return KI_F4;
            break;
        case SDLK_F5:
            return KI_F5;
            break;
        case SDLK_F6:
            return KI_F6;
            break;
        case SDLK_F7:
            return KI_F7;
            break;
        case SDLK_F8:
            return KI_F8;
            break;
        case SDLK_F9:
            return KI_F9;
            break;
        case SDLK_F10:
            return KI_F10;
            break;
        case SDLK_F11:
            return KI_F11;
            break;
        case SDLK_F12:
            return KI_F12;
            break;
        case SDLK_F13:
            return KI_F13;
            break;
        case SDLK_F14:
            return KI_F14;
            break;
        case SDLK_F15:
            return KI_F15;
            break;
        case SDLK_NUMLOCKCLEAR:
            return KI_NUMLOCK;
            break;
        case SDLK_SCROLLLOCK:
            return KI_SCROLL;
            break;
        case SDLK_LSHIFT:
            return KI_LSHIFT;
            break;
        case SDLK_RSHIFT:
            return KI_RSHIFT;
            break;
        case SDLK_LCTRL:
            return KI_LCONTROL;
            break;
        case SDLK_RCTRL:
            return KI_RCONTROL;
            break;
        case SDLK_LALT:
            return KI_LMENU;
            break;
        case SDLK_RALT:
            return KI_RMENU;
            break;
        case SDLK_LGUI:
            return KI_LMETA;
            break;
        case SDLK_RGUI:
            return KI_RMETA;
            break;
        /*case SDLK_LSUPER:
            return KI_LWIN;
            break;
        case SDLK_RSUPER:
            return KI_RWIN;
            break;*/
        default:
            break;
    }
    PDEBUGF(LOG_V2, LOG_GUI, "unknown key code: %d\n", sdlkey);
    return KI_UNKNOWN;
}

int RocketSystemInterface::TranslateMouseButton(Uint8 button)
{
    switch(button)
    {
        case SDL_BUTTON_LEFT:
            return 0;
        case SDL_BUTTON_RIGHT:
            return 1;
        case SDL_BUTTON_MIDDLE:
            return 2;
        default:
            return 3;
    }
}

int RocketSystemInterface::GetKeyModifiers()
{
    SDL_Keymod sdlMods = SDL_GetModState();

    int retval = 0;

    if(sdlMods & KMOD_CTRL)
        retval |= Rocket::Core::Input::KM_CTRL;

    if(sdlMods & KMOD_SHIFT)
        retval |= Rocket::Core::Input::KM_SHIFT;

    if(sdlMods & KMOD_ALT)
        retval |= Rocket::Core::Input::KM_ALT;

    return retval;
}

float RocketSystemInterface::GetElapsedTime()
{
	return SDL_GetTicks() / 1000;
}

bool RocketSystemInterface::LogMessage(Rocket::Core::Log::Type type, const Rocket::Core::String& message)
{
	int logpri = LOG_DEBUG;
	int verb = LOG_V0;
	switch(type)
	{
	case Rocket::Core::Log::LT_INFO:
	case Rocket::Core::Log::LT_ALWAYS:
	case Rocket::Core::Log::LT_ASSERT:
		logpri = LOG_INFO;
		verb = LOG_V1;
		break;
	case Rocket::Core::Log::LT_ERROR:
		logpri = LOG_ERROR;
		verb = LOG_V0;
		break;
	case Rocket::Core::Log::LT_WARNING:
		logpri = LOG_WARNING;
		verb = LOG_V1;
		break;
	case Rocket::Core::Log::LT_DEBUG:
		logpri = LOG_DEBUG;
		verb = LOG_V2;
		break;
    case Rocket::Core::Log::LT_MAX:
        break;
	};

	LOG(logpri,LOG_GUI,verb,"%s\n", message.CString());

	return true;
};



const char RocketSystemInterface::ascii_map[4][51] =
{
    // shift off and capslock off
    {
		0,
		' ',
		'0',
		'1',
		'2',
		'3',
		'4',
		'5',
		'6',
		'7',
		'8',
		'9',
		'a',
		'b',
		'c',
		'd',
		'e',
		'f',
		'g',
		'h',
		'i',
		'j',
		'k',
		'l',
		'm',
		'n',
		'o',
		'p',
		'q',
		'r',
		's',
		't',
		'u',
		'v',
		'w',
		'x',
		'y',
		'z',
		';',
		'=',
		',',
		'-',
		'.',
		'/',
		'`',
		'[',
		'\\',
		']',
		'\'',
		0,
		0
	},

	// shift on and capslock off
    {
		0,
		' ',
		')',
		'!',
		'@',
		'#',
		'$',
		'%',
		'^',
		'&',
		'*',
		'(',
		'A',
		'B',
		'C',
		'D',
		'E',
		'F',
		'G',
		'H',
		'I',
		'J',
		'K',
		'L',
		'M',
		'N',
		'O',
		'P',
		'Q',
		'R',
		'S',
		'T',
		'U',
		'V',
		'W',
		'X',
		'Y',
		'Z',
		':',
		'+',
		'<',
		'_',
		'>',
		'?',
		'~',
		'{',
		'|',
		'}',
		'"',
		0,
		0
	},

	// shift on and capslock on
    {
		0,
		' ',
		')',
		'!',
		'@',
		'#',
		'$',
		'%',
		'^',
		'&',
		'*',
		'(',
		'a',
		'b',
		'c',
		'd',
		'e',
		'f',
		'g',
		'h',
		'i',
		'j',
		'k',
		'l',
		'm',
		'n',
		'o',
		'p',
		'q',
		'r',
		's',
		't',
		'u',
		'v',
		'w',
		'x',
		'y',
		'z',
		':',
		'+',
		'<',
		'_',
		'>',
		'?',
		'~',
		'{',
		'|',
		'}',
		'"',
		0,
		0
	},

	// shift off and capslock on
    {
		0,
		' ',
		'1',
		'2',
		'3',
		'4',
		'5',
		'6',
		'7',
		'8',
		'9',
		'0',
		'A',
		'B',
		'C',
		'D',
		'E',
		'F',
		'G',
		'H',
		'I',
		'J',
		'K',
		'L',
		'M',
		'N',
		'O',
		'P',
		'Q',
		'R',
		'S',
		'T',
		'U',
		'V',
		'W',
		'X',
		'Y',
		'Z',
		';',
		'=',
		',',
		'-',
		'.',
		'/',
		'`',
		'[',
		'\\',
		']',
		'\'',
		0,
		0
	}
};

const char RocketSystemInterface::keypad_map[2][18] =
{
	{
		'0',
		'1',
		'2',
		'3',
		'4',
		'5',
		'6',
		'7',
		'8',
		'9',
		'\n',
		'*',
		'+',
		0,
		'-',
		'.',
		'/',
		'='
	},

	{
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		'\n',
		'*',
		'+',
		0,
		'-',
		0,
		'/',
		'='
	}
};

Rocket::Core::word RocketSystemInterface::GetCharacterCode(
		Rocket::Core::Input::KeyIdentifier key_identifier,
		int key_modifier_state)
{
	// Check if we have a keycode capable of generating characters on the main keyboard (ie, not on the numeric
	// keypad; that is dealt with below).
	if (key_identifier <= Rocket::Core::Input::KI_OEM_102)
	{
		// Get modifier states
		bool shift = (key_modifier_state & Rocket::Core::Input::KM_SHIFT) > 0;
		bool capslock = (key_modifier_state & Rocket::Core::Input::KM_CAPSLOCK) > 0;

		// Return character code based on identifier and modifiers
		if (shift && !capslock)
			return RocketSystemInterface::ascii_map[1][key_identifier];

		if (shift && capslock)
			return RocketSystemInterface::ascii_map[2][key_identifier];

		if (!shift && capslock)
			return RocketSystemInterface::ascii_map[3][key_identifier];

		return RocketSystemInterface::ascii_map[0][key_identifier];
	}

	// Check if we have a keycode from the numeric keypad.
	else if (key_identifier <= Rocket::Core::Input::KI_OEM_NEC_EQUAL)
	{
		if (key_modifier_state & Rocket::Core::Input::KM_NUMLOCK)
			return RocketSystemInterface::keypad_map[0][key_identifier - Rocket::Core::Input::KI_NUMPAD0];
		else
			return RocketSystemInterface::keypad_map[1][key_identifier - Rocket::Core::Input::KI_NUMPAD0];
	}

	else if (key_identifier == Rocket::Core::Input::KI_RETURN)
		return '\n';

	return 0;
}
