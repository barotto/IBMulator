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

#define CPU_PQ_MAX_SIZE  16

class CPUBus;
extern CPUBus g_cpubus;


class CPUBus
{
private:
	struct {
		uint32_t cseip;
		uint32_t eip;
		uint8_t  pq[CPU_PQ_MAX_SIZE];
		bool     pq_valid;
		uint32_t pq_tail;
		uint32_t pq_left;
		int      pq_len;
	} m_s;

	int m_width;
	int m_pq_size;
	int m_pq_thres;
	int m_fetch_cycles;
	int m_mem_r_cycles;
	int m_mem_w_cycles;
	int m_pmem_cycles;   // pipelined memory cycles
	int m_pfetch_cycles; // pipelined fetch cycles
	int m_cycles_ahead;

public:
	CPUBus();

	void init();
	void reset();
	void config_changed();

	inline void reset_counters() {
		m_fetch_cycles  = 0;
		m_mem_r_cycles  = 0;
		m_mem_w_cycles  = 0;
		m_pmem_cycles   = 0;
		m_pfetch_cycles = 0;
	}

	inline bool memory_accessed() const { return (m_mem_r_cycles || m_mem_w_cycles); }
	inline int  fetch_cycles() const { return m_fetch_cycles; }
	inline int  mem_r_cycles() const { return m_mem_r_cycles; }
	inline int  mem_w_cycles() const { return m_mem_w_cycles; }
	inline int  mem_tx_cycles() const { return m_mem_r_cycles + m_mem_w_cycles; }
	inline int  pipelined_mem_cycles() const { return m_pmem_cycles; }
	inline int  pipelined_fetch_cycles() const { return m_pfetch_cycles; }
	inline int  cycles_ahead() const { return m_cycles_ahead; }
	inline bool pq_is_valid() const { return m_s.pq_valid; }

	void update(int _cycles);

	//instruction fetching
	#if USE_PREFETCH_QUEUE
	uint8_t  fetchb()  { return fetch<uint8_t, 1>(); }
	uint16_t fetchw()  { return fetch<uint16_t,2>(); }
	uint32_t fetchdw() { return fetch<uint32_t,4>(); }
	#else
	uint8_t  fetchb()  { return fetch_noqueue<uint8_t, 1>(); }
	uint16_t fetchw()  { return fetch_noqueue<uint16_t,2>(); }
	uint32_t fetchdw() { return fetch_noqueue<uint32_t,4>(); }
	#endif

	inline uint32_t eip() const { return m_s.eip; }
	inline uint32_t cseip() const { return m_s.cseip; }

	inline void invalidate_pq() {
		m_s.pq_valid = false;
		m_s.pq_len = 0;
		m_s.pq_left = m_s.cseip;
		m_s.pq_tail = m_s.pq_left;
	}
	void reset_pq();

	template<unsigned> uint32_t mem_read(uint32_t _addr) { assert(false); }
	template<unsigned> void mem_write(uint32_t _addr, uint32_t _data) { assert(false); }

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

	int write_pq_to_logfile(FILE *_dest);

private:
	template<unsigned> uint32_t mem_read(uint32_t _addr, int &_cycles) { assert(false); }
	template<unsigned> void mem_write(uint32_t _addr, uint32_t _data, int &_cycles) { assert(false); }

	ALWAYS_INLINE
	inline int pq_free_space() {
		return m_pq_size - m_s.pq_len;
	}
	ALWAYS_INLINE
	inline int pq_idx() {
		return m_s.cseip - m_s.pq_left;
	}
	ALWAYS_INLINE
	inline bool pq_is_empty() {
		return m_s.pq_len == 0;
	}
	int fill_pq_16(int _amount, int _cycles);
	int fill_pq_32(int _amount, int _cycles);
	int (CPUBus::*fill_pq)(int, int);

	template<class T, int L>
	T fetch()
	{
		if(m_s.pq_len < L) {
			m_fetch_cycles += (this->*fill_pq)(L-m_s.pq_len, 0);
			if(m_cycles_ahead) {
				m_pfetch_cycles += m_cycles_ahead;
				m_cycles_ahead = 0;
			}
		}
		T data = *(T*)&m_s.pq[pq_idx()];
		m_s.pq_len -= L;
		m_s.cseip += L;
		m_s.eip += L;
		return data;
	}

	template<class T, int L>
	T fetch_noqueue()
	{
		T data = mem_read<L>(m_s.cseip);
		m_s.cseip += L;
		m_s.eip += L;
		return data;
	}
};


template<> inline
uint32_t CPUBus::mem_read<1>(uint32_t _addr, int &_cycles)
{
	return g_memory.read_t<1>(_addr, 1, _cycles);
}
template<> uint32_t CPUBus::mem_read<2>(uint32_t _addr, int &_cycles);
template<> uint32_t CPUBus::mem_read<3>(uint32_t _addr, int &_cycles);
template<> uint32_t CPUBus::mem_read<4>(uint32_t _addr, int &_cycles);

template<> inline
uint32_t CPUBus::mem_read<1>(uint32_t _addr)
{
	return mem_read<1>(_addr, m_mem_r_cycles);
}
template<> inline
uint32_t CPUBus::mem_read<2>(uint32_t _addr)
{
	return mem_read<2>(_addr, m_mem_r_cycles);
}
template<> inline
uint32_t CPUBus::mem_read<3>(uint32_t _addr)
{
	return mem_read<3>(_addr, m_mem_r_cycles);
}
template<> inline
uint32_t CPUBus::mem_read<4>(uint32_t _addr)
{
	return mem_read<4>(_addr, m_mem_r_cycles);
}


template<> inline
void CPUBus::mem_write<1>(uint32_t _addr, uint32_t _data, int &_cycles)
{
	g_memory.write_t<1>(_addr, _data, 1, _cycles);
}
template<> void CPUBus::mem_write<2>(uint32_t _addr, uint32_t _data, int &_cycles);
template<> void CPUBus::mem_write<3>(uint32_t _addr, uint32_t _data, int &_cycles);
template<> void CPUBus::mem_write<4>(uint32_t _addr, uint32_t _data, int &_cycles);

template<> inline
void CPUBus::mem_write<1>(uint32_t _addr, uint32_t _data)
{
	mem_write<1>(_addr, _data, m_mem_w_cycles);
}
template<> inline
void CPUBus::mem_write<2>(uint32_t _addr, uint32_t _data)
{
	mem_write<2>(_addr, _data, m_mem_w_cycles);
}
template<> inline
void CPUBus::mem_write<3>(uint32_t _addr, uint32_t _data)
{
	mem_write<3>(_addr, _data, m_mem_w_cycles);
}
template<> inline
void CPUBus::mem_write<4>(uint32_t _addr, uint32_t _data)
{
	mem_write<4>(_addr, _data, m_mem_w_cycles);
}


#endif
