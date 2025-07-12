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

#include "tts_format_ssml.h"
#include "utils.h"

std::string TTSFormat_SSML::fmt_value(std::string _text) const
{
	str_replace_all(_text, "&", "&amp;"); // <-- keep it the first one!
	str_replace_all(_text, "<", "&lt;");
	str_replace_all(_text, ">", "&gt;");
	str_replace_all(_text, "\"", "&quot;");
	return _text;
}

std::string TTSFormat_SSML::fmt_sentence(std::string _text) const
{
	return "<s>" + _text + "</s>";
}

std::string TTSFormat_SSML::fmt_volume(int _vol, std::string _text) const
{
	// input values are -10 .. +10
	// convert to -90db .. +90db
	// values are made up so don't copy this code!
	if(_vol == 0) {
		return _text;
	}
	double db = lerp(-90.0, 90.0, double(_vol + 10) / 20.0);
	return str_format("<prosody volume=\"%+.2fdb\">%s</prosody>", db, _text.c_str());
}

std::string TTSFormat_SSML::fmt_rate(int _rate, std::string _text) const
{
	// input values are -10 .. +10
	// convert to percentage 30% .. 200%
	// values are made up so don't copy this code!
	if(_rate < 0) {
		_rate = int(lerp(30.0, 100.0, double(_rate + 10) / 10.0));
	} else if(_rate > 0) {
		_rate = int(lerp(100.0, 200.0, double(_rate) / 10.0));
	} else {
		return _text;
	}
	return str_format("<prosody rate=\"%d%%\">%s</prosody>", _rate, _text.c_str());
}

std::string TTSFormat_SSML::fmt_pitch(int _pitch, std::string _text) const
{
	// input values are -10 .. +10
	// convert to percentage -90% .. +150%
	// values are made up so don't copy this code!
	if(_pitch < 0) {
		_pitch = int(lerp(-90.0, 0.0, double(_pitch + 10) / 10.0));
	} else if(_pitch > 0) {
		_pitch = int(lerp(0.0, 200.0, double(_pitch) / 10.0));
	} else {
		return _text;
	}
	return str_format("<prosody pitch=\"%+d%%\">%s</prosody>", _pitch, _text.c_str());
}

std::string TTSFormat_SSML::fmt_spell(std::string _text) const
{
	return "<say-as interpret-as=\"characters\">" + _text + "</say-as>";
}
