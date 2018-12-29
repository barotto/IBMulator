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
#include "vga_attrctrl.h"

// remove this with C++17
constexpr const std::array<const char*, ATTC_REGCOUNT> VGA_AttrCtrl::regnames;
constexpr const std::array<std::array<uint8_t,ATTC_REGCOUNT>,0x14> VGA_AttrCtrl::modes;

uint8_t VGA_AttrCtrl::get_register(uint8_t _index) const
{
	switch(_index) {
		case 0x00: case 0x01: case 0x02: case 0x03:
		case 0x04: case 0x05: case 0x06: case 0x07:
		case 0x08: case 0x09: case 0x0a: case 0x0b:
		case 0x0c: case 0x0d: case 0x0e: case 0x0f
		                   : return palette[_index];
		case ATTC_ATTMODE  : return attr_mode;
		case ATTC_OVERSCAN : return overscan_color;
		case ATTC_COLPLANE : return color_plane_enable;
		case ATTC_HPELPAN  : return horiz_pel_panning;
		case ATTC_COLSEL   : return color_select;
	}
	return 0;
}

void VGA_AttrCtrl::set_register(uint8_t _index, uint8_t _v)
{
	switch(_index) {
		case 0x00: case 0x01: case 0x02: case 0x03:
		case 0x04: case 0x05: case 0x06: case 0x07:
		case 0x08: case 0x09: case 0x0a: case 0x0b:
		case 0x0c: case 0x0d: case 0x0e: case 0x0f
		                   : palette[_index]    = _v; return;
		case ATTC_ATTMODE  : attr_mode          = _v; return;
		case ATTC_OVERSCAN : overscan_color     = _v; return;
		case ATTC_COLPLANE : color_plane_enable = _v; return;
		case ATTC_HPELPAN  : horiz_pel_panning  = _v; return;
		case ATTC_COLSEL   : color_select       = _v; return;
	}
}

void VGA_AttrCtrl::set_registers(const std::array<uint8_t,ATTC_REGCOUNT> _regs)
{
	for(int i=0; i<=0x0f; i++) {
		palette[i] = _regs[i];
	}
	attr_mode           = _regs[ATTC_ATTMODE ];
	overscan_color      = _regs[ATTC_OVERSCAN];
	color_plane_enable  = _regs[ATTC_COLPLANE];
	horiz_pel_panning   = _regs[ATTC_HPELPAN ];
	color_select        = _regs[ATTC_COLSEL  ];
}

std::array<uint8_t,ATTC_REGCOUNT> VGA_AttrCtrl::get_registers()
{
	std::array<uint8_t,ATTC_REGCOUNT> regs;
	for(int i=0; i<=0x0f; i++) {
		regs[i] = palette[i];
	}
	regs[ATTC_ATTMODE ] = attr_mode;
	regs[ATTC_OVERSCAN] = overscan_color;
	regs[ATTC_COLPLANE] = color_plane_enable;
	regs[ATTC_HPELPAN ] = horiz_pel_panning;
	regs[ATTC_COLSEL  ] = color_select;
	return regs;
}

const char * VGA_AttrCtrl::register_to_string(uint8_t _index) const
{
	thread_local static std::string s;
	s = regnames[_index%ATTC_REGCOUNT];
	switch(_index) {
		case ATTC_ATTMODE  : s += " ["; s += (const char*)attr_mode;          s += "]"; break;
		case ATTC_COLPLANE : s += " ["; s += (const char*)color_plane_enable; s += "]"; break;
		case ATTC_COLSEL   : s += " ["; s += (const char*)color_select;       s += "]"; break;
	}
	return s.c_str();
}

void VGA_AttrCtrl::registers_to_textfile(FILE *_file)
{
	for(int i=0; i<ATTC_REGCOUNT; i++) {
		fprintf(_file, "0x%02X 0x%02X  %s\n", i, get_register(i), register_to_string(i));
	}
}
