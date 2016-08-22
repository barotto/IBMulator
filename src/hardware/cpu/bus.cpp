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

ini_enum_map_t g_cpu_busw = {
	{ "286",   2 },
	{ "386SX", 2 },
	{ "386DX", 4 }
};

/* http://www.rcollins.org/secrets/PrefetchQueue.html
 * The 80386 is documented as having a 16-byte prefetch queue. At one time,
 * it did, but due to a bug in the pipelining architecture, Intel had to
 * abandon the 16-byte queue, and only use a 12-byte queue. The change
 * occurred (I believe) between the D0, and D1 step of the '386.
 * The '386SX wasn't affected by the bug, and therefore hasn't changed.
 */
ini_enum_map_t g_cpu_pqs = {
	{ "286",   6 },
	{ "386SX", 16 },
	{ "386DX", 12 }
};

ini_enum_map_t g_cpu_pqt = {
	{ "286",   2 },
	{ "386SX", 4 },
	{ "386DX", 4 }
};

#define CPUBUS_STATE_NAME "CPUBus"
#define CPU_PQ_ODDPENALTY ((!m_s.pq_valid) && (m_s.pq_tail & 1))


CPUBus::CPUBus()
: m_cycles_ahead(0)
{
	memset(&m_s,0,sizeof(m_s));
	reset_counters();
}

void CPUBus::init()
{
	m_write_queue.reserve(10);
}

void CPUBus::reset()
{
	invalidate_pq();
	update(0);
	m_cycles_ahead = 0;
}

void CPUBus::config_changed()
{
	m_width = g_program.config().get_enum(CPU_SECTION, CPU_MODEL, g_cpu_busw);
	m_pq_size = g_program.config().get_enum(CPU_SECTION, CPU_MODEL, g_cpu_pqs);
	m_pq_thres = g_program.config().get_enum(CPU_SECTION, CPU_MODEL, g_cpu_pqt);

	PINFOF(LOG_V1, LOG_CPU, "Bus width: %d-bit, Prefetch Queue: %d byte\n",
			m_width*8, m_pq_size);
}

void CPUBus::save_state(StateBuf &_state)
{
	StateHeader h;
	h.name = CPUBUS_STATE_NAME;
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);
}

void CPUBus::restore_state(StateBuf &_state)
{
	StateHeader h;
	h.name = CPUBUS_STATE_NAME;
	h.data_size = sizeof(m_s);
	_state.read(&m_s,h);
}

void CPUBus::pq_fill(uint btoread)
{
	uint pos = (get_pq_cur_index() + get_pq_cur_size()) % m_pq_size;
	while(btoread--) {
		uint32_t addr = m_s.pq_tail;
		m_s.pq[pos++] = g_memory.read<1>(addr);
		m_s.pq_tail++;
		pos %= m_pq_size;
	}
	int hadv = m_s.pq_tail-m_s.pq_head-m_pq_size;
	if(hadv>0) {
		m_s.pq_head += hadv;
		m_s.pq_headpos = (m_s.pq_headpos+hadv) % m_pq_size;
	}
	m_s.pq_valid = true;
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
	m_s.pq_head = m_s.cseip;
	m_s.pq_headpos = 0;
	m_s.pq_tail = m_s.cseip;
	m_cycles_ahead = 0;
}

void CPUBus::update(int _cycles)
{
	if(m_s.pq_valid) {
		_cycles -= m_cycles_ahead;
	}
	if(_cycles>0) {
		m_cycles_ahead = 0;
		uint free_space = get_pq_free_space();
		if(free_space >= m_pq_thres) {
			uint bytes = CPU_PQ_ODDPENALTY;
			_cycles -= DRAM_TX_CYCLES*bytes;
			while(_cycles>0) {
				bytes += 2;
				_cycles -= DRAM_TX_CYCLES;
			}
			if(bytes>free_space) {
				bytes = free_space;
			} else {
				m_cycles_ahead = (uint)(-1 * _cycles);
			}
			pq_fill(bytes);
		}
	} else {
		m_cycles_ahead = (uint)(-1 * _cycles);
	}
	//flush the write queue
	for(wq_data& data : m_write_queue) {
		if(data.len==1) {
			g_memory.write<1>(data.address, data.data, data.trap_len);
		} else if (data.len==2) {
			g_memory.write<2>(data.address, data.data, data.trap_len);
		} else {
			assert(data.len==4);
			g_memory.write<4>(data.address, data.data, data.trap_len);
		}
		m_cycles_ahead += data.cycles;
	}
	m_write_queue.clear();
}

uint8_t CPUBus::fetchb()
{
	uint8_t b;
	if(USE_PREFETCH_QUEUE) {
		assert(m_s.cseip <= m_s.pq_tail);
		if(m_s.cseip == m_s.pq_tail) {
			//the pq is empty
			if(CPU_PQ_ODDPENALTY) {
				pq_fill(1);
			} else {
				pq_fill(2);
			}
			m_dram_r += 1;
			if(m_cycles_ahead) {
				m_fetch_cycles += m_cycles_ahead;
				m_cycles_ahead = 0;
			}
		}
		b = m_s.pq[get_pq_cur_index()];
	} else {
		b = g_memory.read<1>(m_s.cseip);
		m_dram_r++;
	}
	m_s.cseip++;
	m_s.eip++;
	return b;
}

uint16_t CPUBus::fetchw()
{
	uint8_t b0, b1;
	if(USE_PREFETCH_QUEUE) {
		assert(m_s.cseip <= m_s.pq_tail);
		if(m_s.cseip >= m_s.pq_tail - 1) {
			//the pq is empty or not full enough
			if(CPU_PQ_ODDPENALTY) {
				m_dram_r += 2;
				pq_fill(3);
			} else {
				m_dram_r += 1;
				pq_fill(2);
			}
			if(m_cycles_ahead) {
				m_fetch_cycles += m_cycles_ahead;
				m_cycles_ahead = 0;
			}
		}
		b0 = m_s.pq[get_pq_cur_index()];
		m_s.cseip++;
		b1 = m_s.pq[get_pq_cur_index()];
		m_s.cseip++;
	} else {
		b0 = g_memory.read<1>(m_s.cseip);
		b1 = g_memory.read<1>(m_s.cseip+1);
		m_dram_r += 1+(m_s.cseip & 1);
		m_s.cseip += 2;
	}
	m_s.eip += 2;
	return uint16_t(b1)<<8 | b0;
}

uint32_t CPUBus::fetchdw()
{
	uint32_t w0, w1;
	//TODO STUB
	w0 = fetchw();
	w1 = fetchw();
	return (w1<<16) | w0;
}

int CPUBus::write_pq_to_logfile(FILE *_dest)
{
	int res = 0;
	if(m_s.pq_valid) {
		res = fprintf(_dest, "v");
	} else {
		res = fprintf(_dest, " ");
	}
	if(res<0) {
		return -1;
	}

	if(is_pq_empty()) {
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

	uint idx = get_pq_cur_index();
	for(uint i=0; i<get_pq_cur_size(); i++) {
		if(fprintf(_dest, "%02X ", m_s.pq[idx]) < 0) {
			return -1;
		}
		idx = (idx+1)%m_pq_size;
	}

	return 0;
}
