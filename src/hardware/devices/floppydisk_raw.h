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

#ifndef IBMULATOR_HW_FLOPPYDISK_RAW_H
#define IBMULATOR_HW_FLOPPYDISK_RAW_H

#include "floppydisk.h"

/*
 * FloppyDisk for the special case of standard IBM formatted disks to be used
 * by the FloppyCtrl_RAW implementation.
 * Shares almost all the code of flux FloppyDisk except for specific read/write 
 * sector data functions.
 */
class FloppyDisk_Raw : public FloppyDisk
{
public:
	FloppyDisk_Raw(const Properties &_props) :
		FloppyDisk(_props) {}

	bool track_is_formatted(int track, int head);
	void read_sector(uint8_t _c, uint8_t _h, uint8_t _s, uint8_t *buffer, uint32_t bytes);
	void write_sector(uint8_t _c, uint8_t _h, uint8_t _s, const uint8_t *buffer, uint32_t bytes);
};

#endif
