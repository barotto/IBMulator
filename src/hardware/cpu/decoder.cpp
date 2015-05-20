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
#include "decoder.h"
#include "core.h"
#include "executor.h"
#include "../memory.h"


CPUDecoder g_cpudecoder;


Instruction * CPUDecoder::decode()
{
	uint8_t opcode;

	m_ilen = 0;
	m_rep = false;
	m_instr.valid = true;
	m_instr.rep = false;
	m_instr.rep_zf = false;
	m_instr.rep_equal = false;
	m_instr.seg = REGI_NONE;
	m_instr.ip = g_cpubus.get_ip();
	m_instr.csip = g_cpubus.get_csip();
	m_instr.cycles = {
		0,  //base
		0,  //memop
		0,  //extra
		0,  //rep
		0,  //base_rep
		0,  //pmode
		0,  //noj
		0,  //tgate
		0,  //samep
		0,  //morep
		0,  //tss
		0   //bu
	};

restart_opcode:

	opcode = fetchb();
	switch(opcode) {
		case 0x26: { // segment ovr: ES
			m_instr.seg = REGI_ES;
			goto restart_opcode;
		}
		case 0x2E: { // segment ovr: CS
			m_instr.seg = REGI_CS;
			goto restart_opcode;
		}
		case 0x36: { // segment ovr: SS
			m_instr.seg = REGI_SS;
			goto restart_opcode;
		}
		case 0x3E: { // segment ovr: DS
			m_instr.seg = REGI_DS;
			goto restart_opcode;
		}
		case 0xF0: { // LOCK
			PDEBUGF(LOG_V2, LOG_CPU, "LOCK\n");
			goto restart_opcode;
		}
		case 0xF1: {
			/* The 0xF1 opcode is a prefix which performs no function. It counts
			 * like any other prefix towards the maximum instruction length.
			 */
			goto restart_opcode;
		}
		case 0xF2: { // REPNE
			m_rep = true;
			m_instr.rep_equal = false;
			m_instr.cycles.rep = 5;
			goto restart_opcode;
		}
		case 0xF3: { // REP/REPE
			m_rep = true;
			m_instr.rep_equal = true;
			m_instr.cycles.rep = 5;
			goto restart_opcode;
		}
		case 0x0F: {
			opcode = fetchb();
			prefix_0F(opcode);
			if(CPULOG) {
				m_instr.opcode = 0xF00 + opcode;
			}
			break;
		}
		default: {
			prefix_none(opcode);
			if(CPULOG) {
				m_instr.opcode = opcode;
			}
			break;
		}
	}

	m_instr.size = m_ilen;

	return &m_instr;
}

void CPUDecoder::illegal_opcode()
{
	/*
	 * illegal opcodes throw an exception only when executed.
	 */
	m_instr.valid = false;
}
