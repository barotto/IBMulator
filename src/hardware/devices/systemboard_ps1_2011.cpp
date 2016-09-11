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
		//POS 3
		SystemBoard::m_s.POS_3 = 0x0F;
		m_s.HDD_enabled = true;
		update_POS3_state();

		//POS 4
		SystemBoard::m_s.POS_4 = 0x1F;
		m_s.RAM_bank1_en = true;
		m_s.RAM_bank2_en = true;
		m_s.RAM_bank3_en = true;
		m_s.RAM_bank4_en = true;
		m_s.RAM_bank5_en = true;
		update_POS4_state();

		//POS 5
		SystemBoard::m_s.POS_5 = 0x0F;
		m_s.RAM_fast = true;
		update_POS5_state();
	}
}

void SystemBoard_PS1_2011::config_changed()
{
	SystemBoard::config_changed();

	m_COM_port = 1; // COM1 fixed on model 2011
	SystemBoard::m_s.COM_port = m_COM_port;

	update_state();
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

uint16_t SystemBoard_PS1_2011::read(uint16_t _address, unsigned _io_len)
{
	return SystemBoard::read(_address, _io_len);
}

void SystemBoard_PS1_2011::write(uint16_t _address, uint16_t _value, unsigned _io_len)
{
	switch(_address) {
		case 0x0102:
			_value |= 0x8; //the serial port is fixed 1 for model 2011
			SystemBoard::write(_address, _value, _io_len);
			break;
		case 0x0103:
			m_s.HDD_enabled = (_value >> 3) & 1;
			SystemBoard::write(_address, _value, _io_len);
			break;
		case 0x0104:
			m_s.RAM_bank1_en = (_value >> 0) & 1;
			m_s.RAM_bank2_en = (_value >> 1) & 1;
			m_s.RAM_bank3_en = (_value >> 2) & 1;
			m_s.RAM_bank4_en = (_value >> 3) & 1;
			m_s.RAM_bank5_en = (_value >> 4) & 1;
			SystemBoard::write(_address, _value, _io_len);
			break;
		case 0x0105:
			m_s.RAM_fast = (_value >> 3) & 1;
			SystemBoard::write(_address, _value, _io_len);
			break;
		default:
			SystemBoard::write(_address, _value, _io_len);
			break;
	}
}

