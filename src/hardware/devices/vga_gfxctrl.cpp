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

void VGA_GfxCtrl::write_data(uint8_t _value, uint8_t data_[4])
{
	switch (gfx_mode.WM) {
		case 0:
		{
			// Write Mode 0
			// Each memory map is written with the system data rotated by the count
			// in the Data Rotate register. If the set/reset function is enabled for a
			// specific map, that map receives the 8-bit value contained in the
			// Set/Reset register.
			const bool sr0 = set_reset.SR0;
			const bool sr1 = set_reset.SR1;
			const bool sr2 = set_reset.SR2;
			const bool sr3 = set_reset.SR3;
			const bool esr0 = enable_set_reset.ESR0;
			const bool esr1 = enable_set_reset.ESR1;
			const bool esr2 = enable_set_reset.ESR2;
			const bool esr3 = enable_set_reset.ESR3;
			// perform rotate on CPU data in case its needed
			if(data_rotate.ROTC) {
				_value = (_value >> data_rotate.ROTC) |
				         (_value << (8 - data_rotate.ROTC));
			}
			data_[0] = latch[0] & ~bitmask;
			data_[1] = latch[1] & ~bitmask;
			data_[2] = latch[2] & ~bitmask;
			data_[3] = latch[3] & ~bitmask;
			switch (data_rotate.FS) {
				case 0: // replace
					data_[0] |= (esr0 ? (sr0 ? bitmask : 0) : (_value & bitmask));
					data_[1] |= (esr1 ? (sr1 ? bitmask : 0) : (_value & bitmask));
					data_[2] |= (esr2 ? (sr2 ? bitmask : 0) : (_value & bitmask));
					data_[3] |= (esr3 ? (sr3 ? bitmask : 0) : (_value & bitmask));
					break;
				case 1: // AND
					data_[0] |= esr0 ? (sr0 ? (latch[0] & bitmask) : 0)
					                   : (_value & latch[0]) & bitmask;
					data_[1] |= esr1 ? (sr1 ? (latch[1] & bitmask) : 0)
					                   : (_value & latch[1]) & bitmask;
					data_[2] |= esr2 ? (sr2 ? (latch[2] & bitmask) : 0)
					                   : (_value & latch[2]) & bitmask;
					data_[3] |= esr3 ? (sr3 ? (latch[3] & bitmask) : 0)
					                   : (_value & latch[3]) & bitmask;
					break;
				case 2: // OR
					data_[0] |= esr0 ? (sr0 ? bitmask : (latch[0] & bitmask))
					                   : ((_value | latch[0]) & bitmask);
					data_[1] |= esr1 ? (sr1 ? bitmask : (latch[1] & bitmask))
					                   : ((_value | latch[1]) & bitmask);
					data_[2] |= esr2 ? (sr2 ? bitmask : (latch[2] & bitmask))
					                   : ((_value | latch[2]) & bitmask);
					data_[3] |= esr3 ? (sr3 ? bitmask : (latch[3] & bitmask))
					                   : ((_value | latch[3]) & bitmask);
					break;
				case 3: // XOR
					data_[0] |= esr0 ? (sr0 ? (~latch[0] & bitmask)
					                          : (latch[0] & bitmask))
					                   : (_value ^ latch[0]) & bitmask;
					data_[1] |= esr1 ? (sr1 ? (~latch[1] & bitmask)
					                          : (latch[1] & bitmask))
					                   : (_value ^ latch[1]) & bitmask;
					data_[2] |= esr2 ? (sr2 ? (~latch[2] & bitmask)
					                          : (latch[2] & bitmask))
					                   : (_value ^ latch[2]) & bitmask;
					data_[3] |= esr3 ? (sr3 ? (~latch[3] & bitmask)
					                          : (latch[3] & bitmask))
					                   : (_value ^ latch[3]) & bitmask;
					break;
			}
			break;
		}

		case 1:
		{
			// Write Mode 1
			// Each memory map is written with the contents of the system latches.
			// These latches are loaded by a system read operation.
			for(int i=0; i<4; i++) {
				data_[i] = latch[i];
			}
			break;
		}

		case 2:
		{
			// Write Mode 2
			// Memory map n (0 through 3) is filled with 8 bits of the value of data
			// bit n.
			const bool p0 = (_value & 1);
			const bool p1 = (_value & 2);
			const bool p2 = (_value & 4);
			const bool p3 = (_value & 8);
			data_[0] = latch[0] & ~bitmask;
			data_[1] = latch[1] & ~bitmask;
			data_[2] = latch[2] & ~bitmask;
			data_[3] = latch[3] & ~bitmask;
			switch (data_rotate.FS) {
				case 0: // write
					data_[0] |= p0 ? bitmask : 0;
					data_[1] |= p1 ? bitmask : 0;
					data_[2] |= p2 ? bitmask : 0;
					data_[3] |= p3 ? bitmask : 0;
					break;
				case 1: // AND
					data_[0] |= p0 ? (latch[0] & bitmask) : 0;
					data_[1] |= p1 ? (latch[1] & bitmask) : 0;
					data_[2] |= p2 ? (latch[2] & bitmask) : 0;
					data_[3] |= p3 ? (latch[3] & bitmask) : 0;
					break;
				case 2: // OR
					data_[0] |= p0 ? bitmask : (latch[0] & bitmask);
					data_[1] |= p1 ? bitmask : (latch[1] & bitmask);
					data_[2] |= p2 ? bitmask : (latch[2] & bitmask);
					data_[3] |= p3 ? bitmask : (latch[3] & bitmask);
					break;
				case 3: // XOR
					data_[0] |= p0 ? (~latch[0] & bitmask) : (latch[0] & bitmask);
					data_[1] |= p1 ? (~latch[1] & bitmask) : (latch[1] & bitmask);
					data_[2] |= p2 ? (~latch[2] & bitmask) : (latch[2] & bitmask);
					data_[3] |= p3 ? (~latch[3] & bitmask) : (latch[3] & bitmask);
					break;
			}
			break;
		}

		case 3:
		{
			// Write Mode 3
			// Each memory map is written with the 8-bit value contained in the
			// Set/Reset register for that map (the Enable Set/Reset register has no
			// effect). Rotated system data is ANDed with the Bit Mask register to
			// form an 8-bit value that performs the same function as the Bit Mask
			// register in write modes 0 and 2
			const uint8_t mask = bitmask & _value;
			const uint8_t v0 = set_reset.SR0 ? _value : 0;
			const uint8_t v1 = set_reset.SR1 ? _value : 0;
			const uint8_t v2 = set_reset.SR2 ? _value : 0;
			const uint8_t v3 = set_reset.SR3 ? _value : 0;

			// perform rotate on CPU data
			if(data_rotate.ROTC) {
				_value = (_value >> data_rotate.ROTC) |
				         (_value << (8 - data_rotate.ROTC));
			}
			data_[0] = latch[0] & ~mask;
			data_[1] = latch[1] & ~mask;
			data_[2] = latch[2] & ~mask;
			data_[3] = latch[3] & ~mask;

			_value &= mask;

			switch (data_rotate.FS) {
				case 0: // write
					data_[0] |= v0;
					data_[1] |= v1;
					data_[2] |= v2;
					data_[3] |= v3;
					break;
				case 1: // AND
					data_[0] |= v0 & latch[0];
					data_[1] |= v1 & latch[1];
					data_[2] |= v2 & latch[2];
					data_[3] |= v3 & latch[3];
					break;
				case 2: // OR
					data_[0] |= v0 | latch[0];
					data_[1] |= v1 | latch[1];
					data_[2] |= v2 | latch[2];
					data_[3] |= v3 | latch[3];
					break;
				case 3: // XOR
					data_[0] |= v0 ^ latch[0];
					data_[1] |= v1 ^ latch[1];
					data_[2] |= v2 ^ latch[2];
					data_[3] |= v3 ^ latch[3];
					break;
			}
			break;
		}
	}
}
