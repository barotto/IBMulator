/*
 * Copyright (c) 2002-2009  The Bochs Project
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

#ifndef IBMULATOR_HW_PIC_H
#define IBMULATOR_HW_PIC_H

#include "hardware/iodevice.h"


class PIC : public IODevice
{
	IODEVICE(PIC, "8259 PIC")

private:
	struct i8259 {
		bool    single_PIC;        // 0=cascaded PIC, 1=master only
		uint8_t interrupt_offset;  // programmable interrupt vector offset
		union {
			uint8_t slave_connect_mask; // for master, a bit for each interrupt line
			                            //  0=not connect to a slave, 1=connected
			uint8_t slave_id;           // for slave, id number of slave PIC
		} u;
		bool    sfnm;              // specially fully nested mode?
		bool    buffered_mode;     // buffered mode?
		bool    is_master;         // is this the master?
		bool    auto_eoi;          // 0=manual EOI, 1=automatic EOI
		uint8_t imr;               // interrupt mask register, 1=masked
		uint8_t isr;               // in service register
		uint8_t irr;               // interrupt request register
		uint8_t read_reg_select;   // 0=IRR, 1=ISR
		uint8_t irq;               // current IRQ number
		uint8_t lowest_priority;   // current lowest priority irq
		bool    INT;               // INT request pin of PIC
		uint8_t IRQ_in;            // IRQ pins of PIC
		struct {
			bool in_init;
			bool requires_4;
			uint8_t byte_expected;
		} init;
		bool special_mask;
		bool polled;            // Set when poll command is issued.
		bool rotate_on_autoeoi; // Set when should rotate in auto-eoi mode.
		uint8_t edge_level;     // bitmap for irq mode (0=edge, 1=level)

		constexpr const char * name() const {
			return (is_master) ? "master" : "slave";
		}
	};

	struct {
		i8259 master;
		i8259 slave;
	} m_s;

	uint16_t read(i8259 & _pic, uint16_t _address, unsigned _io_len);
	void write(i8259 &, uint8_t address, uint8_t value);
	void service(i8259 &);
	void clear_highest_interrupt(i8259 &);
	void set_master_imr(uint8_t value);
	void set_slave_imr(uint8_t value);

public:
	PIC(Devices* _dev);
	~PIC();

	void install();
	void remove();
	uint16_t read(uint16_t address, uint io_len);
	void write(uint16_t address, uint16_t value, uint io_len);

	void reset(unsigned type);
	void lower_irq(unsigned irq_no);
	void raise_irq(unsigned irq_no);
	uint8_t IAC(uint8_t *_irq=nullptr);

	inline uint16_t get_irr() const {
		return (uint16_t(m_s.master.irr) | (uint16_t(m_s.slave.irr)<<8));
	}
	inline uint16_t get_imr() const {
		return (uint16_t(m_s.master.imr) | (uint16_t(m_s.slave.imr)<<8));
	}
	inline uint16_t get_isr() const {
		return (uint16_t(m_s.master.isr) | (uint16_t(m_s.slave.isr)<<8));
	}
	inline uint16_t get_irq() const {
		return (uint16_t(m_s.master.IRQ_in) | (uint16_t(m_s.slave.IRQ_in)<<8));
	}

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
};

#endif
