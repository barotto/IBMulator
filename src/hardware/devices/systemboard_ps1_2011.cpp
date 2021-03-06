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

#include "ibmulator.h"
#include "machine.h"
#include "systemboard_ps1_2011.h"
#include "utils.h"
#include <cstring>

IODEVICE_PORTS(SystemBoard_PS1_2011) = {};

SystemBoard_PS1_2011::SystemBoard_PS1_2011(Devices* _dev)
: SystemBoard(_dev)
{
	memset(&m_s, 0, sizeof(m_s));
}

void SystemBoard_PS1_2011::install()
{
	IODevice::install(&SystemBoard::ioports()->at(0), SystemBoard::ioports()->size());
}

void SystemBoard_PS1_2011::remove()
{
	IODevice::remove(&SystemBoard::ioports()->at(0), SystemBoard::ioports()->size());
}

void SystemBoard_PS1_2011::reset(unsigned _signal)
{
	SystemBoard::reset(_signal);

	if(_signal == MACHINE_POWER_ON || _signal == MACHINE_HARD_RESET) {
		reset_POS3_state();
		reset_POS4_state();
		reset_POS5_state();
	}
}

void SystemBoard_PS1_2011::config_changed()
{
	SystemBoard::config_changed();

	m_COM_port = 1; // COM1 fixed on model 2011

	reset_board_state();
}

void SystemBoard_PS1_2011::save_state(StateBuf &_state)
{
	SystemBoard::save_state(_state);

	PINFOF(LOG_V1, LOG_MACHINE, "saving %s state\n", name());
	_state.write(&m_s, {sizeof(m_s), name()});
}

void SystemBoard_PS1_2011::restore_state(StateBuf &_state)
{
	SystemBoard::restore_state(_state);

	PINFOF(LOG_V1, LOG_MACHINE, "restoring %s state\n", name());
	_state.read(&m_s, {sizeof(m_s), name()});
}


void SystemBoard_PS1_2011::reset_POS3_state()
{
	SystemBoard::m_s.POS[3] = 0x0F;
	update_POS3_state();
}

void SystemBoard_PS1_2011::reset_POS4_state()
{
	SystemBoard::m_s.POS[4] = 0x1F;
	update_POS4_state();
}

void SystemBoard_PS1_2011::reset_POS5_state()
{
	SystemBoard::m_s.POS[5] = 0x0F;
	update_POS5_state();
}

void SystemBoard_PS1_2011::update_POS3_state()
{
	//TODO HDD presence
}

void SystemBoard_PS1_2011::update_POS4_state()
{
	//TODO RAM banks
}

void SystemBoard_PS1_2011::update_POS5_state()
{
	//TODO RAM timings
}

void SystemBoard_PS1_2011::write(uint16_t _address, uint16_t _value, unsigned _io_len)
{
	switch(_address) {
		case 0x0102:
			_value |= 0x8; //the serial port is fixed 1 for model 2011
			break;
		default:
			break;
	}
	SystemBoard::write(_address, _value, _io_len);
}

std::string SystemBoard_PS1_2011::debug_POS_decode(int _posreg, uint8_t _value)
{
	switch(_posreg) {
		case 3: {
			return bitfield_to_string(_value,
			{ "b0", "b1", "b2", "HDD_EN",  "b4", "b5", "b6", "b7" },
			{ "",   "",   "",   "HDD_DIS", "",   "",   "",   ""   });
		}
		case 4: {
			return bitfield_to_string(_value,
			{ "RAM_B1", "RAM_B2", "RAM_B3", "RAM_B4", "RAM_B5", "b5", "b6", "b7" },
			{ "",       "",       "",       "",       "",       "",   "",   ""   });
		}
		case 5: {
			return bitfield_to_string(_value,
			{ "b0", "b1", "b2", "RAM_WS1", "b4", "b5", "b6", "b7" },
			{ "",   "",   "",   "",         "",   "",   "",   ""  });
		}
	}
	return SystemBoard::debug_POS_decode(_posreg, _value);
}
