/*
 * Copyright (C) 2018-2019  Marco Bortolin
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
#include "vga_gfxctrl.h"


// can remove with C++17?
constexpr const std::array<const char*, GFXC_REGCOUNT> VGA_GfxCtrl::regnames;
constexpr const std::array<std::array<uint8_t,GFXC_REGCOUNT>,0x14> VGA_GfxCtrl::modes;

uint8_t VGA_GfxCtrl::get_register(uint8_t _index) const
{
	switch(_index) {
		case GFXC_SET_RESET     : return set_reset;
		case GFXC_EN_SET_RESET  : return enable_set_reset;
		case GFXC_COL_COMPARE   : return color_compare;
		case GFXC_DATA_ROTATE   : return data_rotate;
		case GFXC_READ_MAP_SEL  : return read_map_select;
		case GFXC_GFX_MODE      : return gfx_mode;
		case GFXC_MISC          : return misc;
		case GFXC_COL_DONT_CARE : return color_dont_care;
		case GFXC_BIT_MASK      : return bitmask;
	}
	return 0;
}

void VGA_GfxCtrl::set_register(uint8_t _index, uint8_t _v)
{
	switch(_index) {
		case GFXC_SET_RESET     : set_reset        = _v; return;
		case GFXC_EN_SET_RESET  : enable_set_reset = _v; return;
		case GFXC_COL_COMPARE   : color_compare    = _v; return;
		case GFXC_DATA_ROTATE   : data_rotate      = _v; return;
		case GFXC_READ_MAP_SEL  : read_map_select  = _v; return;
		case GFXC_GFX_MODE      : gfx_mode         = _v; return;
		case GFXC_MISC          : misc             = _v; return;
		case GFXC_COL_DONT_CARE : color_dont_care  = _v; return;
		case GFXC_BIT_MASK      : bitmask          = _v; return;
	}
}

void VGA_GfxCtrl::set_registers(const std::array<uint8_t,GFXC_REGCOUNT> _regs)
{
	set_reset        = _regs[GFXC_SET_RESET    ];
	enable_set_reset = _regs[GFXC_EN_SET_RESET ];
	color_compare    = _regs[GFXC_COL_COMPARE  ];
	data_rotate      = _regs[GFXC_DATA_ROTATE  ];
	read_map_select  = _regs[GFXC_READ_MAP_SEL ];
	gfx_mode         = _regs[GFXC_GFX_MODE     ];
	misc             = _regs[GFXC_MISC         ];
	color_dont_care  = _regs[GFXC_COL_DONT_CARE];
	bitmask          = _regs[GFXC_BIT_MASK     ];
}

std::array<uint8_t,GFXC_REGCOUNT> VGA_GfxCtrl::get_registers()
{
	std::array<uint8_t,GFXC_REGCOUNT> regs;
	regs[GFXC_SET_RESET    ] = set_reset;
	regs[GFXC_EN_SET_RESET ] = enable_set_reset;
	regs[GFXC_COL_COMPARE  ] = color_compare;
	regs[GFXC_DATA_ROTATE  ] = data_rotate;
	regs[GFXC_READ_MAP_SEL ] = read_map_select;
	regs[GFXC_GFX_MODE     ] = gfx_mode;
	regs[GFXC_MISC         ] = misc;
	regs[GFXC_COL_DONT_CARE] = color_dont_care;
	regs[GFXC_BIT_MASK     ] = bitmask;
	return regs;
}

const char * VGA_GfxCtrl::register_to_string(uint8_t _index) const
{
	static std::string s;
	s = regnames[_index%GFXC_REGCOUNT];
	switch(_index) {
		case GFXC_SET_RESET     : s+=" ["; s+=(const char*)set_reset;        s+="]"; break;
		case GFXC_EN_SET_RESET  : s+=" ["; s+=(const char*)enable_set_reset; s+="]"; break;
		case GFXC_COL_COMPARE   : s+=" ["; s+=(const char*)color_compare;    s+="]"; break;
		case GFXC_DATA_ROTATE   : s+=" ["; s+=(const char*)data_rotate;      s+="]"; break;
		case GFXC_GFX_MODE      : s+=" ["; s+=(const char*)gfx_mode;         s+="]"; break;
		case GFXC_MISC          : s+=" ["; s+=(const char*)misc;             s+="]"; break;
		case GFXC_COL_DONT_CARE : s+=" ["; s+=(const char*)color_dont_care;  s+="]"; break;
	}
	return s.c_str();
}

void VGA_GfxCtrl::registers_to_textfile(FILE *_file)
{
	for(int i=0; i<GFXC_REGCOUNT; i++) {
		fprintf(_file, "0x%02X 0x%02X %*u  %s\n", i, get_register(i), 3, get_register(i), register_to_string(i));
	}
}

