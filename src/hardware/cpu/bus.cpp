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


CPUBus::CPUBus()
: m_cycles_ahead(0)
{
	memset(&m_s,0,sizeof(m_s));
	reset_counters();
}

void CPUBus::init()
{
}

void CPUBus::reset()
{
	invalidate_pq();
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

	if(m_width == 16) {
		fill_pq = &CPUBus::fill_pq_16;
	} else {
		fill_pq = &CPUBus::fill_pq_32;
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
	//invalidate_pq(); why?
}

void CPUBus::reset_pq()
{
	m_s.eip = REG_EIP;
	if(IS_PAGING()) {
		m_s.cseip = g_cpummu.TLB_lookup(REG_CS.desc.base + REG_EIP, 1, IS_USER_PL, false);
		//MMU writes to memory
		update(0);
	} else {
		m_s.cseip = REG_CS.desc.base + REG_EIP;
	}
	invalidate_pq();
	m_cycles_ahead = 0;
}

void CPUBus::update(int _cycles)
{
	if(m_mem_r_cycles || m_mem_w_cycles) {
		m_pmem_cycles += m_cycles_ahead;
		m_cycles_ahead = 0;
	}
	if(m_s.pq_valid) {
		_cycles -= m_cycles_ahead;
	}
	if(_cycles > 0) {
		m_cycles_ahead = 0;
		if(USE_PREFETCH_QUEUE && (pq_free_space() >= m_pq_thres)) {
			_cycles -= (this->*fill_pq)(0, _cycles);
		}
	}
	if(_cycles <= 0) {
		m_cycles_ahead = (-1 * _cycles);
	}
	m_cycles_ahead += m_mem_w_cycles;
}

int CPUBus::fill_pq_16(int _amount, int _cycles)
{
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
	uint64_t pq_limit = uint64_t(m_s.pq_tail) + pq_free_space() - 2;
	int c = 0;
	pq_ptr = &m_s.pq[m_s.pq_len]; // the next free byte slot
	// fill until the requested amount is reached or there are available cycles
	// and there are free space in the queue
	while((_amount>0 || _cycles>c) && m_s.pq_tail <= pq_limit) {
		int adv = 2 - (m_s.pq_tail & 0x1);
		if(adv == 1) {
			*pq_ptr++ = g_memory.read<1>(m_s.pq_tail, c);
		} else {
			*((uint16_t*)pq_ptr) = g_memory.read<2>(m_s.pq_tail, c);
		}
		pq_ptr += adv;
		m_s.pq_tail += adv;
		_amount -= adv;
		m_s.pq_len += adv;
	}
	m_s.pq_valid = true;
	return c;
}

int CPUBus::fill_pq_32(int _amount, int _cycles)
{
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
	uint64_t pq_limit = uint64_t(m_s.pq_tail) + pq_free_space() - 2;
	int c = 0;
	pq_ptr = &m_s.pq[m_s.pq_len]; // the next free byte slot
	// fill until the requested amount is reached or there are available cycles
	// and there are free space in the queue
	while((_amount>0 || _cycles>c) && m_s.pq_tail <= pq_limit) {
		int adv = 4 - (m_s.pq_tail & 0x3);
		switch(adv) {
			case 4: // dword aligned
				*((uint32_t*)pq_ptr) = g_memory.read<4>(m_s.pq_tail, c);
				break;
			case 3: { // 1-byte unaligned (left)
				uint32_t v = g_memory.read<4>(m_s.pq_tail-1, c);
				*pq_ptr = v >> 8;
				*((uint16_t*)(pq_ptr+1)) = v >> 16;
				break; }
			case 2: // word aligned
				*((uint16_t*)pq_ptr) = g_memory.read<2>(m_s.pq_tail, c);
				break;
			case 1: // 1-byte unaligned (right)
				*pq_ptr = g_memory.read<1>(m_s.pq_tail, c);
				break;
		}
		pq_ptr += adv;
		m_s.pq_tail += adv;
		_amount -= adv;
		m_s.pq_len += adv;
	}
	m_s.pq_valid = true;
	return c;
}

template<>
uint32_t CPUBus::mem_read<2>(uint32_t _addr, int &_cycles)
{
	if(((_addr&0x1)==0) || (m_width==32 && ((_addr&0x3)==1))) {
		// even address or a word inside a dword boundary on a 32bit bus
		return g_memory.read_t<2>(_addr, 2, _cycles);
	}
	// odd address and (not 32-bit bus or between dwords)
	return (
		g_memory.read_t<1>(_addr,  2, _cycles) |
		g_memory.read<1>(  _addr+1,   _cycles) << 8
	);
}

template<>
uint32_t CPUBus::mem_read<3>(uint32_t _addr, int &_cycles)
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
uint32_t CPUBus::mem_read<4>(uint32_t _addr, int &_cycles)
{
	if(m_width == 16) {
		if((_addr&0x1) == 0) {
			// word aligned
			return (
				g_memory.read_t<2>(_addr,   4, _cycles) |
				g_memory.read<2>(  _addr+2,    _cycles) << 16
			);
		}
		return (
			g_memory.read_t<1>(_addr,   4, _cycles) |
			g_memory.read<2>(  _addr+1,    _cycles) << 8   |
			g_memory.read<1>(  _addr+3,    _cycles) << 24
		);
	} else {
		if((_addr&0x3) == 0) {
			// dword aligned
			return g_memory.read_t<4>(_addr, 4, _cycles);
		}
		uint32_t v;
		if((_addr&0x3) == 2) {
			// word aligned
			v = g_memory.read<2>(_addr,   _cycles) |
				g_memory.read<2>(_addr+2, _cycles) << 16;
		} else if((_addr&0x3) == 1) {
			// 1-byte unaligned (left)
			v = g_memory.read<4>(_addr-1, _cycles) >> 8 |
				g_memory.read<1>(_addr+3, _cycles) << 24;
		} else {
			// 1-byte unaligned (right)
			v = g_memory.read<1>(_addr,   _cycles) |
				g_memory.read<4>(_addr+1, _cycles) << 8;
		}
		g_memory.check_trap(_addr, MEM_TRAP_READ, v, 4);
		return v;
	}
}

template<>
void CPUBus::mem_write<2>(uint32_t _addr, uint32_t _data, int &_cycles)
{
	if(((_addr&0x1)==0) || (m_width==32 && ((_addr&0x3)==1))) {
		// even address or a word inside a dword boundary on a 32bit bus
		g_memory.write_t<2>(_addr, _data, 2, _cycles);
		return;
	}
	// odd address or word across two dwords on 32-bit bus
	g_memory.write_t<1>(_addr,   _data,   2, _cycles);
	g_memory.write<1>(  _addr+1, _data>>8,   _cycles);
}

template<>
void CPUBus::mem_write<3>(uint32_t _addr, uint32_t _data, int &_cycles)
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
void CPUBus::mem_write<4>(uint32_t _addr, uint32_t _data, int &_cycles)
{
	if(m_width == 16) {
		if((_addr&0x1) == 0) {
			// word aligned
			g_memory.write_t<2>(_addr,   _data,    4, _cycles);
			g_memory.write<2>(  _addr+2, _data>>16,   _cycles);
		} else {
			g_memory.write_t<1>(_addr,   _data,    4, _cycles);
			g_memory.write<2>(  _addr+1, _data>>8,    _cycles);
			g_memory.write<1>(  _addr+3, _data>>24,   _cycles);
		}
	} else {
		if((_addr&0x3) == 0) {
			// dword aligned
			g_memory.write_t<4>(_addr, _data, 4, _cycles);
			return;
		}
		int c;
		if((_addr&0x3) == 2) {
			// word aligned
			g_memory.write<2>(_addr,   _data,     _cycles);
			g_memory.write<2>(_addr+2, _data>>16, _cycles);
		} else if((_addr&0x3) == 1) {
			// 1-byte unaligned (left)
			uint32_t v = g_memory.read<1>(_addr-1, c);
			v |= (_data<<8);
			g_memory.write<4>(_addr-1, v,         _cycles);
			g_memory.write<1>(_addr+3, _data>>24, _cycles);
		} else {
			// 1-byte unaligned (right)
			g_memory.write<1>(_addr, _data, _cycles);
			uint32_t v = g_memory.read<1>(_addr+4, c);
			v = (v<<24) | (_data>>8);
			g_memory.write<4>(_addr+1, v, _cycles);
		}
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
