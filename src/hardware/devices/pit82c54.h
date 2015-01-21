/*
 * 	Copyright (c) 2001-2013  The Bochs Project
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

/*
 * Emulator of an Intel 8254/82C54 Programmable Interval Timer.
 * Greg Alexander <yakovlev@usa.com>
 */

#ifndef IBMULATOR_HW_PIT_82C54_H
#define IBMULATOR_HW_PIT_82C54_H

typedef void (*out_handler_t)(bool value, uint32_t cycles);

class PIT_82C54
{
private:

	enum rw_status {
		LSByte=0,
		MSByte=1,
		LSByte_multiple=2,
		MSByte_multiple=3
	};

	enum {
		MAX_COUNTER=2,
		MAX_ADDRESS=3,
		CONTROL_ADDRESS=3,
		MAX_MODE=5
	};

	enum real_RW_status {
		LSB_real=1,
		MSB_real=2,
		BOTH_real=3
	};

	enum problem_type {
		UNL_2P_READ=1
	};

	struct PIT_counter {
		uint name;

		//Chip IOs;
		bool GATE; //GATE Input value at end of cycle
		bool OUTpin; //OUT output this cycle

		//Architected state;
		uint32_t count; //Counter value this cycle
		uint16_t outlatch; //Output latch this cycle
		uint16_t inlatch; //Input latch this cycle
		uint8_t status_latch;

		//Status Register data;
		uint8_t rw_mode; //2-bit R/W mode from command word register.
		uint8_t mode; //3-bit mode from command word register.
		bool bcd_mode; //1-bit BCD vs. Binary setting.
		bool null_count; //Null count bit of status register.

		//Latch status data;
		bool count_LSB_latched;
		bool count_MSB_latched;
		bool status_latched;

		//Miscelaneous State;
		uint32_t count_binary; //Value of the count in binary.
		bool triggerGATE; //Whether we saw GATE rise this cycle.
		rw_status write_state; //Read state this cycle
		rw_status read_state; //Read state this cycle
		bool count_written; //Whether a count written since programmed
		bool first_pass; //Whether or not this is the first loaded count.
		bool state_bit_1; //Miscelaneous state bits.
		bool state_bit_2;
		uint32_t next_change_time; //Next time (cycles) something besides count changes.
							 //0 means never.
		out_handler_t out_handler; // OUT pin callback

		int seen_problems;

		void reset();
		void dbg_print();
		void latch();
		void set_OUT(bool data, uint32_t _cycles);
		void set_count(uint32_t data);
		void set_count_to_binary();
		void set_binary_to_count();
		bool decrement();
		bool decrement_multiple(uint32_t cycles);
		void clock(uint32_t cycles);
		void clock_multiple(uint32_t cycles);
		uint8_t read();
		void write(uint8_t);
		void set_GATE(bool data);
	};

	PIT_counter counter[3];

	uint8_t controlword;

public:

	void init();
	void reset(unsigned type);

	void clock_all(uint32_t cycles);
	void clock_multiple(uint8_t cnum, uint32_t cycles);

	uint8_t read(uint8_t address);
	void write(uint8_t address, uint8_t data);

	uint32_t get_next_event_time();

	void print_cnum(uint8_t cnum);

	inline bool new_count_ready(int countnum) const {
		ASSERT(countnum<=MAX_COUNTER);
		return (counter[countnum].write_state != MSByte_multiple);
	}

	inline void set_OUT_handler(uint8_t counternum, out_handler_t outh) {
		ASSERT(counternum<=MAX_COUNTER);
		counter[counternum].out_handler = outh;
	}

	inline bool read_OUT(uint8_t cnum) const {
		ASSERT(cnum<=MAX_COUNTER);
		return counter[cnum].OUTpin;
	}

	inline bool read_GATE(uint8_t cnum) const {
		ASSERT(cnum<=MAX_COUNTER);
		return counter[cnum].GATE;
	}

	inline uint8_t read_mode(uint8_t cnum) const {
		ASSERT(cnum<=MAX_COUNTER);
		return counter[cnum].mode;
	}

	inline uint32_t read_CNT(uint8_t cnum) const {
		ASSERT(cnum<=MAX_COUNTER);
		return counter[cnum].count;
	}

	inline uint16_t read_inlatch(uint8_t cnum) const {
		ASSERT(cnum<=MAX_COUNTER);
		return counter[cnum].inlatch;
	}

	inline void set_GATE(uint8_t cnum, bool data) {
		ASSERT(cnum<=MAX_COUNTER);
		counter[cnum].set_GATE(data);
	}
};

#endif
