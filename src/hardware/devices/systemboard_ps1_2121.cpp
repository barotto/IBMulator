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
#include "systemboard_ps1_2121.h"
#include <cstring>

IODEVICE_PORTS(SystemBoard_PS1_2121) = {
	{ 0x0E0, 0x0E0, PORT_8BIT|PORT__W }, // Cfg reg address (IBM PS/1 CLOCK on Ralf Brown's PORTS.TXT)
	{ 0x0E1, 0x0E1, PORT_8BIT|PORT_RW }  // Cfg registers (IBM PS/1 CLOCK on Ralf Brown's PORTS.TXT)
};

SystemBoard_PS1_2121::SystemBoard_PS1_2121(Devices* _dev)
: SystemBoard(_dev)
{
	memset(&m_s, 0, sizeof(m_s));
}

void SystemBoard_PS1_2121::install()
{
	IODevice::install(&SystemBoard::ioports()->at(0), SystemBoard::ioports()->size());
	IODevice::install(&ioports()->at(0), ioports()->size());
}

void SystemBoard_PS1_2121::remove()
{
	IODevice::remove(&SystemBoard::ioports()->at(0), SystemBoard::ioports()->size());
	IODevice::remove(&ioports()->at(0), ioports()->size());
}

void SystemBoard_PS1_2121::reset(unsigned _signal)
{
	SystemBoard::reset(_signal);

	m_s.E0_addr = 0;

	if(_signal == MACHINE_POWER_ON || _signal == MACHINE_HARD_RESET) {
		memset(&m_s.E0_regs, 0, sizeof(m_s.E0_regs));
	}
}

void SystemBoard_PS1_2121::config_changed()
{
	SystemBoard::config_changed();
	update_state();
}

void SystemBoard_PS1_2121::save_state(StateBuf &_state)
{
	SystemBoard::save_state(_state);

	PINFOF(LOG_V1, LOG_MACHINE, "saving %s state\n", name());
	_state.write(&m_s, {sizeof(m_s), name()});
}

void SystemBoard_PS1_2121::restore_state(StateBuf &_state)
{
	SystemBoard::restore_state(_state);

	PINFOF(LOG_V1, LOG_MACHINE, "restoring %s state\n", name());
	_state.read(&m_s, {sizeof(m_s), name()});
}

void SystemBoard_PS1_2121::update_state()
{
	update_E0_state();
	SystemBoard::update_state();
}

void SystemBoard_PS1_2121::update_E0_state()
{
	//TODO RAM control

	/* reg0:bit0 = Enables the first 512KB of RAM
	 * reg1:bit0 = Enables 512-640KB (128KB) of RAM
	 */
}

uint16_t SystemBoard_PS1_2121::read(uint16_t _address, unsigned _io_len)
{
	uint8_t value = ~0;

	switch(_address) {
		case 0x00E1:
			value = m_s.E0_regs[m_s.E0_addr];
			break;
		default:
			value = SystemBoard::read(_address, _io_len);
			break;
	}

	return value;
}

void SystemBoard_PS1_2121::write(uint16_t _address, uint16_t _value, unsigned _io_len)
{
	switch(_address) {
		case 0x00E0:
			m_s.E0_addr = _value;
			break;
		case 0x00E1:
			m_s.E0_regs[m_s.E0_addr] = _value;
			PDEBUGF(LOG_V2, LOG_MACHINE, "PS/1 2121 port E1[%d] := 0x%02X\n", m_s.E0_addr, _value);
			if(!SystemBoard::m_s.board_enable) {
				update_E0_state();
			}
			break;
		case 0x0105:
			 /* bit 7 forced high or 128KB of RAM will be missed on cold boot */
			_value |= 0x80;
			SystemBoard::write(_address, _value, _io_len);
			break;
		default:
			SystemBoard::write(_address, _value, _io_len);
			break;
	}
}
