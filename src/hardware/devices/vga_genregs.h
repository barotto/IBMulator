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

#include "utils.h"

enum VGA_GenRegsPOL {
	POL_340 = 0, // 340 line mode (not defined in specs)
	POL_400 = 1,
	POL_350 = 2,
	POL_480 = 3
};

enum VGA_GenRegsCS {
	CS_25MHZ = 0,
	CS_28MHZ = 1,
	CS_EXT   = 2,
};

struct VGA_GenRegs
{
	struct {
		uint8_t POL;  // Sync Polarity (6-7)
		uint8_t PAGE; // Select high 64k bank (5)
		uint8_t CS;   // Clock Select (2-3)
		uint8_t ERAM; // Enable RAM (1)
		uint8_t IOS;  // I/O Address Select (0)
		operator uint8_t() const {
			return
			((IOS  & 0x01) << 0) |
			((ERAM & 0x01) << 1) |
			((CS   & 0x03) << 2) |
			((PAGE & 0x01) << 5) |
			((POL  & 0x03) << 6);
		}
		void operator=(uint8_t _v) {
			IOS  = (_v >> 0) & 0x01;
			ERAM = (_v >> 1) & 0x01;
			CS   = (_v >> 2) & 0x03;
			PAGE = (_v >> 5) & 0x01;
			POL  = (_v >> 6) & 0x03;
		}
		operator const char*() const {
			return register_to_string((uint8_t)(*this),
			{{1,"IOS"},{1,"ERAM"},{2,"CS"},{1,"PAGE"},{1,"POL"}});
		}
	} misc_output;     // Miscellaneous Output

	bool video_enable; // Video Subsystem Enable

	void registers_to_textfile(FILE *_txtfile);
};
