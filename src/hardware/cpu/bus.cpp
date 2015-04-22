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

#include "ibmulator.h"
#include "bus.h"
#include <cstring>

CPUBus g_cpubus;

#define CPUBUS_STATE_NAME "CPUBus"

#define CPU_PQ_READS_PER_FETCH 2
#define CPU_PQ_THRESHOLD       2



CPUBus::CPUBus()
: m_cycles_surplus(0)
{
	memset(&m_s,0,sizeof(m_s));
	reset_counters();
}

void CPUBus::init()
{

}

void CPUBus::config_changed()
{
	//m_cycle_time = g_cpu.get_cycle_time_ns();
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

void CPUBus::pq_fill(uint free_space, uint btoread)
{
	if(!USE_PREFETCH_QUEUE) {
		return;
	}

	ASSERT(free_space<=CPU_PQ_SIZE);

	if(btoread < CPU_PQ_READS_PER_FETCH)
		btoread = CPU_PQ_READS_PER_FETCH;
	if(btoread > free_space)
		btoread = free_space;
	m_pq_fetches = btoread;
	uint pos = (get_pq_cur_index() + get_pq_cur_size()) % CPU_PQ_SIZE;
	while(btoread--) {
		uint32_t addr = m_s.pq_tail;
		m_s.pq[pos++] = g_memory.read_byte(addr);
		m_s.pq_tail++;
		pos %= CPU_PQ_SIZE;
	}
	int hadv = m_s.pq_tail-m_s.pq_head-CPU_PQ_SIZE;
	if(hadv>0) {
		m_s.pq_head += hadv;
		m_s.pq_headpos = (m_s.pq_headpos+hadv) % CPU_PQ_SIZE;
	}
	m_s.pq_valid = true;
}

void CPUBus::update_pq(uint _cycles)
{
	if(!USE_PREFETCH_QUEUE) {
		return;
	}
	if(!m_s.pq_valid) {
		//csip will always be >CPU_PQ_SIZE
		//ASSERT(m_s.csip>CPU_PQ_SIZE);
		m_s.pq_head = m_s.csip;
		m_s.pq_headpos = 0;
		m_s.pq_tail = m_s.csip;
		m_cycles_penalty = 0;
		m_cycles_surplus = 0;
	} else if(_cycles>0) {
		uint free_space = get_pq_free_space();
		if(free_space==CPU_PQ_SIZE) {
			m_cycles_penalty = m_cycles_surplus;
		}
		m_cycles_surplus = 0;
		if(free_space>=CPU_PQ_THRESHOLD) {
			//round up the int division
			uint wtoread = (_cycles + DRAM_TX_CYCLES-1) / DRAM_TX_CYCLES;
			if(wtoread>0) {
				//the pq can fetch during the instuction execution
				pq_fill(free_space,wtoread*2);
				uint free_reads = _cycles/DRAM_TX_CYCLES;
				if(wtoread > free_reads) {
					//read too much
					m_cycles_surplus = wtoread*DRAM_TX_CYCLES - _cycles;
				}
			}
		}
	}
}

uint8_t CPUBus::fetchb()
{
	ASSERT(!USE_PREFETCH_QUEUE || m_s.csip<=m_s.pq_tail);
	if(USE_PREFETCH_QUEUE && m_s.csip == m_s.pq_tail) {
		//the pq is empty
		if(!m_s.pq_valid && (m_s.csip & 1)) {
			/*
			 * lack of word alignment of the target instruction for any branch
			 * effectively cuts the instruction-fetching power of the 286 in half
			 * for the first instruction fetch after that branch.
			 */
			m_dram_tx++;
		}
		pq_fill(CPU_PQ_SIZE, CPU_PQ_READS_PER_FETCH);
		m_dram_tx++;
		m_cycles_penalty = m_cycles_surplus;
	}
	uint8_t b;
	if(USE_PREFETCH_QUEUE) {
		b = m_s.pq[get_pq_cur_index()];
	} else {
		b = g_memory.read_byte(m_s.csip);
		m_dram_tx++;
	}
	m_s.csip++;
	m_s.ip++;
	return b;
}

uint16_t CPUBus::fetchw()
{
	ASSERT(!USE_PREFETCH_QUEUE || m_s.csip<=m_s.pq_tail);
	if(USE_PREFETCH_QUEUE && (m_s.csip >= m_s.pq_tail - 1)) {
		//the pq is empty or not full enough
		if(!m_s.pq_valid && (m_s.csip & 1)) {
			m_dram_tx++;
		}
		pq_fill(get_pq_free_space(), CPU_PQ_READS_PER_FETCH);
		m_dram_tx++;
		m_cycles_penalty = m_cycles_surplus;
	}
	uint8_t b0, b1;
	if(USE_PREFETCH_QUEUE) {
		b0 = m_s.pq[get_pq_cur_index()];
		m_s.csip++;
		b1 = m_s.pq[get_pq_cur_index()];
		m_s.csip++;
	} else {
		b0 = g_memory.read_byte(m_s.csip);
		b1 = g_memory.read_byte(m_s.csip+1);
		m_s.csip += 2;
		if(m_s.csip & 1) {
			m_dram_tx++;
		}
		m_dram_tx++;
	}
	m_s.ip += 2;
	return uint16_t(b1)<<8 | b0;
}

void CPUBus::write_pq_to_logfile(FILE *_dest)
{
	if(m_s.pq_valid) {
		fprintf(_dest, "v");
	} else {
		fprintf(_dest, " ");
	}
	if(is_pq_empty()) {
		fprintf(_dest, "e");
	} else {
		fprintf(_dest, " ");
	}

	fprintf(_dest, " ");
	uint idx = get_pq_cur_index();
	for(uint i=0; i<get_pq_cur_size(); i++) {
		fprintf(_dest, "%02X ", m_s.pq[idx]);
		idx = (idx+1)%CPU_PQ_SIZE;
	}
}
