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
#include "tts_format.h"
#include "utils.h"

std::string TTSFormat::convert(std::string _text) const
{
	return str_convert(_text, m_codepage.c_str(), "UTF-8");
}

std::string TTSFormat::spell_symbols(std::string _text) const
{
	str_replace_all(_text, ":", " colon ");
	str_replace_all(_text, "/", " slash ");
	str_replace_all(_text, "\\", " back slash ");
	str_replace_all(_text, ".", " dot ");
	str_replace_all(_text, "-", " dash ");

	return _text;
}