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
#define CPU_BUS_WQ_SIZE  50

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
	int m_paddress; // pipelined address
	int m_fetch_cycles;
	int m_mem_r_cycles;
	int m_mem_w_cycles;
	int m_mem_w_count;
	int m_pmem_cycles;   // pipelined memory cycles
	int m_pfetch_cycles; // pipelined fetch cycles
	int m_cycles_ahead;

	struct wq_data {
		void (CPUBus::*w_fn)(uint32_t _addr, uint32_t _data, int &_cycles);
		uint32_t address;
		uint32_t data;
	};
	wq_data m_write_queue[CPU_BUS_WQ_SIZE];
	int m_wq_idx;

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

	inline bool memory_accessed() const { return (m_mem_r_cycles || (m_wq_idx>=0)); }
	inline bool memory_written() const { return (m_wq_idx>=0) || m_mem_w_cycles; }
	inline int  fetch_cycles() const { return m_fetch_cycles; }
	inline int  mem_r_cycles() const { return m_mem_r_cycles; }
	inline int  mem_tx_cycles() const { return m_mem_r_cycles + m_mem_w_cycles; }
	inline int  pipelined_mem_cycles() const { return m_pmem_cycles; }
	inline int  pipelined_fetch_cycles() const { return m_pfetch_cycles; }
	inline int  cycles_ahead() const { return m_cycles_ahead; }
	inline bool pq_is_valid() const { return m_s.pq_valid; }
	inline int  width() const { return m_width; }

	void update(int _cycles);
	void enable_paging(bool _enabled);

	//instruction fetching
	#if USE_PREFETCH_QUEUE
	inline uint8_t  fetchb()  { return fetch<uint8_t, 1>(); }
	inline uint16_t fetchw()  { return fetch<uint16_t,2>(); }
	inline uint32_t fetchdw() { return fetch<uint32_t,4>(); }
	#else
	inline uint8_t  fetchb()  { return fetch_noqueue<uint8_t, 1>(); }
	inline uint16_t fetchw()  { return fetch_noqueue<uint16_t,2>(); }
	inline uint32_t fetchdw() { return fetch_noqueue<uint32_t,4>(); }
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

	template<unsigned S> inline uint32_t mem_read(uint32_t _addr)
	{
		return p_mem_read<S>(_addr, m_mem_r_cycles);
	}
	template<unsigned S> inline void mem_write(uint32_t _addr, uint32_t _data)
	{
		/* Memory writes need to be executed after a PQ update, because code
		 * prefetching is done after the instruction execution, in relation to
		 * the available cpu cycles. The executed instruction could be a mov
		 * used to modify the code though, and the prefetching would read the
		 * already modified code in memory.
		 */
		assert(m_wq_idx<CPU_BUS_WQ_SIZE-1);
		m_write_queue[++m_wq_idx] = { &CPUBus::p_mem_write<S>, _addr, _data };
	}

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

	int write_pq_to_logfile(FILE *_dest);

private:
	template<unsigned> uint32_t p_mem_read(uint32_t _addr, int &_cycles) { assert(false); }
	template<unsigned> void p_mem_write(uint32_t _addr, uint32_t _data, int &_cycles) { assert(false); }

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
	uint8_t *move_pq_data();
	template<int len> uint32_t mmu_read(uint32_t _linear, int &_cycles);
	template<int bytes, bool paging> int fill_pq(int _amount, int _cycles, bool _paddress);
	int (CPUBus::*fill_pq_fn)(int, int, bool);

	template<class T, int L>
	T fetch()
	{
		if(m_s.pq_len < L) {
			m_fetch_cycles += (this->*fill_pq_fn)(L-m_s.pq_len, 0, m_fetch_cycles>0);
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
uint32_t CPUBus::p_mem_read<1>(uint32_t _addr, int &_cycles)
{
	return g_memory.read_t<1>(_addr, 1, _cycles);
}
template<> uint32_t CPUBus::p_mem_read<2>(uint32_t _addr, int &_cycles);
template<> uint32_t CPUBus::p_mem_read<3>(uint32_t _addr, int &_cycles);
template<> uint32_t CPUBus::p_mem_read<4>(uint32_t _addr, int &_cycles);

template<> inline
void CPUBus::p_mem_write<1>(uint32_t _addr, uint32_t _data, int &_cycles)
{
	g_memory.write_t<1>(_addr, _data, 1, _cycles);
}
template<> void CPUBus::p_mem_write<2>(uint32_t _addr, uint32_t _data, int &_cycles);
template<> void CPUBus::p_mem_write<3>(uint32_t _addr, uint32_t _data, int &_cycles);
template<> void CPUBus::p_mem_write<4>(uint32_t _addr, uint32_t _data, int &_cycles);


#endif
