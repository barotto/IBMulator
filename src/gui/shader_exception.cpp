/*
 * Copyright (C) 2022  Marco Bortolin
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
#include "shader_exception.h"
#include "utils.h"

void ShaderLinkExc::log_print(unsigned _facility)
{
	PERRF(_facility, "Error linking program %d:\n", program());
	PERRF(_facility, " %s\n", what());
}

void ShaderCompileExc::log_print(unsigned _facility)
{
	PERRF(_facility, "Error compiling shader '%s'\n", progname());
	auto lines = str_parse_tokens(what(), "\n");
	if(line() > 0) {
		PERRF(_facility, " Line %d\n", line());
	}
	for(auto & line : lines) {
		PERRF(_facility, "  %s\n", line.c_str());
	}
	PERRF(_facility, " Source:\n");
	int l=1;
	for(auto & line : progsrc()) {
		PERRF(_facility, "  %d: %s", l++, line.c_str());
	}
}

void ShaderPresetExc::log_print(unsigned _facility)
{
	PERRF(_facility, "Error parsing preset '%s'\n", m_name.c_str());
	PERRF(_facility, " Line: %d\n", m_line);
	PERRF(_facility, " Source:\n");
	int l=1;
	for(auto & line : m_data) {
		PERRF(_facility, "  %d: %s", l++, line.c_str());
	}
}