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

#ifndef IBMULATOR_CPU_BUS_H
#define IBMULATOR_CPU_BUS_H

#include "core.h"
#include "../memory.h"
#ifndef NDEBUG
	#include "machine.h"
#endif

#define CPU_PQ_SIZE          6
#define MEMORY_ACCESS_CYCLES 2
#define MEMORY_WAIT_STATES   1
#define MEMORY_TX_CYCLES (MEMORY_ACCESS_CYCLES+MEMORY_WAIT_STATES)


class CPUBus;
extern CPUBus g_cpubus;


class CPUBus
{
private:
	struct {
		uint32_t csip;
		uint16_t ip;
		uint8_t pq[CPU_PQ_SIZE];
		bool pq_valid;
		uint32_t pq_head;
		uint32_t pq_tail;
		uint32_t pq_headpos;
	} m_s;

	//uint m_bytes_fetched;
	uint m_read_tx;
	uint m_write_tx;
	uint m_pq_fetches;
	uint m_cycles_penalty;

	void pq_fill(uint free_space, uint toread);
	GCC_ATTRIBUTE(always_inline)
	inline uint get_pq_free_space() {
		return CPU_PQ_SIZE - (m_s.pq_tail-m_s.pq_head) + (m_s.csip - m_s.pq_head);
	}
	GCC_ATTRIBUTE(always_inline)
	inline uint get_pq_cur_index() {
		return (m_s.pq_headpos + (m_s.csip - m_s.pq_head)) % CPU_PQ_SIZE;
	}
	inline uint get_pq_cur_size() {
		return CPU_PQ_SIZE - get_pq_free_space();
	}

public:

	CPUBus();

	void init();
	void config_changed();

	inline void reset_counters() {
		m_read_tx = 0;
		m_write_tx = 0;
		m_cycles_penalty = 0;
		m_pq_fetches = 0;
	}

	inline uint get_read_tx() { return m_read_tx; }
	inline uint get_write_tx() { return m_write_tx; }
	inline uint get_mem_tx() { return m_read_tx + m_write_tx; }
	inline uint get_cycles_penalty() { return m_cycles_penalty; }
	inline uint get_pq_fetches() { return m_pq_fetches; }
	inline bool is_pq_valid() { return m_s.pq_valid; }
	void update_pq(uint _cycles);

	//instruction fetching
	uint8_t fetchb();
	uint16_t fetchw();

	inline uint32_t get_ip() { return m_s.ip; }
	inline uint32_t get_csip() { return m_s.csip; }

	inline void invalidate_pq() {
		m_s.pq_valid = false;
		m_s.ip = REG_IP;
		m_s.csip = GET_PHYADDR(CS, m_s.ip);
	}

	inline uint8_t mem_read_byte(uint32_t _addr) {
		m_read_tx += 1;
		return g_memory.read_byte(_addr);
	}
	inline uint16_t mem_read_word(uint32_t _addr) {
		/* When the 286 is asked to perform a word-sized access
		 * starting at an odd address, it actually performs two separate
		 * accesses, each of which fetches 1 byte, just as the 8088 does for
		 * all word-sized accesses.
		 */
		if(_addr & 1) {
			m_read_tx += 2;
		} else {
			m_read_tx += 1;
		}
		return g_memory.read_word(_addr);
	}
	inline uint32_t mem_read_dword(uint32_t _addr) {
		uint32_t w0 = mem_read_word(_addr);
		uint32_t w1 = mem_read_word(_addr+2);
		return w1<<16 | w0;
	}
	inline uint64_t mem_read_qword(uint32_t _addr) {
		uint64_t w0 = mem_read_word(_addr);
		uint64_t w1 = mem_read_word(_addr+2);
		uint64_t w2 = mem_read_word(_addr+4);
		uint64_t w3 = mem_read_word(_addr+6);
		return w3<<48 | w2<<32 | w1<<16 | w0;

	}

	inline void mem_write_byte(uint32_t _addr, uint8_t _data) {
		m_write_tx += 1;
		g_memory.write_byte(_addr, _data);
	}
	inline void mem_write_word(uint32_t _addr, uint16_t _data) {
		if(_addr & 1) {
			m_write_tx += 2;
		} else {
			m_write_tx += 1;
		}
		g_memory.write_word(_addr, _data);
	}

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
};

#endif
