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

using std::placeholders::_1;


void CPUDecoder::prefix_0F(uint8_t _opcode)
{

switch(_opcode) {

/*
0F 00 /0   SLDT ew     2,mem=3    Store Local Descriptor Table register to EA word
0F 00 /1   STR ew      2,mem=3    Store Task Register to EA word
0F 00 /2   LLDT ew     17,mem=19  Load selector ew into Local  Descriptor Table register
0F 00 /3   LTR ew      17,mem=19  Load EA word into Task Register
0F 00 /4   VERR ew     14,mem=16  Set ZF=1 if seg. can be read, selector ew
0F 00 /5   VERW ew     14,mem=16  Set ZF=1 if seg. can be written, selector ew
*/
case 0x00:
{
	m_instr.modrm.load();
	switch(m_instr.modrm.n) {
		case 0:
			CYCLES(2,1);
			m_instr.fn = &CPUExecutor::SLDT_ew;
			break;
		case 1:
			CYCLES(2,1);
			m_instr.fn = &CPUExecutor::STR_ew;
			break;
		case 2:
			CYCLES(17,17);
			m_instr.fn = &CPUExecutor::LLDT_ew;
			break;
		case 3:
			CYCLES(17,17);
			m_instr.fn = &CPUExecutor::LTR_ew;
			break;
		case 4:
			CYCLES(14,14);
			m_instr.fn = &CPUExecutor::VERR_ew;
			break;
		case 5:
			CYCLES(14,14);
			m_instr.fn = &CPUExecutor::VERW_ew;
			break;
		default:
			illegal_opcode();
	}
	break;
}

/*
0F 01 /0   SGDT m       11        Store Global Descriptor Table register to m
0F 01 /1   SIDT m       12        Store Interrupt Descriptor Table register to m
0F 01 /2   LGDT m       11        Load m into Global Descriptor Table reg
0F 01 /3   LIDT m       12        Load m into Interrupt Descriptor Table reg
0F 01 /4   SMSW ew      2,mem=3   Store Machine Status Word to EA word
0F 01 /6   LMSW ew      3,mem=6   Load EA word into Machine Status Word
*/
case 0x01:
{
	m_instr.modrm.load();
	switch(m_instr.modrm.n) {
		case 0:
			CYCLES(7,7);
			m_instr.fn = &CPUExecutor::SGDT;
			break;
		case 1:
			CYCLES(8,8);
			m_instr.fn = &CPUExecutor::SIDT;
			break;
		case 2:
			CYCLES(7,7);
			m_instr.fn = &CPUExecutor::LGDT;
			break;
		case 3:
			CYCLES(8,8);
			m_instr.fn = &CPUExecutor::LIDT;
			break;
		case 4:
			CYCLES(2,1);
			m_instr.fn = &CPUExecutor::SMSW_ew;
			break;
		case 6:
			CYCLES(3,4);
			m_instr.fn = &CPUExecutor::LMSW_ew;
			break;
		default:
			illegal_opcode();
	}
	break;
}

/* 0F 02 /r   LAR rw,ew    14,mem=16    Load: high(rw)= Access Rights byte, selector ew */
case 0x02:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::LAR_rw_ew;
	CYCLES(14,14);
	break;
}

/* 0F 03 /r     LSL rw,ew   14,mem=16   Load: rw = Segment Limit, selector ew */
case 0x03:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::LSL_rw_ew;
	CYCLES(14,14);
	break;
}

/*  0F 05      LOADALL         195        Load CPU registers from memory */
case 0x05:
{
	m_instr.fn = &CPUExecutor::LOADALL;
	CYCLES(93,93);
	break;
}

/*  0F 06      CLTS            2          Clear task switched flag */
case 0x06:
{
	m_instr.fn = &CPUExecutor::CLTS;
	CYCLES(2,2);
	break;
}


default:
{
	illegal_opcode();
}

} //switch

} //decode_prefix_0F
