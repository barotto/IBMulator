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

#include "ibmulator.h"
#include "bus.h"
#include "mmu.h"
#include "../cpu.h"
#include "program.h"
#include <cstring>

CPUBus g_cpubus;

#define PIPELINED_ADDR_286  0
#define PIPELINED_ADDR_386  0
#define MIN_MEM_CYCLES      2

CPUBus::CPUBus()
: m_cycles_ahead(0),
  m_wq_idx(-1)
{
	memset(&m_s, 0, sizeof(m_s));
	memset(&m_write_queue, 0, sizeof(m_write_queue));
	reset_counters();
}

void CPUBus::init()
{
}

void CPUBus::reset()
{
	invalidate_pq();
	enable_paging(false);
	update(0);
	m_cycles_ahead = 0;
}

void CPUBus::config_changed()
{
	ini_enum_map_t cpu_busw = {
		{ "286",   16 },
		{ "386SX", 16 },
		{ "386DX", 32 }
	};

	/* http://www.rcollins.org/secrets/PrefetchQueue.html
	 * The 80386 is documented as having a 16-byte prefetch queue. At one time,
	 * it did, but due to a bug in the pipelining architecture, Intel had to
	 * abandon the 16-byte queue, and only use a 12-byte queue. The change
	 * occurred (I believe) between the D0, and D1 step of the '386.
	 * The '386SX wasn't affected by the bug, and therefore hasn't changed.
	 */
	ini_enum_map_t cpu_pqs = {
		{ "286",   6  },
		{ "386SX", 16 },
		{ "386DX", 12 }
	};

	ini_enum_map_t cpu_pqt = {
		{ "286",   2 },
		{ "386SX", 4 },
		{ "386DX", 4 }
	};

	m_width = cpu_busw[g_cpu.model()];
	m_pq_size = cpu_pqs[g_cpu.model()];
	m_pq_thres = cpu_pqt[g_cpu.model()];

	if(g_cpu.family() >= CPU_386) {
		m_paddress = PIPELINED_ADDR_386;
	} else {
		m_paddress = PIPELINED_ADDR_286;
	}

	PINFOF(LOG_V1, LOG_CPU, "  Bus width: %d-bit, Prefetch Queue: %d byte\n",
			m_width, m_pq_size);
}

void CPUBus::save_state(StateBuf &_state)
{
	_state.write(&m_s, {sizeof(m_s), "CPUBus"});
}

void CPUBus::restore_state(StateBuf &_state)
{
	_state.read(&m_s, {sizeof(m_s), "CPUBus"});
	enable_paging(IS_PAGING());
}

void CPUBus::enable_paging(bool _enabled)
{
	if(_enabled) {
		if(m_width == 16) {
			fill_pq_fn = &CPUBus::fill_pq<2,true>;
		} else {
			fill_pq_fn = &CPUBus::fill_pq<4,true>;
		}
	} else {
		if(m_width == 16) {
			fill_pq_fn = &CPUBus::fill_pq<2,false>;
		} else {
			fill_pq_fn = &CPUBus::fill_pq<4,false>;
		}
	}
}

void CPUBus::reset_pq()
{
	m_s.eip = REG_EIP;
	m_s.cseip = REG_CS.desc.base + REG_EIP;
	invalidate_pq();
	m_cycles_ahead = 0;
}

void CPUBus::update(int _cycles)
{
#if USE_PREFETCH_QUEUE
		if(m_mem_r_cycles || (m_wq_idx>=0)) {
			m_pmem_cycles += m_cycles_ahead;
			m_cycles_ahead = 0;
		}
		if(m_s.pq_valid) {
			_cycles -= m_cycles_ahead;
		}
		if(_cycles > 0) {
			m_cycles_ahead = 0;
			if(pq_free_space() >= m_pq_thres) {
				_cycles -= (this->*fill_pq_fn)(0, _cycles, 0);
			}
		}
		if(_cycles <= 0) {
			m_cycles_ahead = (-1 * _cycles);
		}
		for(int i=0; i<=m_wq_idx; i++) {
			(this->*m_write_queue[i].w_fn)(
					m_write_queue[i].address,
					m_write_queue[i].data,
					m_mem_w_cycles);
		}
		m_wq_idx = -1;
		m_cycles_ahead += m_mem_w_cycles;
#else
		m_mem_r_cycles += m_mem_w_cycles;
#endif
}

template<int len>
uint32_t CPUBus::mmu_read(uint32_t _linear, int &_cycles)
{
	uint32_t phy = g_cpummu.TLB_lookup(_linear, len, IS_USER_PL, false);
	return g_memory.read<len>(phy, _cycles);
}

template<int bytes, bool paging>
int CPUBus::fill_pq(int _amount, int _cycles, bool _paddress)
{
	UNUSED(_paddress);

	uint8_t *pq_ptr;
	if(m_s.pq_valid && m_s.pq_len) {
		// move valid bytes to the left
		int move = m_s.cseip - m_s.pq_left;
		int len = m_s.pq_len;
		pq_ptr = &m_s.pq[0];
		while(len--) {
			*pq_ptr = *(pq_ptr+move);
			pq_ptr++;
		}
	}
	m_s.pq_left = m_s.cseip;
	uint64_t pq_limit = uint64_t(m_s.pq_tail) + pq_free_space() - bytes;
	int cycles = 0;
#if (PIPELINED_ADDR_286 || PIPELINED_ADDR_386)
	int paddress = _paddress*m_paddress;
#endif
	pq_ptr = &m_s.pq[m_s.pq_len]; // the next free byte slot
	int amount = _amount;
	// fill until the requested amount is reached or there are available cycles
	// and there are free space in the queue
	while((amount>0 || _cycles>cycles) && m_s.pq_tail <= pq_limit) {
	/* alternative, more permissive strategy:
	 * while((amount>0 || (_cycles && _cycles>=cycles)) && m_s.pq_tail <= pq_limit)
	 */
		int adv = bytes - (m_s.pq_tail & (bytes-1));
		int c = 0;
		// one of these if-else blocks should be removed by the compiler
		if(paging) {
			// reads must be inside dword boundaries
			assert((PAGE_OFFSET(m_s.pq_tail) + adv) <= 4096);
			try {
				switch(adv) {
					case 2: // word aligned
						*((uint16_t*)pq_ptr) = mmu_read<2>(m_s.pq_tail, c);
						break;
					case 1: // 1-byte unaligned (right)
						*pq_ptr = mmu_read<1>(m_s.pq_tail, c);
						break;
					case 4: // dword aligned
						*((uint32_t*)pq_ptr) = mmu_read<4>(m_s.pq_tail, c);
						break;
					case 3: { // 1-byte unaligned (left)
						uint32_t v = mmu_read<4>(m_s.pq_tail-1, c);
						*pq_ptr = v >> 8;
						*((uint16_t*)(pq_ptr+1)) = v >> 16;
						break;
					}
				}
			} catch(CPUException &) {
				// #PF are catched here
				if(_amount) {
					// the requested amount is not present
					// throw page fault for instruction decoding
					m_s.pq_valid = false;
					throw;
				} else {
					// no amount required, queue is filled with 0 or more valid bytes
					// don't throw exceptions for code prefetching
					m_s.pq_valid = true;
					return cycles;
				}
			}
		} else {
			switch(adv) {
				case 2: // word aligned
					*((uint16_t*)pq_ptr) = g_memory.read<2>(m_s.pq_tail, c);
					break;
				case 1: // 1-byte unaligned (right)
					*pq_ptr = g_memory.read<1>(m_s.pq_tail, c);
					break;
				case 4: // dword aligned
					*((uint32_t*)pq_ptr) = g_memory.read<4>(m_s.pq_tail, c);
					break;
				case 3: { // 1-byte unaligned (left)
					uint32_t v = g_memory.read<4>(m_s.pq_tail-1, c);
					*pq_ptr = v >> 8;
					*((uint16_t*)(pq_ptr+1)) = v >> 16;
					break;
				}
			}
		}
#if (PIPELINED_ADDR_286 || PIPELINED_ADDR_386)
		c -= paddress;
		paddress = m_paddress;
		c = std::max(c,MIN_MEM_CYCLES);
#endif
		cycles += c;
		pq_ptr += adv;
		m_s.pq_tail += adv;
		amount -= adv;
		m_s.pq_len += adv;
	}
	m_s.pq_valid = true;
	return cycles;
}

template<>
uint32_t CPUBus::p_mem_read<2>(uint32_t _addr, int &_cycles)
{
	if(((_addr&0x1)==0) || (m_width==32 && ((_addr&0x3)==1))) {
		// even address or a word inside a dword boundary on a 32bit bus
		return g_memory.read_t<2>(_addr, 2, _cycles);
	}
	// odd address and (not 32-bit bus or between dwords)
	int c = -m_paddress;
	uint16_t v = g_memory.read_t<1>(_addr,  2, c) |
	             g_memory.read<1>(  _addr+1,   c) << 8;
	c = std::max(c,MIN_MEM_CYCLES*2);
	_cycles += c;
	g_memory.check_trap(_addr, MEM_TRAP_READ, v, 2);
	return v;
}

template<>
uint32_t CPUBus::p_mem_read<3>(uint32_t _addr, int &_cycles)
{
	// this is called only for unaligned cross page dword reads
	// see cpu/executor/memory.cpp
	if(m_width == 16) {
		if(_addr&0x1) {
			return g_memory.read<1>(_addr,   _cycles) |
			       g_memory.read<2>(_addr+1, _cycles) << 8;
		}
		return g_memory.read<2>(_addr,   _cycles) |
		       g_memory.read<1>(_addr+2, _cycles) << 16;
	} else {
		if(_addr&0x1) {
			return g_memory.read<4>(_addr-1, _cycles) >> 8;
		}
		assert((_addr&0x3) == 0);
		return g_memory.read<4>(_addr, _cycles) & 0x00FFFFFF;
	}
}

template<>
uint32_t CPUBus::p_mem_read<4>(uint32_t _addr, int &_cycles)
{
	uint32_t v;
	int c;
	if(m_width == 16) {
		if((_addr&0x1) == 0) {
			// word aligned
			c = -m_paddress;
			v =	g_memory.read<2>(_addr,   c) |
				g_memory.read<2>(_addr+2, c) << 16;
			c = std::max(c,MIN_MEM_CYCLES*2);
			_cycles += c;
			g_memory.check_trap(_addr, MEM_TRAP_READ, v, 4);
			return v;
		} else {
			c = -(m_paddress*2);
			v =	g_memory.read<1>(_addr,   _cycles) |
				g_memory.read<2>(_addr+1, _cycles) << 8   |
				g_memory.read<1>(_addr+3, _cycles) << 24;
			c = std::max(c,MIN_MEM_CYCLES*3);
			_cycles += c;
			g_memory.check_trap(_addr, MEM_TRAP_READ, v, 4);
			return v;
		}
	} else {
		if((_addr&0x3) == 0) {
			// dword aligned
			return g_memory.read_t<4>(_addr, 4, _cycles);
		}
		c = -m_paddress;
		if((_addr&0x3) == 2) {
			// word aligned
			v = g_memory.read<2>(_addr,   c) |
				g_memory.read<2>(_addr+2, c) << 16;
		} else if((_addr&0x3) == 1) {
			// 1-byte unaligned (left)
			v = g_memory.read<4>(_addr-1, c) >> 8 |
				g_memory.read<1>(_addr+3, c) << 24;
		} else {
			// 1-byte unaligned (right)
			v = g_memory.read<1>(_addr,   c) |
				g_memory.read<4>(_addr+1, c) << 8;
		}
		c = std::max(c,MIN_MEM_CYCLES*2);
		_cycles += c;
		g_memory.check_trap(_addr, MEM_TRAP_READ, v, 4);
		return v;
	}
}

template<>
void CPUBus::p_mem_write<2>(uint32_t _addr, uint32_t _data, int &_cycles)
{
	if(((_addr&0x1)==0) || (m_width==32 && ((_addr&0x3)==1))) {
		// even address or a word inside a dword boundary on a 32bit bus
		g_memory.write_t<2>(_addr, _data, 2, _cycles);
		return;
	}
	// odd address or word across two dwords on 32-bit bus
	int c = -m_paddress;
	g_memory.write_t<1>(_addr,   _data,   2, c);
	g_memory.write<1>(  _addr+1, _data>>8,   c);
	c = std::max(c,MIN_MEM_CYCLES*2);
	_cycles += c;
}

template<>
void CPUBus::p_mem_write<3>(uint32_t _addr, uint32_t _data, int &_cycles)
{
	// this is called only for unaligned cross page dword writes
	// see cpu/executor/memory.cpp
	if(_addr&0x1) {
		g_memory.write<1>(_addr,   _data,    _cycles);
		g_memory.write<2>(_addr+1, _data>>8, _cycles);
	} else {
		g_memory.write<2>(_addr,   _data,     _cycles);
		g_memory.write<1>(_addr+2, _data>>16, _cycles);
	}
}

template<>
void CPUBus::p_mem_write<4>(uint32_t _addr, uint32_t _data, int &_cycles)
{
	int c;
	if(m_width == 16) {
		if((_addr&0x1) == 0) {
			// word aligned
			c = -m_paddress;
			g_memory.write_t<2>(_addr,   _data,    4, c);
			g_memory.write<2>(  _addr+2, _data>>16,   c);
			c = std::max(c,MIN_MEM_CYCLES*2);
		} else {
			c = -(m_paddress*2);
			g_memory.write_t<1>(_addr,   _data,    4, c);
			g_memory.write<2>(  _addr+1, _data>>8,    c);
			g_memory.write<1>(  _addr+3, _data>>24,   c);
			c = std::max(c,MIN_MEM_CYCLES*3);
		}
		_cycles += c;
	} else {
		if((_addr&0x3) == 0) {
			// dword aligned
			g_memory.write_t<4>(_addr, _data, 4, _cycles);
			return;
		}
		c = -m_paddress;
		if((_addr&0x3) == 2) {
			// word aligned
			g_memory.write<2>(_addr,   _data,     c);
			g_memory.write<2>(_addr+2, _data>>16, c);
		} else if((_addr&0x3) == 1) {
			// 1-byte unaligned (left)
			int t;
			uint32_t v = g_memory.read<1>(_addr-1, t);
			v |= (_data<<8);
			g_memory.write<4>(_addr-1, v,         c);
			g_memory.write<1>(_addr+3, _data>>24, c);
		} else {
			// 1-byte unaligned (right)
			g_memory.write<1>(_addr, _data, c);
			int t;
			uint32_t v = g_memory.read<1>(_addr+4, t);
			v = (v<<24) | (_data>>8);
			g_memory.write<4>(_addr+1, v, c);
		}
		c = std::max(c,MIN_MEM_CYCLES*2);
		_cycles += c;
		g_memory.check_trap(_addr, MEM_TRAP_READ, _data, 4);
	}
}

int CPUBus::write_pq_to_logfile(FILE *_dest)
{
	int res = 0;
	if(pq_is_valid()) {
		res = fprintf(_dest, "v");
	} else {
		res = fprintf(_dest, " ");
	}
	if(res<0) {
		return -1;
	}

	if(pq_is_empty()) {
		res = fprintf(_dest, "e");
	} else {
		res = fprintf(_dest, " ");
	}
	if(res<0) {
		return -1;
	}

	if(fprintf(_dest, " ") < 0) {
		return -1;
	}

	for(int i=0; i<m_s.pq_len; i++) {
		if(fprintf(_dest, "%02X ", m_s.pq[pq_idx()+i]) < 0) {
			return -1;
		}
	}
	return 0;
}
