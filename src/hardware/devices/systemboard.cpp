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
#include "vga.h"
#include "serial.h"
#include "parallel.h"
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
#if BOCHS_BIOS_COMPAT
	,{ 0x400, 0x403, PORT_8BIT|PORT__W }  // Bochs rombios virtual ports
#endif
};

SystemBoard::SystemBoard(Devices* _dev)
: IODevice(_dev),
m_COM_port(1),
m_LPT_port(0),
m_parallel(nullptr),
m_serial(nullptr)
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

		//Card Select Feedback
		m_s.CSF = 0;

		// board POS registers
		memset(m_s.POS, 0, 5);
		reset_POS2_state();

		// for CPU_SOFT_RESET the A20 line is enabled only on 486+ systems.
		g_memory.set_A20_line(true);
	}
}

void SystemBoard::config_changed()
{
	m_parallel = m_devices->device<Parallel>();
	m_serial = m_devices->device<Serial>();

	//TODO m_COM_port = g_program.config().get_enum(COM_SECTION, COM_PORT, Serial::ms_com_ports);
	m_COM_port = 1;
	m_LPT_port = g_program.config().get_enum(LPT_SECTION, LPT_PORT, Parallel::ms_lpt_ports);
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

void SystemBoard::reset_POS2_state()
{
	m_s.POS[2] = false          << 0 | // unknown bit 0 (enable system board?)
	             false          << 1 | // unknown bit 1 (enable diskette drive?)
	             true           << 2 | // COM enabled
	             (m_COM_port&1) << 3 | // COM port
	             true           << 4 | // LPT enabled
	             (m_LPT_port&3) << 5 | // LPT port
	             PARPORT_COMPATIBLE << 7; // LPT mode
	update_POS2_state();
}

void SystemBoard::update_POS2_state()
{
	int POS2_bit0   = (m_s.POS[2] >> 0) & 1;
	int POS2_bit1   = (m_s.POS[2] >> 1) & 1;
	int COM_enabled = (m_s.POS[2] >> 2) & 1;
	int COM_port    = (m_s.POS[2] >> 3) & 1;
	int LPT_enabled = (m_s.POS[2] >> 4) & 1;
	int LPT_port    = (m_s.POS[2] >> 5) & 3;
	int LPT_mode    = (m_s.POS[2] >> 7) & 1;

	UNUSED(POS2_bit0);
	UNUSED(POS2_bit1);

	if(m_parallel) {
		m_parallel->set_enabled(LPT_enabled);
		m_parallel->set_mode(LPT_mode);
		m_parallel->set_port(LPT_port);
	}
	if(m_serial) {
		m_serial->set_enabled(COM_enabled);
		m_serial->set_port(COM_port);
	}
}

void SystemBoard::reset_board_state()
{
	reset_POS2_state();
	reset_POS3_state();
	reset_POS4_state();
	reset_POS5_state();
}

void SystemBoard::update_board_state()
{
	update_POS2_state();
	update_POS3_state();
	update_POS4_state();
	update_POS5_state();
}

uint16_t SystemBoard::read(uint16_t _address, unsigned _io_len)
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
		case 0x0101:
		case 0x0102:
		case 0x0103:
		case 0x0104:
		case 0x0105:
			if(!m_s.VGA_enable) {
				// The VGA is in setup mode, it responds to POS registers
				value = m_devices->vga()->read(_address, _io_len);
			} else {
				value = m_s.POS[_address - 0x100];
			}
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

void SystemBoard::write(uint16_t _address, uint16_t _value, unsigned _io_len)
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
			PDEBUGF(LOG_V2, LOG_MACHINE, "A20:%u\n", a20);
			g_memory.set_A20_line(a20);
			if(_value & 0x01) { /* high speed reset */
				PDEBUGF(LOG_V2, LOG_MACHINE, "iowrite to port 0x92 : reset requested\n");
				g_machine.reset(CPU_SOFT_RESET);
			}
			break;
		}
		case 0x0094: {
			m_s.VGA_enable = (_value >> 5) & 1;
			bool board_en  = (_value >> 7) & 1;
			PDEBUGF(LOG_V2, LOG_MACHINE, "VGA:%d, Board:%d\n",
					m_s.VGA_enable, board_en);
			if((!m_s.board_enable) && board_en) {
				update_board_state();
			}
			m_s.board_enable = board_en;
			break;
		}
		case 0x0102:
		case 0x0103:
		case 0x0104:
		case 0x0105: {
			if(!m_s.VGA_enable) {
				// The VGA is in setup mode, it responds to POS registers
				PDEBUGF(LOG_V2, LOG_MACHINE, "to VGA\n");
				m_devices->vga()->write(_address, _value, _io_len);
				return;
			}
			int reg = _address - 0x100;
			m_s.POS[reg] = _value;
			PDEBUGF(LOG_V2, LOG_MACHINE, "%s\n", debug_POS_decode(reg, _value).c_str());
			break;
		}
		case 0x0191:
		case 0x0190: {
			PDEBUGF(LOG_V2, LOG_MACHINE, "\n");
			PINFOF(LOG_V1, LOG_MACHINE, "POST code %02X\n", _value);
			m_s.POST = _value;
			break;
		}

#if BOCHS_BIOS_COMPAT
		// 0x400-0x401 are used as panic ports for the rombios
		case 0x0401:
			PDEBUGF(LOG_V2, LOG_MACHINE, "\n");
			if(_value == 0) {
				// The next message sent to the info port will cause a panic
				m_s.bios_panic_flag = 1;
			} else if(m_s.bios_message_i > 0) {
				// if there are bits of message in the buffer, print them as the
				// panic message.  Otherwise fall into the next case.
				if(m_s.bios_message_i >= BOCHS_BIOS_MESSAGE_SIZE)
					m_s.bios_message_i = BOCHS_BIOS_MESSAGE_SIZE-1;
				m_s.bios_message[ m_s.bios_message_i] = 0;
				m_s.bios_message_i = 0;
				PERRF(LOG_MACHINE, "BIOS: %s\n", m_s.bios_message);
			}
			break;
		case 0x0400:
			PDEBUGF(LOG_V2, LOG_MACHINE, "\n");
			if(_value > 0) {
				PERRF(LOG_MACHINE, "BIOS panic at rombios.c, line %d", _value);
			}
			break;

		// 0x0402 is used as the info port for the rombios
		// 0x0403 is used as the debug port for the rombios
		case 0x0402:
		case 0x0403:
			PDEBUGF(LOG_V2, LOG_MACHINE, "\n");
			m_s.bios_message[m_s.bios_message_i] = _value;
			m_s.bios_message_i ++;
			if(m_s.bios_message_i >= BOCHS_BIOS_MESSAGE_SIZE) {
				m_s.bios_message[ BOCHS_BIOS_MESSAGE_SIZE - 1] = 0;
				m_s.bios_message_i = 0;
				if(_address == 0x403) {
					PDEBUGF(LOG_V1, LOG_MACHINE, "BIOS: %s\n", m_s.bios_message);
				} else {
					PINFOF(LOG_V1, LOG_MACHINE, "BIOS: %s\n", m_s.bios_message);
				}
			} else if((_value & 0xff) == '\n') {
				m_s.bios_message[ m_s.bios_message_i - 1 ] = 0;
				m_s.bios_message_i = 0;
				if(m_s.bios_panic_flag == 1) {
					PERRF(LOG_MACHINE, "BIOS: %s", m_s.bios_message);
				} else if(_address == 0x403) {
					PDEBUGF(LOG_V1, LOG_MACHINE, "BIOS: %s\n", m_s.bios_message);
				} else {
					PINFOF(LOG_V1, LOG_MACHINE, "BIOS: %s\n", m_s.bios_message);
				}
				m_s.bios_panic_flag = 0;
			}
			break;
#endif

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
			{ "b0", "b1", "COM_EN",  "COM1", "LPT_EN",  "LPT_P0=1", "LPT_P1=1", "LPT_EXT"  },
			{ "",   "",   "COM_DIS", "COM2", "LPT_DIS", "LPT_P0=0", "LPT_P1=0", "LPT_NORM" });
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
