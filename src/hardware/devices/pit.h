/*
 * Copyright (c) 2001-2014  The Bochs Project
 * Copyright (C) 2015-2023  Marco Bortolin
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

#ifndef IBMULATOR_HW_PIT_H
#define IBMULATOR_HW_PIT_H

#include "hardware/iodevice.h"
#include "pcspeaker.h"

class PIC;

#define PIT_CLK_TIME (uint64_t(838))
#define PIT_FREQ (1193317)

class PIT : public IODevice
{
	IODEVICE(PIT, "8254 PIT")

private:
	enum rw_status {
		LSByte = 0,
		MSByte = 1,
		LSByte_multiple = 2,
		MSByte_multiple = 3
	};

	enum {
		MAX_COUNTER = 2,
		MAX_ADDRESS = 3,
		CONTROL_ADDRESS = 3,
		MAX_MODE = 5
	};

	enum real_RW_status {
		LSB_real = 1,
		MSB_real = 2,
		BOTH_real = 3
	};

	enum problem_type {
		UNL_2P_READ = 1
	};

	struct Counter {
		// Chip IOs;
		bool GATE;   // GATE Input value at end of cycle
		bool OUTpin; // OUT output this cycle

		// Architected state;
		uint32_t count;    // Counter value this cycle
		uint16_t outlatch; // Output latch this cycle
		uint16_t inlatch;  // Input latch this cycle
		uint8_t status_latch;

		// Status Register data;
		uint8_t rw_mode; // 2-bit R/W mode from command word register.
		uint8_t mode;    // 3-bit mode from command word register.
		bool bcd_mode;   // 1-bit BCD vs. Binary setting.
		bool null_count; // Null count bit of status register.

		// Latch status data;
		bool count_LSB_latched;
		bool count_MSB_latched;
		bool status_latched;

		// Miscelaneous State;
		uint32_t count_binary; // Value of the count in binary.
		bool triggerGATE;      // Whether we saw GATE rise this cycle.
		rw_status write_state; // Read state this cycle
		rw_status read_state;  // Read state this cycle
		bool count_written;    // Whether a count written since programmed
		bool first_pass;       // Whether or not this is the first loaded count.
		bool state_bit_1;      // Miscelaneous state bits.
		bool state_bit_2;
		uint32_t next_change_time; // Next time (cycles) something besides count changes. 0 means never.
		int seen_problems;
	};

	struct {
		Counter counters[3];
		uint8_t control_word;
		bool speaker_data_on;
		uint64_t pit_time;
		uint64_t pit_ticks;
	} m_s;

	TimerID m_systimer = NULL_TIMER_ID;
	uint32_t m_crnt_emulated_ticks = 0;
	std::atomic<uint64_t> m_mt_pit_ticks = 0;
	PCSpeaker *m_pcspeaker = nullptr;

public:
	PIT(Devices *_dev);

	void install();
	void remove();
	void reset(unsigned _type);
	void config_changed();
	uint16_t read(uint16_t _address, unsigned _io_len);
	void write(uint16_t _address, uint16_t _value, unsigned _io_len);

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

	uint64_t get_pit_ticks_mt() const {
		return m_mt_pit_ticks;
	}

	bool read_OUT(uint8_t _cnum) const {
		assert(_cnum <= MAX_COUNTER);
		return m_s.counters[_cnum].OUTpin;
	}
	bool read_GATE(uint8_t _cnum) const {
		assert(_cnum <= MAX_COUNTER);
		return m_s.counters[_cnum].GATE;
	}
	uint8_t read_mode(uint8_t _cnum) const {
		assert(_cnum <= MAX_COUNTER);
		return m_s.counters[_cnum].mode;
	}
	uint32_t read_CNT(uint8_t _cnum) const {
		assert(_cnum <= MAX_COUNTER);
		return m_s.counters[_cnum].count;
	}
	uint16_t read_inlatch(uint8_t _cnum) const {
		assert(_cnum <= MAX_COUNTER);
		return m_s.counters[_cnum].inlatch;
	}

private:
	uint8_t read_timer(uint8_t _address);
	void write_timer(uint8_t _address, uint8_t _data);

	void handle_systimer(uint64_t);
	void update_emulation(uint64_t _pit_time);
	void update_systimer(uint64_t _cpu_time);

	bool new_count_ready(int _cnum) const {
		assert(_cnum <= MAX_COUNTER);
		return (m_s.counters[_cnum].write_state != MSByte_multiple);
	}

	uint32_t get_next_event_ticks(uint8_t &_timer);

	void latch(uint8_t _cnum);
	void set_count(uint8_t _cnum, uint32_t data);
	void set_count_to_binary(uint8_t _cnum);
	void set_binary_to_count(uint8_t _cnum);
	bool decrement(uint8_t _cnum);
	bool decrement_multiple(uint8_t _cnum, uint32_t cycles);
	void clock(uint8_t _cnum, uint32_t _cycles);
	void clock_multiple(uint8_t _cnum, uint32_t _cycles);
	void clock_all(uint32_t _cycles);

	void set_OUT(uint8_t _cnum, bool _value, uint32_t _cycles);
	void set_GATE(uint8_t _cnum, bool _value);
};

#endif
