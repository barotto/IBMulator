/*
 * Copyright (c) 2002-2014  The Bochs Project
 * Copyright (C) 2015-2021  Marco Bortolin
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
#include "pic.h"
#include "machine.h"
#include "hardware/cpu.h"
#include "hardware/devices.h"
#include <cstring>

IODEVICE_PORTS(PIC) = {
	{ 0x20, 0x21, PORT_8BIT|PORT_RW },
	{ 0xA0, 0xA1, PORT_8BIT|PORT_RW }
};

PIC::PIC(Devices* _dev)
: IODevice(_dev)
{
	memset(&m_s, 0, sizeof(m_s));
}

PIC::~PIC()
{

}

void PIC::install()
{
	IODevice::install();
	g_machine.register_irq(2, name());
}

void PIC::remove()
{
	IODevice::remove();
	g_machine.unregister_irq(2, name());
}

void PIC::reset(unsigned)
{
	m_s.master.single_PIC           = false;
	m_s.master.interrupt_offset     = 0x08; // IRQ0 = INT 0x08
	m_s.master.u.slave_connect_mask = 0x04; // slave PIC connected to IRQ2 of master
	m_s.master.sfnm                 = false;// normal nested mode (UNUSED)
	m_s.master.buffered_mode        = false;// unbuffered mode (UNUSED)
	m_s.master.is_master            = true; // master PIC
	m_s.master.auto_eoi             = false;// manual EOI from CPU
	m_s.master.imr                  = 0xFF; // all IRQ's initially masked
	m_s.master.isr                  = 0x00; // no IRQ's in service
	m_s.master.irr                  = 0x00; // no IRQ's requested
	m_s.master.read_reg_select      = 0;    // IRR
	m_s.master.irq                  = 0;
	m_s.master.lowest_priority      = 7;
	m_s.master.INT                  = false;
	m_s.master.IRQ_in               = 0;
	m_s.master.init.in_init         = false;
	m_s.master.init.requires_4      = false;
	m_s.master.init.byte_expected   = 0;
	m_s.master.special_mask         = false;
	m_s.master.polled               = false;
	m_s.master.rotate_on_autoeoi    = false;
	m_s.master.edge_level           = 0;    // never set again (mode changes in Bochs for PIIX3)

	m_s.slave.single_PIC            = false;
	m_s.slave.interrupt_offset      = 0x70; // IRQ8 = INT 0x70
	m_s.slave.u.slave_id            = 0x02; // slave PIC connected to IRQ2 of master
	m_s.slave.sfnm                  = false;// normal nested mode (UNUSED)
	m_s.slave.buffered_mode         = false;// unbuffered mode (UNUSED)
	m_s.slave.is_master             = false;// slave PIC
	m_s.slave.auto_eoi              = false;// manual EOI from CPU
	m_s.slave.imr                   = 0xFF; // all IRQ's initially masked
	m_s.slave.isr                   = 0x00; // no IRQ's in service
	m_s.slave.irr                   = 0x00; // no IRQ's requested
	m_s.slave.read_reg_select       = 0;    // IRR
	m_s.slave.irq                   = 0;
	m_s.slave.lowest_priority       = 7;
	m_s.slave.INT                   = false;
	m_s.slave.IRQ_in                = 0;
	m_s.slave.init.in_init          = false;
	m_s.slave.init.requires_4       = false;
	m_s.slave.init.byte_expected    = 0;
	m_s.slave.special_mask          = false;
	m_s.slave.polled                = false;
	m_s.slave.rotate_on_autoeoi     = false;
	m_s.slave.edge_level            = 0;    // never set again (mode changes in Bochs for PIIX3)
}

void PIC::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_PIC, "saving state\n");

	StateHeader h;
	h.name = name();
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);
}

void PIC::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_PIC, "restoring state\n");

	StateHeader h;
	h.name = name();
	h.data_size = sizeof(m_s);
	_state.read(&m_s,h);
}

uint16_t PIC::read(i8259 & _pic, uint16_t _address, unsigned _io_len)
{
	uint16_t value = 0;

	if(_pic.polled) {
		// In polled mode. Treat this as an interrupt acknowledge
		PDEBUGF(LOG_V2, LOG_PIC, "%s: polled, read current IRQ\n", _pic.name());
		clear_highest_interrupt(_pic);
		_pic.polled = 0;
		service(_pic);
		value = _pic.irq;
		if(_io_len > 1) {
			value = value << 8 | _pic.irq;
		}
	} else {
		switch(_address) {
			case 0:
				if(_pic.read_reg_select) {
					PDEBUGF(LOG_V2, LOG_PIC, "%s: read ISR\n", _pic.name());
					value = _pic.isr;
				} else {
					PDEBUGF(LOG_V2, LOG_PIC, "%s: read IRR\n", _pic.name());
					value = _pic.irr;
				}
				break;
			case 1:
				PDEBUGF(LOG_V2, LOG_PIC, "%s: read IMR\n", _pic.name());
				value = _pic.imr;
				break;
			default:
				break;
		}
	}

	return value;
}

uint16_t PIC::read(uint16_t _address, unsigned io_len)
{
	uint16_t value = 0;

	switch(_address) {
		case 0x20:
		case 0x21:
			value = read(m_s.master, _address - 0x20, io_len);
			break;
		case 0xA0:
		case 0xA1:
			value = read(m_s.slave, _address - 0xA0, io_len);
			break;
		default:
			PERRF(LOG_PIC, "io read from address %04x\n", _address);
			break;
	}

	PDEBUGF(LOG_V2, LOG_PIC, "read  0x%x -> 0x%x\n", _address, value);

	return value;
}

void PIC::write(i8259 &_pic, uint8_t _address, uint8_t _value)
{
	switch(_address) {
		case 0:
			// ICW1
			if(_value & 0x10) {
				_pic.single_PIC = (_value & 0x02);
				_pic.init.in_init = true;
				_pic.init.requires_4 = (_value & 0x01);
				_pic.init.byte_expected = 2; // operation command 2
				_pic.imr = 0x00; // clear the irq mask register
				_pic.isr = 0x00; // no IRQ's in service
				_pic.irr = 0x00; // no IRQ's requested
				_pic.lowest_priority = 7;
				_pic.INT = false; // reprogramming clears previous INTR request
				_pic.auto_eoi = false;
				_pic.rotate_on_autoeoi = false;
				PDEBUGF(LOG_V1, LOG_PIC, "%s: ICW1: %s, %s, %s\n",
						_pic.name(),
						(_pic.init.requires_4) ? "w/ ICW4" : "w/o ICW4",
						(_pic.single_PIC) ? "single" : "cascade",
						(_value & 0x08) ? "level sensitive" : "edge triggered"
				);
				if(_pic.single_PIC) {
					PERRF(LOG_PIC, "%s: ICW1: single mode not supported\n", _pic.name());
				}
				if(_value & 0x08) {
					PERRF(LOG_PIC, "%s: ICW1: level sensitive mode not supported\n", _pic.name());
				}
				if(_pic.is_master) {
					g_cpu.clear_INTR();
				} else {
					m_s.master.IRQ_in &= ~(m_s.master.u.slave_connect_mask);
				}
				return;
			}

			// OCW3
			if((_value & 0x18) == 0x08) {
				uint8_t special_mask = (_value & 0x60) >> 5;
				uint8_t poll         = (_value & 0x04) >> 2;
				uint8_t read_op      = (_value & 0x03);
				if(poll) {
					_pic.polled = true;
					PDEBUGF(LOG_V2, LOG_PIC, "%s: OCW3: polling\n", _pic.name());
					// according to spec, polling overrides read
					return;
				}
				if(read_op == 0x02) {
					_pic.read_reg_select = 0;
					PDEBUGF(LOG_V2, LOG_PIC, "%s: OCW3: read IRR\n", _pic.name());
				} else if(read_op == 0x03) {
					PDEBUGF(LOG_V2, LOG_PIC, "%s: OCW3: read ISR\n", _pic.name());
					_pic.read_reg_select = 1;
				}
				if(special_mask == 0x02) {
					PDEBUGF(LOG_V2, LOG_PIC, "%s: OCW3: cancel special mask\n", _pic.name());
					_pic.special_mask = 0;
				} else if(special_mask == 0x03) {
					PDEBUGF(LOG_V2, LOG_PIC, "%s: OCW3: set special mask\n", _pic.name());
					_pic.special_mask = 1;
					service(_pic);
				}
				return;
			}

			// OCW2
			switch(_value) {
				case 0x00: // Rotate in auto eoi mode clear
				case 0x80: // Rotate in auto eoi mode set
					_pic.rotate_on_autoeoi = (_value != 0);
					PDEBUGF(LOG_V2, LOG_PIC, "%s: OCW2: rotate on Auto-EOI: %u\n",
							_pic.name(), _pic.rotate_on_autoeoi);
					break;

				case 0xA0: // Rotate on non-specific end of interrupt
				case 0x20: // end of interrupt command
					clear_highest_interrupt(_pic);
					if(_value == 0xA0) {
						_pic.lowest_priority++;
						if(_pic.lowest_priority > 7) {
							_pic.lowest_priority = 0;
						}
						PDEBUGF(LOG_V2, LOG_PIC, "%s: OCW2: EOI with rotation, lowest priority %u\n",
								_pic.name(), _pic.lowest_priority);
					} else {
						PDEBUGF(LOG_V2, LOG_PIC, "%s: OCW2: EOI\n", _pic.name());
					}
					service(_pic);
					break;

				case 0x40: // Intel PIC spec-sheet seems to indicate this should be ignored
					PDEBUGF(LOG_V2, LOG_PIC, "%s: OCW2: IRQ no-op\n", _pic.name());
					break;

				case 0x60: // specific EOI 0
				case 0x61: // specific EOI 1
				case 0x62: // specific EOI 2
				case 0x63: // specific EOI 3
				case 0x64: // specific EOI 4
				case 0x65: // specific EOI 5
				case 0x66: // specific EOI 6
				case 0x67: // specific EOI 7
					_pic.isr &= ~(1 << (_value-0x60));
					PDEBUGF(LOG_V2, LOG_PIC, "%s: OCW2: specific EOI %u\n", _pic.name(), _value-0x60);
					service(_pic);
					break;

				// IRQ lowest priority commands
				case 0xC0: // 0 7 6 5 4 3 2 1
				case 0xC1: // 1 0 7 6 5 4 3 2
				case 0xC2: // 2 1 0 7 6 5 4 3
				case 0xC3: // 3 2 1 0 7 6 5 4
				case 0xC4: // 4 3 2 1 0 7 6 5
				case 0xC5: // 5 4 3 2 1 0 7 6
				case 0xC6: // 6 5 4 3 2 1 0 7
				case 0xC7: // 7 6 5 4 3 2 1 0
					_pic.lowest_priority = _value - 0xC0;
					PDEBUGF(LOG_V2, LOG_PIC, "%s: OCW2: set IRQ lowest priority %u\n", _pic.name(), _pic.lowest_priority);
					break;

				case 0xE0: // specific EOI and rotate 0
				case 0xE1: // specific EOI and rotate 1
				case 0xE2: // specific EOI and rotate 2
				case 0xE3: // specific EOI and rotate 3
				case 0xE4: // specific EOI and rotate 4
				case 0xE5: // specific EOI and rotate 5
				case 0xE6: // specific EOI and rotate 6
				case 0xE7: // specific EOI and rotate 7
					_pic.isr &= ~(1 << (_value-0xE0));
					_pic.lowest_priority = (_value - 0xE0);
					PDEBUGF(LOG_V2, LOG_PIC, "%s: OCW2: specific EOI and rotate %u\n", _pic.name(), _value-0xE0);
					service(_pic);
					break;

				case 0x02: // single mode bit: 1 = single, 0 = cascade
					// ignore. 386BSD writes this value but works with it ignored.
					PDEBUGF(LOG_V2, LOG_PIC, "%s: OCW2: single mode %u\n", _pic.name(), _value-0xE0);
					break;

				default:
					PERRF(LOG_PIC, "%s: OCW2: invalid value %02x\n", _pic.name(), _value);
					break;
			}
			break;

		case 1:
			// initialization mode operation
			if(_pic.init.in_init) {
				switch(_pic.init.byte_expected) {
					case 2:
						_pic.interrupt_offset = _value & 0xf8;
						_pic.init.byte_expected = 3;
						PDEBUGF(LOG_V1, LOG_PIC, "%s: ICW2: offset INT 0x%02x\n", _pic.name(), _pic.interrupt_offset);
						return;
					case 3:
						PDEBUGF(LOG_V1, LOG_PIC, "%s: ICW3: 0x%02x\n", _pic.name(), _value);
						if(_pic.init.requires_4) {
							_pic.init.byte_expected = 4;
						} else {
							_pic.init.in_init = false;
						}
						return;
					case 4:
						_pic.auto_eoi = (_value & 0x02) >> 1;
						PDEBUGF(LOG_V1, LOG_PIC, "%s: ICW4: %s\n", _pic.name(), _pic.auto_eoi ? "auto EOI" : "normal EOI");
						if(!(_value & 0x01)) {
							PERRF(LOG_PIC, "%s: ICW4: MCS-80/86 mode not supported!\n");
						}
						_pic.init.in_init = false;
						return;
					default:
						PERRF(LOG_PIC, "%s expecting bad init command\n", _pic.name());
						break;
				}
			}

			// normal operation
			PDEBUGF(LOG_V1, LOG_PIC, "%s: setting IMR=0x%02X\n", _pic.name(), _value);
			//_pic.imr = _value;
			//service(_pic);
			if(_pic.is_master) {
				set_master_imr(_value);
			} else {
				set_slave_imr(_value);
			}
			return;
		default:
			break;
	}
}

void PIC::write(uint16_t _address, uint16_t _value, unsigned /*io_len*/)
{
	PDEBUGF(LOG_V2, LOG_PIC, "write 0x%x <- 0x%x\n", _address, _value);

	switch(_address) {
		case 0x20:
		case 0x21:
			write(m_s.master, _address - 0x20, _value);
			break;
		case 0xA0:
		case 0xA1:
			write(m_s.slave, _address - 0xA0, _value);
			break;
		default:
			break;
	}
}

void PIC::lower_irq(unsigned irq_no)
{
	uint8_t mask = (1 << (irq_no & 7));
	if((irq_no <= 7) && (m_s.master.IRQ_in & mask)) {
		PDEBUGF(LOG_V2, LOG_PIC, "IRQ line %u (%s) now low\n", irq_no,
				g_machine.get_irq_names(irq_no).c_str());
		m_s.master.IRQ_in &= ~(mask);
		m_s.master.irr &= ~(mask);
	} else if((irq_no > 7) && (irq_no <= 15) && (m_s.slave.IRQ_in & mask)) {
		PDEBUGF(LOG_V2, LOG_PIC, "IRQ line %u (%s) now low\n", irq_no,
				g_machine.get_irq_names(irq_no).c_str());
		m_s.slave.IRQ_in &= ~(mask);
		m_s.slave.irr &= ~(mask);
	}
}

void PIC::raise_irq(unsigned irq_no)
{
	uint8_t mask = (1 << (irq_no & 7));
	if((irq_no <= 7) && !(m_s.master.IRQ_in & mask)) {
		PDEBUGF(LOG_V1, LOG_PIC, "IRQ line %u (%s) now high (mIMR=%X,mINT=%u,IF=%u)\n", irq_no,
				g_machine.get_irq_names(irq_no).c_str(), m_s.master.imr, m_s.master.INT, FLAG_IF);
		m_s.master.IRQ_in |= mask;
		m_s.master.irr |= mask;
		service(m_s.master);
	} else if((irq_no > 7) && (irq_no <= 15) && !(m_s.slave.IRQ_in & mask)) {
		PDEBUGF(LOG_V1, LOG_PIC, "IRQ line %u (%s) now high (sIMR=%X,sINT=%u,IF=%u)\n", irq_no,
				g_machine.get_irq_names(irq_no).c_str(), m_s.slave.imr, m_s.slave.INT, FLAG_IF);
		m_s.slave.IRQ_in |= mask;
		m_s.slave.irr |= mask;
		service(m_s.slave);
	}
}

void PIC::set_master_imr(uint8_t _imr)
{
	// The interrupt request seen by the CPU can also be removed by the 8259A
	// interrupt controller even though the interrupt from the I/O device remains
	// active. For example, system software may mask an 8259A interrupt input
	// just after the I/O device asserts it. The net effect is an INTR signal
	// at the 80286 that goes active then inactive.
	// (even if the 8259A is in edge-triggered mode);
	if(m_s.master.imr == _imr) {
		return;
	}
	if(m_s.master.INT) {
		// resetting INT and INTR before the service routine is needed by POST
		// procedures 42,43,44
		m_s.master.INT = false;
		g_cpu.clear_INTR();
	}
	m_s.master.imr = _imr;
	service(m_s.master);
}

void PIC::set_slave_imr(uint8_t _imr)
{
	if(m_s.slave.imr == _imr) {
		return;
	}
	if(m_s.slave.INT) {
		m_s.slave.INT = false;
		if(m_s.master.irq == 2) { //???
			m_s.master.INT = false;
		}
		g_cpu.clear_INTR();
	}
	m_s.slave.imr = _imr;
	service(m_s.slave);
}

void PIC::clear_highest_interrupt(i8259 &_pic)
{
	// clear highest current in service bit
	int lowest_priority = _pic.lowest_priority;
	int highest_priority = lowest_priority + 1;
	if(highest_priority > 7) {
		highest_priority = 0;
	}

	int irq = highest_priority;
	do {
		if(_pic.isr & (1 << irq)) {
			_pic.isr &= ~(1 << irq);
			break; // Return mask of bit cleared.
		}

		irq ++;
		if(irq > 7)
			irq = 0;
	} while(irq != highest_priority);
}

void PIC::service(i8259 &_pic)
{
	uint8_t highest_priority = _pic.lowest_priority + 1;

	if(highest_priority > 7) {
		highest_priority = 0;
	}
	if(_pic.INT) {
		PDEBUGF(LOG_V2, LOG_PIC, "%s: last interrupt still not acknowleged\n", _pic.name());
		return;
	}

	uint8_t max_irq;
	uint8_t isr = _pic.isr;
	if(_pic.special_mask) {
		// all priorities may be enabled.  check all IRR bits except ones
		// which have corresponding ISR bits set
		max_irq = highest_priority;
	} else {
		// normal mode
		// Find the highest priority IRQ that is enabled due to current ISR
		max_irq = highest_priority;
		if(isr) {
			while((isr & (1 << max_irq)) == 0) {
				max_irq++;
				if(max_irq > 7) {
					max_irq = 0;
				}
			}
			if(max_irq == highest_priority) {
				return; // Highest priority interrupt in-service,
			            // no other priorities allowed
			}
			if(max_irq > 7) {
				PERRF(LOG_PIC, "%s: error in service()\n", _pic.name());
			}
		}
	}

	// now, see if there are any higher priority requests
	uint8_t unmasked_requests = (_pic.irr & ~_pic.imr);
	if(unmasked_requests) {
		int irq = highest_priority;
		do {
			// for special mode, since we're looking at all IRQ's, skip if
			// current IRQ is already in-service
			if( !(_pic.special_mask && ((isr >> irq) & 0x01)) ) {
				if(unmasked_requests & (1 << irq)) {
					_pic.INT = true;
					_pic.irq = irq;
					if(_pic.is_master) {
						PDEBUGF(LOG_V2, LOG_PIC, "%s: signalling IRQ %u (%s)\n",
								_pic.name(), irq, g_machine.get_irq_names(irq).c_str());
						g_cpu.raise_INTR();
					} else {
						PDEBUGF(LOG_V2, LOG_PIC, "%s: signalling IRQ %u (%s)\n",
								_pic.name(), 8 + irq, g_machine.get_irq_names(8 + irq).c_str());
						raise_irq(_pic.u.slave_id); // request IRQ on master pic
					}
					return;
				}
			}
			irq++;
			if(irq > 7) {
				irq = 0;
			}
		} while(irq != max_irq);
	}
}

/*
 * CPU handshakes with PIC after acknowledging interrupt
 */
uint8_t PIC::IAC(uint8_t *_dbg_irq)
{
	uint8_t vector;
	uint8_t irq;

	g_cpu.clear_INTR();
	m_s.master.INT = false;
	// Check for spurious interrupt
	if(m_s.master.irr == 0) {
		return (m_s.master.interrupt_offset + 7);
	}
	// In level sensitive mode don't clear the irr bit.
	if(!(m_s.master.edge_level & (1 << m_s.master.irq))) {
		m_s.master.irr &= ~(1 << m_s.master.irq);
	}
	// In autoeoi mode don't set the isr bit.
	if(!m_s.master.auto_eoi) {
		m_s.master.isr |= (1 << m_s.master.irq);
	} else if(m_s.master.rotate_on_autoeoi) {
		m_s.master.lowest_priority = m_s.master.irq;
	}

	if(m_s.master.irq != 2) {
		irq    = m_s.master.irq;
		vector = irq + m_s.master.interrupt_offset;
	} else { // IRQ2 = slave pic IRQ8..15
		m_s.slave.INT = false;
		m_s.master.IRQ_in &= ~(1 << 2);
		// Check for spurious interrupt
		if(m_s.slave.irr == 0) {
			return (m_s.slave.interrupt_offset + 7);
		}
		irq    = m_s.slave.irq;
		vector = irq + m_s.slave.interrupt_offset;
		// In level sensitive mode don't clear the irr bit.
		if(!(m_s.slave.edge_level & (1 << m_s.slave.irq))) {
			m_s.slave.irr &= ~(1 << m_s.slave.irq);
		}
		// In autoeoi mode don't set the isr bit.
		if(!m_s.slave.auto_eoi) {
			m_s.slave.isr |= (1 << m_s.slave.irq);
		} else if(m_s.slave.rotate_on_autoeoi) {
			m_s.slave.lowest_priority = m_s.slave.irq;
		}
		service(m_s.slave);
		irq += 8; // for debug printing purposes only!
	}

	service(m_s.master);

	//PDEBUGF(LOG_V2, LOG_PIC, "event at t=%ld IRQ irq=%u vec=%x\n", g_machine.get_virt_time(), irq, vector);
	if(_dbg_irq) {
		*_dbg_irq = irq;
	}
	return vector;
}

