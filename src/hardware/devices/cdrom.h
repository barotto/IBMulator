/*
 * Copyright (C) 2017  Marco Bortolin
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
/*
 * This class is a STUB.
 */

#ifndef IBMULATOR_HW_CDROMDRIVE_H
#define IBMULATOR_HW_CDROMDRIVE_H

#include "storagedev.h"


class CDROMDrive : public StorageDev
{
public:
	CDROMDrive() {}
	~CDROMDrive() {}

	bool read_toc(uint8_t */*buf*/, int */*length*/, bool /*msf*/, int /*start_track*/, int /*format*/)
		{ return false; }
};

#endif

