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
	IODEVICE(SystemBoard_PS1_2011, "PS/1 2011 System Board")

private:
	// system board POS register 3:
	//   bit 3, HDD enabled / present
	// system board POS register 4:
	//   bit 0, Enable First 128KB Bank   000000 - O1FFFF
	//   bit 1, Enable Second 128KB Bank  020000 - 03FFFF
	//   bit 2, Enable Third 128KB Bank   040000 - 05FFFF
	//   bit 3, Enable Fourth 128KB Bank  060000 - 07FFFF
	//   bit 4, Enable Fifth 128KB Bank   080000 - 09FFFF
	//   bit 5-7, reserved, always 0
	// system board POS register 5:
	//   bit 3, memory timings (wait states?)
	void update_POS3_state();
	void update_POS4_state();
	void update_POS5_state();
	void reset_POS3_state();
	void reset_POS4_state();
	void reset_POS5_state();

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
