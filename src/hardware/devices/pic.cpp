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

#include "ibmulator.h"
#include "pic.h"
#include "machine.h"
#include "hardware/cpu.h"
#include "hardware/devices.h"
#include <cstring>

PIC g_pic;


PIC::PIC()
{

}

PIC::~PIC()
{

}

void PIC::init()
{
	/* 8259 PIC (Programmable Interrupt Controller) */
	g_devices.register_read_handler(this, 0x20, 1);
	g_devices.register_read_handler(this, 0x21, 1);
	g_devices.register_read_handler(this, 0xA0, 1);
	g_devices.register_read_handler(this, 0xA1, 1);

	g_devices.register_write_handler(this, 0x20, 1);
	g_devices.register_write_handler(this, 0x21, 1);
	g_devices.register_write_handler(this, 0xA0, 1);
	g_devices.register_write_handler(this, 0xA1, 1);

	g_machine.register_irq(2, "cascade");
}

void PIC::reset(unsigned)
{
	m_s.master.single_PIC           = 0;
	m_s.master.interrupt_offset     = 0x08; /* IRQ0 = INT 0x08 */
	m_s.master.u.slave_connect_mask = 0x04; /* slave PIC connected to IRQ2 of master */
	m_s.master.sfnm                 = 0;    /* normal nested mode */
	m_s.master.buffered_mode        = 0;    /* unbuffered mode */
	m_s.master.master_slave         = 1;    /* master PIC */
	m_s.master.auto_eoi             = 0;    /* manual EOI from CPU */
	m_s.master.imr                  = 0xFF; /* all IRQ's initially masked */
	m_s.master.isr                  = 0x00; /* no IRQ's in service */
	m_s.master.irr                  = 0x00; /* no IRQ's requested */
	m_s.master.read_reg_select      = 0;    /* IRR */
	m_s.master.irq                  = 0;
	m_s.master.lowest_priority      = 7;
	m_s.master.INT                  = false;
	m_s.master.IRQ_in               = 0;
	m_s.master.init.in_init         = 0;
	m_s.master.init.requires_4      = 0;
	m_s.master.init.byte_expected   = 0;
	m_s.master.special_mask         = false;
	m_s.master.polled               = false;
	m_s.master.rotate_on_autoeoi    = false;
	m_s.master.edge_level           = 0;

	m_s.slave.single_PIC            = 0;
	m_s.slave.interrupt_offset      = 0x70; /* IRQ8 = INT 0x70 */
	m_s.slave.u.slave_id            = 0x02; /* slave PIC connected to IRQ2 of master */
	m_s.slave.sfnm                  = 0;    /* normal nested mode */
	m_s.slave.buffered_mode         = 0;    /* unbuffered mode */
	m_s.slave.master_slave          = 0;    /* slave PIC */
	m_s.slave.auto_eoi              = 0;    /* manual EOI from CPU */
	m_s.slave.imr                   = 0xFF; /* all IRQ's initially masked */
	m_s.slave.isr                   = 0x00; /* no IRQ's in service */
	m_s.slave.irr                   = 0x00; /* no IRQ's requested */
	m_s.slave.read_reg_select       = 0;    /* IRR */
	m_s.slave.irq                   = 0;
	m_s.slave.lowest_priority       = 7;
	m_s.slave.INT                   = false;
	m_s.slave.IRQ_in                = 0;
	m_s.slave.init.in_init          = 0;
	m_s.slave.init.requires_4       = 0;
	m_s.slave.init.byte_expected    = 0;
	m_s.slave.special_mask          = false;
	m_s.slave.polled                = false;
	m_s.slave.rotate_on_autoeoi     = false;
	m_s.slave.edge_level            = 0;

}

void PIC::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_PIC, "saving state\n");

	StateHeader h;
	h.name = get_name();
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);
}

void PIC::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_PIC, "restoring state\n");

	StateHeader h;
	h.name = get_name();
	h.data_size = sizeof(m_s);
	_state.read(&m_s,h);
}

uint16_t PIC::read(uint16_t address, unsigned io_len)
{

	PDEBUGF(LOG_V2, LOG_PIC, "PIC read from %04x\n", address);

	/*
	8259A PIC
	*/

	if((address == 0x20 || address == 0x21) && m_s.master.polled) {
		// In polled mode. Treat this as an interrupt acknowledge
		clear_highest_interrupt(& m_s.master);
		m_s.master.polled = 0;
		service_master_pic();
		// Return the current irq requested
		if(io_len==1) {
			return m_s.master.irq;
		} else {
			return (m_s.master.irq)<<8|(m_s.master.irq);
		}
	}

	if((address == 0xa0 || address == 0xa1) && m_s.slave.polled) {
		// In polled mode. Treat this as an interrupt acknowledge
		clear_highest_interrupt(& m_s.slave);
		m_s.slave.polled = 0;
		service_slave_pic();
		// Return the current irq requested
		if(io_len==1) {
			return m_s.slave.irq;
		} else {
			return (m_s.slave.irq)<<8|(m_s.slave.irq);
		}
	}

	switch (address) {
		case 0x20:
			if(m_s.master.read_reg_select) { /* ISR */
				PDEBUGF(LOG_V2, LOG_PIC, "read master ISR = %02x\n", m_s.master.isr);
				return m_s.master.isr;
			} else { /* IRR */
				PDEBUGF(LOG_V2, LOG_PIC, "read master IRR = %02x\n", m_s.master.irr);
				return m_s.master.irr;
			}
			break;
		case 0x21:
			PDEBUGF(LOG_V2, LOG_PIC, "read master IMR = %02x\n", m_s.master.imr);
			return m_s.master.imr;
		case 0xA0:
			if(m_s.slave.read_reg_select) { /* ISR */
				PDEBUGF(LOG_V2, LOG_PIC, "read slave ISR = %02x\n", m_s.slave.isr);
				return m_s.slave.isr;
			} else { /* IRR */
				PDEBUGF(LOG_V2, LOG_PIC, "read slave IRR = %02x\n", m_s.slave.irr);
				return m_s.slave.irr;
			}
			break;
		case 0xA1:
			PDEBUGF(LOG_V2, LOG_PIC, "read slave IMR = %02x\n", m_s.slave.imr);
			return m_s.slave.imr;
	}

	PERRF(LOG_PIC, "io read to address %04x\n", address);
	return 0; /* default if not found above */
}


void PIC::write(uint16_t address, uint16_t value, unsigned /*io_len*/)
{
	PDEBUGF(LOG_V2, LOG_PIC, "PIC write to %04x = %02x\n", address, value);

	/*
	8259A PIC
	*/

	switch (address) {
		case 0x20:
			if(value & 0x10) { /* initialization command 1 */
				PDEBUGF(LOG_V2, LOG_PIC, "master: init command 1 found");
				PDEBUGF(LOG_V2, LOG_PIC, " requires 4 = %u", (unsigned) (value & 0x01));
				PDEBUGF(LOG_V2, LOG_PIC, " cascade mode: [0=cascade,1=single] %u\n", (unsigned) ((value & 0x02) >> 1));
				m_s.master.init.in_init = 1;
				m_s.master.init.requires_4 = (value & 0x01);
				m_s.master.init.byte_expected = 2; /* operation command 2 */
				m_s.master.imr           = 0x00; /* clear the irq mask register */
				m_s.master.isr           = 0x00; /* no IRQ's in service */
				m_s.master.irr           = 0x00; /* no IRQ's requested */
				m_s.master.lowest_priority = 7;
				m_s.master.INT = 0; /* reprogramming clears previous INTR request */
				m_s.master.auto_eoi = 0;
				m_s.master.rotate_on_autoeoi = 0;
				if(value & 0x02)
					PERRF(LOG_PIC, "master: ICW1: single mode not supported\n");
				if(value & 0x08) {
					PERRF(LOG_PIC, "master: ICW1: level sensitive mode not supported\n");
				} else {
					PDEBUGF(LOG_V2, LOG_PIC, "master: ICW1: edge triggered mode selected\n");
				}
				g_cpu.clear_INTR();
				return;
			}

			if((value & 0x18) == 0x08) { /* OCW3 */
				uint8_t special_mask, poll, read_op;

				special_mask = (value & 0x60) >> 5;
				poll         = (value & 0x04) >> 2;
				read_op      = (value & 0x03);
				if(poll) {
					m_s.master.polled = 1;
					return;
				}
				if(read_op == 0x02) /* read IRR */
					m_s.master.read_reg_select = 0;
				else if(read_op == 0x03) /* read ISR */
					m_s.master.read_reg_select = 1;
				if(special_mask == 0x02) { /* cancel special mask */
					m_s.master.special_mask = 0;
				} else if(special_mask == 0x03) { /* set specific mask */
					m_s.master.special_mask = 1;
					service_master_pic();
				}
				return;
			}

			/* OCW2 */
			switch (value) {
				case 0x00: // Rotate in auto eoi mode clear
				case 0x80: // Rotate in auto eoi mode set
					m_s.master.rotate_on_autoeoi = (value != 0);
					break;

				case 0xA0: // Rotate on non-specific end of interrupt
				case 0x20: /* end of interrupt command */
					clear_highest_interrupt(& m_s.master);

					if(value == 0xA0) {// Rotate in Auto-EOI mode
						m_s.master.lowest_priority ++;
						if(m_s.master.lowest_priority > 7)
							m_s.master.lowest_priority = 0;
					}

					service_master_pic();
					break;

				case 0x40: // Intel PIC spec-sheet seems to indicate this should be ignored
					PINFOF(LOG_V2, LOG_PIC, "IRQ no-op\n");
					break;

				case 0x60: /* specific EOI 0 */
				case 0x61: /* specific EOI 1 */
				case 0x62: /* specific EOI 2 */
				case 0x63: /* specific EOI 3 */
				case 0x64: /* specific EOI 4 */
				case 0x65: /* specific EOI 5 */
				case 0x66: /* specific EOI 6 */
				case 0x67: /* specific EOI 7 */
					m_s.master.isr &= ~(1 << (value-0x60));
					service_master_pic();
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
					PINFOF(LOG_V2, LOG_PIC, "IRQ lowest command 0x%x\n", value);
					m_s.master.lowest_priority = value - 0xC0;
					break;

				case 0xE0: // specific EOI and rotate 0
				case 0xE1: // specific EOI and rotate 1
				case 0xE2: // specific EOI and rotate 2
				case 0xE3: // specific EOI and rotate 3
				case 0xE4: // specific EOI and rotate 4
				case 0xE5: // specific EOI and rotate 5
				case 0xE6: // specific EOI and rotate 6
				case 0xE7: // specific EOI and rotate 7
					m_s.master.isr &= ~(1 << (value-0xE0));
					m_s.master.lowest_priority = (value - 0xE0);
					service_master_pic();
					break;

				case 0x02: // single mode bit: 1 = single, 0 = cascade
					// ignore. 386BSD writes this value but works with it ignored.
					break;

				default:
					PERRF(LOG_PIC, "write to port 20h = %02x\n", value);
					break;
			} /* switch (value) */
			break;

		case 0x21:
			/* initialization mode operation */
			if(m_s.master.init.in_init) {
				switch (m_s.master.init.byte_expected) {
					case 2:
						m_s.master.interrupt_offset = value & 0xf8;
						m_s.master.init.byte_expected = 3;
						PDEBUGF(LOG_V2, LOG_PIC, "master: init command 2 = %02x", (unsigned) value);
						PDEBUGF(LOG_V2, LOG_PIC, " offset = INT %02x\n", m_s.master.interrupt_offset);
						return;
						break;
					case 3:
						PDEBUGF(LOG_V2, LOG_PIC, "master: init command 3 = %02x\n", (unsigned) value);
						if(m_s.master.init.requires_4) {
							m_s.master.init.byte_expected = 4;
						} else {
							m_s.master.init.in_init = 0;
						}
						return;
						break;
					case 4:
						PDEBUGF(LOG_V2, LOG_PIC, "master: init command 4 = %02x", (unsigned) value);
						if(value & 0x02) {
							PDEBUGF(LOG_V2, LOG_PIC, " auto EOI");
							m_s.master.auto_eoi = 1;
						} else {
							PDEBUGF(LOG_V2, LOG_PIC, " normal EOI interrupt");
							m_s.master.auto_eoi = 0;
						}
						if(value & 0x01) {
							PDEBUGF(LOG_V2, LOG_PIC, " 80x86 mode\n");
						} else {
							PERRF(LOG_PIC, " not 80x86 mode\n");
						}
						m_s.master.init.in_init = 0;
						return;
					default:
						PERRF(LOG_PIC, "master expecting bad init command\n");
						break;
				}
			}

			/* normal operation */
			PDEBUGF(LOG_V2, LOG_PIC, "setting master pic IMR to %02x\n", value);
			//m_s.master.imr = value;
			//service_master_pic();
			set_master_imr(value);
			return;

		case 0xA0:
			if(value & 0x10) { /* initialization command 1 */
				PDEBUGF(LOG_V2, LOG_PIC, "slave: init command 1 found");
				PDEBUGF(LOG_V2, LOG_PIC, " requires 4 = %u", (unsigned) (value & 0x01));
				PDEBUGF(LOG_V2, LOG_PIC, " cascade mode: [0=cascade,1=single] %u\n", (unsigned) ((value & 0x02) >> 1));
				m_s.slave.init.in_init = 1;
				m_s.slave.init.requires_4 = (value & 0x01);
				m_s.slave.init.byte_expected = 2; /* operation command 2 */
				m_s.slave.imr           = 0x00; /* clear irq mask */
				m_s.slave.isr           = 0x00; /* no IRQ's in service */
				m_s.slave.irr           = 0x00; /* no IRQ's requested */
				m_s.slave.lowest_priority = 7;
				m_s.slave.INT = 0; /* reprogramming clears previous INTR request */
				m_s.master.IRQ_in &= ~(1 << 2);
				m_s.slave.auto_eoi = 0;
				m_s.slave.rotate_on_autoeoi = 0;
				if(value & 0x02)
					PERRF(LOG_PIC, "slave: ICW1: single mode not supported\n");
				if(value & 0x08) {
					PERRF(LOG_PIC, "slave: ICW1: level sensitive mode not supported\n");
				} else {
					PDEBUGF(LOG_V2, LOG_PIC, "slave: ICW1: edge triggered mode selected\n");
				}
				return;
			}

			if((value & 0x18) == 0x08) { /* OCW3 */
				uint8_t special_mask, poll, read_op;

				special_mask = (value & 0x60) >> 5;
				poll         = (value & 0x04) >> 2;
				read_op      = (value & 0x03);
				if(poll) {
					m_s.slave.polled = 1;
					return;
				}
				if(read_op == 0x02) /* read IRR */
					m_s.slave.read_reg_select = 0;
				else if(read_op == 0x03) /* read ISR */
					m_s.slave.read_reg_select = 1;
				if(special_mask == 0x02) { /* cancel special mask */
					m_s.slave.special_mask = 0;
				} else if(special_mask == 0x03) { /* set specific mask */
					m_s.slave.special_mask = 1;
					service_slave_pic();
				}
				return;
			}

			switch (value) {
				case 0x00: // Rotate in auto eoi mode clear
				case 0x80: // Rotate in auto eoi mode set
					m_s.slave.rotate_on_autoeoi = (value != 0);
					break;

				case 0xA0: // Rotate on non-specific end of interrupt
				case 0x20: /* end of interrupt command */
					clear_highest_interrupt(& m_s.slave);

					if(value == 0xA0) {// Rotate in Auto-EOI mode
						m_s.slave.lowest_priority ++;
						if(m_s.slave.lowest_priority > 7)
							m_s.slave.lowest_priority = 0;
					}

					service_slave_pic();
					break;

				case 0x40: // Intel PIC spec-sheet seems to indicate this should be ignored
					PINFOF(LOG_V2, LOG_PIC, "IRQ no-op\n");
					break;

				case 0x60: /* specific EOI 0 */
				case 0x61: /* specific EOI 1 */
				case 0x62: /* specific EOI 2 */
				case 0x63: /* specific EOI 3 */
				case 0x64: /* specific EOI 4 */
				case 0x65: /* specific EOI 5 */
				case 0x66: /* specific EOI 6 */
				case 0x67: /* specific EOI 7 */
					m_s.slave.isr &= ~(1 << (value-0x60));
					service_slave_pic();
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
					PINFOF(LOG_V2, LOG_PIC, "IRQ lowest command 0x%x\n", value);
					m_s.slave.lowest_priority = value - 0xC0;
					break;

				case 0xE0: // specific EOI and rotate 0
				case 0xE1: // specific EOI and rotate 1
				case 0xE2: // specific EOI and rotate 2
				case 0xE3: // specific EOI and rotate 3
				case 0xE4: // specific EOI and rotate 4
				case 0xE5: // specific EOI and rotate 5
				case 0xE6: // specific EOI and rotate 6
				case 0xE7: // specific EOI and rotate 7
					m_s.slave.isr &= ~(1 << (value-0xE0));
					m_s.slave.lowest_priority = (value - 0xE0);
					service_slave_pic();
					break;

				case 0x02: // single mode bit: 1 = single, 0 = cascade
					// ignore. 386BSD writes this value but works with it ignored.
					break;

				default:
					PERRF(LOG_PIC, "write to port A0h = %02x", value);
					break;
			} /* switch (value) */
			break;

		case 0xA1:
			/* initialization mode operation */
			if(m_s.slave.init.in_init) {
				switch (m_s.slave.init.byte_expected) {
					case 2:
						m_s.slave.interrupt_offset = value & 0xf8;
						m_s.slave.init.byte_expected = 3;
						PDEBUGF(LOG_V2, LOG_PIC, "slave: init command 2 = %02x", (unsigned) value);
						PDEBUGF(LOG_V2, LOG_PIC, " offset = INT %02x\n", m_s.slave.interrupt_offset);
						return;
					case 3:
						PDEBUGF(LOG_V2, LOG_PIC, "slave: init command 3 = %02x\n", (unsigned) value);
						if(m_s.slave.init.requires_4) {
							m_s.slave.init.byte_expected = 4;
						} else {
							m_s.slave.init.in_init = 0;
						}
						return;
					case 4:
						PDEBUGF(LOG_V2, LOG_PIC, "slave: init command 4 = %02x", (unsigned) value);
						if(value & 0x02) {
							PDEBUGF(LOG_V2, LOG_PIC, " auto EOI");
							m_s.slave.auto_eoi = 1;
						} else {
							PDEBUGF(LOG_V2, LOG_PIC, " normal EOI interrupt");
							m_s.slave.auto_eoi = 0;
						}
						if(value & 0x01) {
							PDEBUGF(LOG_V2, LOG_PIC, " 80x86 mode\n");
						} else {
							PERRF(LOG_PIC, " not 80x86 mode\n");
						}
						m_s.slave.init.in_init = 0;
						return;
					default:
						PERRF(LOG_PIC, "slave: expecting bad init command\n");
						break;
				}
			}

			/* normal operation */
			PDEBUGF(LOG_V2, LOG_PIC, "setting slave pic IMR to %02x\n", value);
			set_slave_imr(value);
			//m_s.slave.imr = value;
			//service_slave_pic();
			return;
	} /* switch (address) */
}

void PIC::lower_irq(unsigned irq_no)
{
	uint8_t mask = (1 << (irq_no & 7));
	if((irq_no <= 7) && (m_s.master.IRQ_in & mask)) {
		PDEBUGF(LOG_V2, LOG_PIC, "IRQ line %u (%s) now low\n", irq_no,
				g_machine.get_irq_name(irq_no));
		m_s.master.IRQ_in &= ~(mask);
		m_s.master.irr &= ~(mask);
	} else if((irq_no > 7) && (irq_no <= 15) && (m_s.slave.IRQ_in & mask)) {
		PDEBUGF(LOG_V2, LOG_PIC, "IRQ line %u (%s) now low\n", irq_no,
				g_machine.get_irq_name(irq_no));
		m_s.slave.IRQ_in &= ~(mask);
		m_s.slave.irr &= ~(mask);
	}
}

void PIC::raise_irq(unsigned irq_no)
{
	uint8_t mask = (1 << (irq_no & 7));
	if((irq_no <= 7) && !(m_s.master.IRQ_in & mask)) {
		PDEBUGF(LOG_V2, LOG_PIC, "IRQ line %u (%s) now high (mIMR=%X,mINT=%u,IF=%u)\n", irq_no,
				g_machine.get_irq_name(irq_no), m_s.master.imr, m_s.master.INT, FLAG_IF);
		m_s.master.IRQ_in |= mask;
		m_s.master.irr |= mask;
		service_master_pic();
	} else if((irq_no > 7) && (irq_no <= 15) && !(m_s.slave.IRQ_in & mask)) {
		PDEBUGF(LOG_V2, LOG_PIC, "IRQ line %u (%s) now high (sIMR=%X,sINT=%u,IF=%u)\n", irq_no,
				g_machine.get_irq_name(irq_no), m_s.slave.imr, m_s.slave.INT, FLAG_IF);
		m_s.slave.IRQ_in |= mask;
		m_s.slave.irr |= mask;
		service_slave_pic();
	}
}

void PIC::set_mode(bool ma_sl, uint8_t mode)
{
	if(ma_sl) {
		m_s.master.edge_level = mode;
	} else {
		m_s.slave.edge_level = mode;
	}
}


void PIC::set_master_imr(uint8_t _imr)
{
	/* The interrupt request seen by the CPU can also be removed by the 8259A
	 * interrupt controller even though the interrupt from the I/O device remains
	 * active. For example, system software may mask an 8259A interrupt input
	 * just after the I/O device asserts it. The net effect is an INTR signal
	 * at the 80286 that goes active then inactive.
	 * (even if the 8259A is in edge-triggered mode);
	 */
	if(m_s.master.imr == _imr) {
		return;
	}
	if(m_s.master.INT) {
		 /* resetting INT and INTR before the service routine is needed by POST
		 * procedures 42,43,44
		 */
		m_s.master.INT = false;
		g_cpu.clear_INTR();
	}
	m_s.master.imr = _imr;
	service_master_pic();
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
	service_slave_pic();
}

void PIC::clear_highest_interrupt(pic_t *pic)
{
	int irq;
	int lowest_priority;
	int highest_priority;

	/* clear highest current in service bit */
	lowest_priority = pic->lowest_priority;
	highest_priority = lowest_priority + 1;
	if(highest_priority > 7) {
		highest_priority = 0;
	}

	irq = highest_priority;
	do {
		if(pic->isr & (1 << irq)) {
			pic->isr &= ~(1 << irq);
			break; /* Return mask of bit cleared. */
		}

		irq ++;
		if(irq > 7)
			irq = 0;
	} while(irq != highest_priority);
}

void PIC::service_master_pic()
{
	uint8_t unmasked_requests;
	int irq;
	uint8_t isr, max_irq;
	uint8_t highest_priority = m_s.master.lowest_priority + 1;

	if(highest_priority > 7) {
		highest_priority = 0;
	}
	if(m_s.master.INT) { /* last interrupt still not acknowleged */
		PDEBUGF(LOG_V2, LOG_PIC, "master: last interrupt still not acknowleged\n");
		return;
	}

	isr = m_s.master.isr;
	if(m_s.master.special_mask) {
		/* all priorities may be enabled.  check all IRR bits except ones
		 * which have corresponding ISR bits set
		 */
		max_irq = highest_priority;
	} else { /* normal mode */
		/* Find the highest priority IRQ that is enabled due to current ISR */
		max_irq = highest_priority;
		if(isr) {
			while((isr & (1 << max_irq)) == 0) {
				max_irq++;
				if(max_irq > 7) {
					max_irq = 0;
				}
			}
			if(max_irq == highest_priority) {
				return; /* Highest priority interrupt in-service,
			             * no other priorities allowed */
			}
			if(max_irq > 7) {
				PERRF(LOG_PIC, "error in service_master_pic()\n");
			}
		}
	}

	/* now, see if there are any higher priority requests */
	unmasked_requests = (m_s.master.irr & ~m_s.master.imr);
	if(unmasked_requests) {
		irq = highest_priority;
		do {
			/* for special mode, since we're looking at all IRQ's, skip if
			 * current IRQ is already in-service
			 */
			if( !(m_s.master.special_mask && ((isr >> irq) & 0x01)) ) {
				if(unmasked_requests & (1 << irq)) {
					PDEBUGF(LOG_V2, LOG_PIC, "signalling IRQ %u (%s)\n", irq,
							g_machine.get_irq_name(irq));
					m_s.master.INT = true;
					m_s.master.irq = irq;
					g_cpu.raise_INTR();
					return;
				}
			}

			irq ++;
			if(irq > 7) {
				irq = 0;
			}
		} while(irq != max_irq); /* do ... */
	} /* if(unmasked_requests = ... */
}

void PIC::service_slave_pic()
{
	uint8_t unmasked_requests;
	int irq;
	uint8_t isr, max_irq;
	uint8_t highest_priority = m_s.slave.lowest_priority + 1;

	if(highest_priority > 7) {
		highest_priority = 0;
	}
	if(m_s.slave.INT) { /* last interrupt still not acknowleged */
		PDEBUGF(LOG_V2, LOG_PIC, "slave: last interrupt still not acknowleged\n");
		return;
	}

	isr = m_s.slave.isr;
	if(m_s.slave.special_mask) {
		/* all priorities may be enabled.  check all IRR bits except ones
		 * which have corresponding ISR bits set
		 */
		max_irq = highest_priority;
	} else { /* normal mode */
		/* Find the highest priority IRQ that is enabled due to current ISR */
		max_irq = highest_priority;
		if(isr) {
			while((isr & (1 << max_irq)) == 0) {
				max_irq++;
				if(max_irq > 7) {
					max_irq = 0;
				}
			}
			if(max_irq == highest_priority) {
				return; /* Highest priority interrupt in-service,
			             * no other priorities allowed */
			}
			if(max_irq > 7) {
				PERRF(LOG_PIC, "error in service_slave_pic()\n");
			}
		}
	}

	/* now, see if there are any higher priority requests */
	if((unmasked_requests = (m_s.slave.irr & ~m_s.slave.imr))) {
		irq = highest_priority;
		do {
			/* for special mode, since we're looking at all IRQ's, skip if
			 * current IRQ is already in-service
			 */
			if( !(m_s.slave.special_mask && ((isr >> irq) & 0x01)) ) {
				if(unmasked_requests & (1 << irq)) {
					PDEBUGF(LOG_V2, LOG_PIC, "slave: signalling IRQ %u\n", 8 + irq);

					m_s.slave.INT = true;
					m_s.slave.irq = irq;
					raise_irq(2); /* request IRQ 2 on master pic */
					return;
				} /* if(unmasked_requests & ... */
			}

			irq ++;
			if(irq > 7) {
				irq = 0;
			}

		} while(irq != max_irq); /* do ... */
	} /* if(unmasked_requests = ... */
}

/* CPU handshakes with PIC after acknowledging interrupt */
uint8_t PIC::IAC()
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
	} else { /* IRQ2 = slave pic IRQ8..15 */
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
		service_slave_pic();
		irq += 8; // for debug printing purposes
	}

	service_master_pic();

	//PDEBUGF(LOG_V2, LOG_PIC, "event at t=%ld IRQ irq=%u vec=%x\n", g_machine.get_virt_time(), irq, vector);

	return vector;
}

