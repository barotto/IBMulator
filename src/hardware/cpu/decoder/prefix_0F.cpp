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
#include "hardware/cpu/decoder.h"
#include "hardware/cpu/executor.h"



void CPUDecoder::prefix_0F(uint8_t _opcode, unsigned &ctb_idx_, unsigned &ctb_op_)
{

	ctb_op_ = _opcode;
	ctb_idx_ = CTB_IDX_0F;

switch(_opcode) {

/*
0F 00 /0   SLDT ew      Store Local Descriptor Table register to EA word
0F 00 /1   STR ew       Store Task Register to EA word
0F 00 /2   LLDT ew      Load selector ew into Local  Descriptor Table register
0F 00 /3   LTR ew       Load EA word into Task Register
0F 00 /4   VERR ew      Set ZF=1 if seg. can be read, selector ew
0F 00 /5   VERW ew      Set ZF=1 if seg. can be written, selector ew
*/
case 0x00:
{
	m_instr.modrm.load();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::SLDT_ew;
			break;
		case 1:
			m_instr.fn = &CPUExecutor::STR_ew;
			break;
		case 2:
			m_instr.fn = &CPUExecutor::LLDT_ew;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::LTR_ew;
			break;
		case 4:
			m_instr.fn = &CPUExecutor::VERR_ew;
			break;
		case 5:
			m_instr.fn = &CPUExecutor::VERW_ew;
			break;
		default:
			illegal_opcode();
	}
	ctb_op_ = m_instr.modrm.n;
	ctb_idx_ = CTB_IDX_0F00;
	break;
}

/*
0F 01 /0   SGDT m       Store Global Descriptor Table register to m
0F 01 /1   SIDT m       Store Interrupt Descriptor Table register to m
0F 01 /2   LGDT m       Load m into Global Descriptor Table reg
0F 01 /3   LIDT m       Load m into Interrupt Descriptor Table reg
0F 01 /4   SMSW ew      Store Machine Status Word to EA word
0F 01 /6   LMSW ew      Load EA word into Machine Status Word
*/
case 0x01:
{
	m_instr.modrm.load();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::SGDT;
			break;
		case 1:
			m_instr.fn = &CPUExecutor::SIDT;
			break;
		case 2:
			m_instr.fn = &CPUExecutor::LGDT_16;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::LIDT_16;
			break;
		case 4:
			m_instr.fn = &CPUExecutor::SMSW_ew;
			break;
		case 6:
			m_instr.fn = &CPUExecutor::LMSW_ew;
			break;
		default:
			illegal_opcode();
	}
	ctb_op_ = m_instr.modrm.n;
	ctb_idx_ = CTB_IDX_0F01;
	break;
}

/* 0F 02 /r   LAR rw,ew    Load: high(rw)= Access Rights byte, selector ew */
case 0x02:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::LAR_rw_ew;
	break;
}

/* 0F 03 /r     LSL rw,ew   1Load: rw = Segment Limit, selector ew */
case 0x03:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::LSL_rw_ew;
	break;
}

/*  0F 05      LOADALL        Load CPU registers from memory */
case 0x05:
{
	m_instr.fn = &CPUExecutor::LOADALL_286;
	break;
}

/*  0F 06      CLTS            Clear task switched flag */
case 0x06:
{
	m_instr.fn = &CPUExecutor::CLTS;
	break;
}


default:
{
	illegal_opcode();
}

} //switch

} //decode_prefix_0F
