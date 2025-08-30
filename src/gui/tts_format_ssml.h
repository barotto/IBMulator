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

#ifndef IBMULATOR_TTS_FORMAT_SSML_H
#define IBMULATOR_TTS_FORMAT_SSML_H

#include "tts_format.h"

class TTSFormat_SSML : public TTSFormat
{
	bool m_dot_required = false;

public:
	TTSFormat_SSML() : TTSFormat() {}
	TTSFormat_SSML(std::string _codepage, bool _dot_required = false)
		: TTSFormat(_codepage), m_dot_required(_dot_required) {}
	~TTSFormat_SSML() {}

	std::string fmt_value(std::string _text) const override;
	std::string fmt_sentence(std::string _text) const override;
	std::string fmt_volume(int, std::string _text) const override;
	std::string fmt_rate(int, std::string _text) const override;
	std::string fmt_pitch(int, std::string _text) const override;
	std::string fmt_spell(std::string _text) const override;
};

#endif