/*
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IBMULATOR_GUI_SYS_INTERFACE_H
#define IBMULATOR_GUI_SYS_INTERFACE_H

#include <SDL2/SDL.h>
#include <Rocket/Core/SystemInterface.h>
#include <Rocket/Core/Input.h>


class RocketSystemInterface : public Rocket::Core::SystemInterface
{
	static const char ascii_map[4][51];
	static const char keypad_map[2][18];

public:
    Rocket::Core::Input::KeyIdentifier TranslateKey(SDL_Keycode sdlkey);
    int TranslateMouseButton(Uint8 button);
	int GetKeyModifiers();
	float GetElapsedTime();
    bool LogMessage(Rocket::Core::Log::Type type, const Rocket::Core::String& message);

	static Rocket::Core::word GetCharacterCode(
			Rocket::Core::Input::KeyIdentifier key_identifier,
			int key_modifier_state);
};

#endif
