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

#ifndef IBMULATOR_TTS_FORMAT_H
#define IBMULATOR_TTS_FORMAT_H

class TTSFormat
{
public:
	std::string m_codepage;

public:
	TTSFormat() : m_codepage("437") {}
	TTSFormat(std::string _codepage) : m_codepage(_codepage) {}
	virtual ~TTSFormat() {}

	virtual int get_volume(int _volume) const { return std::clamp(_volume, -10, 10); }
	virtual int get_rate(int _rate) const { return std::clamp(_rate, -10, 10); }
	virtual int get_pitch(int _pitch) const { return std::clamp(_pitch, -10, 10); }

	virtual std::string fmt_value(std::string _text) const { return _text; }
	virtual std::string fmt_sentence(std::string _text) const { return _text; }
	virtual std::string fmt_volume(int, std::string _text) const { return _text; }
	virtual std::string fmt_rate(int, std::string _text) const { return _text; }
	virtual std::string fmt_pitch(int, std::string _text) const { return _text; }
	virtual std::string fmt_spell(std::string _text) const { return _text; }

	std::string convert(std::string _text) const;
};

#endif