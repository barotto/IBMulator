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

#ifndef IBMULATOR_HW_SYSTEM_BOARD_PS1_2121_H
#define IBMULATOR_HW_SYSTEM_BOARD_PS1_2121_H

#include "systemboard.h"

class SystemBoard_PS1_2121 : public SystemBoard
{
	IODEVICE(SystemBoard_PS1_2121, "System Board PS/1 2121")

private:
	struct {
		// Ports 0x00E0-0x00E1
		uint8_t E0_addr;
		uint8_t E0_regs[256];
	} m_s;

	void update_E0_state();
	void update_state();

public:
	SystemBoard_PS1_2121(Devices* _dev);
	~SystemBoard_PS1_2121() {}

	void install();
	void remove();

	void reset(unsigned type);
	void config_changed();
	uint16_t read(uint16_t _address, unsigned _io_len);
	void write(uint16_t _address, uint16_t _value, unsigned _io_len);

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
};

#endif
