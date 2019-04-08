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
#include "vga_genregs.h"


void VGA_GenRegs::registers_to_textfile(FILE *_file)
{
	fprintf(_file, "0x%02X %*u  Miscellaneous Output [%s]\n",
			(uint8_t)misc_output, 3, (uint8_t)misc_output, (const char*)misc_output);

	fprintf(_file, "0x%02X %*u  Video Subsystem Enable\n", video_enable, 3, video_enable);
}

