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

#include "ibmulator.h"
#include "program.h"
#include "machine.h"
#include "systemboard.h"
#include "hardware/memory.h"
#include "hardware/devices/serial.h"
#include "hardware/devices/parallel.h"
#include "utils.h"
#include <cstring>

IODEVICE_PORTS(SystemBoard) = {
	{ 0x090, 0x090, PORT_8BIT|PORT_RW }, // Central Arbitration Control Port
	{ 0x091, 0x091, PORT_8BIT|PORT_R_ }, // Card Selected Feedback
	{ 0x092, 0x092, PORT_8BIT|PORT_RW }, // System Control Port A
	{ 0x094, 0x094, PORT_8BIT|PORT_RW }, // System Board Enable/Setup Register
	{ 0x096, 0x096, PORT_8BIT|PORT_RW }, // Adapter Enable/Setup Register
	{ 0x100, 0x101, PORT_8BIT|PORT_R_ }, // Programmable Option Select (Adapter ID)
	{ 0x102, 0x105, PORT_8BIT|PORT_RW }, // Programmable Option Select
	{ 0x190, 0x191, PORT_8BIT|PORT__W }  // POST procedure codes
};

SystemBoard::SystemBoard(Devices* _dev)
: IODevice(_dev)
{
	memset(&m_s, 0, sizeof(m_s));
}

void SystemBoard::reset(unsigned _signal)
{
	m_s.POST = 0;

	if(_signal == MACHINE_POWER_ON || _signal == MACHINE_HARD_RESET) {
		//System Board Enable/Setup Register:
		m_s.VGA_enable = true;
		m_s.board_enable = true;
		update_board_state();

		// POS 0 & 1
		m_s.adapter_ID = ~0;

		// board POS 2
		m_s.VGA_awake = true;
		m_s.POS2_bit1 = false;
		m_s.COM_enabled = true;
		m_s.COM_port = m_COM_port;
		m_s.LPT_enabled = true;
		m_s.LPT_port = m_LPT_port;
		m_s.LPT_mode = PARPORT_COMPATIBLE;
		update_POS2_state();

		// other POS regs
		m_s.POS_3 = m_s.POS_4 = m_s.POS_5 = ~0;

		//Card Select Feedback
		m_s.CSF = 0;
	}
}

void SystemBoard::config_changed()
{
	m_parallel = m_devices->device<Parallel>();
	m_serial = m_devices->device<Serial>();

	//TODO m_COM_port = g_program.config().get_enum(COM_SECTION, COM_PORT, Serial::ms_com_ports);
	m_COM_port = 1;
	m_LPT_port = g_program.config().get_enum(LPT_SECTION, LPT_PORT, Parallel::ms_lpt_ports);

	m_s.COM_port = m_COM_port;
	m_s.LPT_port = m_LPT_port;
}

void SystemBoard::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_MACHINE, "saving main board state\n");
	_state.write(&m_s,{sizeof(m_s), name()});
}

void SystemBoard::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_MACHINE, "restoring main board state\n");
	_state.read(&m_s,{sizeof(m_s), name()});
}

void SystemBoard::update_state()
{
	update_POS2_state();
	update_POS3_state();
	update_POS4_state();
	update_POS5_state();
	update_board_state();
}

void SystemBoard::update_POS2_state()
{
	if(m_parallel) {
		m_parallel->set_enabled(m_s.LPT_enabled);
		m_parallel->set_mode(m_s.LPT_mode);
		m_parallel->set_port(m_s.LPT_port);
	}
	if(m_serial) {
		m_serial->set_enabled(m_s.COM_enabled);
		m_serial->set_port(m_s.COM_port);
	}
}

void SystemBoard::update_board_state()
{
	//TODO VGA
}

uint16_t SystemBoard::read(uint16_t _address, unsigned /*_io_len*/)
{
	uint8_t value = ~0;

	switch(_address) {
		case 0x0091: //Card Selected Feedback;
			//CSF is cleared on read
			value = m_s.CSF;
			m_s.CSF = 0;
			break;
		case 0x0092: // System Control Port A
			//TODO bit 3 is unimplemented. but according to the PS/1 tech ref the
			//password is not supported anyway.
			value = (g_memory.get_A20_line() << 1);
			break;
		case 0x0094:// System Board Enable/Setup
			value = (m_s.VGA_enable << 5) | (m_s.board_enable << 7);
			break;
		case 0x0100:
			// adapter ID low byte
			value = m_s.adapter_ID;
			break;
		case 0x0101:
			// adapter ID high byte
			value = m_s.adapter_ID >> 8;
			break;
		case 0x0102:
			value = m_s.VGA_awake   << 0 |
			        m_s.POS2_bit1   << 1 |
			        m_s.COM_enabled << 2 |
			        m_s.COM_port    << 3 |
			        m_s.LPT_enabled << 4 |
			        m_s.LPT_port    << 5 |
			        m_s.LPT_mode    << 7;
			break;
		case 0x0103:
			value = m_s.POS_3;
			break;
		case 0x0104:
			value = m_s.POS_4;
			break;
		case 0x0105:
			value = m_s.POS_5;
			break;
		case 0x0190:
			value = m_s.POST;
			break;
		default:
			PERRF_ABORT(LOG_MACHINE, "Unhandled read from port 0x%04X\n", _address);
	}

	PDEBUGF(LOG_V2, LOG_MACHINE, "read  0x%03X -> 0x%04X\n", _address, value);

	return value;
}

void SystemBoard::write(uint16_t _address, uint16_t _value, unsigned /*_io_len*/)
{
	PDEBUGF(LOG_V2, LOG_MACHINE, "write 0x%03X <- 0x%04X ", _address, _value);

	switch(_address) {
		case 0x0090: {
			//what should we do?
			PDEBUGF(LOG_V2, LOG_MACHINE, "\n");
			break;
		}
		case 0x0092: {
			bool a20 = (_value & 0x02);
			g_memory.set_A20_line(a20);
			PDEBUGF(LOG_V2, LOG_MACHINE, "A20:%u\n", a20);
			if(_value & 0x01) { /* high speed reset */
				PDEBUGF(LOG_V2, LOG_MACHINE, "iowrite to port 0x92 : reset requested\n");
				g_machine.reset(CPU_SOFT_RESET);
			}
			break;
		}
		case 0x0094:
			m_s.VGA_enable   = (_value >> 5) & 1;
			m_s.board_enable = (_value >> 7) & 1;
			PDEBUGF(LOG_V2, LOG_MACHINE, "VGA:%d, Board:%d\n",
					m_s.VGA_enable, m_s.board_enable);
			update_board_state();
			break;
		case 0x102: {
			m_s.VGA_awake   = (_value >> 0) & 1;
			m_s.POS2_bit1   = (_value >> 1) & 1;
			m_s.COM_enabled = (_value >> 2) & 1;
			m_s.COM_port    = (_value >> 3) & 1;
			m_s.LPT_enabled = (_value >> 4) & 1;
			m_s.LPT_port    = (_value >> 5) & 3;
			m_s.LPT_mode    = (_value >> 7) & 1;
			PDEBUGF(LOG_V2, LOG_MACHINE, "%s\n", debug_POS_decode(2,_value).c_str());
			if(!m_s.board_enable) {
				update_POS2_state();
			}
			break;
		}
		case 0x0103:
			m_s.POS_3 = _value;
			PDEBUGF(LOG_V2, LOG_MACHINE, "%s\n", debug_POS_decode(3,_value).c_str());
			if(!m_s.board_enable) {
				update_POS3_state();
			}
			break;
		case 0x0104:
			m_s.POS_4 = _value;
			PDEBUGF(LOG_V2, LOG_MACHINE, "%s\n", debug_POS_decode(4,_value).c_str());
			if(!m_s.board_enable) {
				update_POS4_state();
			}
			break;
		case 0x0105:
			m_s.POS_5 = _value;
			PDEBUGF(LOG_V2, LOG_MACHINE, "%s\n", debug_POS_decode(5,_value).c_str());
			if(!m_s.board_enable) {
				update_POS5_state();
			}
			break;
		case 0x0191:
		case 0x0190: {
			PDEBUGF(LOG_V2, LOG_MACHINE, "\n");
			PINFOF(LOG_V1, LOG_MACHINE, "POST code %02X\n", _value);
			m_s.POST = _value;
			break;
		}
		default:
			PERRF_ABORT(LOG_MACHINE, "Unhandled write to port 0x%04X\n", _address);
	}
}

void SystemBoard::set_feedback()
{
	m_s.CSF |= 1;
}

std::string SystemBoard::debug_POS_decode(int _posreg, uint8_t _value)
{
	switch(_posreg) {
		case 2: {
			return bitfield_to_string(_value,
			{ "VGA_EN", "b1", "COM_EN",  "COM1", "LPT_EN",  "LPT_P0=1", "LPT_P1=1", "LPT_EXT"  },
			{ "VGA_DIS", "",  "COM_DIS", "COM2", "LPT_DIS", "LPT_P0=0", "LPT_P1=0", "LPT_NORM" });
		}
		case 3:
		case 4:
		case 5:
		{
			return bitfield_to_string(_value,
			{ "b0", "b1", "b2", "b3", "b4", "b5", "b6", "b7" },
			{ "",   "",   "",   "",   "",   "",   "",   ""   });
		}
	}
	return "";
}
