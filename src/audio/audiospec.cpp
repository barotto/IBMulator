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
#include "audiospec.h"
#include <sstream>

std::string AudioSpec::to_string() const
{
	std::stringstream ss;
	std::map<AudioFormat, std::string> formats = {
		{ AUDIO_FORMAT_U8, "8-bit unsigned" },
		{ AUDIO_FORMAT_S16, "16-bit signed" },
		{ AUDIO_FORMAT_F32, "32-bit float" }
	};
	ss << formats[format] << ", " << channels << " ch., " << rate << " Hz";
	return ss.str();
}
