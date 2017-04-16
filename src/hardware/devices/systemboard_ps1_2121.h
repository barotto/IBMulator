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
class FloppyCtrl;

class SystemBoard_PS1_2121 : public SystemBoard
{
	IODEVICE(SystemBoard_PS1_2121, "PS/1 2121 System Board")

private:
	struct {
		// Ports 0x00E0-0x00E1: Memory banks control
		//   32 banks of 512K each for a maximum of 16MB
		//   bit 7 = 1  on the last active bank
		uint8_t E0_addr;
		uint8_t E1_regs[32];
		// RAM configuration register at port 0E8h
		uint8_t E8;
		/* E8 value  E1 value           RAM installed
		 * 0000      0-7  11224444      4mb   (2mb expansion)
		 * 0001      0-4  11224         2.5mb (.5mb expansion)
		 * 0010      0-11 112244448888  6mb   (4mb expansion)
		 * 0011      0-3  1122          2mb   (no expansion)
		 * 0100      0-7  11224444      4mb
		 * 0101      0-4  11224         2.5mb
		 * 0110      0-11 112244448888  6mb
		 * 0111      0-3  1122          2mb
		 * 1000      0-5  114444        3mb
		 * 1001      0-2  114           1.5mb
		 * 1010      0-9  1144448888    5mb
		 * 1011      0-1  11            1mb
		 * 1100      0-5  114444        3mb
		 * 1101      0-2  114           1.5mb
		 * 1110      0-9  1144448888    5mb
		 * 1111      0-1  11            1mb
		 *
		 * bit 0-1 provides the memory card ID
		 * bit 2 is probably to select different timings
		 * if bit 4 is set to 1 then 1M of on board RAM is disabled.
		 */
	} m_s;

	FloppyCtrl *m_floppy;

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

private:
	void update_board_state();
	void set_memory_state();
};

#endif
