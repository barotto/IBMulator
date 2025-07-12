/*
 * Copyright (C) 2025  Marco Bortolin
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

#include "tts_format_msxml.h"
#include "utils.h"

std::string TTSFormat_MSXML::fmt_value(std::string _text) const
{
	str_replace_all(_text, "&", "&amp;"); // <-- keep it the first one
	str_replace_all(_text, "<", "&lt;");
	str_replace_all(_text, ">", "&gt;");
	str_replace_all(_text, "\"", "&quot;");
	return _text;
}

std::string TTSFormat_MSXML::fmt_sentence(std::string _text) const
{
	if(str_trim(_text).back() != '.') {
		_text += ".";
	}
	return _text;
}

std::string TTSFormat_MSXML::fmt_volume(int _vol, std::string _text) const
{
	// input values are -10 .. 0
	// convert to 0 .. 100
	// MSXML only allows lowering the volume set at the system level.
	// values are made up so don't copy this code!
	if(_vol == 0) {
		return _text;
	}
	_vol = int(lerp(0.0, 100.0, double(_vol + 10) / 10.0));
	return str_format("<volume level=\"%d\">%s</volume>", _vol, _text.c_str());
}

std::string TTSFormat_MSXML::fmt_rate(int _rate, std::string _text) const
{
	if(_rate == 0) {
		return _text;
	}
	return str_format("<rate absspeed=\"%d\">%s</rate>", _rate, _text.c_str());
}

std::string TTSFormat_MSXML::fmt_pitch(int _pitch, std::string _text) const
{
	if(_pitch == 0) {
		return _text;
	}
	return str_format("<pitch absmiddle=\"%d\">%s</pitch>", _pitch, _text.c_str());
}

std::string TTSFormat_MSXML::fmt_spell(std::string _text) const
{
	return "<spell>" + _text + "</spell>";
}
