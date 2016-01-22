/*
 * Copyright (C) 2015, 2016  Marco Bortolin
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

#ifndef IBMULATOR_HW_DEVICE_H
#define IBMULATOR_HW_DEVICE_H

#include "statebuf.h"

class IODevice
{
public:
	virtual ~IODevice() {}

	virtual void init() {}
	virtual void reset(uint /*_signal*/) {}
	virtual void power_off() {}
	virtual void config_changed() {}
	virtual uint16_t read(uint16_t /*_address*/, unsigned /*_io_len*/) { return ~0; }
	virtual void write(uint16_t /*_address*/, uint16_t /*_value*/, unsigned /*_io_len*/) {}
	virtual const char *get_name() { return "null device"; }

	virtual void save_state(StateBuf &) {  }
	virtual void restore_state(StateBuf &) {  }
};


#endif
