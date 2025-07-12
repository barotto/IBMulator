/*
 * Copyright (C) 2015-2025  Marco Bortolin
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

#ifndef IBMULATOR_GUI_SYS_INTERFACE_H
#define IBMULATOR_GUI_SYS_INTERFACE_H

#include <SDL.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/Input.h>


class RmlSystemInterface : public Rml::SystemInterface
{
	static const char ascii_map[4][51];
	static const char keypad_map[2][18];

public:

	/// Get the number of seconds elapsed since the start of the application.
	/// @return Elapsed time, in seconds.
	double GetElapsedTime();
	
	/// Log the specified message.
	/// @param[in] type Type of log message, ERROR, WARNING, etc.
	/// @param[in] message Message to log.
	/// @return True to continue execution, false to break into the debugger.
	bool LogMessage(Rml::Log::Type _type, const std::string &_message);

	static char GetCharacterCode(
			Rml::Input::KeyIdentifier key_identifier,
			int key_modifier_state);

	Rml::Input::KeyIdentifier TranslateKey(SDL_Keycode sdlkey);
	int TranslateMouseButton(Uint8 button);
	int GetKeyModifiers(Uint16 _sdl_mods);
	int GetKeyModifiers();
};

#endif
