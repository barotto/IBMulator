/*
 * Copyright (C) 2016  Marco Bortolin
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

#ifndef IBMULATOR_HW_STORAGECTRL_H
#define IBMULATOR_HW_STORAGECTRL_H

#include "hardware/iodevice.h"

#define HDC_CUSTOM_BIOS_IDX  1 // table index where to inject the custom hdd parameters
                               // using an index >44 confuses CONFIGUR.EXE


class StorageCtrl : public IODevice
{
	IODEVICE(StorageCtrl, "Storage Controller")

public:
	StorageCtrl(Devices *_dev);
	virtual ~StorageCtrl();

	virtual bool is_busy() const { return false; }
};

#endif

