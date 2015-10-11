/*
 * 	Copyright (c) 2002-2014  The Bochs Project
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

// Now features proper implementation of keyboard opcodes 0xF4 to 0xF6
// Silently ignores PS/2 keyboard extensions (0xF7 to 0xFD)
// Explicit panic on resend (0xFE)
//
// Emmanuel Marty <core@ggi-project.org>

// NB: now the PS/2 mouse support is in, outb changes meaning
// in conjunction with auxb
// auxb == 0 && outb == 0  => both buffers empty (nothing to read)
// auxb == 0 && outb == 1  => keyboard controller output buffer full
// auxb == 1 && outb == 0  => not used
// auxb == 1 && outb == 1  => mouse output buffer full.
// (das)

// Notes from Christophe Bothamy <cbbochs@free.fr>
//
// This file includes code from Ludovic Lange (http://ludovic.lange.free.fr)
// Implementation of 3 scancodes sets mf1,mf2,mf3 with or without translation.
// Default is mf2 with translation
// Ability to switch between scancodes sets
// Ability to turn translation on or off

#include "ibmulator.h"
#include "machine.h"
#include "program.h"
#include "keyboard.h"
#include "pic.h"
#include "hardware/devices.h"
#include "hardware/devices/cmos.h"
#include "hardware/devices/floppy.h"
#include "hardware/memory.h"
#include "scancodes.h"
#include "keys.h"
#include "gui/gui.h"
#include "gui/keymap.h"
#include <cstring>
#include "math.h"
#include <functional>
using namespace std::placeholders;

Keyboard g_keyboard;


Keyboard::Keyboard()
{

}

Keyboard::~Keyboard()
{

}

void Keyboard::init()
{
	g_devices.register_read_handler(this, 0x60, 1);
	g_devices.register_read_handler(this, 0x64, 1);
	g_devices.register_write_handler(this, 0x60, 1);
	g_devices.register_write_handler(this, 0x64, 1);

	g_machine.register_irq(1, "8042 Keyboard controller");
	g_machine.register_irq(12, "8042 Keyboard controller (PS/2 mouse)");

	m_timer_handle = g_machine.register_timer(
			std::bind(&Keyboard::timer_handler,this),
			KBD_SERIAL_DELAY, //usec
			1, //continuous
			1, //active
			get_name() //name
	);

	config_changed();

	set_kbd_clock_enable(false);
	set_aux_clock_enable(false);
	m_s.mouse.enable = false;
}

void Keyboard::reset(unsigned _type)
{
	memset(&m_s.kbd_ctrl, 0, sizeof(m_s.kbd_ctrl));

	reset_internals(true);

	m_s.kbd_buffer.led_status = 0;
	m_s.kbd_buffer.scanning_enabled = true;

	m_mouse_lock.lock();
	m_s.mouse_buffer.num_elements = 0;
	for(uint i=0; i<MOUSE_BUFF_SIZE; i++) {
		m_s.mouse_buffer.buffer[i] = 0;
	}
	m_s.mouse_buffer.head = 0;
	m_mouse_lock.unlock();

	m_s.kbd_ctrl.pare = false;
	m_s.kbd_ctrl.tim  = false;
	m_s.kbd_ctrl.auxb = false;
	m_s.kbd_ctrl.keyl = true;
	m_s.kbd_ctrl.c_d  = true;
	m_s.kbd_ctrl.sysf = false;
	m_s.kbd_ctrl.inpb = false; //is this always false???
	m_s.kbd_ctrl.outb = false;

	m_s.kbd_ctrl.kbd_clock_enabled         = false;
	m_s.kbd_ctrl.aux_clock_enabled         = false;
	m_s.kbd_ctrl.allow_irq1                = true;
	m_s.kbd_ctrl.allow_irq12               = true;
	m_s.kbd_ctrl.kbd_output_buffer         = 0;
	m_s.kbd_ctrl.aux_output_buffer         = 0;
	m_s.kbd_ctrl.last_comm                 = 0;
	m_s.kbd_ctrl.expecting_port60h         = 0;
	m_s.kbd_ctrl.irq1_requested            = false;
	m_s.kbd_ctrl.irq12_requested           = false;
	m_s.kbd_ctrl.expecting_mouse_parameter = 0;
	m_s.kbd_ctrl.bat_in_progress           = false;
	m_s.kbd_ctrl.scancodes_translate       = true;
	if(_type != DEVICE_SOFT_RESET) {
		m_s.kbd_ctrl.self_test_in_progress = false;
		m_s.kbd_ctrl.self_test_completed   = false;
	}

	m_s.kbd_ctrl.timer_pending = 0;

	// Mouse initialization stuff
	m_s.mouse.type            = g_program.config().get_enum(GUI_SECTION, GUI_MOUSE_TYPE, g_mouse_types);
	m_s.mouse.sample_rate     = 100; // reports per second
	m_s.mouse.resolution_cpmm = 4;   // 4 counts per millimeter
	m_s.mouse.scaling         = 1;   /* 1:1 (default) */
	m_s.mouse.mode            = MOUSE_MODE_RESET;
	m_s.mouse.saved_mode      = 0;
	m_s.mouse.enable          = false;
	// don't reset the button_status, it depends on the current state of the real mouse
	m_s.mouse.delayed_dx      = 0;
	m_s.mouse.delayed_dy      = 0;
	m_s.mouse.delayed_dz      = 0;
	m_s.mouse.im_request      = 0; // wheel mouse mode request
	m_s.mouse.im_mode         = 0; // wheel mouse mode

	for(uint i=0; i<KBD_CONTROLLER_QSIZE; i++) {
		m_s.kbd_ctrl.Q[i] = 0;
	}
	m_s.kbd_ctrl.Qsize = 0;
	m_s.kbd_ctrl.Qsource = 0;

	// on a reset the A20 address line is enabled
	g_memory.set_A20_line(true);
}

void Keyboard::power_off()
{
	set_kbd_clock_enable(false);
	set_aux_clock_enable(false);
	m_s.mouse.enable = false;
}

void Keyboard::config_changed()
{
	uint mouse = g_program.config().get_enum(GUI_SECTION, GUI_MOUSE_TYPE, g_mouse_types);
	if((mouse == MOUSE_TYPE_PS2) || (mouse == MOUSE_TYPE_IMPS2)) {
		g_machine.register_mouse_fun(
			std::bind(&Keyboard::mouse_motion, this, _1, _2, _3, _4)
		);
		PINFOF(LOG_V0, LOG_KEYB, "Installed PS/2 mouse\n");
	}

	m_mouse_acc = g_program.config().get_bool(GUI_SECTION, GUI_MOUSE_ACCELERATION);
	int dpi = g_program.config().get_int(GUI_SECTION, GUI_SCREEN_DPI);
	m_s.screen_mmpd = 25.4f/float(dpi);
	if(m_mouse_acc) {
		PINFOF(LOG_V1, LOG_KEYB, "Mouse acceleration: ON (%.1fmmpd)\n", m_s.screen_mmpd);
	}
}

void Keyboard::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_KEYB, "saving state\n");

	std::lock_guard<std::mutex> klock(m_kbd_lock);
	std::lock_guard<std::mutex> mlock(m_mouse_lock);
	StateHeader h;
	h.name = get_name();
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);
}

void Keyboard::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_KEYB, "restoring state\n");

	std::lock_guard<std::mutex> klock(m_kbd_lock);
	std::lock_guard<std::mutex> mlock(m_mouse_lock);
	StateHeader h;
	h.name = get_name();
	h.data_size = sizeof(m_s);
	_state.read(&m_s,h);
}

// flush internal buffer and reset keyboard settings to power-up condition
void Keyboard::reset_internals(bool powerup)
{
	std::lock_guard<std::mutex> lock(m_kbd_lock);

	m_s.kbd_buffer.num_elements = 0;
	for (int i=0; i<KBD_ELEMENTS; i++)
		m_s.kbd_buffer.buffer[i] = 0;
	m_s.kbd_buffer.head = 0;

	m_s.kbd_buffer.expecting_typematic = false;

	// Default scancode set is mf2 (translation is controlled by the 8042)
	m_s.kbd_ctrl.expecting_scancodes_set = 0;
	m_s.kbd_ctrl.current_scancodes_set = 1;

	if(powerup) {
		m_s.kbd_buffer.expecting_led_write = false;
		m_s.kbd_buffer.delay = 1; // 500 mS
		m_s.kbd_buffer.repeat_rate = 0x0b; // 10.9 chars/sec
	}
}

void Keyboard::update_controller_Q()
{
	unsigned i;
	m_s.kbd_ctrl.outb = true;
	if(m_s.kbd_ctrl.Qsource == 0) { // keyboard
		m_s.kbd_ctrl.kbd_output_buffer = m_s.kbd_ctrl.Q[0];
		m_s.kbd_ctrl.auxb = false;
		if(m_s.kbd_ctrl.allow_irq1) {
			m_s.kbd_ctrl.irq1_requested = true;
		}
	} else { // mouse
		m_s.kbd_ctrl.aux_output_buffer = m_s.kbd_ctrl.Q[0];
		m_s.kbd_ctrl.auxb = true;
		if(m_s.kbd_ctrl.allow_irq12) {
			m_s.kbd_ctrl.irq12_requested = true;
		}
	}
	for(i=0; i< m_s.kbd_ctrl.Qsize-1; i++) {
		// move Q elements towards head of queue by one
		m_s.kbd_ctrl.Q[i] =  m_s.kbd_ctrl.Q[i+1];
	}
	PDEBUGF(LOG_V2, LOG_KEYB, "controller_Qsize: %02X\n", m_s.kbd_ctrl.Qsize);
	m_s.kbd_ctrl.Qsize--;
}

uint16_t Keyboard::read(uint16_t address, unsigned /*io_len*/)
{
	uint8_t val;

	if(address == 0x60) { /* output buffer */
		if(m_s.kbd_ctrl.auxb) { /* mouse byte available */
			val = m_s.kbd_ctrl.aux_output_buffer;
			m_s.kbd_ctrl.aux_output_buffer = 0;
			m_s.kbd_ctrl.outb = false;
			m_s.kbd_ctrl.auxb = false;
			m_s.kbd_ctrl.irq12_requested = false;
			if(m_s.kbd_ctrl.Qsize) {
				update_controller_Q();
			}
			g_pic.lower_irq(12);
			activate_timer();
			PDEBUGF(LOG_V2, LOG_KEYB, "[mouse] read from 0x60 -> 0x%02X\n", val);
			return val;
		} else if(m_s.kbd_ctrl.outb) { /* kbd byte available */
			val = m_s.kbd_ctrl.kbd_output_buffer;
			m_s.kbd_ctrl.outb = false;
			m_s.kbd_ctrl.auxb = false;
			m_s.kbd_ctrl.irq1_requested = false;
			m_s.kbd_ctrl.bat_in_progress = false;
			if(m_s.kbd_ctrl.Qsize) {
				update_controller_Q();
			}
			g_pic.lower_irq(1);
			activate_timer();
			PDEBUGF(LOG_V2, LOG_KEYB, "read from 0x60 -> 0x%02X\n", val);
			return val;
		} else {
			//m_s.kbd_buffer.num_elements is not thread safe, but it's just a debug print...
			PDEBUGF(LOG_V2, LOG_KEYB, "num_elements = %d", m_s.kbd_buffer.num_elements);
			PDEBUGF(LOG_V2, LOG_KEYB, " read from port 60h with outb empty\n");
			return m_s.kbd_ctrl.kbd_output_buffer;
		}
	} else if(address == 0x64) { /* status register */
		val = (m_s.kbd_ctrl.pare << 7) |
		      (m_s.kbd_ctrl.tim  << 6) |
		      (m_s.kbd_ctrl.auxb << 5) |
		      (m_s.kbd_ctrl.keyl << 4) |
		      (m_s.kbd_ctrl.c_d  << 3) |
		      (m_s.kbd_ctrl.sysf << 2) |
		      (m_s.kbd_ctrl.inpb << 1) |
		       m_s.kbd_ctrl.outb;

		m_s.kbd_ctrl.tim = false;
		PDEBUGF(LOG_V2, LOG_KEYB, "read from 0x64 -> 0x%02X\n", val);
		return val;
	}

	PDEBUGF(LOG_V2, LOG_KEYB, "unknown address in io read to keyboard port 0x%02X\n", address);
	return 0; /* keep compiler happy */
}

void Keyboard::write(uint16_t address, uint16_t value, unsigned /*io_len*/)
{
	uint8_t   command_byte;

	PDEBUGF(LOG_V2, LOG_KEYB, "write to 0x%04x <- 0x%02x\n", address, value);

	switch(address) {
		case 0x60: // input buffer
			// if expecting data byte from command last sent to port 64h
			if(m_s.kbd_ctrl.expecting_port60h) {
				m_s.kbd_ctrl.expecting_port60h = 0;
				// data byte written last to 0x60
				m_s.kbd_ctrl.c_d = false;
				if(m_s.kbd_ctrl.inpb) {
					PDEBUGF(LOG_V2, LOG_KEYB, "write to port 60h, not ready for write\n");
				}
				switch(m_s.kbd_ctrl.last_comm) {
					case 0x60: // write command byte
					{
						bool scan_convert, disable_keyboard,
						disable_aux;

						scan_convert = (value >> 6) & 0x01;
						disable_aux      = (value >> 5) & 0x01;
						disable_keyboard = (value >> 4) & 0x01;
						m_s.kbd_ctrl.sysf = (value >> 2) & 0x01;
						m_s.kbd_ctrl.allow_irq1  = (value >> 0) & 0x01;
						m_s.kbd_ctrl.allow_irq12 = (value >> 1) & 0x01;
						set_kbd_clock_enable(!disable_keyboard);
						set_aux_clock_enable(!disable_aux);
						if(m_s.kbd_ctrl.allow_irq12 &&  m_s.kbd_ctrl.auxb)
							m_s.kbd_ctrl.irq12_requested = true;
						else if(m_s.kbd_ctrl.allow_irq1  &&  m_s.kbd_ctrl.outb)
							m_s.kbd_ctrl.irq1_requested = true;

						PDEBUGF(LOG_V2, LOG_KEYB, " allow_irq12 set to %u\n", (unsigned)  m_s.kbd_ctrl.allow_irq12);
						if(!scan_convert) {
							PDEBUGF(LOG_V1, LOG_KEYB, "keyboard: scan convert turned off\n");
						}

						// (mch) NT needs this
						m_s.kbd_ctrl.scancodes_translate = scan_convert;
					}
					break;

					case 0xcb: // write keyboard controller mode
						PDEBUGF(LOG_V2, LOG_KEYB, "write keyboard controller mode with value %02xh\n",
								value);
						break;

					case 0xd1: // write output port
						PDEBUGF(LOG_V2, LOG_KEYB, "write output port with value %02xh\n", (unsigned) value);
						PDEBUGF(LOG_V2, LOG_KEYB, "write output port : %sable A20\n",(value & 0x02)?"en":"dis");
						g_memory.set_A20_line((value & 0x02) != 0);
						if(!(value & 0x01)) {
							PINFOF(LOG_V2, LOG_KEYB, "write output port : processor reset requested!\n");
							g_machine.reset(CPU_SOFT_RESET);
						}
						break;

					case 0xd4: // Write to mouse
						// I don't think this enables the AUX clock
						//set_aux_clock_enable(1); // enable aux clock line
						kbd_ctrl_to_mouse(value);
						// ??? should I reset to previous value of aux enable?
						break;

					case 0xd3: // write mouse output buffer
						// Queue in mouse output buffer
						controller_enQ(value, 1);
						break;

					case 0xd2:
						// Queue in keyboard output buffer
						controller_enQ(value, 0);
						break;

					default:
						PERRF(LOG_KEYB, "=== unsupported write to port 60h(lastcomm=%02x): %02x\n",
								m_s.kbd_ctrl.last_comm, value);
						break;
				}
			} else {
				// data byte written last to 0x60
				m_s.kbd_ctrl.c_d = false;
				m_s.kbd_ctrl.expecting_port60h = 0;
				/* pass byte to keyboard */
				/* ??? should conditionally pass to mouse device here ??? */
				if(!m_s.kbd_ctrl.kbd_clock_enabled) {
					set_kbd_clock_enable(1);
				}
				kbd_ctrl_to_kbd(value);
			}
			break;

		case 0x64: // control register
			// command byte written last to 0x64
			m_s.kbd_ctrl.c_d = true;
			m_s.kbd_ctrl.last_comm = value;
			// most commands NOT expecting port60 write next
			m_s.kbd_ctrl.expecting_port60h = 0;

			switch(value) {
				case 0x20: // get keyboard command byte
					PDEBUGF(LOG_V2, LOG_KEYB, "get keyboard command byte\n");
					// controller output buffer must be empty
					if(m_s.kbd_ctrl.outb) {
						PERRF(LOG_KEYB, "kbd: OUTB set and command 0x%02X encountered\n", value);
						break;
					}
					command_byte =
						(m_s.kbd_ctrl.scancodes_translate << 6) |
						((!m_s.kbd_ctrl.aux_clock_enabled) << 5) |
						((!m_s.kbd_ctrl.kbd_clock_enabled) << 4) |
						(0 << 3) |
						(m_s.kbd_ctrl.sysf << 2) |
						(m_s.kbd_ctrl.allow_irq12 << 1) |
						(m_s.kbd_ctrl.allow_irq1  << 0);
					controller_enQ(command_byte, 0);
					break;

				case 0x60: // write command byte
					PDEBUGF(LOG_V2, LOG_KEYB, "write command byte\n");
					// following byte written to port 60h is command byte
					m_s.kbd_ctrl.expecting_port60h = 1;
					break;

				case 0xa0:
					PDEBUGF(LOG_V2, LOG_KEYB, "keyboard BIOS name not supported\n");
					break;

				case 0xa1:
					PDEBUGF(LOG_V2, LOG_KEYB, "keyboard BIOS version not supported\n");
					break;

				case 0xa7: // disable the aux device
					set_aux_clock_enable(false);
					PDEBUGF(LOG_V2, LOG_KEYB, "aux device disabled\n");
					break;

				case 0xa8: // enable the aux device
					set_aux_clock_enable(true);
					PDEBUGF(LOG_V2, LOG_KEYB, "aux device enabled\n");
					break;

				case 0xa9: // Test Mouse Port
					// controller output buffer must be empty
					if(m_s.kbd_ctrl.outb) {
						PERRF(LOG_KEYB, "kbd: OUTB set and command 0x%02X encountered\n", value);
						break;
					}
					controller_enQ(0x00, 0); // no errors detected
					break;

				case 0xaa: // motherboard controller self test
					PDEBUGF(LOG_V2, LOG_KEYB, "Self Test\n");
					/* The Self Test command performs tests of the KBC and on success,
					 * sends 55h to the host; that much is documented by IBM
					 * and others. However, the self test command also effectively
					 * resets the KBC and puts it into a known state. That means,
					 * among other things, that the A20 address line is enabled,
					 * keyboard interface is disabled, and scan code translation is enabled.
					 * Furthermore, after the system is powered on, the keyboard
					 * controller does not start operating until the self test command
					 * is sent by the host and successfully completed by the KBC.
					 */
					// controller output buffer must be empty
					if(m_s.kbd_ctrl.outb) {
						PERRF(LOG_KEYB,"kbd: OUTB set and command 0x%02X encountered\n", value);
						break;
					}
					reset(DEVICE_SOFT_RESET);
					m_s.kbd_ctrl.self_test_in_progress = true;
					m_s.kbd_ctrl.self_test_completed = false;
					// self-test is supposed to take some time to complete.
					activate_timer(500);
					break;

				case 0xab: // Interface Test
					// controller output buffer must be empty
					if(m_s.kbd_ctrl.outb) {
						PERRF(LOG_KEYB, "kbd: OUTB set and command 0x%02X encountered\n", value);
						break;
					}
					controller_enQ(0x00, 0);
					break;

				case 0xad: // disable keyboard
					set_kbd_clock_enable(0);
					PDEBUGF(LOG_V2, LOG_KEYB, "keyboard disabled\n");
					break;

				case 0xae: // enable keyboard
					set_kbd_clock_enable(1);
					PDEBUGF(LOG_V2, LOG_KEYB, "keyboard enabled\n");
					break;

				case 0xaf: // get controller version
					PINFOF(LOG_V1, LOG_KEYB, "'get controller version' not supported yet\n");
					break;

				case 0xc0: // read input port
				{
					// controller output buffer must be empty
					if(m_s.kbd_ctrl.outb) {
						PERRF(LOG_KEYB, "kbd: OUTB set and command 0x%02X encountered\n", value);
						break;
					}
					// bit 7 = 1 keyboard not locked
					// bit 6 = 0 if current FDD is 3.5, 1 if it's 5.25
					// bit 2 = 1 for POST 56
					uint8_t data = 0x84;
					uint drive = g_floppy.get_current_drive();
					uint8_t dtype = g_floppy.get_drive_type(drive);
					if(dtype == FDD_525DD || dtype == FDD_525HD) {
						data |= 0x40;
					}
					controller_enQ(data, 0);
					break;
				}
				case 0xca: // read keyboard controller mode
					controller_enQ(0x01, 0); // PS/2 (MCA)interface
					break;

				case 0xcb: //  write keyboard controller mode
					PDEBUGF(LOG_V2, LOG_KEYB, "write keyboard controller mode\n");
					// write keyboard controller mode to bit 0 of port 0x60
					m_s.kbd_ctrl.expecting_port60h = 1;
					break;

				case 0xd0: // read output port: next byte read from port 60h
					PDEBUGF(LOG_V2, LOG_KEYB, "io write to port 64h, command d0h (partial)\n");
					// controller output buffer must be empty
					if(m_s.kbd_ctrl.outb) {
						PERRF(LOG_KEYB, "kbd: OUTB set and command 0x%02X encountered\n", value);
						break;
					}
					controller_enQ(
							(m_s.kbd_ctrl.irq12_requested << 5) |
							(m_s.kbd_ctrl.irq1_requested << 4) |
							(g_memory.get_A20_line() << 1) |
							0x01, 0);
					break;

				case 0xd1: // write output port: next byte written to port 60h
					PDEBUGF(LOG_V2, LOG_KEYB, "write output port\n");
					// following byte to port 60h written to output port
					m_s.kbd_ctrl.expecting_port60h = 1;
					break;

				case 0xd3: // write mouse output buffer
					//FIXME: Why was this a panic?
					PDEBUGF(LOG_V2, LOG_KEYB, "io write 0x64: command = 0xD3(write mouse outb)\n");
					// following byte to port 60h written to output port as mouse write.
					m_s.kbd_ctrl.expecting_port60h = 1;
					break;

				case 0xd4: // write to mouse
					PDEBUGF(LOG_V2, LOG_KEYB, "io write 0x64: command = 0xD4 (write to mouse)\n");
					// following byte written to port 60h
					m_s.kbd_ctrl.expecting_port60h = 1;
					break;

				case 0xd2: // write keyboard output buffer
					PDEBUGF(LOG_V2, LOG_KEYB, "io write 0x64: write keyboard output buffer\n");
					m_s.kbd_ctrl.expecting_port60h = 1;
					break;

				case 0xdd: // Disable A20 Address Line
					g_memory.set_A20_line(false);
					break;

				case 0xdf: // Enable A20 Address Line
					g_memory.set_A20_line(true);
					break;

				case 0xc1: // Continuous Input Port Poll, Low
				case 0xc2: // Continuous Input Port Poll, High
					PERRF(LOG_KEYB, "io write 0x64: command = %02xh\n", (unsigned) value);
					break;
				case 0xe0: // Read Test Inputs
					//return T0 and T1 as 0 to please the POST procedure 56
					controller_enQ(0x00, 0);
					break;

				case 0xfe: // System (cpu?) Reset, transition to real mode
					PDEBUGF(LOG_V2, LOG_KEYB, "io write 0x64: command 0xfe: reset cpu\n");
					g_machine.reset(CPU_SOFT_RESET);
					break;

				default:
					if(value==0xff || (value>=0xf0 && value<=0xfd)) {
						/* useless pulse output bit commands ??? */
						PDEBUGF(LOG_V2, LOG_KEYB, "io write to port 64h, useless command %02x\n", (unsigned) value);
						return;
					}
					PERRF(LOG_KEYB,"unsupported io write to keyboard port %x, value = %x\n",
							(unsigned) address, (unsigned) value);
					break;
			}
			break;

		default:
			PERRF(LOG_KEYB, "unknown address in Keyboard::write()\n");
			break;
	}
}

void Keyboard::gen_scancode(uint32_t key)
{
	//thread safety: this procedure is called only by the GUI via the Machine
	unsigned char *scancode;
	uint8_t  i;

	// Ignore scancode if keyboard clock is driven low
	if(!m_s.kbd_ctrl.kbd_clock_enabled || !m_s.kbd_ctrl.self_test_completed) {
		return;
	}

	PDEBUGF(LOG_V2, LOG_KEYB, "gen_scancode(): %s %s\n",
			g_keymap.get_key_name(key), (key >> 31)?"released":"pressed");

	if(!m_s.kbd_ctrl.scancodes_translate) {
		PDEBUGF(LOG_V2, LOG_KEYB, "keyboard: gen_scancode with scancode_translate cleared\n");
	}

	// Ignore scancode if scanning is disabled
	if(!m_s.kbd_buffer.scanning_enabled) {
		return;
	}

	// Switch between make and break code
	if(key & KEY_RELEASED)
		scancode = (unsigned char*)g_scancodes[(key&0xFF)][m_s.kbd_ctrl.current_scancodes_set].brek;
	else
		scancode = (unsigned char*)g_scancodes[(key&0xFF)][m_s.kbd_ctrl.current_scancodes_set].make;

	if(m_s.kbd_ctrl.scancodes_translate) {
		// Translate before send
		uint8_t escaped=0x00;

		for(i=0; i<strlen((const char*)scancode); i++) {
			if(scancode[i] == 0xF0)
				escaped=0x80;
			else {
				PDEBUGF(LOG_V2, LOG_KEYB, "gen_scancode(): writing translated %02x\n", g_translation8042[scancode[i]] | escaped);
				kbd_enQ(g_translation8042[scancode[i]] | escaped);
				escaped=0x00;
			}
		}
	} else {
		// Send raw data
		for(i=0; i<strlen((const char *)scancode); i++) {
			PDEBUGF(LOG_V2, LOG_KEYB, "gen_scancode(): writing raw %02x\n",scancode[i]);
			kbd_enQ(scancode[i]);
		}
	}
}

void Keyboard::set_kbd_clock_enable(bool value)
{
	bool prev_kbd_clock_enabled;

	if(!value) {
		m_s.kbd_ctrl.kbd_clock_enabled = false;
	} else {
		/* is another byte waiting to be sent from the keyboard ? */
		prev_kbd_clock_enabled = m_s.kbd_ctrl.kbd_clock_enabled;
		m_s.kbd_ctrl.kbd_clock_enabled = true;

		if(!prev_kbd_clock_enabled && !m_s.kbd_ctrl.outb) {
			activate_timer();
		}
	}
}

void Keyboard::set_aux_clock_enable(bool value)
{
	bool prev_aux_clock_enabled;

	PDEBUGF(LOG_V2, LOG_KEYB, "set_aux_clock_enable(%u)\n", (unsigned) value);
	if(!value) {
		m_s.kbd_ctrl.aux_clock_enabled = false;
	} else {
		/* is another byte waiting to be sent from the keyboard ? */
		prev_aux_clock_enabled = m_s.kbd_ctrl.aux_clock_enabled;
		m_s.kbd_ctrl.aux_clock_enabled = true;
		if(!prev_aux_clock_enabled && !m_s.kbd_ctrl.outb) {
			activate_timer();
		}
	}
}

uint8_t Keyboard::get_kbd_enable(void)
{
	PDEBUGF(LOG_V2, LOG_KEYB, "get_kbd_enable(): getting kbd_clock_enabled of: %02x\n",
			(unsigned) m_s.kbd_ctrl.kbd_clock_enabled);

	return(m_s.kbd_ctrl.kbd_clock_enabled);
}

void Keyboard::controller_enQ(uint8_t data, unsigned source)
{
	// source is 0 for keyboard, 1 for mouse

	PDEBUGF(LOG_V2, LOG_KEYB, "controller_enQ(%02x) source=%02x\n", (unsigned) data,source);

	// see if we need to Q this byte from the controller
	// remember this includes mouse bytes.
	if(m_s.kbd_ctrl.outb) {
		if(m_s.kbd_ctrl.Qsize >= KBD_CONTROLLER_QSIZE) {
			PERRF(LOG_KEYB, "controller_enq(): controller_Q full!\n");
		}
		m_s.kbd_ctrl.Q[m_s.kbd_ctrl.Qsize++] = data;
		m_s.kbd_ctrl.Qsource = source;
		return;
	}

	// the Q is empty
	if(source == 0) { // keyboard
		m_s.kbd_ctrl.kbd_output_buffer = data;
		m_s.kbd_ctrl.outb = true;
		m_s.kbd_ctrl.auxb = false;
		m_s.kbd_ctrl.inpb = false;
		if(m_s.kbd_ctrl.allow_irq1) {
			m_s.kbd_ctrl.irq1_requested = true;
		}
	} else { // mouse
		m_s.kbd_ctrl.aux_output_buffer = data;
		m_s.kbd_ctrl.outb = true;
		m_s.kbd_ctrl.auxb = true;
		m_s.kbd_ctrl.inpb = false;
		if(m_s.kbd_ctrl.allow_irq12) {
			m_s.kbd_ctrl.irq12_requested = true;
		}
	}
}

void Keyboard::kbd_enQ_imm(uint8_t val)
{
	std::lock_guard<std::mutex> lock(m_kbd_lock);

	if(m_s.kbd_buffer.num_elements >= KBD_ELEMENTS) {
		PERRF(LOG_KEYB, "internal keyboard buffer full (imm)\n");
		return;
	}

	/* enqueue scancode in multibyte internal keyboard buffer */
	/*
	  int tail = ( m_s.kbd_buffer.head +  m_s.kbd_buffer.num_elements) %
		KBD_ELEMENTS;
	*/
	m_s.kbd_ctrl.kbd_output_buffer = val;
	m_s.kbd_ctrl.outb = true;

	if(m_s.kbd_ctrl.allow_irq1)
		m_s.kbd_ctrl.irq1_requested = true;
}

void Keyboard::kbd_enQ(uint8_t scancode)
{
	int tail;

	std::lock_guard<std::mutex> lock(m_kbd_lock);

	PDEBUGF(LOG_V2, LOG_KEYB, "kbd_enQ(0x%02X)\n", (unsigned) scancode);

	if(m_s.kbd_buffer.num_elements >= KBD_ELEMENTS) {
		PINFOF(LOG_V1, LOG_KEYB, "internal keyboard buffer full, ignoring scancode.(%02x)\n",
				(unsigned) scancode);
		return;
	}

	/* enqueue scancode in multibyte internal keyboard buffer */
	PDEBUGF(LOG_V2, LOG_KEYB, "kbd_enQ: putting scancode 0x%02X in internal buffer\n", (unsigned) scancode);
	tail = (m_s.kbd_buffer.head +  m_s.kbd_buffer.num_elements) % KBD_ELEMENTS;
	m_s.kbd_buffer.buffer[tail] = scancode;
	m_s.kbd_buffer.num_elements++;

	if(!m_s.kbd_ctrl.outb && m_s.kbd_ctrl.kbd_clock_enabled) {
		activate_timer();
		PDEBUGF(LOG_V2, LOG_KEYB, "activating timer...\n");
		return;
	}
}

bool Keyboard::mouse_enQ_packet(uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4)
{
	int bytes = 3;
	if(m_s.mouse.im_mode) {
		bytes = 4;
	}

	std::lock_guard<std::mutex> lock(m_mouse_lock);

	if((m_s.mouse_buffer.num_elements + bytes) >= MOUSE_BUFF_SIZE) {
		return false; /* buffer doesn't have the space */
	}

	//MT: mouse_enQ is called only here and it doesn't throw,
	//so the lock_guard can stay in this method
	mouse_enQ(b1);
	mouse_enQ(b2);
	mouse_enQ(b3);
	if(m_s.mouse.im_mode) {
		mouse_enQ(b4);
	}

	return true;
}

void Keyboard::mouse_enQ(uint8_t mouse_data)
{
	/* this method should be called only by mouse_enQ_packet, otherwise rethink
	 * the internal buffer mutex locking procedure
	 */
	int tail;

	PDEBUGF(LOG_V2, LOG_KEYB, "mouse_enQ(%02x)\n", mouse_data);

	if(m_s.mouse_buffer.num_elements >= MOUSE_BUFF_SIZE) {
		PERRF(LOG_KEYB, "[mouse] internal mouse buffer full, ignoring mouse data.(%02x)\n",
				mouse_data);
		return;
	}

	/* enqueue mouse data in multibyte internal mouse buffer */
	tail = (m_s.mouse_buffer.head +  m_s.mouse_buffer.num_elements) % MOUSE_BUFF_SIZE;
	m_s.mouse_buffer.buffer[tail] = mouse_data;
	m_s.mouse_buffer.num_elements++;

	if(!m_s.kbd_ctrl.outb && m_s.kbd_ctrl.aux_clock_enabled) {
		activate_timer();
		return;
	}
}

void Keyboard::kbd_ctrl_to_kbd(uint8_t value)
{
	PDEBUGF(LOG_V2, LOG_KEYB, "controller passed byte %02xh to keyboard\n", value);

	if(m_s.kbd_buffer.expecting_typematic) {
		m_s.kbd_buffer.expecting_typematic = false;
		m_s.kbd_buffer.delay = (value >> 5) & 0x03;
		switch ( m_s.kbd_buffer.delay) {
			case 0: PINFOF(LOG_V1, LOG_KEYB, "setting delay to 250 mS (unused)\n"); break;
			case 1: PINFOF(LOG_V1, LOG_KEYB, "setting delay to 500 mS (unused)\n"); break;
			case 2: PINFOF(LOG_V1, LOG_KEYB, "setting delay to 750 mS (unused)\n"); break;
			case 3: PINFOF(LOG_V1, LOG_KEYB, "setting delay to 1000 mS (unused)\n"); break;
		}
		m_s.kbd_buffer.repeat_rate = value & 0x1f;
		double cps = 1 /((double)(8 + (value & 0x07)) * (double)exp(log((double)2) * (double)((value >> 3) & 0x03)) * 0.00417);
		PINFOF(LOG_V1, LOG_KEYB, "setting repeat rate to %.1f cps (unused)\n", cps);
		kbd_enQ(0xFA); // send ACK
		return;
	}

	if(m_s.kbd_buffer.expecting_led_write) {
		m_s.kbd_buffer.expecting_led_write = false;
		m_s.kbd_buffer.led_status = value;
		PDEBUGF(LOG_V2, LOG_KEYB, "LED status set to %02x\n", (unsigned)  m_s.kbd_buffer.led_status);
		/*
		g_gui.statusbar_setitem( m_statusbar_id[0], value & 0x02);
		g_gui.statusbar_setitem( m_statusbar_id[1], value & 0x04);
		g_gui.statusbar_setitem( m_statusbar_id[2], value & 0x01);
		*/
		kbd_enQ(0xFA); // send ACK %%%
		return;
	}

	if(m_s.kbd_ctrl.expecting_scancodes_set) {
		m_s.kbd_ctrl.expecting_scancodes_set = 0;
		if(value != 0) {
			if(value < 4) {
				m_s.kbd_ctrl.current_scancodes_set = (value-1);
				PINFOF(LOG_V1, LOG_KEYB, "Switched to scancode set %d\n", (unsigned)  m_s.kbd_ctrl.current_scancodes_set + 1);
				kbd_enQ(0xFA);
			} else {
				PERRF(LOG_KEYB, "Received scancodes set out of range: %d\n", value);
				kbd_enQ(0xFF); // send ERROR
			}
		} else {
			// Send ACK (SF patch #1159626)
			kbd_enQ(0xFA);
			// Send current scancodes set to port 0x60
			kbd_enQ(1 + (m_s.kbd_ctrl.current_scancodes_set));
		}
		return;
	}

	switch(value) {
		case 0x00: // ??? ignore and let OS timeout with no response
			kbd_enQ(0xFA); // send ACK %%%
			break;

		case 0x05: // ???
			// (mch) trying to get this to work...
			m_s.kbd_ctrl.sysf = true;
			kbd_enQ_imm(0xfe); // send NACK
			break;

		case 0xed: // LED Write
			m_s.kbd_buffer.expecting_led_write = true;
			kbd_enQ_imm(0xFA); // send ACK %%%
			break;

		case 0xee: // echo
			kbd_enQ(0xEE); // return same byte (EEh) as echo diagnostic
			break;

		case 0xf0: // Select alternate scan code set
			m_s.kbd_ctrl.expecting_scancodes_set = 1;
			PDEBUGF(LOG_V2, LOG_KEYB, "Expecting scancode set info...\n");
			kbd_enQ(0xFA); // send ACK
			break;

		case 0xf2:  // identify keyboard
			PDEBUGF(LOG_V2, LOG_KEYB, "identify keyboard command received\n");

			// XT sends nothing, AT sends ACK
			// MFII with translation sends ACK+ABh+41h
			// MFII without translation sends ACK+ABh+83h
			if(KBD_TYPE != KBD_TYPE_XT) {
				kbd_enQ(0xFA);
				if(KBD_TYPE == KBD_TYPE_MF) {
					kbd_enQ(0xAB);
					if( m_s.kbd_ctrl.scancodes_translate)
						kbd_enQ(0x41);
					else
						kbd_enQ(0x83);
				}
			}
			break;

		case 0xf3:  // typematic info
			m_s.kbd_buffer.expecting_typematic = true;
			PDEBUGF(LOG_V2, LOG_KEYB, "setting typematic info\n");
			kbd_enQ(0xFA); // send ACK
			break;

		case 0xf4:  // enable keyboard
			m_s.kbd_buffer.scanning_enabled = true;
			kbd_enQ(0xFA); // send ACK
			break;

		case 0xf5:  // reset keyboard to power-up settings and disable scanning
			reset_internals(true);
			kbd_enQ(0xFA); // send ACK
			m_s.kbd_buffer.scanning_enabled = false;
			PDEBUGF(LOG_V2, LOG_KEYB, "reset-disable command received\n");
			break;

		case 0xf6:  // reset keyboard to power-up settings and enable scanning
			reset_internals(true);
			kbd_enQ(0xFA); // send ACK
			m_s.kbd_buffer.scanning_enabled = true;
			PDEBUGF(LOG_V2, LOG_KEYB, "reset-enable command received\n");
			break;

		case 0xfe:  // resend. aiiee.
			PERRF(LOG_KEYB, "got 0xFE (resend)");
			break;

		case 0xff:  // reset: internal keyboard reset and afterwards the BAT
			PDEBUGF(LOG_V2, LOG_KEYB, "reset command received\n");
			reset_internals(true);
			kbd_enQ(0xFA); // send ACK
			m_s.kbd_ctrl.bat_in_progress = true;
			kbd_enQ(0xAA); // BAT test passed
			break;

		case 0xd3:
			kbd_enQ(0xfa);
			break;

		case 0xf7:  // PS/2 Set All Keys To Typematic
		case 0xf8:  // PS/2 Set All Keys to Make/Break
		case 0xf9:  // PS/2 Set All Keys to Make
		case 0xfa:  // PS/2 Set All Keys to Typematic Make/Break
		case 0xfb:  // PS/2 Set Key Type to Typematic
		case 0xfc:  // PS/2 Set Key Type to Make/Break
		case 0xfd:  // PS/2 Set Key Type to Make
		default:
			PERRF(LOG_KEYB, "kbd_ctrl_to_kbd(): got value of 0x%02X\n", value);
			kbd_enQ(0xFE); /* send NACK */
			break;
	}
}

void Keyboard::timer_handler()
{
	unsigned retval;

	retval = periodic(KBD_SERIAL_DELAY);

	if(retval&0x01) {
		g_pic.raise_irq(1);
	}

	if(retval&0x02) {
		g_pic.raise_irq(12);
	}
}

unsigned Keyboard::periodic(uint32_t usec_delta)
{
	uint8_t  retval;

	if(m_s.kbd_ctrl.self_test_in_progress) {
		if(usec_delta >= m_s.kbd_ctrl.timer_pending) {
			// self test complete
			m_s.kbd_ctrl.self_test_completed = true;
			m_s.kbd_ctrl.self_test_in_progress = false;
			m_s.kbd_ctrl.sysf = true;
			controller_enQ(0x55, 0);  // controller OK
		} else {
			m_s.kbd_ctrl.timer_pending -= usec_delta;
			return 0;
		}
	}

	retval = m_s.kbd_ctrl.irq1_requested | (m_s.kbd_ctrl.irq12_requested << 1);
	m_s.kbd_ctrl.irq1_requested = false;
	m_s.kbd_ctrl.irq12_requested = false;

	if(m_s.kbd_ctrl.timer_pending == 0) {
		return retval;
	}

	if(usec_delta >= m_s.kbd_ctrl.timer_pending) {
		m_s.kbd_ctrl.timer_pending = 0;
	} else {
		m_s.kbd_ctrl.timer_pending -= usec_delta;
		return retval;
	}

	if(m_s.kbd_ctrl.outb) {
		return retval;
	}

	std::lock_guard<std::mutex> klock(m_kbd_lock);

	/* nothing in outb, look for possible data xfer from keyboard or mouse */
	if(m_s.kbd_buffer.num_elements &&
			(m_s.kbd_ctrl.kbd_clock_enabled || m_s.kbd_ctrl.bat_in_progress)) {
		m_s.kbd_ctrl.kbd_output_buffer = m_s.kbd_buffer.buffer[m_s.kbd_buffer.head];
		m_s.kbd_ctrl.outb = true;
		PDEBUGF(LOG_V2, LOG_KEYB, "key in internal buffer waiting = 0x%02x\n",m_s.kbd_ctrl.kbd_output_buffer);
		// commented out since this would override the current state of the
		// mouse buffer flag - no bug seen - just seems wrong (das)
		//     m_s.kbd_ctrl.auxb = false;
		m_s.kbd_buffer.head = (m_s.kbd_buffer.head + 1) % KBD_ELEMENTS;
		m_s.kbd_buffer.num_elements--;
		if(m_s.kbd_ctrl.allow_irq1) {
			m_s.kbd_ctrl.irq1_requested = true;
		}
	} else {
		create_mouse_packet(false);
		std::lock_guard<std::mutex> mlock(m_mouse_lock);
		if(m_s.kbd_ctrl.aux_clock_enabled &&  m_s.mouse_buffer.num_elements) {
			m_s.kbd_ctrl.aux_output_buffer = m_s.mouse_buffer.buffer[m_s.mouse_buffer.head];
			m_s.kbd_ctrl.outb = true;
			m_s.kbd_ctrl.auxb = true;
			PDEBUGF(LOG_V2, LOG_KEYB, "[mouse] key in internal buffer waiting = 0x%02x\n", m_s.kbd_ctrl.aux_output_buffer);
			m_s.mouse_buffer.head = ( m_s.mouse_buffer.head + 1) % MOUSE_BUFF_SIZE;
			m_s.mouse_buffer.num_elements--;
			if(m_s.kbd_ctrl.allow_irq12) {
				m_s.kbd_ctrl.irq12_requested = true;
			}
		} else {
			PDEBUGF(LOG_V2, LOG_KEYB, "no keys waiting\n");
		}
	}
	return retval;
}

void Keyboard::activate_timer(uint32_t _usec_delta)
{
	if(m_s.kbd_ctrl.timer_pending == 0) {
		m_s.kbd_ctrl.timer_pending = _usec_delta;
	}
}

void Keyboard::kbd_ctrl_to_mouse(uint8_t value)
{
	// if we are not using a ps2 mouse, some of the following commands need to return different values
	bool is_ps2 = 0;
	if( (m_s.mouse.type == MOUSE_TYPE_PS2) || (m_s.mouse.type == MOUSE_TYPE_IMPS2) ) {
		is_ps2 = 1;
	}

	PDEBUGF(LOG_V2, LOG_KEYB, "MOUSE: kbd_ctrl_to_mouse(%02xh)", (unsigned) value);
	PDEBUGF(LOG_V2, LOG_KEYB, "  enable = %u", (unsigned)  m_s.mouse.enable);
	PDEBUGF(LOG_V2, LOG_KEYB, "  allow_irq12 = %u", (unsigned)  m_s.kbd_ctrl.allow_irq12);
	PDEBUGF(LOG_V2, LOG_KEYB, "  aux_clock_enabled = %u\n", (unsigned)  m_s.kbd_ctrl.aux_clock_enabled);

	// an ACK (0xFA) is always the first response to any valid input
	// received from the system other than Set-Wrap-Mode & Resend-Command

	if(m_s.kbd_ctrl.expecting_mouse_parameter) {
		m_s.kbd_ctrl.expecting_mouse_parameter = 0;
		switch(m_s.kbd_ctrl.last_mouse_command) {

			case 0xf3: // Set Mouse Sample Rate
				m_s.mouse.sample_rate = value;
				PDEBUGF(LOG_V2, LOG_KEYB, "mouse: sampling rate set: %d Hz\n", value);
				if((value == 200) && (!m_s.mouse.im_request)) {
					m_s.mouse.im_request = 1;
				} else if((value == 100) && (m_s.mouse.im_request == 1)) {
					m_s.mouse.im_request = 2;
				} else if((value == 80) && (m_s.mouse.im_request == 2)) {
					if(m_s.mouse.type == MOUSE_TYPE_IMPS2) {
						PINFOF(LOG_V1, LOG_KEYB, "wheel mouse mode enabled\n");
						m_s.mouse.im_mode = true;
					} else {
						PINFOF(LOG_V1, LOG_KEYB, "wheel mouse mode request rejected\n");
					}
					m_s.mouse.im_request = 0;
				} else {
					m_s.mouse.im_request = 0;
				}
				controller_enQ(0xFA, 1); // ack
				break;

			case 0xe8: // Set Mouse Resolution
				switch(value) {
					case 0:
						m_s.mouse.resolution_cpmm = 1;
						break;
					case 1:
						m_s.mouse.resolution_cpmm = 2;
						break;
					case 2:
						m_s.mouse.resolution_cpmm = 4;
						break;
					case 3:
						m_s.mouse.resolution_cpmm = 8;
						break;
					default:
						PDEBUGF(LOG_V1, LOG_KEYB, "mouse: unknown resolution %d\n", value);
						break;
				}
				PDEBUGF(LOG_V1, LOG_KEYB, "mouse: resolution set to %d counts per mm\n", m_s.mouse.resolution_cpmm);
				controller_enQ(0xFA, 1); // ack
				break;

			default:
				PERRF(LOG_KEYB, "MOUSE: unknown last command (%02xh)\n", (unsigned)  m_s.kbd_ctrl.last_mouse_command);
				break;
		}
	} else {
		m_s.kbd_ctrl.expecting_mouse_parameter = 0;
		m_s.kbd_ctrl.last_mouse_command = value;

		// test for wrap mode first
		if(m_s.mouse.mode == MOUSE_MODE_WRAP) {
			// if not a reset command or reset wrap mode
			// then just echo the byte.
			if((value != 0xff) && (value != 0xec)) {
				PDEBUGF(LOG_V2, LOG_KEYB, "mouse: wrap mode: ignoring command 0x%02X\n",value);
				controller_enQ(value, 1);
				// bail out
				return;
			}
		}

		switch (value) {
			case 0xe6: // Set Mouse Scaling to 1:1
				controller_enQ(0xFA, 1); // ACK
				m_s.mouse.scaling = 2;
				PDEBUGF(LOG_V1, LOG_KEYB, "mouse: scaling set to 1:1\n");
				break;

			case 0xe7: // Set Mouse Scaling to 2:1
				controller_enQ(0xFA, 1); // ACK
				m_s.mouse.scaling = 2;
				PDEBUGF(LOG_V1, LOG_KEYB, "mouse: scaling set to 2:1\n");
				break;

			case 0xe8: // Set Mouse Resolution
				controller_enQ(0xFA, 1); // ACK
				m_s.kbd_ctrl.expecting_mouse_parameter = 1;
				break;

			case 0xea: // Set Stream Mode
				PDEBUGF(LOG_V2, LOG_KEYB, "mouse: stream mode on\n");
				m_s.mouse.mode = MOUSE_MODE_STREAM;
				controller_enQ(0xFA, 1); // ACK
				break;

			case 0xec: // Reset Wrap Mode
				// unless we are in wrap mode ignore the command
				if( m_s.mouse.mode == MOUSE_MODE_WRAP) {
					PDEBUGF(LOG_V2, LOG_KEYB, "mouse: wrap mode off\n");
					// restore previous mode except disable stream mode reporting.
					// ### TODO disabling reporting in stream mode
					m_s.mouse.mode =  m_s.mouse.saved_mode;
					controller_enQ(0xFA, 1); // ACK
				}
				break;
			case 0xee: // Set Wrap Mode
				// ### TODO flush output queue.
				// ### TODO disable interrupts if in stream mode.
				PDEBUGF(LOG_V2, LOG_KEYB, "mouse: wrap mode on\n");
				m_s.mouse.saved_mode =  m_s.mouse.mode;
				m_s.mouse.mode = MOUSE_MODE_WRAP;
				controller_enQ(0xFA, 1); // ACK
				break;

			case 0xf0: // Set Remote Mode (polling mode, i.e. not stream mode.)
				PDEBUGF(LOG_V2, LOG_KEYB, "mouse: remote mode on\n");
				// ### TODO should we flush/discard/ignore any already queued packets?
				m_s.mouse.mode = MOUSE_MODE_REMOTE;
				controller_enQ(0xFA, 1); // ACK
				break;

			case 0xf2: // Read Device Type
				controller_enQ(0xFA, 1); // ACK
				if(m_s.mouse.im_mode)
					controller_enQ(0x03, 1); // Device ID (wheel z-mouse)
				else
					controller_enQ(0x00, 1); // Device ID (standard)
				PDEBUGF(LOG_V2, LOG_KEYB, "mouse: read mouse ID\n");
				break;

			case 0xf3: // Set Mouse Sample Rate (sample rate written to port 60h)
				controller_enQ(0xFA, 1); // ACK
				m_s.kbd_ctrl.expecting_mouse_parameter = 1;
				break;

			case 0xf4: // Enable (in stream mode)
				// is a mouse present?
				if(is_ps2) {
					m_s.mouse.enable = true;
					controller_enQ(0xFA, 1); // ACK
					PDEBUGF(LOG_V2, LOG_KEYB, "mouse enabled (stream mode)\n");
				} else {
					// a mouse isn't present.  We need to return a 0xFE (resend) instead of a 0xFA (ACK)
					controller_enQ(0xFE, 1); // RESEND
					m_s.kbd_ctrl.tim = true;
				}
				break;

			case 0xf5: // Disable (in stream mode)
				m_s.mouse.enable = false;
				controller_enQ(0xFA, 1); // ACK
				PDEBUGF(LOG_V2, LOG_KEYB, "mouse disabled (stream mode)\n");
				break;

			case 0xf6: // Set Defaults
				m_s.mouse.sample_rate     = 100; /* reports per second (default) */
				m_s.mouse.resolution_cpmm = 4; /* 4 counts per millimeter (default) */
				m_s.mouse.scaling         = 1;   /* 1:1 (default) */
				m_s.mouse.enable = false;
				m_s.mouse.mode            = MOUSE_MODE_STREAM;
				controller_enQ(0xFA, 1); // ACK
				PDEBUGF(LOG_V2, LOG_KEYB, "mouse: set defaults\n");
				break;

			case 0xff: // Reset
				// is a mouse present?
				if(is_ps2) {
					m_s.mouse.sample_rate     = 100; /* reports per second (default) */
					m_s.mouse.resolution_cpmm = 4; /* 4 counts per millimeter (default) */
					m_s.mouse.scaling         = 1;   /* 1:1 (default) */
					m_s.mouse.mode            = MOUSE_MODE_RESET;
					m_s.mouse.enable = false;
					if(m_s.mouse.im_mode)
						PINFOF(LOG_V1, LOG_KEYB, "wheel mouse mode disabled\n");
						m_s.mouse.im_mode         = 0;
						/* (mch) NT expects an ack here */
						controller_enQ(0xFA, 1); // ACK
						controller_enQ(0xAA, 1); // completion code
						controller_enQ(0x00, 1); // ID code (standard after reset)
						PDEBUGF(LOG_V2, LOG_KEYB, "mouse reset\n");
				} else {
					// a mouse isn't present.  We need to return a 0xFE (resend) instead of a 0xFA (ACK)
					controller_enQ(0xFE, 1); // RESEND
					m_s.kbd_ctrl.tim = true;
				}
				break;

			case 0xe9: { // Get mouse information
				controller_enQ(0xFA, 1); // ACK
				uint8_t status_byte = m_s.mouse.get_status_byte();
				controller_enQ(status_byte, 1); // status
				controller_enQ(m_s.mouse.get_resolution_byte(), 1); // resolution
				controller_enQ(m_s.mouse.sample_rate, 1); // sample rate
				PDEBUGF(LOG_V2, LOG_KEYB, "mouse: get mouse information: 0x%02X\n", status_byte);
				break;
			}

			case 0xeb: // Read Data (send a packet when in Remote Mode)
				controller_enQ(0xFA, 1); // ACK
				// perhaps we should be adding some movement here.
				mouse_enQ_packet(((m_s.mouse.button_status & 0x0f) | 0x08),
						0x00, 0x00, 0x00); // bit3 of first byte always set
				//assumed we really aren't in polling mode, a rather odd assumption.
				PERRF(LOG_KEYB, "mouse: Warning: Read Data command partially supported\n");
				break;

			case 0xbb: // OS/2 Warp 3 uses this command
				PERRF(LOG_KEYB, "mouse: ignoring 0xbb command\n");
				break;

			default:
				// If PS/2 mouse present, send NACK for unknown commands, otherwise ignore
				if(is_ps2) {
					PERRF(LOG_KEYB, "kbd_ctrl_to_mouse(): got value of 0x%02X\n", value);
					controller_enQ(0xFE, 1); /* send NACK */
				}
				break;
		}
	}
}

void Keyboard::create_mouse_packet(bool force_enq)
{
	uint8_t b1, b2, b3, b4;

	if(m_s.mouse_buffer.num_elements && !force_enq)
		return;

	int16_t delta_x = m_s.mouse.delayed_dx;
	int16_t delta_y = m_s.mouse.delayed_dy;
	uint8_t button_state = m_s.mouse.button_status | 0x08;

	if(!force_enq && !delta_x && !delta_y) {
		return;
	}

	if(delta_x>254) { delta_x = 254; }
	if(delta_x<-254) { delta_x = -254; }
	if(delta_y>254) { delta_y = 254; }
	if(delta_y<-254) { delta_y = -254; }

	b1 = (button_state & 0x0f) | 0x08; // bit3 always set

	if((delta_x>=0) && (delta_x<=255)) {
		b2 = (uint8_t) delta_x;
		m_s.mouse.delayed_dx -= delta_x;
	} else if(delta_x > 255) {
		b2 = (uint8_t) 0xff;
		m_s.mouse.delayed_dx -= 255;
	} else if(delta_x >= -256) {
		b2 = (uint8_t) delta_x;
		b1 |= 0x10;
		m_s.mouse.delayed_dx -= delta_x;
	} else {
		b2 = (uint8_t) 0x00;
		b1 |= 0x10;
		m_s.mouse.delayed_dx += 256;
	}

	if((delta_y>=0) && (delta_y<=255)) {
		b3 = (uint8_t) delta_y;
		m_s.mouse.delayed_dy -= delta_y;
	} else if(delta_y > 255) {
		b3 = (uint8_t) 0xff;
		m_s.mouse.delayed_dy -= 255;
	} else if(delta_y >= -256) {
		b3 = (uint8_t) delta_y;
		b1 |= 0x20;
		m_s.mouse.delayed_dy -= delta_y;
	} else {
		b3 = (uint8_t) 0x00;
		b1 |= 0x20;
		m_s.mouse.delayed_dy += 256;
	}

	b4 = uint8_t(-m_s.mouse.delayed_dz);

	mouse_enQ_packet(b1, b2, b3, b4);
}

void Keyboard::mouse_motion(int delta_x, int delta_y, int delta_z, uint button_state)
{
	bool force_enq = false;

	// don't generate interrupts if we are in remote mode.
	if(m_s.mouse.mode == MOUSE_MODE_REMOTE) {
		// is there any point in doing any work if we don't act on the result?
		// so go home.
		return;
	}

	if(!m_s.mouse.im_mode) {
		delta_z = 0;
	}

	button_state &= 0x7;

	if((delta_x==0) && (delta_y==0) && (delta_z==0)
			&& (m_s.mouse.button_status == button_state))
	{
		PDEBUGF(LOG_V2, LOG_KEYB, "mouse: useless call. ignoring.\n");
		return;
	} else {
		PDEBUGF(LOG_V2, LOG_KEYB, "mouse motion: dx=%d, dy=%d, dz=%d, btns=%d\n",
				delta_x, delta_y, delta_z, button_state);
	}

	if((m_s.mouse.button_status != button_state) || delta_z) {
		force_enq = true;
	}

	m_s.mouse.button_status = button_state;

	if(!m_s.mouse.enable || !m_s.kbd_ctrl.self_test_completed) {
		return;
	}

	if(m_mouse_acc) {
		//deltas are in pixels
		//calc the counters value taking the mouse resolution in consideration
		float x_mm = (delta_x * m_s.screen_mmpd);
		float y_mm = (delta_y * m_s.screen_mmpd);
		delta_x = m_s.mouse.resolution_cpmm * x_mm;
		delta_y = m_s.mouse.resolution_cpmm * y_mm;
	}

	if(delta_x>255) { delta_x = 255; }
	if(delta_y>255) { delta_y = 255; }
	if(delta_x<-256) { delta_x = -256; }
	if(delta_y<-256) { delta_y = -256; }

	m_s.mouse.delayed_dx += delta_x;
	m_s.mouse.delayed_dy += delta_y;
	m_s.mouse.delayed_dz = delta_z;

	if((m_s.mouse.delayed_dx>255) || (m_s.mouse.delayed_dx<-256)
		|| (m_s.mouse.delayed_dy>255) || (m_s.mouse.delayed_dy<-256))
	{
		force_enq = true;
	}

	create_mouse_packet(force_enq);
}

uint8_t Keyboard::State::Mouse::get_status_byte()
{
	// top bit is 0 , bit 6 is 1 if remote mode.
	uint8_t ret = (uint8_t) ((mode == MOUSE_MODE_REMOTE) ? 0x40 : 0);
	ret |= (enable << 5);
	ret |= (scaling == 1) ? 0 : (1 << 4);
	ret |= ((button_status & 0x1) << 2); // left button
	ret |= ((button_status & 0x2) >> 1); // right button
	return ret;
}

uint8_t Keyboard::State::Mouse::get_resolution_byte()
{
	uint8_t ret = 0;
	switch(resolution_cpmm) {
		case 1:
			ret = 0;
			break;
		case 2:
			ret = 1;
			break;
		case 4:
			ret = 2;
			break;
		case 8:
			ret = 3;
			break;
		default:
			PERRF(LOG_KEYB, "mouse: invalid resolution_cpmm\n");
	};
	return ret;
}
