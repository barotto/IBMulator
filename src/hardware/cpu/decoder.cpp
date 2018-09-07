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
#include "decoder.h"
#include "core.h"
#include "executor.h"
#include "logger.h"
#include "../memory.h"
#include "hardware/cpu.h"

CPUDecoder g_cpudecoder;


Instruction * CPUDecoder::decode()
{
	uint8_t opcode;
	unsigned cycles_table = CTB_IDX_NONE;
	unsigned cycles_op = 0;
	bool lock = false;

	m_ilen = 0;
	m_instr.valid = true;
	m_instr.op32 = REG_CS.desc.big;
	m_instr.addr32 = REG_CS.desc.big;
	m_instr.rep = false;
	m_instr.rep_zf = false;
	m_instr.rep_equal = false;
	m_instr.seg = REGI_NONE;
	m_instr.eip = g_cpubus.eip();
	m_instr.cseip = g_cpubus.cseip();
	m_instr.cycles = {0,0,0,0,0,0,0,0};

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
		case 0x64: { // segment ovr: FS
			if(CPU_FAMILY >= CPU_386) {
				m_instr.seg = REGI_FS;
				goto restart_opcode;
			} else {
				illegal_opcode();
				break;
			}
		}
		case 0x65: { // segment ovr: GS
			if(CPU_FAMILY >= CPU_386) {
				m_instr.seg = REGI_GS;
				goto restart_opcode;
			} else {
				illegal_opcode();
				break;
			}
		}
		case 0x66: { // operand-size
			if(CPU_FAMILY >= CPU_386) {
				m_instr.op32 = !REG_CS.desc.big;
				goto restart_opcode;
			} else {
				illegal_opcode();
				break;
			}
		}
		case 0x67: { // address-size
			if(CPU_FAMILY >= CPU_386) {
				m_instr.addr32 = !REG_CS.desc.big;
				goto restart_opcode;
			} else {
				illegal_opcode();
				break;
			}
		}
		case 0xF0: { // LOCK
			lock = true;
			goto restart_opcode;
		}
		case 0xF1: {
			if(CPU_FAMILY >= CPU_386) {
				// INT1 - undocumented ICEBP
				prefix_none(opcode, cycles_table, cycles_op);
				m_instr.opcode = opcode;
				break;
			} else {
				/* The 0xF1 opcode is a prefix which performs no function. It
				 * counts like any other prefix towards the maximum instruction
				 * length. Does not generate #UD.
				 */
				goto restart_opcode;
			}
		}
		case 0xF2: { // REPNE
			m_instr.rep = true;
			m_instr.rep_first = true;
			m_instr.rep_equal = false;
			goto restart_opcode;
		}
		case 0xF3: { // REP/REPE
			m_instr.rep = true;
			m_instr.rep_first = true;
			m_instr.rep_equal = true;
			goto restart_opcode;
		}
		case 0x0F: {
			opcode = fetchb();
			if(m_instr.op32) {
				prefix_0F_32(opcode,cycles_table,cycles_op);
			} else {
				prefix_0F(opcode,cycles_table,cycles_op);
			}
			m_instr.opcode = 0xF00 + opcode;
			break;
		}
		default: {
			if(m_instr.op32) {
				prefix_none_32(opcode,cycles_table,cycles_op);
			} else {
				prefix_none(opcode,cycles_table,cycles_op);
			}
			m_instr.opcode = opcode;
			break;
		}
	}
	if(UNLIKELY(lock)) {
		if(!is_lockable() || m_instr.modrm.mod_is_reg()) {
			illegal_opcode();
		}
	}
	m_instr.cycles = ms_cycles[cycles_table][cycles_op*CPU_COUNT + (CPU_FAMILY-CPU_286)];
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

bool CPUDecoder::is_lockable()
{
	switch(m_instr.opcode) {
		case 0x80:
		case 0x81:
		case 0x82:
		case 0x83:
			if(m_instr.modrm.n == 7) {
				// CMP
				return false;
			}
			// ADD, OR, ADC, SBB, AND, SUB, XOR
			return true;
		case 0x86:
		case 0x87:
			// XCHG
			return true;
		case 0xF6:
		case 0xF7:
			if(m_instr.modrm.n == 2 || m_instr.modrm.n == 3) {
				// NOT, NEG
				return true;
			}
			return false;
		case 0xFE:
		case 0xFF:
			if(m_instr.modrm.n <= 1) {
				// INC, DEC
				return true;
			}
			return false;
		case 0x0FAB: // BTS
		case 0x0FB3: // BTR
		case 0x0FBB: // BTC
			return true;
		case 0x0FBA:
			if(m_instr.modrm.n >= 5) {
				// BTC, BTR, BTS
				return true;
			}
			return false;
		default:
			break;
	}
	return false;
}
