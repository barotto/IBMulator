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

#ifndef IBMULATOR_HW_HDC_H
#define IBMULATOR_HW_HDC_H

#include "hardware/iodevice.h"
#include "hdd.h"
#include <memory>

#define HDC_CUSTOM_BIOS_IDX  1 // table index where to inject the custom hdd parameters
                               // using an index >44 confuses CONFIGUR.EXE


class HardDiskCtrl : public IODevice
{
	IODEVICE(HardDiskCtrl, "Hard Disk Controller")

protected:
	HardDiskDrive m_disk;

public:
	HardDiskCtrl(Devices *_dev);
	~HardDiskCtrl();

	void install();
	void remove();
	void reset(unsigned type);
	void power_off();
	void config_changed();
	uint16_t read(uint16_t address, unsigned io_len);
	void write(uint16_t address, uint16_t value, unsigned io_len);

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

	virtual bool is_busy() const { return false; }
};

#endif

