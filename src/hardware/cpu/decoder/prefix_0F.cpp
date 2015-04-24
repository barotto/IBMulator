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
			m_instr.fn = std::bind(&CPUExecutor::SLDT_ew, _1);
			break;
		case 1:
			CYCLES(2,1);
			m_instr.fn = std::bind(&CPUExecutor::STR_ew, _1);
			break;
		case 2:
			CYCLES(17,17);
			m_instr.fn = std::bind(&CPUExecutor::LLDT_ew, _1);
			break;
		case 3:
			CYCLES(17,17);
			m_instr.fn = std::bind(&CPUExecutor::LTR_ew, _1);
			break;
		case 4:
			CYCLES(14,14);
			m_instr.fn = std::bind(&CPUExecutor::VERR_ew, _1);
			break;
		case 5:
			CYCLES(14,14);
			m_instr.fn = std::bind(&CPUExecutor::VERW_ew, _1);
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
			m_instr.fn = std::bind(&CPUExecutor::SGDT, _1);
			break;
		case 1:
			CYCLES(8,8);
			m_instr.fn = std::bind(&CPUExecutor::SIDT, _1);
			break;
		case 2:
			CYCLES(7,7);
			m_instr.fn = std::bind(&CPUExecutor::LGDT, _1);
			break;
		case 3:
			CYCLES(8,8);
			m_instr.fn = std::bind(&CPUExecutor::LIDT, _1);
			break;
		case 4:
			CYCLES(2,1);
			m_instr.fn = std::bind(&CPUExecutor::SMSW_ew, _1);
			break;
		case 6:
			CYCLES(3,4);
			m_instr.fn = std::bind(&CPUExecutor::LMSW_ew, _1);
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
	m_instr.fn = std::bind(&CPUExecutor::LAR_rw_ew, _1);
	CYCLES(14,14);
	break;
}

/* 0F 03 /r     LSL rw,ew   14,mem=16   Load: rw = Segment Limit, selector ew */
case 0x03:
{
	m_instr.modrm.load();
	m_instr.fn = std::bind(&CPUExecutor::LSL_rw_ew, _1);
	CYCLES(14,14);
	break;
}

/*  0F 05      LOADALL         195        Load CPU registers from memory */
case 0x05:
{
	m_instr.fn = std::bind(&CPUExecutor::LOADALL, _1);
	CYCLES(93,93);
	break;
}

/*  0F 06      CLTS            2          Clear task switched flag */
case 0x06:
{
	m_instr.fn = std::bind(&CPUExecutor::CLTS, _1);
	CYCLES(2,2);
	break;
}


default:
{
	illegal_opcode();
}

} //switch

} //decode_prefix_0F
