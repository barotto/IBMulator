/*
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ibmulator.h"
#include "program.h"
#include "machine.h"
#include "systemboard.h"
#include "hardware/devices.h"
#include "hardware/memory.h"
#include "hardware/cpu/core.h"
#include "hardware/devices/serial.h"
#include "hardware/devices/parallel.h"
#include <cstring>

SystemBoard g_sysboard;

void SystemBoard::init(void)
{
	//Central Arbitration Control Port
	g_devices.register_read_handler(this, 0x90, 1);
	g_devices.register_write_handler(this, 0x90, 1);

	//Card Selected Feedback:
	g_devices.register_read_handler(this, 0x91, 1);

	//System Control Port A:
	g_devices.register_read_handler(this, 0x92, 1);
	g_devices.register_write_handler(this, 0x92, 1);

	//System Board Enable/Setup Register:
	g_devices.register_read_handler(this, 0x94, 1);
	g_devices.register_write_handler(this, 0x94, 1);

	//Adapter Enable/Setup Register
	g_devices.register_read_handler(this, 0x96, 1);
	g_devices.register_write_handler(this, 0x96, 1);

	//Programmable Option Select:
	for(uint p=0x100; p<=0x105; p++) {
		g_devices.register_read_handler(this, p, 1);
		g_devices.register_write_handler(this, p, 1);
	}

	//POST procedure code:
	g_devices.register_write_handler(this, 0x190, 1);
	// 191 is not the POST port, but it's used for POST 00 so I register it anyway
	g_devices.register_write_handler(this, 0x191, 1);

}

void SystemBoard::reset(unsigned type)
{
	if(type == MACHINE_SOFT_RESET) {
		return;
	}

	//HARD reset and POWER ON

	m_s.POST = 0;

	//System Board Enable/Setup Register:
	m_s.VGA_enable = true;
	m_s.board_enable = true;
	update_board_status();

	//POS 2
	m_s.VGA_awake = true;
	m_s.POS2_bit1 = false;
	m_s.COM_enabled = g_program.config().get_bool(COM_SECTION, COM_ENABLED);
	m_s.COM_port = 1; // COM1 fixed on model 2011
	m_s.LPT_enabled = g_program.config().get_bool(LPT_SECTION, LPT_ENABLED);
	m_s.LPT_port =  g_program.config().get_enum(LPT_SECTION, LPT_PORT, Parallel::ms_lpt_ports);
	m_s.LPT_mode = PARPORT_COMPATIBLE;
	update_POS2_status();

	//POS 3
	m_s.POS_3 = 0x0F;
	update_POS3_status();

	//POS 4
	m_s.RAM_bank1_en = true;
	m_s.RAM_bank2_en = true;
	m_s.RAM_bank3_en = true;
	m_s.RAM_bank4_en = true;
	m_s.RAM_bank5_en = true;
	update_POS4_status();

	//POS 5
	m_s.POS_5 = 0x0F;

	//
	m_s.CSF = 0;
}

void SystemBoard::config_changed()
{
	m_s.COM_enabled = g_program.config().get_bool(COM_SECTION, COM_ENABLED);
	m_s.LPT_enabled = g_program.config().get_bool(LPT_SECTION, LPT_ENABLED);
	m_s.LPT_port =  g_program.config().get_enum(LPT_SECTION, LPT_PORT, Parallel::ms_lpt_ports);

	update_status();
}

void SystemBoard::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_MACHINE, "saving board state\n");

	StateHeader h;
	h.name = get_name();
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);
}

void SystemBoard::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_MACHINE, "restoring board state\n");

	StateHeader h;
	h.name = get_name();
	h.data_size = sizeof(m_s);
	_state.read(&m_s,h);
}

void SystemBoard::update_status()
{
	update_POS2_status();
	update_POS3_status();
	update_POS4_status();
	update_board_status();
}

void SystemBoard::update_POS2_status()
{
	//parallel
	g_parallel.set_mode(m_s.LPT_mode);
	g_parallel.set_port(m_s.LPT_port);
	g_parallel.set_enabled(m_s.LPT_enabled);

	//serial
	g_serial.set_enabled(m_s.COM_enabled);
	g_serial.set_port(m_s.COM_port);
}

void SystemBoard::update_POS3_status()
{
	//TODO HDD
}

void SystemBoard::update_POS4_status()
{
	//TODO RAM
}

void SystemBoard::update_board_status()
{
	//TODO VGA
}

uint16_t SystemBoard::read(uint16_t _address, unsigned /*_io_len*/)
{
	uint8_t value = 0;

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
		case 0x0102:
			value = (m_s.VGA_awake   << 0) |
			        (m_s.POS2_bit1   << 1) |
			        (m_s.COM_enabled << 2) |
			        (m_s.COM_port    << 3) |
			        (m_s.LPT_enabled << 4) |
			        (m_s.LPT_port    << 5) |
			        (m_s.LPT_mode    << 7);
			break;
		case 0x0103:
			value = m_s.POS_3;
			break;
		case 0x0104:
			value =	(m_s.RAM_bank1_en << 0) |
			        (m_s.RAM_bank2_en << 1) |
			        (m_s.RAM_bank3_en << 2) |
			        (m_s.RAM_bank4_en << 3) |
			        (m_s.RAM_bank5_en << 4);
			break;
		case 0x0105:
			value = m_s.POS_5;
			break;
		case 0x0190:
			value = m_s.POST;
			break;
		default:
			PWARNF(LOG_MACHINE, "Unhandled read from port 0x%04X (CS:IP=%X:%X)\n",
					_address, REG_CS.sel.value, REG_IP);
			value = 0xFF;
			break;
	}

	return value;
}

void SystemBoard::write(uint16_t _address, uint16_t _value, unsigned /*_io_len*/)
{
	switch(_address) {
		case 0x0090: {
			//what should we do?
			break;
		}
		case 0x0092: {
			bool a20 = (_value & 0x02);
			g_memory.set_A20_line(a20);
			PDEBUGF(LOG_V2, LOG_MACHINE, "A20: now %u\n", (uint)a20);
			#if (1) // does the PS/1 support this?
			if(_value & 0x01) { /* high speed reset */
				PDEBUGF(LOG_V2, LOG_MACHINE, "iowrite to port 0x92 : reset resquested\n");
				g_machine.reset(MACHINE_SOFT_RESET);
			}
			#endif
			break;
		}
		case 0x0094:
			m_s.VGA_enable   = (_value >> 5) & 1;
			m_s.board_enable = (_value >> 7) & 1;
			PDEBUGF(LOG_V2, LOG_MACHINE, "VGA mode=%u, Board mode=%u\n",
					m_s.VGA_enable, m_s.board_enable);
			update_board_status();
			break;
		case 0x102:
			m_s.VGA_awake   = _value & 1;
			m_s.POS2_bit1   = (_value >> 1) & 1;
			m_s.COM_enabled = (_value >> 2) & 1;
			//m_s.COM_port  = (_value >> 3) & 1; //the serial port is fixed 1 for model 2011
			m_s.LPT_enabled = (_value >> 4) & 1;
			m_s.LPT_port    = (_value >> 5) & 3;
			m_s.LPT_mode    = (_value >> 7) & 1;
			PDEBUGF(LOG_V2, LOG_MACHINE, "POS register 2 := 0x%02X\n", _value);
			if(!m_s.board_enable) {
				update_POS2_status();
			}
			break;
		case 0x0103:
			m_s.POS_3 = _value;
			m_s.HDD_enabled = (_value >> 3) & 1;
			PDEBUGF(LOG_V2, LOG_MACHINE, "POS register 3 := 0x%02X\n", _value);
			if(!m_s.board_enable) {
				update_POS3_status();
			}
			break;
		case 0x0104:
			m_s.RAM_bank1_en = _value & 1;
			m_s.RAM_bank2_en = (_value >> 1) & 1;
			m_s.RAM_bank3_en = (_value >> 2) & 1;
			m_s.RAM_bank4_en = (_value >> 3) & 1;
			m_s.RAM_bank5_en = (_value >> 4) & 1;
			PDEBUGF(LOG_V2, LOG_MACHINE, "POS register 4 := 0x%02X\n", _value);
			if(!m_s.board_enable) {
				update_POS4_status();
			}
			break;
		case 0x0105:
			m_s.POS_5 = _value;
			m_s.RAM_fast = (_value >> 3) & 1;
			PDEBUGF(LOG_V2, LOG_MACHINE, "POS register 5 := 0x%02X\n", _value);
			if(!m_s.board_enable) {
				//TODO?
				//update_POS5_status();
			}
			break;
		case 0x0191:
		case 0x0190: {
			PINFOF(LOG_V1, LOG_MACHINE, "POST code %02X\n", _value);
			m_s.POST = _value;
			break;
		}
		default:
			PWARNF(LOG_MACHINE, "Unhandled write to port 0x%04X (CS:IP=%X:%X)\n",
					_address, REG_CS.sel.value, REG_IP);
			break;
	}
}

void SystemBoard::set_feedback()
{
	m_s.CSF |= 1;
}
