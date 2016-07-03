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

#ifndef IBMULATOR_CPU_BUS_H
#define IBMULATOR_CPU_BUS_H

#include "core.h"
#include "../memory.h"
#ifndef NDEBUG
	#include "machine.h"
#endif
#include <vector>

#define CPU_PQ_SIZE          6
#define DRAM_ACCESS_CYCLES   2
#define DRAM_WAIT_STATES     1
#define DRAM_TX_CYCLES (DRAM_ACCESS_CYCLES+DRAM_WAIT_STATES)
#define DRAM_REFRESH_CYCLES  DRAM_TX_CYCLES*2
/*TODO
 * The following is an oversimplification of the video memory access time.
 * Also, the video ram wait states depend on the CPU frequency.
 * For an in depth explanation of the Display Adapter Cycle-Eater read Michael
 * Abrash's Graphics Programming Black Book, Ch.4
 */
#define VRAM_WAIT_STATES     13
#define VRAM_TX_CYCLES (DRAM_ACCESS_CYCLES+VRAM_WAIT_STATES)


class CPUBus;
extern CPUBus g_cpubus;


class CPUBus
{
private:
	struct {
		uint32_t cseip;
		uint32_t eip;
		uint8_t pq[CPU_PQ_SIZE];
		bool pq_valid;
		uint32_t pq_head;
		uint32_t pq_tail;
		uint32_t pq_headpos;
	} m_s;

	uint m_dram_r;
	uint m_dram_w;
	uint m_vram_r;
	uint m_vram_w;
	uint m_pq_fetches;
	uint m_mem_cycles;
	uint m_fetch_cycles;
	uint m_cycles_ahead;
	struct wq_data {
		uint32_t address;
		uint16_t value;
		uint16_t len;
		uint cycles;
		wq_data(uint32_t _address, uint16_t _data, uint16_t _len, uint _cycles)
			: address(_address), value(_data), len(_len), cycles(_cycles)
		{}
	};
	std::vector<wq_data> m_write_queue;

	void pq_fill(uint toread);
	GCC_ATTRIBUTE(always_inline)
	inline uint get_pq_free_space() {
		return CPU_PQ_SIZE - (m_s.pq_tail-m_s.pq_head) + (m_s.cseip - m_s.pq_head);
	}
	GCC_ATTRIBUTE(always_inline)
	inline uint get_pq_cur_index() {
		return (m_s.pq_headpos + (m_s.cseip - m_s.pq_head)) % CPU_PQ_SIZE;
	}
	inline uint get_pq_cur_size() {
		return CPU_PQ_SIZE - get_pq_free_space();
	}
	inline bool is_pq_empty() {
		return (m_s.cseip == m_s.pq_tail);
	}

public:

	CPUBus();

	void init();
	void reset();
	void config_changed();

	inline void reset_counters() {
		m_dram_r = 0;
		m_dram_w = 0;
		m_vram_r = 0;
		m_vram_w = 0;
		m_mem_cycles = 0;
		m_fetch_cycles = 0;
	}

	inline uint get_dram_r()  { return m_dram_r; }
	inline uint get_dram_w()  { return m_dram_w; }
	inline uint get_dram_tx() { return m_dram_r+m_dram_w; }
	inline uint get_vram_r()  { return m_vram_r; }
	inline uint get_vram_w()  { return m_vram_w; }
	inline uint get_vram_tx() { return m_vram_r+m_vram_w; }
	inline uint get_mem_cycles()  { return m_mem_cycles; }
	inline uint get_fetch_cycles(){ return m_fetch_cycles; }
	inline uint get_cycles_ahead(){ return m_cycles_ahead; }
	inline bool is_pq_valid() { return m_s.pq_valid; }
	void update(int _cycles);

	//instruction fetching
	uint8_t fetchb();
	uint16_t fetchw();
	uint32_t fetchdw();

	inline uint32_t get_eip() const { return m_s.eip; }
	inline uint32_t get_cseip() const { return m_s.cseip; }

	inline void invalidate_pq() {
		m_s.pq_valid = false;
		m_s.eip = REG_EIP;
		m_s.cseip = GET_PHYADDR(CS, m_s.eip);
	}

	inline uint8_t mem_read_byte(uint32_t _addr) {
		//TODO video ram range is a special case; also, this check is already
		//done in Memory::read* and Memory::write*. Move into the Memory class.
		if(_addr >= 0xA0000 && _addr <= 0xBFFFF) {
			m_vram_r += 1;
		} else {
			m_dram_r += 1;
		}
		if(m_cycles_ahead) {
			m_mem_cycles += m_cycles_ahead;
			m_cycles_ahead = 0;
		}
		return g_memory.read_byte(_addr);
	}
	inline uint16_t mem_read_word(uint32_t _addr) {
		/* When the 286 is asked to perform a word-sized access
		 * starting at an odd address, it actually performs two separate
		 * accesses, each of which fetches 1 byte, just as the 8088 does for
		 * all word-sized accesses.
		 */
		if(_addr >= 0xA0000 && _addr <= 0xBFFFF) {
			m_vram_r += 2; //apparently, the VGA has a 8-bit data bus
		} else {
			m_dram_r += 1 + (_addr & 1);
		}
		if(m_cycles_ahead) {
			m_mem_cycles += m_cycles_ahead;
			m_cycles_ahead = 0;
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
		uint c;
		if(_addr >= 0xA0000 && _addr <= 0xBFFFF) {
			m_vram_w += 1;
			c = VRAM_TX_CYCLES;
		} else {
			m_dram_w += 1;
			c = DRAM_TX_CYCLES;
		}
		if(m_cycles_ahead) {
			m_mem_cycles += m_cycles_ahead;
			m_cycles_ahead = 0;
		}
		m_write_queue.push_back(wq_data(_addr,_data,1,c));
	}
	inline void mem_write_word(uint32_t _addr, uint16_t _data) {
		uint c;
		if(_addr >= 0xA0000 && _addr <= 0xBFFFF) {
			m_vram_w += 2; //apparently, the VGA has a 8-bit data bus
			c = VRAM_TX_CYCLES * 2;
		} else {
			c = 1 + (_addr & 1);
			m_dram_w += c;
			c *= DRAM_TX_CYCLES;
		}
		if(m_cycles_ahead) {
			m_mem_cycles += m_cycles_ahead;
			m_cycles_ahead = 0;
		}
		m_write_queue.push_back(wq_data(_addr,_data,2,c));
	}

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

	int write_pq_to_logfile(FILE *_dest);
};

#endif
