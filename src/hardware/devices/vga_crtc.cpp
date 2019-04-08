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
#include "vga_crtc.h"

// can remove with C++17?
constexpr const std::array<const char*, CRTC_REGCOUNT> VGA_CRTC::regnames;
constexpr const std::array<std::array<uint8_t,CRTC_REGCOUNT>,0x14> VGA_CRTC::modes;


uint8_t VGA_CRTC::get_register(uint8_t _index) const
{
	switch(_index) {
		case CRTC_HTOTAL         : return htotal;
		case CRTC_HDISPLAY_END   : return hdisplay_end;
		case CRTC_START_HBLANK   : return start_hblank;
		case CRTC_END_HBLANK     : return end_hblank;
		case CRTC_START_HRETRACE : return start_hretrace;
		case CRTC_END_HRETRACE   : return end_hretrace;
		case CRTC_VTOTAL         : return vtotal;
		case CRTC_OVERFLOW       : return overflow;
		case CRTC_PRESET_ROW_SCAN: return preset_row_scan;
		case CRTC_MAX_SCANLINE   : return max_scanline;
		case CRTC_CURSOR_START   : return cursor_start;
		case CRTC_CURSOR_END     : return cursor_end;
		case CRTC_STARTADDR_HI   : return startaddr_hi;
		case CRTC_STARTADDR_LO   : return startaddr_lo;
		case CRTC_CURSOR_HI      : return cursor_hi;
		case CRTC_CURSOR_LO      : return cursor_lo;
		case CRTC_VRETRACE_START : return vretrace_start;
		case CRTC_VRETRACE_END   : return vretrace_end;
		case CRTC_VDISPLAY_END   : return vdisplay_end;
		case CRTC_OFFSET         : return offset;
		case CRTC_UNDERLINE      : return underline;
		case CRTC_START_VBLANK   : return start_vblank;
		case CRTC_END_VBLANK     : return end_vblank;
		case CRTC_MODE_CONTROL   : return mode_control;
		case CRTC_LINE_COMPARE   : return line_compare;
	}
	return 0;
}

void VGA_CRTC::set_register(uint8_t _index, uint8_t _v)
{
	switch(_index) {
	case CRTC_HTOTAL: //00
		htotal = _v;
		break;
	case CRTC_HDISPLAY_END: //01
		hdisplay_end = _v;
		break;
	case CRTC_START_HBLANK: //02
		start_hblank = _v;
		break;
	case CRTC_END_HBLANK: //03
		end_hblank = _v;
		latch_end_hblank();
		break;
	case CRTC_START_HRETRACE: //04
		start_hretrace = _v;
		break;
	case CRTC_END_HRETRACE: //05
		end_hretrace = _v;
		latch_end_hblank();
		break;
	case CRTC_VTOTAL: //06
		vtotal = _v;
		latch_vtotal();
		break;
	case CRTC_OVERFLOW: //07
		overflow = _v;
		latch_vretrace_start();
		latch_vdisplay_end();
		latch_vtotal();
		latch_line_compare();
		latch_start_vblank();
		break;
	case CRTC_PRESET_ROW_SCAN: //08
		preset_row_scan = _v;
		break;
	case CRTC_MAX_SCANLINE: //09
		max_scanline = _v;
		latch_line_compare();
		latch_start_vblank();
		break;
	case CRTC_CURSOR_START: //0A
		cursor_start = _v;
		break;
	case CRTC_CURSOR_END: //0B
		cursor_end = _v;
		break;
	case CRTC_STARTADDR_HI: //0C
		startaddr_hi = _v;
		// latched at vertical retrace
		start_address_modified = true;
		break;
	case CRTC_STARTADDR_LO: //0D
		startaddr_lo = _v;
		// latched at vertical retrace
		start_address_modified = true;
		break;
	case CRTC_CURSOR_HI: //0E
		cursor_hi = _v;
		latch_cursor_location();
		break;
	case CRTC_CURSOR_LO: //0F
		cursor_lo = _v;
		latch_cursor_location();
		break;
	case CRTC_VRETRACE_START: //10
		vretrace_start = _v;
		latch_vretrace_start();
		break;
	case CRTC_VRETRACE_END: //11
		vretrace_end = _v;
		break;
	case CRTC_VDISPLAY_END: //12
		vdisplay_end = _v;
		latch_vdisplay_end();
		break;
	case CRTC_OFFSET: //13
		offset = _v;
		latch_line_offset();
		break;
	case CRTC_UNDERLINE: //14
		underline = _v;
		latch_line_offset();
		break;
	case CRTC_START_VBLANK: //15
		start_vblank = _v;
		latch_start_vblank();
		break;
	case CRTC_END_VBLANK: //16
		end_vblank = _v;
		break;
	case CRTC_MODE_CONTROL: //17
		mode_control = _v;
		latch_line_offset();
		break;
	case CRTC_LINE_COMPARE: //18
		line_compare = _v;
		latch_line_compare();
		break;
	}
}

void VGA_CRTC::latch_line_offset()
{
	latches.line_offset = offset << 1;
	if(underline.DW) {
		latches.line_offset <<= 2;
	} else if(mode_control.WB == 0) {
		latches.line_offset <<= 1;
	}
}

void VGA_CRTC::latch_line_compare()
{
	latches.line_compare = (line_compare) | (overflow.LC8<<8) | (max_scanline.LC9<<9);
}

void VGA_CRTC::latch_vretrace_start()
{
	latches.vretrace_start = vretrace_start | (overflow.VRS8<<8) | (overflow.VRS9<<9);
}

void VGA_CRTC::latch_vdisplay_end()
{
	latches.vdisplay_end = (vdisplay_end) | (overflow.VDE8<<8) | (overflow.VDE9<<9);
}

void VGA_CRTC::latch_vtotal()
{
	latches.vtotal = vtotal | (overflow.VT8<<8) | (overflow.VT9<<9);
}

void VGA_CRTC::latch_end_hblank()
{
	latches.end_hblank = end_hblank.EB | (end_hretrace.EB5<<5);
}

void VGA_CRTC::latch_start_vblank()
{
	latches.start_vblank = start_vblank | (overflow.VBS8<<8) | (max_scanline.VBS9<<9);
}

void VGA_CRTC::latch_start_address()
{
	latches.start_address = (startaddr_hi << 8) | startaddr_lo;
	start_address_modified = false;
}

void VGA_CRTC::latch_cursor_location()
{
	latches.cursor_location = (cursor_hi << 8) | cursor_lo;
}

void VGA_CRTC::set_registers(const std::array<uint8_t,CRTC_REGCOUNT> _regs)
{
	htotal           = _regs[CRTC_HTOTAL         ];
	hdisplay_end     = _regs[CRTC_HDISPLAY_END   ];
	start_hblank     = _regs[CRTC_START_HBLANK   ];
	end_hblank       = _regs[CRTC_END_HBLANK     ];
	start_hretrace   = _regs[CRTC_START_HRETRACE ];
	end_hretrace     = _regs[CRTC_END_HRETRACE   ];
	vtotal           = _regs[CRTC_VTOTAL         ];
	overflow         = _regs[CRTC_OVERFLOW       ];
	preset_row_scan  = _regs[CRTC_PRESET_ROW_SCAN];
	max_scanline     = _regs[CRTC_MAX_SCANLINE   ];
	cursor_start     = _regs[CRTC_CURSOR_START   ];
	cursor_end       = _regs[CRTC_CURSOR_END     ];
	startaddr_hi     = _regs[CRTC_STARTADDR_HI   ];
	startaddr_lo     = _regs[CRTC_STARTADDR_LO   ];
	cursor_hi        = _regs[CRTC_CURSOR_HI      ];
	cursor_lo        = _regs[CRTC_CURSOR_LO      ];
	vretrace_start   = _regs[CRTC_VRETRACE_START ];
	vretrace_end     = _regs[CRTC_VRETRACE_END   ];
	vdisplay_end     = _regs[CRTC_VDISPLAY_END   ];
	offset           = _regs[CRTC_OFFSET         ];
	underline        = _regs[CRTC_UNDERLINE      ];
	start_vblank     = _regs[CRTC_START_VBLANK   ];
	end_vblank       = _regs[CRTC_END_VBLANK     ];
	mode_control     = _regs[CRTC_MODE_CONTROL   ];
	line_compare     = _regs[CRTC_LINE_COMPARE   ];

	latch_line_offset();
	latch_line_compare();
	latch_vretrace_start();
	latch_vdisplay_end();
	latch_vtotal();
	latch_end_hblank();
	latch_start_vblank();
	latch_start_address();
	latch_cursor_location();
}

std::array<uint8_t,CRTC_REGCOUNT> VGA_CRTC::get_registers()
{
	std::array<uint8_t,CRTC_REGCOUNT> regs;
	regs[CRTC_HTOTAL         ] = htotal;
	regs[CRTC_HDISPLAY_END   ] = hdisplay_end;
	regs[CRTC_START_HBLANK   ] = start_hblank;
	regs[CRTC_END_HBLANK     ] = end_hblank;
	regs[CRTC_START_HRETRACE ] = start_hretrace;
	regs[CRTC_END_HRETRACE   ] = end_hretrace;
	regs[CRTC_VTOTAL         ] = vtotal;
	regs[CRTC_OVERFLOW       ] = overflow;
	regs[CRTC_PRESET_ROW_SCAN] = preset_row_scan;
	regs[CRTC_MAX_SCANLINE   ] = max_scanline;
	regs[CRTC_CURSOR_START   ] = cursor_start;
	regs[CRTC_CURSOR_END     ] = cursor_end;
	regs[CRTC_STARTADDR_HI   ] = startaddr_hi;
	regs[CRTC_STARTADDR_LO   ] = startaddr_lo;
	regs[CRTC_CURSOR_HI      ] = cursor_hi;
	regs[CRTC_CURSOR_LO      ] = cursor_lo;
	regs[CRTC_VRETRACE_START ] = vretrace_start;
	regs[CRTC_VRETRACE_END   ] = vretrace_end;
	regs[CRTC_VDISPLAY_END   ] = vdisplay_end;
	regs[CRTC_OFFSET         ] = offset;
	regs[CRTC_UNDERLINE      ] = underline;
	regs[CRTC_START_VBLANK   ] = start_vblank;
	regs[CRTC_END_VBLANK     ] = end_vblank;
	regs[CRTC_MODE_CONTROL   ] = mode_control;
	regs[CRTC_LINE_COMPARE   ] = line_compare;
	return regs;
}

const char * VGA_CRTC::register_to_string(uint8_t _index) const
{
	static std::string s;
	s = regnames[_index%CRTC_REGCOUNT];
	switch(_index) {
		case CRTC_END_HBLANK      : s += " ["; s += (const char*)end_hblank;      s += "]"; break;
		case CRTC_END_HRETRACE    : s += " ["; s += (const char*)end_hretrace;    s += "]"; break;
		case CRTC_OVERFLOW        : s += " ["; s += (const char*)overflow;        s += "]"; break;
		case CRTC_PRESET_ROW_SCAN : s += " ["; s += (const char*)preset_row_scan; s += "]"; break;
		case CRTC_MAX_SCANLINE    : s += " ["; s += (const char*)max_scanline;    s += "]"; break;
		case CRTC_CURSOR_START    : s += " ["; s += (const char*)cursor_start;    s += "]"; break;
		case CRTC_CURSOR_END      : s += " ["; s += (const char*)cursor_end;      s += "]"; break;
		case CRTC_VRETRACE_END    : s += " ["; s += (const char*)vretrace_end;    s += "]"; break;
		case CRTC_UNDERLINE       : s += " ["; s += (const char*)underline;       s += "]"; break;
		case CRTC_MODE_CONTROL    : s += " ["; s += (const char*)mode_control;    s += "]"; break;
	}
	return s.c_str();
}

void VGA_CRTC::registers_to_textfile(FILE *_file)
{
	for(int i=0; i<CRTC_REGCOUNT; i++) {
		fprintf(_file, "0x%02X 0x%02X %*u  %s\n", i, get_register(i), 3, get_register(i), register_to_string(i));
	}
}
