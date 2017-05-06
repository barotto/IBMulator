/*
 * Copyright (C) 2016  Marco Bortolin
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

#define PREFIX_NONE return prefix_none(_opcode, ctb_idx_, ctb_op_)

void CPUDecoder::prefix_none_32(uint8_t _opcode, unsigned &ctb_idx_, unsigned &ctb_op_)
{
	ctb_op_ = _opcode;
	ctb_idx_ = CTB_IDX_NONE;

switch(_opcode) {

/* 00 /r     ADD eb,rb    Add byte register into EA byte */
case 0x00: PREFIX_NONE;

/* 01 /r     ADD ed,rd    Add dword register to EA dword */
case 0x01:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::ADD_ed_rd;
	break;
}

/* 02 /r      ADD rb,eb   Add EA byte into byte register */
case 0x02: PREFIX_NONE;

/* 03 /r      ADD rd,ed   Add EA dword into dword register */
case 0x03:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::ADD_rd_ed;
	break;
}

/* 04 ib      ADD AL,ib    Add immediate byte into AL */
case 0x04: PREFIX_NONE;

/* 05 id      ADD EAX,id   Add immediate dword into EAX */
case 0x05:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::ADD_EAX_id;
	break;
}

/* 06         PUSH ES      Push ES */
case 0x06:
{
	m_instr.reg = REGI_ES;
	m_instr.fn = &CPUExecutor::PUSH_SR_dw;
	break;
}

/* 07         POP ES       Pop top of stack into ES */
case 0x07:
{
	m_instr.reg = REGI_ES;
	m_instr.fn = &CPUExecutor::POP_SR_dw;
	break;
}

/* 08 /r      OR eb,rb      Logical-OR byte register into EA byte */
case 0x08: PREFIX_NONE;

/* 09 /r      OR ed,rd      Logical-OR dword register into EA dword */
case 0x09:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::OR_ed_rd;
	break;
}

/* 0A /r      OR rb,eb       Logical-OR EA byte into byte register */
case 0x0A: PREFIX_NONE;

/* 0B /r      OR rd,ed       Logical-OR EA dword into dword register */
case 0x0B:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::OR_rd_ed;
	break;
}

/* 0C ib      OR AL,ib       Logical-OR immediate byte into AL */
case 0x0C: PREFIX_NONE;

/* 0D id      OR EAX,id      Logical-OR immediate dword into EAX */
case 0x0D:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::OR_EAX_id;
	break;
}

/* 0E         PUSH CS        Push CS */
case 0x0E:
{
	m_instr.reg = REGI_CS;
	m_instr.fn = &CPUExecutor::PUSH_SR_dw;
	break;
}

/* 0F 2-byte opcode prefix */

/* 10 /r      ADC eb,rb      Add with carry byte register into EA byte */
case 0x10: PREFIX_NONE;

/* 11 /r      ADC ed,rd      Add with carry dword register into EA dword */
case 0x11:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::ADC_ed_rd;
	break;
}

/* 12 /r      ADC rb,eb      Add with carry EA byte into byte register */
case 0x12: PREFIX_NONE;

/* 13 /r      ADC rd,ed      Add with carry EA dword into dword register */
case 0x13:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::ADC_rd_ed;
	break;
}

/* 14 ib      ADC AL,ib      Add with carry immediate byte into AL */
case 0x14: PREFIX_NONE;

/* 15 id      ADC EAX,id     Add with carry immediate dword into EAX */
case 0x15:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::ADC_EAX_id;
	break;
}

/* 16         PUSH SS        Push SS */
case 0x16:
{
	m_instr.reg = REGI_SS;
	m_instr.fn = &CPUExecutor::PUSH_SR_dw;
	break;
}

/* 17         POP SS         Pop top of stack into SS */
case 0x17:
{
	m_instr.reg = REGI_SS;
	m_instr.fn = &CPUExecutor::POP_SR_dw;
	break;
}

/* 18 /r      SBB eb,rb      Subtract with borrow byte register from EA byte */
case 0x18: PREFIX_NONE;

/* 19 /r      SBB ed,rd      Subtract with borrow dword register from EA dword */
case 0x19:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::SBB_ed_rd;
	break;
}

/* 1A /r       SBB rb,eb    Subtract with borrow EA byte from byte register */
case 0x1A: PREFIX_NONE;

/* 1B /r       SBB rd,ed    Subtract with borrow EA dword from dword register */
case 0x1B:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::SBB_rd_ed;
	break;
}

/* 1C ib       SBB AL,ib    Subtract with borrow imm. byte from AL */
case 0x1C: PREFIX_NONE;

/* 1D id       SBB EAX,id    Subtract with borrow imm. dword from EAX */
case 0x1D:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::SBB_EAX_id;
	break;
}

/* 1E         PUSH DS      Push DS */
case 0x1E:
{
	m_instr.reg = REGI_DS;
	m_instr.fn = &CPUExecutor::PUSH_SR_dw;
	break;
}

/* 1F          POP DS       Pop top of stack into DS */
case 0x1F:
{
	m_instr.reg = REGI_DS;
	m_instr.fn = &CPUExecutor::POP_SR_dw;
	break;
}

/* 20 /r      AND eb,rb     Logical-AND byte register into EA byte */
case 0x20: PREFIX_NONE;

/* 21 /r      AND ed,rd     Logical-AND dword register into EA dword */
case 0x21:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::AND_ed_rd;
	break;
}

/* 22 /r      AND rb,eb     Logical-AND EA byte into byte register */
case 0x22: PREFIX_NONE;

/* 23 /r      AND rd,ed     Logical-AND EA dword into dword register */
case 0x23:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::AND_rd_ed;
	break;
}

/* 24 ib      AND AL,ib     Logical-AND immediate byte into AL */
case 0x24: PREFIX_NONE;

/* 25 id      AND EAX,id     Logical-AND immediate dword into EAX */
case 0x25:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::AND_EAX_id;
	break;
}

/* 26  seg ovr prefix (ES) */

/* 27      DAA            Decimal adjust AL after addition */
case 0x27: PREFIX_NONE;

/* 28 /r      SUB eb,rb     Subtract byte register from EA byte */
case 0x28: PREFIX_NONE;

/* 29 /r      SUB ed,rd     Subtract dword register from EA dword */
case 0x29:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::SUB_ed_rd;
	break;
}

/* 2A /r      SUB rb,eb      Subtract EA byte from byte register */
case 0x2A: PREFIX_NONE;

/* 2B /r      SUB rd,ed      Subtract EA dword from dword register */
case 0x2B:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::SUB_rd_ed;
	break;
}

/* 2C ib      SUB AL,ib      Subtract immediate byte from AL */
case 0x2C: PREFIX_NONE;

/* 2D id      SUB EAX,id      Subtract immediate dword from EAX */
case 0x2D:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::SUB_EAX_id;
	break;
}

/* 2E  seg ovr prefix (CS) */

/* 2F        DAS             Decimal adjust AL after subtraction */
case 0x2F: PREFIX_NONE;

/* 30 /r     XOR eb,rb   Exclusive-OR byte register into EA byte */
case 0x30: PREFIX_NONE;

/* 31 /r     XOR ed,rd   Exclusive-OR dword register into EA dword */
case 0x31:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::XOR_ed_rd;
	break;
}

/* 32 /r     XOR rb,eb   Exclusive-OR EA byte into byte register */
case 0x32: PREFIX_NONE;

/* 33 /r     XOR rd,ed   Exclusive-OR EA dword into dword register */
case 0x33:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::XOR_rd_ed;
	break;
}

/* 34 ib     XOR AL,ib   Exclusive-OR immediate byte into AL */
case 0x34: PREFIX_NONE;

/* 35 id     XOR EAX,id   Exclusive-OR immediate dword into AX */
case 0x35:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::XOR_EAX_id;
	break;
}

/* 36  seg ovr prefix (SS) */

/* 37        AAA         ASCII adjust AL after addition */
case 0x37: PREFIX_NONE;

/* 38 /r        CMP eb,rb      Compare byte register from EA byte */
case 0x38: PREFIX_NONE;

/* 39 /r        CMP ed,rd     Compare dword register from EA dword */
case 0x39:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::CMP_ed_rd;
	break;
}

/* 3A /r        CMP rb,eb     Compare EA byte from byte register*/
case 0x3A: PREFIX_NONE;

/* 3B /r        CMP rd,ed     Compare EA dword from dword register */
case 0x3B:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::CMP_rd_ed;
	break;
}

/* 3C ib        CMP AL,ib      Compare immediate byte from AL */
case 0x3C: PREFIX_NONE;

/* 3D id        CMP EAX,id       Compare immediate dword from EAX */
case 0x3D:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::CMP_EAX_id;
	break;
}

/* 3E  seg ovr prefix (DS) */

/* 3F        AAS       ASCII adjust AL after subtraction */
case 0x3F: PREFIX_NONE;

/* 40+ rd     INC rd    Increment dword register by 1 */
case 0x40: //EAX
case 0x41: //ECX
case 0x42: //EDX
case 0x43: //EBX
case 0x44: //ESP
case 0x45: //EBP
case 0x46: //ESI
case 0x47: //EDI
{
	m_instr.reg = _opcode - 0x40;
	m_instr.fn = &CPUExecutor::INC_rd_op;
	break;
}

/* 48+ rd     DEC rd      Decrement dword register by 1 */
case 0x48: //EAX
case 0x49: //ECX
case 0x4A: //EDX
case 0x4B: //EBX
case 0x4C: //ESP
case 0x4D: //EBP
case 0x4E: //ESI
case 0x4F: //EDI
{
	m_instr.reg = _opcode - 0x48;
	m_instr.fn = &CPUExecutor::DEC_rd_op;
	break;
}

/*  50+ rd     PUSH rd     Push dword register */
case 0x50: //EAX
case 0x51: //ECX
case 0x52: //EDX
case 0x53: //EBX
case 0x54: //ESP
case 0x55: //EBP
case 0x56: //ESI
case 0x57: //EDI
{
	m_instr.reg = _opcode - 0x50;
	m_instr.fn = &CPUExecutor::PUSH_rd_op;
	break;
}

/* 58+ rd     POP rd        Pop top of stack into dword register */
case 0x58: //EAX
case 0x59: //ECX
case 0x5A: //EDX
case 0x5B: //EBX
case 0x5C: //ESP
case 0x5D: //EBP
case 0x5E: //ESI
case 0x5F: //EDI
{
	m_instr.reg = _opcode - 0x58;
	m_instr.fn = &CPUExecutor::POP_rd_op;
	break;
}

/* 60        PUSHAD      Push EAX, ECX, EDX, EBX, original ESP, EBP, ESI, and EDI */
case 0x60:
{
	m_instr.fn = &CPUExecutor::PUSHAD;
	break;
}

/* 61        POPAD        Pop EDI, ESI, EBP, ESP, EDX, ECX, and EAX */
case 0x61:
{
	m_instr.fn = &CPUExecutor::POPAD;
	break;
}

/* 62 /r      BOUND rd,mq     INT 5 if rd not within bounds */
case 0x62:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::BOUND_rd_mq ;
	break;
}

/* 63 /r    ARPL ew,rw   Adjust RPL of EA word not less than RPL of rw */
case 0x63: PREFIX_NONE;

/*
64  seg ovr prefix (FS)
65  seg ovr prefix (GS)
66  operand-size prefix (OS)
67  address-size prefix (AS)
*/

/* 68  id     PUSH id      Push immediate dword */
case 0x68:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::PUSH_id;
	break;
}

/* 69 /r id   IMUL rd,ed,id     Signed multiply (rd = EA dword * imm. dword) */
case 0x69:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::IMUL_rd_ed_id;
	break;
}

/* 6A  ib     PUSH ib      Push immediate sign-extended byte*/
case 0x6A:
{
	m_instr.ib = fetchb();
	m_instr.fn = &CPUExecutor::PUSH_ib_dw;
	break;
}

/* 6B /r ib   IMUL rd,ed,ib    Signed multiply (rd = EA dword * imm. byte) */
case 0x6B:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.ib = fetchb();
	m_instr.fn = &CPUExecutor::IMUL_rd_ed_ib;
	break;
}

/* 6C      INSB           Input byte from port DX into ES:[(E)DI] */
case 0x6C: PREFIX_NONE;

/* 6D      INSD           Input dword from port DX into ES:[(E)DI] */
case 0x6D:
{
	if(m_instr.addr32) {
		m_instr.fn = &CPUExecutor::INSD_a32;
	} else {
		m_instr.fn = &CPUExecutor::INSD_a16;
	}
	break;
}

/* 6E      OUTSB          Output byte DS:[(E)SI] to port number DX */
case 0x6E: PREFIX_NONE;

/* 6F      OUTSD          Output dword DS:[(E)SI] to port number DX */
case 0x6F:
{
	if(m_instr.addr32) {
		m_instr.fn = &CPUExecutor::OUTSD_a32;
	} else {
		m_instr.fn = &CPUExecutor::OUTSD_a16;
	}
	break;
}

case 0x70: /* 70 cb   JO cb    Jump short if overflow (OF=1)                    */
case 0x71: /* 71 cb   JNO cb   Jump short if notoverflow (OF=0)                 */
case 0x72: /* 72 cb   JC cb    Jump short if carry (CF=1)                       */
case 0x73: /* 73 cb   JNC cb   Jump short if not carry (CF=0)                   */
case 0x74: /* 74 cb   JE cb    Jump short if equal (ZF=1)                       */
case 0x75: /* 75 cb   JNE cb   Jump short if not equal (ZF=0)                   */
case 0x76: /* 76 cb   JBE cb   Jump short if below or equal (CF=1 or ZF=1)      */
case 0x77: /* 77 cb   JA cb    Jump short if above (CF=0 and ZF=0)              */
case 0x78: /* 78 cb   JS cb    Jump short if sign (SF=1)                        */
case 0x79: /* 79 cb   JNS cb   Jump short if not sign (SF=0)                    */
case 0x7A: /* 7A cb   JPE cb   Jump short if parity even (PF=1)                 */
case 0x7B: /* 7B cb   JPO cb   Jump short if parity odd (PF=0)                  */
case 0x7C: /* 7C cb   JL cb    Jump short if less (SF<>OF)                      */
case 0x7D: /* 7D cb   JNL cb   Jump short if not less (SF=OF)                   */
case 0x7E: /* 7E cb   JLE cb   Jump short if less or equal (ZF=1 or SF<>OF)     */
case 0x7F: /* 7F cb   JNLE cb  Jump short if not less or equal (ZF=0 and SF=OF) */
	PREFIX_NONE;

/*
80 /0 ib   ADD eb,ib    Add immediate byte into EA byte
80 /1 ib   OR  eb,ib    Logical-OR immediate byte  into EA byte
80 /2 ib   ADC eb,ib    Add with carry immediate byte into EA byte
80 /3 ib   SBB eb,ib    Subtract with borrow imm. byte from EA byte
80 /4 ib   AND eb,ib    Logical-AND immediate byte into EA byte
80 /5 ib   SUB eb,ib    Subtract immediate byte from EA byte
80 /6 ib   XOR eb,ib    Exclusive-OR immediate byte into EA byte
80 /7 ib   CMP eb,ib    Compare immediate byte from EA byte
*/
case 0x80:
case 0x82: PREFIX_NONE;

/*
81 /0 id   ADD ed,id    Add immediate dword into EA dword
81 /1 id   OR  ed,id    Logical-OR immediate dword into EA dword
81 /2 id   ADC ed,id    Add with carry immediate dword into EA dword
81 /3 id   SBB ed,id    Subtract with borrow imm. dword from EA dword
81 /4 id   AND ed,id    Logical-AND immediate dword into EA wdord
81 /5 id   SUB ed,id    Subtract immediate dword from EA dword
81 /6 id   XOR ed,id    Exclusive-OR immediate dword into EA dword
81 /7 id   CMP ed,id    Compare immediate dword from EA dword
*/
case 0x81:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.id1 = fetchdw();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::ADD_ed_id;
			break;
		case 1:
			m_instr.fn = &CPUExecutor::OR_ed_id;
			break;
		case 2:
			m_instr.fn = &CPUExecutor::ADC_ed_id;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::SBB_ed_id;
			break;
		case 4:
			m_instr.fn = &CPUExecutor::AND_ed_id;
			break;
		case 5:
			m_instr.fn = &CPUExecutor::SUB_ed_id;
			break;
		case 6:
			m_instr.fn = &CPUExecutor::XOR_ed_id;
			break;
		case 7:
			m_instr.fn = &CPUExecutor::CMP_ed_id;
			break;
		default:
			illegal_opcode();
	}
	ctb_op_ = m_instr.modrm.n;
	ctb_idx_ = CTB_IDX_81;
	break;
}

/* 82  alias of 80 */

/*
83 /0 ib   ADD ed,ib    Add immediate byte into EA dword
83 /1 ib   OR  ed,ib    Logical-OR immediate byte into EA dword
83 /2 ib   ADC ed,ib    Add with carry immediate byte into EA dword
83 /3 ib   SBB ed,ib    Subtract with borrow imm. byte from EA dword
83 /4 ib   AND ed,ib    Logical-AND immediate byte into EA dword
83 /5 ib   SUB ed,ib    Subtract immediate byte from EA dword
83 /6 ib   XOR ed,ib    Exclusive-OR immediate byte into EA dword
83 /7 ib   CMP ed,ib    Compare immediate byte from EA dword
*/
case 0x83:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.ib = fetchb();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::ADD_ed_ib;
			break;
		case 1:
			m_instr.fn = &CPUExecutor::OR_ed_ib;
			break;
		case 2:
			m_instr.fn = &CPUExecutor::ADC_ed_ib;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::SBB_ed_ib;
			break;
		case 4:
			m_instr.fn = &CPUExecutor::AND_ed_ib;
			break;
		case 5:
			m_instr.fn = &CPUExecutor::SUB_ed_ib;
			break;
		case 6:
			m_instr.fn = &CPUExecutor::XOR_ed_ib;
			break;
		case 7:
			m_instr.fn = &CPUExecutor::CMP_ed_ib;
			break;
		default:
			illegal_opcode();
	}
	ctb_op_ = m_instr.modrm.n;
	ctb_idx_ = CTB_IDX_83;
	break;
}

/* 84 /r      TEST eb,rb    AND byte register into EA byte for flags only */
case 0x84: PREFIX_NONE;

/* 85 /r      TEST ed,rd    AND dword register into EA dword for flags only */
case 0x85:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::TEST_ed_rd;
	break;
}

/* 86 /r     XCHG eb,rb     Exchange byte register with EA byte */
case 0x86: PREFIX_NONE;

/* 87 /r     XCHG ed,rd     Exchange dword register with EA dword */
case 0x87:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::XCHG_ed_rd;
	break;
}

/* 88 /r      MOV eb,rb   Move byte register into EA byte */
case 0x88: PREFIX_NONE;

/* 89 /r      MOV ed,rd   Move dword register into EA dword */
case 0x89:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::MOV_ed_rd;
	break;
}

/* 8A /r      MOV rb,eb   Move EA byte into byte register */
case 0x8A: PREFIX_NONE;

/* 8B /r      MOV rd,ed   Move EA dword into dword register */
case 0x8B:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::MOV_rd_ed;
	break;
}

/* 8C /r      MOV ew,SR      Move Segment Register into EA word */
case 0x8C: PREFIX_NONE;

/* 8D /r    LEA rd,m       Calculate EA offset given by m, place in rd */
case 0x8D:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::LEA_rd_m;
	break;
}

/* 8E /r      MOV SR,mw    Move memory word into Segment Register */
case 0x8E: PREFIX_NONE;

/* 8F /0      POP md       Pop top of stack into memory dword */
case 0x8F:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::POP_md;
	if(m_instr.modrm.n != 0) {
		illegal_opcode();
	}
	break;
}

/* 90            NOP            No OPERATION */
case 0x90: PREFIX_NONE;

/* 90+ rd    XCHG EAX,rd     Exchange dword register with EAX */
case 0x91: //ECX
case 0x92: //EDX
case 0x93: //EBX
case 0x94: //ESP
case 0x95: //EBP
case 0x96: //ESI
case 0x97: //EDI
{
	m_instr.reg = _opcode - 0x90;
	m_instr.fn = &CPUExecutor::XCHG_EAX_rd;
	break;
}

/* 98       CWDE           Convert word into dword */
case 0x98:
{
	m_instr.fn = &CPUExecutor::CWDE;
	break;
}

/* 99       CDQ            Convert dword to qword */
case 0x99:
{
	m_instr.fn = &CPUExecutor::CDQ;
	break;
}

/* 9A cp     CALL cp       Call inter-segment, immediate 6-byte address */
case 0x9A:
{
	m_instr.id1 = fetchdw();
	m_instr.iw2 = fetchw();
	m_instr.fn = &CPUExecutor::CALL_ptr1632;
	break;
}

/* 9B        WAIT           Wait until BUSY pin is inactive (HIGH) */
case 0x9B: PREFIX_NONE;

/* 9C         PUSHFD         Push eflags register */
case 0x9C:
{
	m_instr.fn = &CPUExecutor::PUSHFD;
	break;
}

/* 9D         POPFD           Pop top of stack into eflags register */
case 0x9D:
{
	m_instr.fn = &CPUExecutor::POPFD;
	break;
}

case 0x9E: /* 9E   SAHF     Store AH into flags */
case 0x9F: /* 9F   LAHF     Load flags into AH */
	PREFIX_NONE;

/* A0 iw/id      MOV AL,xb    Move byte variable at seg:offset into AL */
case 0xA0: PREFIX_NONE;

/* A1 iw/id      MOV EAX,xw   Move dword variable at seg:offset into EAX */
case 0xA1:
{
	if(m_instr.addr32) {
		m_instr.offset = fetchdw();
	} else {
		m_instr.offset = fetchw();
	}
	m_instr.fn = &CPUExecutor::MOV_EAX_xd;
	break;
}

/* A2 iw/id      MOV xb,AL       Move AL into byte variable at seg:offset */
case 0xA2: PREFIX_NONE;

/* A3 iw/id      MOV xd,EAX      Move EAX into dword register at seg:offset */
case 0xA3:
{
	if(m_instr.addr32) {
		m_instr.offset = fetchdw();
	} else {
		m_instr.offset = fetchw();
	}
	m_instr.fn = &CPUExecutor::MOV_xd_EAX;
	break;
}

/* A4        MOVSB         Move byte DS:[(E)SI] to ES:[(E)DI] */
case 0xA4: PREFIX_NONE;

/* A5        MOVSD         Move dword DS:[(E)SI] to ES:[(E)DI] */
case 0xA5:
{
	if(m_instr.addr32) {
		m_instr.fn = &CPUExecutor::MOVSD_a32;
	} else {
		m_instr.fn = &CPUExecutor::MOVSD_a16;
	}
	break;
}

/* A6        CMPSB         Compare bytes ES:[(E)DI] from DS:[(E)SI] */
case 0xA6: PREFIX_NONE;

/* A7        CMPSD         Compare dwords ES:[(E)DI] from DS:[(E)SI] */
case 0xA7:
{
	m_instr.rep_zf = true;
	if(m_instr.addr32) {
		m_instr.fn = &CPUExecutor::CMPSD_a32;
	} else {
		m_instr.fn = &CPUExecutor::CMPSD_a16;
	}
	break;
}

/* A8 ib      TEST AL,ib    AND immediate byte into AL for flags only */
case 0xA8: PREFIX_NONE;

/* A9 id      TEST EAX,id    AND immediate dword into EAX for flags only */
case 0xA9:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::TEST_EAX_id;
	break;
}

/* AA       STOSB           Store AL to byte ES:[(E)DI], advance (E)DI */
case 0xAA: PREFIX_NONE;

/* AB       STOSD           Store EAX to dword ES:[(E)DI], advance (E)DI */
case 0xAB:
{
	if(m_instr.addr32) {
		m_instr.fn = &CPUExecutor::STOSD_a32;
	} else {
		m_instr.fn = &CPUExecutor::STOSD_a16;
	}
	break;
}

/* AC         LODSB            Load byte DS:[(E)SI] into AL */
case 0xAC: PREFIX_NONE;

/* AD         LODSD            Load dword DS:[(E)SI] into EAX */
case 0xAD:
{
	if(m_instr.addr32) {
		m_instr.fn = &CPUExecutor::LODSD_a32;
	} else {
		m_instr.fn = &CPUExecutor::LODSD_a16;
	}
	break;
}

/*  AE       SCASB         Compare bytes AL - ES:[(E)DI], advance (E)DI */
case 0xAE: PREFIX_NONE;

/*  AF       SCASD         Compare dwords EAX - ES:[(E)DI], advance (E)DI */
case 0xAF:
{
	m_instr.rep_zf = true;
	if(m_instr.addr32) {
		m_instr.fn = &CPUExecutor::SCASD_a32;
	} else {
		m_instr.fn = &CPUExecutor::SCASD_a16;
	}
	break;
}

/* B0+ rb ib - MOV rb,ib    Move imm byte into byte reg */
case 0xB0: //AL
case 0xB1: //CL
case 0xB2: //DL
case 0xB3: //BL
case 0xB4: //AH
case 0xB5: //CH
case 0xB6: //DH
case 0xB7: //BH
	 PREFIX_NONE;

/* B8+ rd id - MOV rd,id   Move imm dword into dword reg */
case 0xB8: //EAX
case 0xB9: //ECX
case 0xBA: //EDX
case 0xBB: //EBX
case 0xBC: //ESP
case 0xBD: //EBP
case 0xBE: //ESI
case 0xBF: //EDI
{
	m_instr.id1 = fetchdw();
	m_instr.reg = _opcode - 0xB8;
	m_instr.fn = &CPUExecutor::MOV_rd_id;
	break;
}

/*
C0 /0 ib  ROL eb,ib    Rotate 8-bit EA byte left ib times
C0 /1 ib  ROR eb,ib    Rotate 8-bit EA byte right ib times
C0 /2 ib  RCL eb,ib    Rotate 9-bits (CF, EA byte) left ib times
C0 /3 ib  RCR eb,ib    Rotate 9-bits (CF, EA byte) right ib times
C0 /4 ib  SAL eb,ib    Multiply EA byte by 2, ib times
C0 /5 ib  SHR eb,ib    Unsigned divide EA byte by 2, ib times
C0 /6 ib  SHL eb,ib    Multiply EA byte by 2, ib times
C0 /7 ib  SAR eb,ib    Signed divide EA byte by 2, ib times
*/
case 0xC0: PREFIX_NONE;

/*
C1 /0 ib  ROL ed,ib    Rotate 16-bit EA dword left ib times
C1 /1 ib  ROR ed,ib    Rotate 16-bit EA dword right ib times
C1 /2 ib  RCL ed,ib    Rotate 17-bits (CF, EA dword) left ib times
C1 /3 ib  RCR ed,ib    Rotate 17-bits (CF, EA dword) right ib times
C1 /4 ib  SAL ed,ib    Multiply EA dword by 2, ib times
C1 /5 ib  SHR ed,ib    Unsigned divide EA dword by 2, ib times
C1 /6 ib  SHL ed,ib    Multiply EA dword by 2, ib times
C1 /7 ib  SAR ed,ib    Signed divide EA dword by 2, ib times
*/
case 0xC1:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.ib = fetchb();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::ROL_ed_ib;
			break;
		case 1:
			m_instr.fn = &CPUExecutor::ROR_ed_ib;
			break;
		case 2:
			m_instr.fn = &CPUExecutor::RCL_ed_ib;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::RCR_ed_ib;
			break;
		case 4:
		case 6: //SAL and SHL are the same
			m_instr.fn = &CPUExecutor::SAL_ed_ib;
			break;
		case 5:
			m_instr.fn = &CPUExecutor::SHR_ed_ib;
			break;
		case 7:
			m_instr.fn = &CPUExecutor::SAR_ed_ib;
			break;
		default:
			illegal_opcode();
	}
	ctb_op_ = m_instr.modrm.n;
	ctb_idx_ = CTB_IDX_C1;
	break;
}

/* C2 iw   RET iw       Return to near caller, pop iw bytes pushed before Call */
case 0xC2:
{
	m_instr.iw1 = fetchw();
	m_instr.fn = &CPUExecutor::RET_near_o32;
	break;
}

/* C3      RET          Return to near caller */
case 0xC3:
{
	m_instr.iw1 = 0;
	m_instr.fn = &CPUExecutor::RET_near_o32;
	break;
}

/* C4 /r    LES rd,mp    Load ES:r32 with pointer from memory */
case 0xC4:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::LES_rd_mp;
	if(m_instr.modrm.mod_is_reg()) {
		illegal_opcode();
	}
	break;
}

/* C5 /r    LDS rw,mp    Load DS:r32 with pointer from memory */
case 0xC5:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::LDS_rd_mp;
	if(m_instr.modrm.mod_is_reg()) {
		illegal_opcode();
	}
	break;
}

/* C6 /0 ib   MOV eb,ib   Move immediate byte into EA byte */
case 0xC6: PREFIX_NONE;

/* C7 /0 id   MOV ed,id   Move immediate dword into EA dword */
case 0xC7:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::MOV_ed_id;
	if(m_instr.modrm.n != 0) {
		illegal_opcode();
	}
	break;
}

/* C8 iw ib   ENTER iw,ib   Make stack frame for procedure parameters */
case 0xC8:
{
	m_instr.iw1 = fetchw();
	m_instr.ib = fetchb();
	m_instr.fn = &CPUExecutor::ENTER_o32;
	break;
}

/* C9      LEAVE      Set (E)SP to (E)BP, then POP (E)BP */
case 0xC9:
{
	m_instr.fn = &CPUExecutor::LEAVE_o32;
	break;
}

/* CA iw   RET iw     Return to far caller, pop iw bytes */
case 0xCA:
{
	m_instr.iw1 = fetchw();
	m_instr.fn = &CPUExecutor::RET_far_o32;
	break;
}

/* CB      RET        Return to far caller */
case 0xCB:
{
	m_instr.iw1 = 0;
	m_instr.fn = &CPUExecutor::RET_far_o32;
	break;
}

case 0xCC: /* CC      INT 3     Interrupt 3 (trap to debugger) */
case 0xCD: /* CD ib   INT ib    Interrupt numbered by immediate byte */
case 0xCE: /* CE      INTO      Interrupt 4  */
	PREFIX_NONE;

/* CF     IRETD        Interrupt return (far return and pop flags) */
case 0xCF:
{
	m_instr.fn = &CPUExecutor::IRETD;
	break;
}

/*
D0 /0     ROL eb,1     Rotate 8-bit EA byte left once
D0 /1     ROR eb,1     Rotate 8-bit EA byte right once
D0 /2     RCL eb,1     Rotate 9-bits (CF, EA byte) left once
D0 /3     RCR eb,1     Rotate 9-bits (CF, EA byte) right once
D0 /4     SAL eb,1     Multiply EA byte by 2, once
D0 /5     SHR eb,1     Unsigned divide EA byte by 2, once
D0 /6     SHL eb,1     Multiply EA byte by 2, once
D0 /7     SAR eb,1     Signed divide EA byte by 2, once
*/
case 0xD0: PREFIX_NONE;

/*
D1 /0     ROL ed,1    Rotate 32-bit EA dword left once
D1 /1     ROR ed,1    Rotate 32-bit EA dword right once
D1 /2     RCL ed,1    Rotate 33-bits (CF, EA dword) left once
D1 /3     RCR ed,1    Rotate 33-bits (CF, EA dword) right once
D1 /4     SAL ed,1    Multiply EA dword by 2, once
D1 /5     SHR ed,1    Unsigned divide EA dword by 2, once
D1 /6     SHL ed,1    Multiply EA dword by 2, once
D1 /7     SAR ed,1    Signed divide EA dword by 2, once
*/
case 0xD1:
{
	m_instr.modrm.load(m_instr.addr32);
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::ROL_ed_1;
			break;
		case 1:
			m_instr.fn = &CPUExecutor::ROR_ed_1;
			break;
		case 2:
			m_instr.fn = &CPUExecutor::RCL_ed_1;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::RCR_ed_1;
			break;
		case 4:
		case 6: //SAL and SHL are the same
			m_instr.fn = &CPUExecutor::SAL_ed_1;
			break;
		case 5:
			m_instr.fn = &CPUExecutor::SHR_ed_1;
			break;
		case 7:
			m_instr.fn = &CPUExecutor::SAR_ed_1;
			break;
		default:
			illegal_opcode();
	}
	ctb_op_ = m_instr.modrm.n;
	ctb_idx_ = CTB_IDX_D1;
	break;
}

/*
D2 /0     ROL eb,CL    Rotate 8-bit EA byte left CL times
D2 /1     ROR eb,CL    Rotate 8-bit EA byte right CL times
D2 /2     RCL eb,CL    Rotate 9-bits (CF, EA byte) left CL times
D2 /3     RCR eb,CL    Rotate 9-bits (CF, EA byte) right CL times
D2 /4     SAL eb,CL    Multiply EA byte by 2, CL times
D2 /5     SHR eb,CL    Unsigned divide EA byte by 2, CL times
D2 /6     SHR eb,CL    Multiply EA byte by 2, CL times
D2 /7     SAR eb,CL    Signed divide EA byte by 2, CL times
*/
case 0xD2: PREFIX_NONE;

/*
D3 /0     ROL ed,CL    Rotate 32-bit EA dword left CL times
D3 /1     ROR ed,CL    Rotate 32-bit EA dword right CL times
D3 /2     RCL ed,CL    Rotate 33-bits (CF, EA dword) left CL times
D3 /3     RCR ed,CL    Rotate 33-bits (CF, EA dword) right CL times
D3 /4     SAL ed,CL    Multiply EA dword by 2, CL times
D3 /5     SHR ed,CL    Unsigned divide EAd word by 2, CL times
D3 /6     SHR ed,CL    Multiply EA dword by 2, CL times
D3 /7     SAR ed,CL    Signed divide EA dword by 2, CL times
*/
case 0xD3:
{
	m_instr.modrm.load(m_instr.addr32);
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::ROL_ed_CL;
			break;
		case 1:
			m_instr.fn = &CPUExecutor::ROR_ed_CL;
			break;
		case 2:
			m_instr.fn = &CPUExecutor::RCL_ed_CL;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::RCR_ed_CL;
			break;
		case 4:
		case 6: //SAL and SHL are the same
			m_instr.fn = &CPUExecutor::SAL_ed_CL;
			break;
		case 5:
			m_instr.fn = &CPUExecutor::SHR_ed_CL;
			break;
		case 7:
			m_instr.fn = &CPUExecutor::SAR_ed_CL;
			break;
		default:
			illegal_opcode();
	}
	ctb_op_ = m_instr.modrm.n;
	ctb_idx_ = CTB_IDX_D3;
	break;
}

case 0xD4: /* D4 ib    AAM      ASCII adjust AX after multiply */
case 0xD5: /* D5 ib    AAD      ASCII adjust AX before division */
case 0xD6: /* D6       SALC     Set AL If Carry */
case 0xD7: /* D7       XLATB    Set AL to memory byte DS:[eBX + unsigned AL] */
	PREFIX_NONE;

case 0xD8: /* D8-DF    FPU ESC  */
case 0xD9:
case 0xDA:
case 0xDB:
case 0xDC:
case 0xDD:
case 0xDE:
case 0xDF:
	PREFIX_NONE;

case 0xE0: /* E0 cb   LOOPNZ cb  DEC eCX; jump short if eCX<>0 and ZF=0 */
case 0xE1: /* E1 cb   LOOPZ cb   DEC eCX; jump short if eCX<>0 and zero (ZF=1) */
case 0xE2: /* E2 cb   LOOP cb    DEC eCX; jump short if eCX<>0 */
case 0xE3: /* E3 cb   JECXZ cb   Jump short if ECX register is zero */
	PREFIX_NONE;

/* E4 ib     IN AL,ib     Input byte from immediate port into AL */
case 0xE4: PREFIX_NONE;

/* E5 ib     IN EAX,ib    Input dword from immediate port into EAX */
case 0xE5:
{
	m_instr.ib = fetchb();
	m_instr.fn = &CPUExecutor::IN_EAX_ib;
	break;
}

/* E6 ib    OUT ib,AL     Output byte AL to immediate port number ib */
case 0xE6: PREFIX_NONE;

/* E7 ib    OUT ib,EAX     Output dword EAX to immediate port number ib */
case 0xE7:
{
	m_instr.ib = fetchb();
	m_instr.fn = &CPUExecutor::OUT_ib_EAX;
	break;
}

/* E8 cd    CALL cd       Call near, offset relative to next instruction */
case 0xE8:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::CALL_rel32;
	break;
}

/* E9 cd   JMP cd   Jump near displacement relative to next instruction */
case 0xE9:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::JMP_rel32;
	break;
}

/* EA cp   JMP ptr16:32   Jump far/task/call/tss */
case 0xEA:
{
	m_instr.id1 = fetchdw();
	m_instr.iw2 = fetchw();
	m_instr.fn = &CPUExecutor::JMP_ptr1632;
	break;
}

/* EB cb     JMP cb         Jump short */
case 0xEB: PREFIX_NONE;

/* EC        IN AL,DX       Input byte from port DX into AL */
case 0xEC: PREFIX_NONE;

/* ED        IN EAX,DX       Input dword from port DX into EAX */
case 0xED:
{
	m_instr.fn = &CPUExecutor::IN_EAX_DX;
	break;
}

/* EE       OUT DX,AL       Output byte AL to port number DX */
case 0xEE: PREFIX_NONE;

/* EF       OUT DX,EAX     Output dword EAX to port number DX */
case 0xEF:
{
	m_instr.fn = &CPUExecutor::OUT_DX_EAX;
	break;
}

/*
F0   LOCK prefix
F1   prefix, does not generate #UD; INT1 (ICEBP) on 386+ TODO?
F2   REP/REPE prefix
F3   REPNE prefix
*/

/* F4      HLT           Halt */
case 0xF4: PREFIX_NONE;

/* F5      CMC           Complement carry flag */
case 0xF5: PREFIX_NONE;

/*
F6 /0 ib   TEST eb,ib    AND immediate byte into EA byte for flags only
F6 /2      NOT  eb       Reverse each bit of EA byte
F6 /3      NEG  eb       Two's complement negate EA byte
F6 /4      MUL  eb       Unsigned multiply (AX = AL * EA byte)
F6 /5      IMUL eb       Signed multiply (AX = AL * EA byte)
F6 /6      DIV  eb       Unsigned divide AX by EA byte
F6 /7      IDIV eb       Signed divide AX by EA byte (AL=Quo,AH=Rem)
*/
case 0xF6: PREFIX_NONE;

/*
F7 /0 id   TEST ed,id    AND immediate dword into EA dword for flags only
F7 /2      NOT  ed       Reverse each bit of EA dword
F7 /3      NEG  ed       Two's complement negate EA dword
F7 /4      MUL  ed       Unsigned multiply (EDX:EAX = EAX * EA dword)
F7 /5      IMUL ed       Signed multiply (EDX:EAX = EAX * EA dword)
F7 /6      DIV  ed       Unsigned divide EDX:EAX by EA dword
F7 /7      IDIV ed       Signed divide EDX:EAX by EA dword (EAX=Quo,EDX=Rem)
*/
case 0xF7:
{
	m_instr.modrm.load(m_instr.addr32);
	switch(m_instr.modrm.n) {
		case 0:
		case 1: // 1: undocumented alias
		{
			m_instr.id1 = fetchdw();
			m_instr.fn = &CPUExecutor::TEST_ed_id;
			break;
		}
		case 2:
			m_instr.fn = &CPUExecutor::NOT_ed;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::NEG_ed;
			break;
		case 4:
			m_instr.fn = &CPUExecutor::MUL_ed;
			break;
		case 5:
			m_instr.fn = &CPUExecutor::IMUL_ed;
			break;
		case 6:
			m_instr.fn = &CPUExecutor::DIV_ed;
			break;
		case 7:
			m_instr.fn = &CPUExecutor::IDIV_ed;
			break;
		default:
			illegal_opcode();
	}
	ctb_op_ = m_instr.modrm.n;
	ctb_idx_ = CTB_IDX_F7;
	break;
}

case 0xF8: /* F8   CLC    Clear carry flag */
case 0xF9: /* F9   STC    Set carry flag */
case 0xFA: /* FA   CLI    Clear interrupt flag; interrupts disabled */
case 0xFB: /* FB   STI    Set interrupt enable flag, interrupts enabled */
case 0xFC: /* FC   CLD    Clear direction flag, (E)SI and (E)DI will increment */
case 0xFD: /* FD   STD    Set direction flag so (E)SI and (E)DI will decrement */
	PREFIX_NONE;

/*
FE /0     INC eb    Increment EA byte by 1
FE /1     DEC eb    Decrement EA byte by 1
*/
case 0xFE: PREFIX_NONE;

/*
FF /0     INC  ed   Increment EA dword by 1
FF /1     DEC  ed   Decrement EA dword by 1
FF /2     CALL ed   Call near, offset absolute at EA dword
FF /3     CALL ep   Call inter-segment, address at EA pointer
FF /4     JMP  ed   Jump near to EA dword (absolute offset)
FF /5     JMP  ep   Jump far (6-byte effective address in memory)
FF /6     PUSH md   Push memory dword
*/
case 0xFF:
{
	m_instr.modrm.load(m_instr.addr32);
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::INC_ed;
			break;
		case 1:
			m_instr.fn = &CPUExecutor::DEC_ed;
			break;
		case 2:
			m_instr.fn = &CPUExecutor::CALL_ed;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::CALL_m1632;
			break;
		case 4:
			m_instr.fn = &CPUExecutor::JMP_ed;
			break;
		case 5:
			if(m_instr.modrm.mod == 3) {
				illegal_opcode();
				break;
			}
			m_instr.fn = &CPUExecutor::JMP_m1632;
			break;
		case 6:
			m_instr.fn = &CPUExecutor::PUSH_md;
			break;
		default:
			illegal_opcode();
			break;
	}
	ctb_op_ = m_instr.modrm.n;
	ctb_idx_ = CTB_IDX_FF;
	break;
}

default:
{
	illegal_opcode();
}


} //switch

}
