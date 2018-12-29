/*
 * Copyright (C) 2018  Marco Bortolin
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
#include "vga_sequencer.h"


// can remove with C++17?
constexpr const std::array<const char*, SEQ_REGCOUNT> VGA_Sequencer::regnames;
constexpr const std::array<std::array<uint8_t,SEQ_REGCOUNT>,0x14> VGA_Sequencer::modes;

uint8_t VGA_Sequencer::get_register(uint8_t _index) const
{
	switch(_index) {
		case SEQ_RESET    : return reset;
		case SEQ_CLOCKING : return clocking;
		case SEQ_MAP_MASK : return map_mask;
		case SEQ_CHARMAP  : return char_map;
		case SEQ_MEM_MODE : return mem_mode;
	}
	return 0;
}

void VGA_Sequencer::set_register(uint8_t _index, uint8_t _v)
{
	switch(_index) {
		case SEQ_RESET    : reset    = _v; break;
		case SEQ_CLOCKING : clocking = _v; break;
		case SEQ_MAP_MASK : map_mask = _v; break;
		case SEQ_CHARMAP  : char_map = _v; break;
		case SEQ_MEM_MODE : mem_mode = _v; break;
	}
}

void VGA_Sequencer::set_registers(const std::array<uint8_t,SEQ_REGCOUNT> _regs)
{
	reset     = _regs[SEQ_RESET   ];
	clocking  = _regs[SEQ_CLOCKING];
	map_mask  = _regs[SEQ_MAP_MASK];
	char_map  = _regs[SEQ_CHARMAP ];
	mem_mode  = _regs[SEQ_MEM_MODE];
}

std::array<uint8_t,SEQ_REGCOUNT> VGA_Sequencer::get_registers()
{
	std::array<uint8_t,SEQ_REGCOUNT> regs;
	regs[SEQ_RESET   ] = reset;
	regs[SEQ_CLOCKING] = clocking;
	regs[SEQ_MAP_MASK] = map_mask;
	regs[SEQ_CHARMAP ] = char_map;
	regs[SEQ_MEM_MODE] = mem_mode;
	return regs;
}

const char * VGA_Sequencer::register_to_string(uint8_t _index) const
{
	static std::string s;
	s = regnames[_index%SEQ_REGCOUNT];
	switch(_index) {
		case SEQ_RESET    : s+=" ["; s+=(const char*)reset;    s+="]"; break;
		case SEQ_CLOCKING : s+=" ["; s+=(const char*)clocking; s+="]"; break;
		case SEQ_MAP_MASK : s+=" ["; s+=(const char*)map_mask; s+="]"; break;
		case SEQ_CHARMAP  : s+=" ["; s+=(const char*)char_map; s+="]"; break;
		case SEQ_MEM_MODE : s+=" ["; s+=(const char*)mem_mode; s+="]"; break;
	}
	return s.c_str();
}

void VGA_Sequencer::registers_to_textfile(FILE *_file)
{
	for(int i=0; i<SEQ_REGCOUNT; i++) {
		fprintf(_file, "0x%02X 0x%02X  %s\n", i, get_register(i), register_to_string(i));
	}
}
