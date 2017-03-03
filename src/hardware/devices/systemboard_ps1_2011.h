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

#ifndef IBMULATOR_HW_SYSTEM_BOARD_PS1_2011_H
#define IBMULATOR_HW_SYSTEM_BOARD_PS1_2011_H

#include "systemboard.h"

class SystemBoard_PS1_2011 : public SystemBoard
{
	IODEVICE(SystemBoard_PS1_2011, "System Board PS/1 2011")

private:
	struct {
		// POS register 3
		bool HDD_enabled; // bit 3, HDD enabled / present

		// POS register 4
		bool RAM_bank1_en; // bit 0, Enable First 128KB Bank   000000 - O1FFFF
		bool RAM_bank2_en; // bit 1, Enable Second 128KB Bank  020000 - 03FFFF
		bool RAM_bank3_en; // bit 2, Enable Third 128KB Bank   040000 - 05FFFF
		bool RAM_bank4_en; // bit 3, Enable Fourth 128KB Bank  060000 - 07FFFF
		bool RAM_bank5_en; // bit 4, Enable Fifth 128KB Bank   080000 - 09FFFF
		                   // bit 5-7, reserved, always 0

		// POS register 5
		bool RAM_fast; // bit 3, memory timings (wait states?)
	} m_s;

	void update_POS3_state();
	void update_POS4_state();
	void update_POS5_state();

	std::string debug_POS_decode(int _posreg, uint8_t _value);

public:
	SystemBoard_PS1_2011(Devices* _dev);
	~SystemBoard_PS1_2011() {}

	void install();
	void remove();

	void reset(unsigned type);
	void config_changed();
	void write(uint16_t _address, uint16_t _value, unsigned _io_len);

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
};

#endif
