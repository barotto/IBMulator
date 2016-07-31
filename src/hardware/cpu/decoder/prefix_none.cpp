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

//should be 7, but i try to compensate the 2 decode cycles when the pq is invalid
#define JUMP_CYCLES 6
#define LOOP_CYCLES 7


#define FN_BYTE(_fn_) m_instr.fn = &CPUExecutor::_fn_
#define FN_W_DW(_fnw_,_fndw_) (m_instr.op32)?(m_instr.fn = &CPUExecutor::_fndw_):(m_instr.fn = &CPUExecutor::_fnw_)

void CPUDecoder::prefix_none(uint8_t _opcode)
{
	/* Just a big switch..case statement because I hate tables.
	 * The compiler will optimize it as a jump table anyway.
	 */

switch(_opcode) {

/* 00 /r      ADD eb,rb   2/7  2/7  Add byte register into EA byte */
case 0x00:
{
	m_instr.modrm.load();
	FN_BYTE(ADD_eb_rb);
	CYCLES(2,5);
	break;
}

/*
01 /r     ADD ew,rw   2/7 2/7   Add word register into EA word
01 /r     ADD ed,rd       2/7   Add dword register to EA dword
*/
case 0x01:
{
	m_instr.modrm.load();
	FN_W_DW(ADD_ew_rw, ADD_ed_rd);
	CYCLES(2,8);
	break;
}

/* 02 /r      ADD rb,eb   2,mem=7    Add EA byte into byte register */
case 0x02:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::ADD_rb_eb;
	CYCLES(2,5);
	break;
}

/* 03 /r      ADD rw,ew   2,mem=7    Add EA word into word register */
case 0x03:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::ADD_rw_ew;
	CYCLES(2,5);
	break;
}

/* 04 db      ADD AL,db   3          Add immediate byte into AL */
case 0x04:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::ADD_AL_db;
	CYCLES(3,3);
	break;
}

/* 05 dw      ADD AX,dw   3          Add immediate word into AX */
case 0x05:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::ADD_AX_dw;
	CYCLES(3,3);
	break;
}

/* 06         PUSH ES      3         Push ES */
case 0x06:
{
	m_instr.fn = &CPUExecutor::PUSH_ES;
	CYCLES(3,3);
	break;
}

/* 07          POP ES           5,pm=20    Pop top of stack into ES */
case 0x07:
{
	m_instr.fn = &CPUExecutor::POP_ES;
	CYCLES(3,3);
	// 8 descriptor fetch + 2 stack pop = 10 cycles for mem ops
	// 20 by intel docs - 10 memory operations = 10 cycles for the instruction exec
	// 10 pmode - 3 rmode  = 7 cycles of penalty
	CYCLES_PM(7);
	break;
}

/* 08 /r      OR eb,rb       2,mem=7   Logical-OR byte register into EA byte */
case 0x08:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::OR_eb_rb;
	CYCLES(2,5);
	break;
}

/* 09 /r      OR ew,rw       2,mem=7   Logical-OR word register into EA word */
case 0x09:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::OR_ew_rw;
	CYCLES(2,8);
	break;
}

/* 0A /r      OR rb,eb       2,mem=7   Logical-OR EA byte into byte register */
case 0x0A:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::OR_rb_eb;
	CYCLES(2,5);
	break;
}

/* 0B /r      OR rw,ew       2,mem=7   Logical-OR EA word into word register */
case 0x0B:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::OR_rw_ew;
	CYCLES(2,5);
	break;
}

/* 0C db      OR AL,db       3         Logical-OR immediate byte into AL */
case 0x0C:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::OR_AL_db;
	CYCLES(3,3);
	break;
}

/* 0D dw      OR AX,dw       3         Logical-OR immediate word into AX */
case 0x0D:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::OR_AX_dw;
	CYCLES(3,3);
	break;
}

/* 0E         PUSH CS      3         Push CS */
case 0x0E:
{
	m_instr.fn = &CPUExecutor::PUSH_CS;
	CYCLES(3,3);
	break;
}

/* 0F 2-byte opcode prefix */

/* 10 /r      ADC eb,rb   2,mem=7    Add with carry byte register into EA byte */
case 0x10:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::ADC_eb_rb;
	CYCLES(2,5);
	break;
}

/* 11 /r      ADC ew,rw   2,mem=7    Add with carry word register into EA word */
case 0x11:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::ADC_ew_rw;
	CYCLES(2,8);
	break;
}

/* 12 /r      ADC rb,eb   2,mem=7    Add with carry EA byte into byte register */
case 0x12:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::ADC_rb_eb;
	CYCLES(2,5);
	break;
}

/* 13 /r      ADC rw,ew   2,mem=7    Add with carry EA word into word register */
case 0x13:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::ADC_rw_ew;
	CYCLES(2,5);
	break;
}

/* 14 db      ADC AL,db   3          Add with carry immediate byte into AL */
case 0x14:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::ADC_AL_db;
	CYCLES(3,3);
	break;
}

/* 15 dw      ADC AX,dw   3          Add with carry immediate word into AX */
case 0x15:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::ADC_AX_dw;
	CYCLES(3,3);
	break;
}

/* 16         PUSH SS      3         Push SS */
case 0x16:
{
	m_instr.fn = &CPUExecutor::PUSH_SS;
	CYCLES(3,3);
	break;
}

/* 17          POP SS           5,pm=20    Pop top of stack into SS */
case 0x17:
{
	m_instr.fn = &CPUExecutor::POP_SS;
	CYCLES(3,3);
	// see pop es comment
	CYCLES_PM(7);
	break;
}

/* 18 /r       SBB eb,rb    2,mem=7   Subtract with borrow byte register from EA byte */
case 0x18:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::SBB_eb_rb;
	CYCLES(2,8);
	break;
}

/* 19 /r       SBB ew,rw    2,mem=7   Subtract with borrow word register from EA word */
case 0x19:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::SBB_ew_rw;
	CYCLES(2,8);
	break;
}

/* 1A /r       SBB rb,eb    2,mem=7   Subtract with borrow EA byte from byte register */
case 0x1A:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::SBB_rb_eb;
	CYCLES(2,5);
	break;
}

/* 1B /r       SBB rw,ew    2,mem=7   Subtract with borrow EA word from word register */
case 0x1B:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::SBB_rw_ew;
	CYCLES(2,5);
	break;
}

/* 1C db       SBB AL,db    3         Subtract with borrow imm. byte from AL */
case 0x1C:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::SBB_AL_db;
	CYCLES(3,3);
	break;
}

/* 1D dw       SBB AX,dw    3         Subtract with borrow imm. word from AX */
case 0x1D:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::SBB_AX_dw;
	CYCLES(3,3);
	break;
}

/* 1E         PUSH DS      3         Push DS */
case 0x1E:
{
	m_instr.fn = &CPUExecutor::PUSH_DS;
	CYCLES(3,3);
	break;
}

/* 1F          POP DS           5,pm=20    Pop top of stack into DS */
case 0x1F:
{
	m_instr.fn = &CPUExecutor::POP_DS;
	CYCLES(3,3);
	// see pop es comment
	CYCLES_PM(7);
	break;
}

/* 20 /r      AND eb,rb     2,mem=7    Logical-AND byte register into EA byte */
case 0x20:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::AND_eb_rb;
	CYCLES(2,8);
	break;
}

/* 21 /r      AND ew,rw     2,mem=7    Logical-AND word register into EA word */
case 0x21:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::AND_ew_rw;
	CYCLES(2,8);
	break;
}

/* 22 /r      AND rb,eb     2,mem=7    Logical-AND EA byte into byte register */
case 0x22:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::AND_rb_eb;
	CYCLES(2,5);
	break;
}

/* 23 /r      AND rw,ew     2,mem=7    Logical-AND EA word into word register */
case 0x23:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::AND_rw_ew;
	CYCLES(2,5);
	break;
}

/* 24 db      AND AL,db     3          Logical-AND immediate byte into AL */
case 0x24:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::AND_AL_db;
	CYCLES(3,3);
	break;
}

/* 25 dw      AND AX,dw     3          Logical-AND immediate word into AX */
case 0x25:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::AND_AX_dw;
	CYCLES(3,3);
	break;
}

/* 26  seg ovr prefix (ES) */

/* 27      DAA            3         Decimal adjust AL after addition */
case 0x27:
{
	m_instr.fn = &CPUExecutor::DAA;
	CYCLES(3,3);
	break;
}

/* 28 /r      SUB eb,rb      2,mem=7     Subtract byte register from EA byte */
case 0x28:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::SUB_eb_rb;
	CYCLES(2,8);
	break;
}

/* 29 /r      SUB ew,rw      2,mem=7     Subtract word register from EA word */
case 0x29:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::SUB_ew_rw;
	CYCLES(2,8);
	break;
}

/* 2A /r      SUB rb,eb      2,mem=7     Subtract EA byte from byte register */
case 0x2A:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::SUB_rb_eb;
	CYCLES(2,5);
	break;
}

/* 2B /r      SUB rw,ew      2,mem=7     Subtract EA word from word register */
case 0x2B:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::SUB_rw_ew;
	CYCLES(2,5);
	break;
}

/* 2C db      SUB AL,db      3           Subtract immediate byte from AL */
case 0x2C:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::SUB_AL_db;
	CYCLES(3,3);
	break;
}

/* 2D dw      SUB AX,dw      3           Subtract immediate word from AX */
case 0x2D:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::SUB_AX_dw;
	CYCLES(3,3);
	break;
}

/* 2E  seg ovr prefix (CS) */

/* 2F        DAS             3          Decimal adjust AL after subtraction */
case 0x2F:
{
	m_instr.fn = &CPUExecutor::DAS;
	CYCLES(3,3);
	break;
}

/* 30 /r     XOR eb,rb   2,mem=7   Exclusive-OR byte register into EA byte */
case 0x30:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::XOR_eb_rb;
	CYCLES(2,8);
	break;
}

/* 31 /r     XOR ew,rw   2,mem=7   Exclusive-OR word register into EA word */
case 0x31:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::XOR_ew_rw;
	CYCLES(2,8);
	break;
}

/* 32 /r     XOR rb,eb   2,mem=7   Exclusive-OR EA byte into byte register */
case 0x32:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::XOR_rb_eb;
	CYCLES(2,5);
	break;
}

/* 33 /r     XOR rw,ew   2,mem=7   Exclusive-OR EA word into word register */
case 0x33:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::XOR_rw_ew;
	CYCLES(2,5);
	break;
}

/* 34 db     XOR AL,db   3         Exclusive-OR immediate byte into AL */
case 0x34:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::XOR_AL_db;
	CYCLES(3,3);
	break;
}

/* 35 dw     XOR AX,dw   3         Exclusive-OR immediate word into AX */
case 0x35:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::XOR_AX_dw;
	CYCLES(3,3);
	break;
}

/* 36  seg ovr prefix (SS) */

/* 37        AAA         3          ASCII adjust AL after addition */
case 0x37:
{
	m_instr.fn = &CPUExecutor::AAA;
	CYCLES(3,3);
	break;
}

/* 38 /r        CMP eb,rb      2,mem=7      Compare byte register from EA byte */
case 0x38:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::CMP_eb_rb;
	CYCLES(2,5);
	break;
}

/* 39 /r        CMP ew,rw      2,mem=7      Compare word register from EA word */
case 0x39:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::CMP_ew_rw;
	CYCLES(2,5);
	break;
}

/* 3A /r        CMP rb,eb      2,mem=6      Compare EA byte from byte register*/
case 0x3A:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::CMP_rb_eb;
	CYCLES(2,4);
	break;
}

/* 3B /r        CMP rw,ew      2,mem=6      Compare EA word from word register */
case 0x3B:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::CMP_rw_ew;
	CYCLES(2,4);
	break;
}

/* 3C db        CMP AL,db       3           Compare immediate byte from AL */
case 0x3C:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::CMP_AL_db;
	CYCLES(3,3);
	break;
}

/* 3D dw        CMP AX,dw       3           Compare immediate word from AX */
case 0x3D:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::CMP_AX_dw;
	CYCLES(3,3);
	break;
}

/* 3E  seg ovr prefix (DS) */

/* 3F        AAS       3          ASCII adjust AL after subtraction */
case 0x3F:
{
	m_instr.fn = &CPUExecutor::AAS;
	CYCLES(3,3);
	break;
}

/* 40+ rw     INC rw         2             Increment word register by 1 */
case 0x40: //AX
case 0x41: //CX
case 0x42: //DX
case 0x43: //BX
case 0x44: //SP
case 0x45: //BP
case 0x46: //SI
case 0x47: //DI
{
	m_instr.reg = _opcode - 0x40;
	m_instr.fn = &CPUExecutor::INC_rw;
	CYCLES(2,2);
	break;
}

/* 48+ rw     DEC rw         2             Decrement word register by 1 */
case 0x48: //AX
case 0x49: //CX
case 0x4A: //DX
case 0x4B: //BX
case 0x4C: //SP
case 0x4D: //BP
case 0x4E: //SI
case 0x4F: //DI
{
	m_instr.reg = _opcode - 0x48;
	m_instr.fn = &CPUExecutor::DEC_rw;
	CYCLES(2,2);
	break;
}

/*  50+ rw     PUSH rw      3         Push word register */
case 0x50: //AX
case 0x51: //CX
case 0x52: //DX
case 0x53: //BX
case 0x54: //SP
case 0x55: //BP
case 0x56: //SI
case 0x57: //DI
{
	m_instr.reg = _opcode - 0x50;
	m_instr.fn = &CPUExecutor::PUSH_rw;
	CYCLES(3,3);
	break;
}

/* 58+ rw     POP rw           5          Pop top of stack into word register */
case 0x58: //AX
case 0x59: //CX
case 0x5A: //DX
case 0x5B: //BX
case 0x5C: //SP
case 0x5D: //BP
case 0x5E: //SI
case 0x5F: //DI
{
	m_instr.reg = _opcode - 0x58;
	m_instr.fn = &CPUExecutor::POP_rw;
	CYCLES(3,3);
	m_instr.cycles.bu = -3;
	break;
}

/* 60        PUSHA      17        Push in order: AX,CX,DX,BX,original SP,BP,SI,DI */
case 0x60:
{
	m_instr.fn = &CPUExecutor::PUSHA;
	CYCLES(17,17);
	break;
}

/* 61        POPA           19         Pop in order: DI,SI,BP,SP,BX,DX,CX,AX */
case 0x61:
{
	m_instr.fn = &CPUExecutor::POPA ;
	CYCLES(3,3);
	break;
}

/* 62 /r      BOUND rw,md     noj=13     INT 5 if rw not within bounds */
case 0x62:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::BOUND_rw_md ;
	//normal cycles are the same as INT
	CYCLES(7,7);
	CYCLES_JCOND(9);
	break;
}

/* 63 /r    ARPL ew,rw   10,mem=11    Adjust RPL of EA word not less than RPL of rw */
case 0x63:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::ARPL_ew_rw;
	CYCLES(10,9);
	break;
}

/*
64  seg ovr prefix (FS) 386+
	on 8086 is alias for 74 JE cb
65  seg ovr prefix (GS) 386+
	on 8086 is alias for 75 JNE cb
66  operand-size prefix (OS) 386+
	on 8086 is alias for 76 JBE cb
67  address-size prefix (AS) 386+
	on 8086 is alias for 77 JA cb
*/

/* 68  dw     PUSH dw      3         Push immediate word */
case 0x68:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::PUSH_dw;
	CYCLES(3,3);
	break;
}

/* 69 /r dw   IMUL rw,ew,dw  21,mem=24   Signed multiply (rw = EA word * imm. word) */
case 0x69:
{
	m_instr.modrm.load();
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::IMUL_rw_ew_dw;
	CYCLES(21,22);
	break;
}

/* 6A  db     PUSH db      3         Push immediate sign-extended byte*/
case 0x6A:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::PUSH_db;
	CYCLES(3,3);
	break;
}

/* 6B /r db   IMUL rw,ew,db  21,mem=24   Signed multiply (rw = EA word * imm. byte) */
case 0x6B:
{
	m_instr.modrm.load();
	m_instr.dw1 = int8_t(fetchb());
	m_instr.fn = &CPUExecutor::IMUL_rw_ew_dw;
	CYCLES(21,22);
	break;
}

/* 6C      INSB           5         Input byte from port DX into ES:[DI] */
case 0x6C:
{
	m_instr.rep = m_rep;
	m_instr.fn = &CPUExecutor::INSB;
	CYCLES(5,5);
	m_instr.cycles.base_rep = 4;
	break;
}

/* 6D      INSW           5         Input word from port DX into ES:[DI] */
case 0x6D:
{
	m_instr.rep = m_rep;
	m_instr.fn = &CPUExecutor::INSW;
	CYCLES(5,5);
	m_instr.cycles.base_rep = 4;
	break;
}

/* 6E      OUTSB          5          Output byte DS:[SI] to port number DX */
case 0x6E:
{
	m_instr.rep = m_rep;
	m_instr.fn = &CPUExecutor::OUTSB;
	CYCLES(3,3);
	m_instr.cycles.base_rep = 4;
	break;
}

/* 6F      OUTSW          5          Output word DS:[SI] to port number DX */
case 0x6F:
{
	m_instr.rep = m_rep;
	m_instr.fn = &CPUExecutor::OUTSW;
	CYCLES(3,3);
	m_instr.cycles.base_rep = 4;
	break;
}

/* 70  cb     JO cb      7,noj=3   Jump short if overflow (OF=1) */
case 0x70:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JO_cb;
	CYCLES(JUMP_CYCLES,JUMP_CYCLES);
	CYCLES_JCOND(3);
	break;
}

/* 71  cb     JNO cb     7,noj=3   Jump short if notoverflow (OF=0) */
case 0x71:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JNO_cb;
	CYCLES(JUMP_CYCLES,JUMP_CYCLES);
	CYCLES_JCOND(3);
	break;
}

/* 72  cb     JC cb      7,noj=3   Jump short if carry (CF=1) */
case 0x72:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JC_cb;
	CYCLES(JUMP_CYCLES,JUMP_CYCLES);
	CYCLES_JCOND(3);
	break;
}

/* 73  cb     JNC cb     7,noj=3   Jump short if not carry (CF=0) */
case 0x73:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JNC_cb;
	CYCLES(JUMP_CYCLES,JUMP_CYCLES);
	CYCLES_JCOND(3);
	break;
}

/* 74  cb     JE cb      7,noj=3   Jump short if equal (ZF=1) */
case 0x74:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JE_cb;
	CYCLES(JUMP_CYCLES,JUMP_CYCLES);
	CYCLES_JCOND(3);
	break;
}

/* 75  cb     JNE cb     7,noj=3   Jump short if not equal (ZF=0) */
case 0x75:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JNE_cb;
	CYCLES(JUMP_CYCLES,JUMP_CYCLES);
	CYCLES_JCOND(3);
	break;
}

/* 76  cb     JBE cb     7,noj=3   Jump short if below or equal (CF=1 or ZF=1) */
case 0x76:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JBE_cb;
	CYCLES(JUMP_CYCLES,JUMP_CYCLES);
	CYCLES_JCOND(3);
	break;
}

/* 77  cb     JA cb      7,noj=3   Jump short if above (CF=0 and ZF=0) */
case 0x77:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JA_cb;
	CYCLES(JUMP_CYCLES,JUMP_CYCLES);
	CYCLES_JCOND(3);
	break;
}

/* 78  cb     JS cb      7,noj=3   Jump short if sign (SF=1) */
case 0x78:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JS_cb;
	CYCLES(JUMP_CYCLES,JUMP_CYCLES);
	CYCLES_JCOND(3);
	break;
}

/* 79  cb     JNS cb     7,noj=3   Jump short if not sign (SF=0) */
case 0x79:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JNS_cb;
	CYCLES(JUMP_CYCLES,JUMP_CYCLES);
	CYCLES_JCOND(3);
	break;
}

/* 7A  cb     JPE cb     7,noj=3   Jump short if parity even (PF=1) */
case 0x7A:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JPE_cb;
	CYCLES(JUMP_CYCLES,JUMP_CYCLES);
	CYCLES_JCOND(3);
	break;
}

/* 7B  cb     JPO cb     7,noj=3   Jump short if parity odd (PF=0) */
case 0x7B:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JPO_cb;
	CYCLES(JUMP_CYCLES,JUMP_CYCLES);
	CYCLES_JCOND(3);
	break;
}

/* 7C  cb     JL cb      7,noj=3   Jump short if less (SF/=OF) */
case 0x7C:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JL_cb;
	CYCLES(JUMP_CYCLES,JUMP_CYCLES);
	CYCLES_JCOND(3);
	break;
}

/* 7D  cb     JNL cb     7,noj=3   Jump short if not less (SF=OF) */
case 0x7D:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JNL_cb;
	CYCLES(JUMP_CYCLES,JUMP_CYCLES);
	CYCLES_JCOND(3);
	break;
}

/* 7E  cb     JLE cb     7,noj=3   Jump short if less or equal (ZF=1 or SF/=OF) */
case 0x7E:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JLE_cb;
	CYCLES(JUMP_CYCLES,JUMP_CYCLES);
	CYCLES_JCOND(3);
	break;
}

/* 7F  cb     JNLE cb    7,noj=3   Jump short if not less/equal (ZF=0 and SF=OF) */
case 0x7F:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JNLE_cb;
	CYCLES(JUMP_CYCLES,JUMP_CYCLES);
	CYCLES_JCOND(3);
	break;
}

/*
80 /0 db   ADD eb,db   3,mem=7    Add immediate byte into EA byte
80 /1 db   OR  eb,db   3,mem=7    Logical-OR immediate byte  into EA byte
80 /2 db   ADC eb,db   3,mem=7    Add with carry immediate byte into EA byte
80 /3 db   SBB eb,db   3,mem=7    Subtract with borrow imm. byte from EA byte
80 /4 db   AND eb,db   3,mem=7    Logical-AND immediate byte into EA byte
80 /5 db   SUB eb,db   3,mem=7    Subtract immediate byte from EA byte
80 /6 db   XOR eb,db   3,mem=7    Exclusive-OR immediate byte into EA byte
80 /7 db   CMP eb,db   3,mem=6    Compare immediate byte from EA byte
*/
case 0x80:
case 0x82:
{
	m_instr.modrm.load();
	m_instr.db = fetchb();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::ADD_eb_db;
			CYCLES(3,5);
			break;
		case 1:
			m_instr.fn = &CPUExecutor::OR_eb_db;
			CYCLES(3,5);
			break;
		case 2:
			m_instr.fn = &CPUExecutor::ADC_eb_db;
			CYCLES(3,5);
			break;
		case 3:
			m_instr.fn = &CPUExecutor::SBB_eb_db;
			CYCLES(3,5);
			break;
		case 4:
			m_instr.fn = &CPUExecutor::AND_eb_db;
			CYCLES(3,5);
			break;
		case 5:
			m_instr.fn = &CPUExecutor::SUB_eb_db;
			CYCLES(3,5);
			break;
		case 6:
			m_instr.fn = &CPUExecutor::XOR_eb_db;
			CYCLES(3,5);
			break;
		case 7:
			m_instr.fn = &CPUExecutor::CMP_eb_db;
			CYCLES(3,4);
			break;
		default:
			illegal_opcode();
	}
	break;
}

/*
81 /0 dw   ADD ew,dw   3,mem=7    Add immediate word into EA word
81 /1 dw   OR  ew,dw   3,mem=7    Logical-OR immediate word into EA word
81 /2 dw   ADC ew,dw   3,mem=7    Add with carry immediate word into EA word
81 /3 dw   SBB ew,dw   3,mem=7    Subtract with borrow imm. word from EA word
81 /4 dw   AND ew,dw   3,mem=7    Logical-AND immediate word into EA word
81 /5 dw   SUB ew,dw   3,mem=7    Subtract immediate word from EA word
81 /6 dw   XOR ew,dw   3,mem=7    Exclusive-OR immediate word into EA word
81 /7 dw   CMP ew,dw   3,mem=6    Compare immediate word from EA word
*/
case 0x81:
{
	m_instr.modrm.load();
	m_instr.dw1 = fetchw();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::ADD_ew_dw;
			CYCLES(3,8);
			break;
		case 1:
			m_instr.fn = &CPUExecutor::OR_ew_dw;
			CYCLES(3,8);
			break;
		case 2:
			m_instr.fn = &CPUExecutor::ADC_ew_dw;
			CYCLES(3,8);
			break;
		case 3:
			m_instr.fn = &CPUExecutor::SBB_ew_dw;
			CYCLES(3,8);
			break;
		case 4:
			m_instr.fn = &CPUExecutor::AND_ew_dw;
			CYCLES(3,8);
			break;
		case 5:
			m_instr.fn = &CPUExecutor::SUB_ew_dw;
			CYCLES(3,8);
			break;
		case 6:
			m_instr.fn = &CPUExecutor::XOR_ew_dw;
			CYCLES(3,8);
			break;
		case 7:
			m_instr.fn = &CPUExecutor::CMP_ew_dw;
			CYCLES(3,4);
			break;
		default:
			illegal_opcode();
	}
	break;
}

/* 82  alias of 80 */

/*
83 /0 db   ADD ew,db   3,mem=7    Add immediate byte into EA word
83 /1 db   OR  ew,db   3,mem=7    Logical-OR immediate byte into EA word (undocumented!)
83 /2 db   ADC ew,db   3,mem=7    Add with carry immediate byte into EA word
83 /3 db   SBB ew,db   3,mem=7    Subtract with borrow imm. byte from EA word
83 /4 db   AND ew,db   3,mem=7    Logical-AND immediate byte into EA word (undocumented!)
83 /5 db   SUB ew,db   3,mem=7    Subtract immediate byte from EA word
83 /6 db   XOR ew,db   3,mem=7    Exclusive-OR immediate byte into EA word (undocumented!)
83 /7 db   CMP ew,db   3,mem=6    Compare immediate byte from EA word
*/
case 0x83:
{
	m_instr.modrm.load();
	m_instr.db = fetchb();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::ADD_ew_db;
			CYCLES(3,8);
			break;
		case 1:
			m_instr.fn = &CPUExecutor::OR_ew_db;
			CYCLES(3,8);
			break;
		case 2:
			m_instr.fn = &CPUExecutor::ADC_ew_db;
			CYCLES(3,8);
			break;
		case 3:
			m_instr.fn = &CPUExecutor::SBB_ew_db;
			CYCLES(3,8);
			break;
		case 4:
			m_instr.fn = &CPUExecutor::AND_ew_db;
			CYCLES(3,8);
			break;
		case 5:
			m_instr.fn = &CPUExecutor::SUB_ew_db;
			CYCLES(3,8);
			break;
		case 6:
			m_instr.fn = &CPUExecutor::XOR_ew_db;
			CYCLES(3,8);
			break;
		case 7:
			m_instr.fn = &CPUExecutor::CMP_ew_db;
			CYCLES(3,4);
			break;
		default:
			// according to the Intel's 286 user manual and http://ref.x86asm.net
			// 1,4,6 should be 386+ only, but the PS/1 BIOS uses them, so
			// they are clearly 286 opcodes too.
			illegal_opcode();
	}
	break;
}

/* 84 /r      TEST eb,rb    2,mem=6    AND byte register into EA byte for flags only */
case 0x84:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::TEST_eb_rb;
	CYCLES(2,4);
	break;
}

/* 85 /r      TEST ew,rw    2,mem=6    AND word register into EA word for flags only */
case 0x85:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::TEST_ew_rw;
	CYCLES(2,4);
	break;
}

/* 86 /r     XCHG eb,rb     3,mem=5     Exchange byte register with EA byte */
case 0x86:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::XCHG_eb_rb;
	CYCLES(3,3);
	break;
}

/* 87 /r     XCHG ew,rw     3,mem=5     Exchange word register with EA word */
case 0x87:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::XCHG_ew_rw;
	CYCLES(3,3);
	break;
}

/* 88 /r      MOV eb,rb   2,mem=3       Move byte register into EA byte */
case 0x88:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::MOV_eb_rb;
	CYCLES(2,3);
	break;
}

/* 89 /r      MOV ew,rw   2,mem=3       Move word register into EA word */
case 0x89:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::MOV_ew_rw;
	CYCLES(2,3);
	break;
}

/* 8A /r      MOV rb,eb   2,mem=5       Move EA byte into byte register */
case 0x8A:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::MOV_rb_eb;
	CYCLES(2,3);
	break;
}

/* 8B /r      MOV rw,ew   2,mem=5       Move EA word into word register */
case 0x8B:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::MOV_rw_ew;
	CYCLES(2,3);
	break;
}

/*
8C /0      MOV ew,ES   2,mem=3       Move ES into EA word
8C /1      MOV ew,CS   2,mem=3       Move CS into EA word
8C /2      MOV ew,SS   2,mem=3       Move SS into EA word
8C /3      MOV ew,DS   2,mem=3       Move DS into EA word
*/
case 0x8C:
{
	m_instr.modrm.load();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::MOV_ew_ES;
			break;
		case 1:
			m_instr.fn = &CPUExecutor::MOV_ew_CS;
			break;
		case 2:
			m_instr.fn = &CPUExecutor::MOV_ew_SS;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::MOV_ew_DS;
			break;
		default:
			illegal_opcode();
	}
	CYCLES(2,3);
	break;
}

/* 8D /r    LEA rw,m        3       Calculate EA offset given by m, place in rw */
case 0x8D:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::LEA_rw_m;
	CYCLES(3,3);
	break;
}

/*
8E /0      MOV ES,mw   5,pm=19       Move memory word into ES
8E /0      MOV ES,rw   2,pm=17       Move word register into ES
8E /2      MOV SS,mw   5,pm=19       Move memory word into SS
8E /2      MOV SS,rw   2,pm=17       Move word register into SS
8E /3      MOV DS,mw   5,pm=19       Move memory word into DS
8E /3      MOV DS,rw   2,pm=17       Move word register into DS
*/
case 0x8E:
{
	m_instr.modrm.load();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::MOV_ES_ew;
			break;
		case 2:
			m_instr.fn = &CPUExecutor::MOV_SS_ew;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::MOV_DS_ew;
			break;
		default:
			illegal_opcode();
	}
	CYCLES(2,3);
	CYCLES_PM(5);
	break;
}

/*
8F /0       POP mw           5          Pop top of stack into memory word
*/
case 0x8F:
{
	m_instr.modrm.load();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::POP_mw;
			break;
		default:
			illegal_opcode();
	}
	CYCLES(3,3);
	break;
}

/* 90            NOP              3        No OPERATION */
case 0x90:
{
	m_instr.fn = &CPUExecutor::NOP;
	CYCLES(3,3);
	break;
}

/* 90+ rw    XCHG AX,rw     3           Exchange word register with AX */
//case 0x90: //can't xchg AX with itself
case 0x91: //CX
case 0x92: //DX
case 0x93: //BX
case 0x94: //SP
case 0x95: //BP
case 0x96: //SI
case 0x97: //DI
{
	m_instr.reg = _opcode - 0x90;
	m_instr.fn = &CPUExecutor::XCHG_AX_rw;
	CYCLES(3,3);
	break;
}

/* 98       CBW           2        Convert byte into word (AH = top bit of AL) */
case 0x98:
{
	m_instr.fn = &CPUExecutor::CBW;
	CYCLES(2,2);
	break;
}

/* 99       CWD             2           Convert word to doubleword (DX:AX = AX) */
case 0x99:
{
	m_instr.fn = &CPUExecutor::CWD;
	CYCLES(2,2);
	break;
}

/* 9A cd     CALL cd       13,pm=26   Call inter-segment, immediate 4-byte address */
case 0x9A:
{
	m_instr.dw1 = fetchw();
	m_instr.dw2 = fetchw();
	m_instr.fn = &CPUExecutor::CALL_cd;
	CYCLES(5,5);
	//for PM mode:
	// 4 cycles for PQ flush and fill
	// 4 cycles for 2 stack pushes
	// 8 cycles for 4 mem reads (descriptor)
	// 26 - 16 = 10 (5 of penalty)
	//TODO never tested on real hw
	CYCLES_PM(5);
	break;
}

/* 9B        WAIT           3        Wait until BUSY pin is inactive (HIGH) */
case 0x9B:
{
	m_instr.fn = &CPUExecutor::WAIT;
	CYCLES(3,3);
	break;
}

/* 9C         PUSHF           3          Push flags register */
case 0x9C:
{
	m_instr.fn = &CPUExecutor::PUSHF;
	CYCLES(3,3);
	break;
}

/* 9D         POPF            5          Pop top of stack into flags register */
case 0x9D:
{
	m_instr.fn = &CPUExecutor::POPF ;
	CYCLES(3,3);
	break;
}

/* 9E - SAHF     2      Store AH into flags */
case 0x9E:
{
	m_instr.fn = &CPUExecutor::SAHF;
	CYCLES(2,2);
	break;
}

/* 9F - LAHF     2         Load flags into AH */
case 0x9F:
{
	m_instr.fn = &CPUExecutor::LAHF;
	CYCLES(2,2);
	break;
}

/* A0 dw      MOV AL,xb   5          Move byte variable (offset dw) into AL */
case 0xA0:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::MOV_AL_xb;
	CYCLES(3,3);
	break;
}

/* A1 dw      MOV AX,xw   5         Move word variable (offset dw) into AX */
case 0xA1:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::MOV_AX_xw;
	CYCLES(3,3);
	break;
}

/* A2 dw      MOV xb,AL   3         Move AL into byte variable (offset dw) */
case 0xA2:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::MOV_xb_AL;
	CYCLES(3,3);
	break;
}

/* A3 dw      MOV xw,AX   3         Move AX into word register (offset dw) */
case 0xA3:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::MOV_xw_AX;
	CYCLES(3,3);
	break;
}

/* A4        MOVSB         5        Move byte DS:[SI] to ES:[DI] */
case 0xA4:
{
	m_instr.rep = m_rep;
	m_instr.fn = &CPUExecutor::MOVSB;
	CYCLES(3,3);
	m_instr.cycles.base_rep = 0;
	break;
}

/* A5        MOVSW         5        Move word DS:[SI] to ES:[DI] */
case 0xA5:
{
	m_instr.rep = m_rep;
	m_instr.fn = &CPUExecutor::MOVSW;
	CYCLES(3,3);
	m_instr.cycles.base_rep = 0;
	break;
}

/* A6        CMPSB         8         Compare bytes ES:[DI] from DS:[SI] */
case 0xA6:
{
	m_instr.rep = m_rep;
	m_instr.rep_zf = true;
	m_instr.fn = &CPUExecutor::CMPSB;
	CYCLES(4,4);
	m_instr.cycles.base_rep = 5;
	break;
}

/* A7        CMPSW         8         Compare words ES:[DI] from DS:[SI] */
case 0xA7:
{
	m_instr.rep = m_rep;
	m_instr.rep_zf = true;
	m_instr.fn = &CPUExecutor::CMPSW;
	CYCLES(4,4);
	m_instr.cycles.base_rep = 5;
	break;
}

/* A8 db      TEST AL,db    3       AND immediate byte into AL for flags only */
case 0xA8:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::TEST_AL_db;
	CYCLES(3,3);
	break;
}

/* A9 dw      TEST AX,dw    3       AND immediate word into AX for flags only */
case 0xA9:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::TEST_AX_dw;
	CYCLES(3,3);
	break;
}

/* AA       STOSB           3          Store AL to byte ES:[DI], advance DI */
case 0xAA:
{
	m_instr.rep = m_rep;
	m_instr.fn = &CPUExecutor::STOSB;
	CYCLES(3,3);
	m_instr.cycles.rep = 4;
	m_instr.cycles.base_rep = 0;
	break;
}

/* AB       STOSW           3          Store AX to word ES:[DI], advance DI */
case 0xAB:
{
	m_instr.rep = m_rep;
	m_instr.fn = &CPUExecutor::STOSW;
	CYCLES(3,3);
	m_instr.cycles.rep = 4;
	m_instr.cycles.base_rep = 0;
	break;
}

/* AC         LODSB             5         Load byte DS:[SI] into AL */
case 0xAC:
{
	m_instr.rep = m_rep;
	m_instr.fn = &CPUExecutor::LODSB;
	CYCLES(3,3);
	m_instr.cycles.base_rep = 2;
	break;
}

/* AD         LODSW             5         Load word DS:[SI] into AX */
case 0xAD:
{
	m_instr.rep = m_rep;
	m_instr.fn = &CPUExecutor::LODSW;
	CYCLES(3,3);
	m_instr.cycles.base_rep = 2;
	break;
}

/*  AE       SCASB         7        Compare bytes AL - ES:[DI], advance DI */
case 0xAE:
{
	m_instr.rep = m_rep;
	m_instr.rep_zf = true;
	m_instr.fn = &CPUExecutor::SCASB;
	CYCLES(5,5);
	m_instr.cycles.base_rep = 6;
	break;
}

/*  AF       SCASW         7        Compare words AX - ES:[DI], advance DI */
case 0xAF:
{
	m_instr.rep = m_rep;
	m_instr.rep_zf = true;
	m_instr.fn = &CPUExecutor::SCASW;
	CYCLES(5,5);
	m_instr.cycles.base_rep = 6;
	break;
}

/* B0+ rb db - MOV rb,db    2     Move imm byte into byte reg */
case 0xB0: //AL
case 0xB1: //CL
case 0xB2: //DL
case 0xB3: //BL
case 0xB4: //AH
case 0xB5: //CH
case 0xB6: //DH
case 0xB7: //BH
{
	m_instr.db = fetchb();
	m_instr.reg = _opcode - 0xB0;
	m_instr.fn = &CPUExecutor::MOV_rb_db;
	CYCLES(2,2);
	break;
}

/* B8+ rw dw - MOV rw,dw   2   Move imm w into w reg */
case 0xB8: //AX
case 0xB9: //CX
case 0xBA: //DX
case 0xBB: //BX
case 0xBC: //SP
case 0xBD: //BP
case 0xBE: //SI
case 0xBF: //DI
{
	m_instr.dw1 = fetchw();
	m_instr.reg = _opcode - 0xB8;
	m_instr.fn = &CPUExecutor::MOV_rw_dw;
	CYCLES(2,2);
	break;
}

/*
C0 /0 db  ROL eb,db    5,mem=8   Rotate 8-bit EA byte left db times
C0 /1 db  ROR eb,db    5,mem=8   Rotate 8-bit EA byte right db times
C0 /2 db  RCL eb,db    5,mem=8   Rotate 9-bits (CF, EA byte) left db times
C0 /3 db  RCR eb,db    5,mem=8   Rotate 9-bits (CF, EA byte) right db times
C0 /4 db  SAL eb,db    5,mem=8   Multiply EA byte by 2, db times
C0 /5 db  SHR eb,db    5,mem=8   Unsigned divide EA byte by 2, db times
C0 /7 db  SAR eb,db    5,mem=8   Signed divide EA byte by 2, db times
*/
case 0xC0: //eb,db
{
	m_instr.modrm.load();
	m_instr.db = fetchb();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::ROL_eb_db;
			break;
		case 1:
			m_instr.fn = &CPUExecutor::ROR_eb_db;
			break;
		case 2:
			m_instr.fn = &CPUExecutor::RCL_eb_db;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::RCR_eb_db;
			break;
		case 4:
		case 6: //SAL and SHL are the same
			m_instr.fn = &CPUExecutor::SAL_eb_db;
			break;
		case 5:
			m_instr.fn = &CPUExecutor::SHR_eb_db;
			break;
		case 7:
			m_instr.fn = &CPUExecutor::SAR_eb_db;
			break;
		default:
			illegal_opcode();
	}
	CYCLES(5,6);
	break;
}

/*
C1 /0 db  ROL ew,db    5,mem=8   Rotate 16-bit EA word left db times
C1 /1 db  ROR ew,db    5,mem=8   Rotate 16-bit EA word right db times
C1 /2 db  RCL ew,db    5,mem=8   Rotate 17-bits (CF, EA word) left db times
C1 /3 db  RCR ew,db    5,mem=8   Rotate 17-bits (CF, EA word) right db times
C1 /4 db  SAL ew,db    5,mem=8   Multiply EA word by 2, db times
C1 /5 db  SAR ew,db    5,mem=8   Unsigned divide EA word by 2, db times
C1 /7 db  SAR ew,db    5,mem=8   Signed divide EA word by 2, db times
*/
case 0xC1:
{
	m_instr.modrm.load();
	m_instr.db = fetchb();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::ROL_ew_db;
			break;
		case 1:
			m_instr.fn = &CPUExecutor::ROR_ew_db;
			break;
		case 2:
			m_instr.fn = &CPUExecutor::RCL_ew_db;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::RCR_ew_db;
			break;
		case 4:
		case 6: //SAL and SHL are the same
			m_instr.fn = &CPUExecutor::SAL_ew_db;
			break;
		case 5:
			m_instr.fn = &CPUExecutor::SHR_ew_db;
			break;
		case 7:
			m_instr.fn = &CPUExecutor::SAR_ew_db;
			break;
		default:
			illegal_opcode();
	}
	CYCLES(5,6);
	break;
}

/* C2 dw   RET dw       11    RET (near), same privilege, pop dw bytes pushed before Call*/
case 0xC2:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::RET_near;
	CYCLES(7,7);
	break;
}

/* C3      RET          11         Return to near caller, same privilege */
case 0xC3:
{
	m_instr.dw1 = 0;
	m_instr.fn = &CPUExecutor::RET_near;
	CYCLES(7,7);
	break;
}

/* C4 /r    LES rw,ed    7,pm=21   Load EA doubleword into ES and word register */
case 0xC4:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::LES_rw_ed;
	if(m_instr.modrm.mod_is_reg()) {
		illegal_opcode();
	}
	CYCLES(3,3);
	//for PM mode:
	// 4 cycles for pointer load (2 mem reads)
	// 8 cycles for decriptor load (4 mem reads)
	CYCLES_PM(6);
	break;
}

/* C5 /r    LDS rw,ed    7,pm=21   Load EA doubleword into DS and word register */
case 0xC5:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::LDS_rw_ed;
	if(m_instr.modrm.mod_is_reg()) {
		illegal_opcode();
	}
	CYCLES(3,3);
	CYCLES_PM(6);
	break;
}

/* C6 /0 db   MOV eb,db   2,mem=3       Move immediate byte into EA byte */
case 0xC6:
{
	m_instr.modrm.load();
	m_instr.db = fetchb();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::MOV_eb_db;
			break;
		default:
			illegal_opcode();
			break;
	}
	CYCLES(2,3);
	break;
}

/* C7 /0 dw   MOV ew,dw   2,mem=3       Move immediate word into EA word */
case 0xC7:
{
	m_instr.modrm.load();
	m_instr.dw1 = fetchw();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::MOV_ew_dw;
			break;
		default:
			illegal_opcode();
			break;
	}
	CYCLES(2,3);
	break;
}

/*
C8 dw 00   ENTER dw,0     11       Make stack frame for procedure parameters
C8 dw 01   ENTER dw,1     15       Make stack frame for procedure parameters
C8 dw db   ENTER dw,db    12+4db   Make stack frame for procedure parameters
*/
case 0xC8:
{
	m_instr.dw1 = fetchw();
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::ENTER;
	CYCLES(12,12);
	break;
}

/* C9         LEAVE                5             Set SP to BP, then POP BP */
case 0xC9:
{
	m_instr.fn = &CPUExecutor::LEAVE;
	CYCLES(3,3);
	break;
}

/*
CA dw   RET dw       15,pm=25   RET (far), same privilege, pop dw bytes
CA dw   RET dw       55         RET (far), lesser privilege, pop dw bytes
*/
case 0xCA:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::RET_far;
	CYCLES(11,11);
	//TODO consider the lesser provilege case?
	CYCLES_PM(7);
	break;
}

/*
CB      RET          15,pm=25   Return to far caller, same privilege
CB      RET          55         Return, lesser privilege, switch stacks
*/
case 0xCB:
{
	m_instr.dw1 = 0;
	m_instr.fn = &CPUExecutor::RET_far;
	CYCLES(11,11);
	//TODO consider the lesser provilege case
	CYCLES_PM(7);
	break;
}

/*
CC      INT 3         23 (real mode)        Interrupt 3 (trap to debugger)
CC      INT 3         40         Interrupt 3, protected mode, same privilege
CC      INT 3         78         Interrupt 3, protected mode, more privilege
CC      INT 3         167        Interrupt 3, protected mode, via task gate
*/
case 0xCC:
{
	m_instr.fn = &CPUExecutor::INT3;
	CYCLES(13,13);
	CYCLES_PM(7);
	break;
}

/*
CD db   INT db        23 (real mode)        Interrupt numbered by immediate byte
CD db   INT db        40         Interrupt, protected mode, same privilege
CD db   INT db        78         Interrupt, protected mode, more privilege
CD db   INT db        167        Interrupt, protected mode, via task gate
*/
case 0xCD:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::INT_db;
	CYCLES(13,13);
	CYCLES_PM(7);
	break;
}

/* CE      INTO          24,noj=3 */
case 0xCE:
{
	m_instr.fn = &CPUExecutor::INTO;
	break;
}

/*
CF        IRET       17,pm=31   Interrupt return (far return and pop flags)
CF        IRET       55         Interrupt return, lesser privilege
CF        IRET       169        Interrupt return, different task (NT=1)
*/
case 0xCF:
{
	m_instr.fn = &CPUExecutor::IRET;
	CYCLES(11,11);
	CYCLES_PM(7);
	break;
}


/*
D0 /0     ROL eb,1     2,mem=7   Rotate 8-bit EA byte left once
D0 /1     ROR eb,1     2,mem=7   Rotate 8-bit EA byte right once
D0 /2     RCL eb,1     2,mem=7   Rotate 9-bits (CF, EA byte) left once
D0 /3     RCR eb,1     2,mem=7   Rotate 9-bits (CF, EA byte) right once
D0 /4     SAL eb,1     2,mem=7   Multiply EA byte by 2, once
D0 /5     SHR eb,1     2,mem=7   Unsigned divide EA byte by 2, once
D0 /7     SAR eb,1     2,mem=7   Signed divide EA byte by 2, once
*/
case 0xD0:
{
	m_instr.modrm.load();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::ROL_eb_1;
			break;
		case 1:
			m_instr.fn = &CPUExecutor::ROR_eb_1;
			break;
		case 2:
			m_instr.fn = &CPUExecutor::RCL_eb_1;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::RCR_eb_1;
			break;
		case 4:
		case 6: //SAL and SHL are the same
			m_instr.fn = &CPUExecutor::SAL_eb_1;
			break;
		case 5:
			m_instr.fn = &CPUExecutor::SHR_eb_1;
			break;
		case 7:
			m_instr.fn = &CPUExecutor::SAR_eb_1;
			break;
		default:
			illegal_opcode();
	}
	//compensate the m_instr->cycles.extra = 1 in CPUExecutor
	CYCLES(1,7);
	break;
}

/*
D1 /0     ROL ew,1     2,mem=7   Rotate 16-bit EA word left once
D1 /1     ROR ew,1     2,mem=7   Rotate 16-bit EA word right once
D1 /2     RCL ew,1     2,mem=7   Rotate 17-bits (CF, EA word) left once
D1 /3     RCR ew,1     2,mem=7   Rotate 17-bits (CF, EA word) right once
D1 /4     SAL ew,1     2,mem=7   Multiply EA word by 2, once
D1 /5     SHR ew,1     2,mem=7   Unsigned divide EA word by 2, once
D1 /7     SAR ew,1     2,mem=7   Signed divide EA word by 2, once
*/
case 0xD1:
{
	m_instr.modrm.load();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::ROL_ew_1;
			break;
		case 1:
			m_instr.fn = &CPUExecutor::ROR_ew_1;
			break;
		case 2:
			m_instr.fn = &CPUExecutor::RCL_ew_1;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::RCR_ew_1;
			break;
		case 4:
		case 6: //SAL and SHL are the same
			m_instr.fn = &CPUExecutor::SAL_ew_1;
			break;
		case 5:
			m_instr.fn = &CPUExecutor::SHR_ew_1;
			break;
		case 7:
			m_instr.fn = &CPUExecutor::SAR_ew_1;
			break;
		default:
			illegal_opcode();
	}
	//compensate the m_instr->cycles.extra = 1 in CPUExecutor
	CYCLES(1,7);
	break;
}

/*
D2 /0     ROL eb,CL    5,mem=8   Rotate 8-bit EA byte left CL times
D2 /1     ROR eb,CL    5,mem=8   Rotate 8-bit EA byte right CL times
D2 /2     RCL eb,CL    5,mem=8   Rotate 9-bits (CF, EA byte) left CL times
D2 /3     RCR eb,CL    5,mem=8   Rotate 9-bits (CF, EA byte) right CL times
D2 /4     SAL eb,CL    5,mem=8   Multiply EA byte by 2, CL times
D2 /5     SHR eb,CL    5,mem=8   Unsigned divide EA byte by 2, CL times
D2 /7     SAR eb,CL    5,mem=8   Signed divide EA byte by 2, CL times
*/
case 0xD2:
{
	m_instr.modrm.load();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::ROL_eb_CL;
			break;
		case 1:
			m_instr.fn = &CPUExecutor::ROR_eb_CL;
			break;
		case 2:
			m_instr.fn = &CPUExecutor::RCL_eb_CL;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::RCR_eb_CL;
			break;
		case 4:
		case 6: //SAL and SHL are the same
			m_instr.fn = &CPUExecutor::SAL_eb_CL;
			break;
		case 5:
			m_instr.fn = &CPUExecutor::SHR_eb_CL;
			break;
		case 7:
			m_instr.fn = &CPUExecutor::SAR_eb_CL;
			break;
		default:
			illegal_opcode();
	}
	CYCLES(5,6);
	break;
}

/*
D3 /0     ROL ew,CL    5,mem=8   Rotate 16-bit EA word left CL times
D3 /1     ROR ew,CL    5,mem=8   Rotate 16-bit EA word right CL times
D3 /2     RCL ew,CL    5,mem=8   Rotate 17-bits (CF, EA word) left CL times
D3 /3     RCR ew,CL    5,mem=8   Rotate 17-bits (CF, EA word) right CL times
D3 /4     SAL ew,CL    5,mem=8   Multiply EA word by 2, CL times
D3 /5     SHR ew,CL    5,mem=8   Unsigned divide EA word by 2, CL times
D3 /7     SAR ew,CL    5,mem=8   Signed divide EA word by 2, CL times
*/
case 0xD3:
{
	m_instr.modrm.load();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::ROL_ew_CL;
			break;
		case 1:
			m_instr.fn = &CPUExecutor::ROR_ew_CL;
			break;
		case 2:
			m_instr.fn = &CPUExecutor::RCL_ew_CL;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::RCR_ew_CL;
			break;
		case 4:
		case 6: //SAL and SHL are the same
			m_instr.fn = &CPUExecutor::SAL_ew_CL;
			break;
		case 5:
			m_instr.fn = &CPUExecutor::SHR_ew_CL;
			break;
		case 7:
			m_instr.fn = &CPUExecutor::SAR_ew_CL;
			break;
		default:
			illegal_opcode();
	}
	CYCLES(5,6);
	break;
}

/* D4 db      AAM             16          ASCII adjust AX after multiply */
case 0xD4:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::AAM;
	CYCLES(16,16);
	break;
}

/* D5 db      AAD             14          ASCII adjust AX before division */
case 0xD5:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::AAD;
	CYCLES(14,14);
	break;
}

/* D6 SALC ?? Set AL If Carry */
case 0xD6:
{
	m_instr.fn = &CPUExecutor::SALC;
	CYCLES(1,1);
	break;
}

/* D7        XLATB        5        Set AL to memory byte DS:[BX + unsigned AL] */
case 0xD7:
{
	m_instr.fn = &CPUExecutor::XLATB;
	CYCLES(3,3);
	break;
}

/* FPU ESC */
case 0xD8:
case 0xD9:
case 0xDA:
case 0xDB:
case 0xDC:
case 0xDD:
case 0xDE:
case 0xDF:
{
	//TODO fpu support?
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::FPU_ESC;
	CYCLES(1,1);
	break;
}

/* E0 cb    LOOPNZ cb     8,noj=4  DEC CX; jump short if CX<>0 and ZF=0 */
case 0xE0:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::LOOPNZ;
	CYCLES(LOOP_CYCLES,LOOP_CYCLES);
	CYCLES_JCOND(4);
	break;
}

/* E1 cb    LOOPZ cb      8,noj=4  DEC CX; jump short if CX<>0 and zero (ZF=1) */
case 0xE1:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::LOOPZ;
	CYCLES(LOOP_CYCLES,LOOP_CYCLES);
	CYCLES_JCOND(4);
	break;
}

/* E2 cb    LOOP cb       8,noj=4  DEC CX; jump short if CX<>0 */
case 0xE2:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::LOOP;
	CYCLES(LOOP_CYCLES,LOOP_CYCLES);
	CYCLES_JCOND(4);
	break;
}

/* E3  cb     JCXZ cb    8,noj=4   Jump short if CX register is zero */
case 0xE3:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JCXZ_cb;
	CYCLES(LOOP_CYCLES,LOOP_CYCLES);
	CYCLES_JCOND(4);
	break;
}

/* E4 db     IN AL,db       5         Input byte from immediate port into AL */
case 0xE4:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::IN_AL_db;
	CYCLES(3,3);
	break;
}

/* E5 db     IN AX,db       5         Input word from immediate port into AX */
case 0xE5:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::IN_AX_db;
	CYCLES(3,3);
	break;
}

/* E6 db    OUT db,AL     3         Output byte AL to immediate port number db */
case 0xE6:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::OUT_db_AL;
	CYCLES(3,3);
	break;
}

/* E7 db    OUT db,AX     3         Output word AX to immediate port number db */
case 0xE7:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::OUT_db_AX;
	CYCLES(3,3);
	break;
}

/* E8 cw    CALL cw       7         Call near, offset relative to next instruction */
case 0xE8:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::CALL_cw;
	//TODO
	//are the cycles given in the intel docs inclusive of the time needed
	//to replenish the prefetch queue?
	CYCLES(1,1);
	break;
}

/* E9 cw   JMP cw   7   Jump near */
case 0xE9:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::JMP_cw;
	CYCLES(JUMP_CYCLES,JUMP_CYCLES);
	break;
}

/* EA cd   JMP cd   Jump far/task/call/tss */
case 0xEA:
{
	m_instr.dw1 = fetchw();
	m_instr.dw2 = fetchw();
	m_instr.fn = &CPUExecutor::JMP_cd;
	CYCLES(JUMP_CYCLES,JUMP_CYCLES);
	CYCLES_PM(6);
	break;
}

/* EB   cb     JMP cb         7            Jump short */
case 0xEB:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JMP_cb;
	CYCLES(JUMP_CYCLES,JUMP_CYCLES);
	break;
}

/* EC        IN AL,DX       5         Input byte from port DX into AL */
case 0xEC:
{
	m_instr.fn = &CPUExecutor::IN_AL_DX;
	CYCLES(5,5);
	break;
}

/* ED        IN AX,DX       5         Input word from port DX into AX */
case 0xED:
{
	m_instr.fn = &CPUExecutor::IN_AX_DX;
	CYCLES(5,5);
	break;
}

/* EE       OUT DX,AL     3         Output byte AL to port number DX */
case 0xEE:
{
	m_instr.fn = &CPUExecutor::OUT_DX_AL;
	CYCLES(3,3);
	break;
}

/* EF       OUT DX,AX     3         Output word AX to port number DX */
case 0xEF:
{
	m_instr.fn = &CPUExecutor::OUT_DX_AX;
	CYCLES(3,3);
	break;
}

/* F0  LOCK prefix */

/* F1 prefix, does not generate #UD; ICEBP on 386+ */

/* F2 REP/REPE prefix */

/* F3 REPNE prefix */

/* F4        HLT            2         Halt */
case 0xF4:
{
	m_instr.fn = &CPUExecutor::HLT;
	CYCLES(2,2);
	break;
}

/* F5         CMC            2          Complement carry flag */
case 0xF5:
{
	m_instr.fn = &CPUExecutor::CMC;
	CYCLES(2,2);
	break;
}

/*
F6 /0 db   TEST eb,db    3,mem=6     AND immediate byte into EA byte for flags only
F6 /2      NOT eb        2,mem=7     Reverse each bit of EA byte
F6 /3      NEG eb        2,mem=7     Two's complement negate EA byte
F6 /4      MUL eb        13,mem=16   Unsigned multiply (AX = AL * EA byte)
F6 /5      IMUL eb       13,mem=16   Signed multiply (AX = AL * EA byte)
F6 /6      DIV eb        14,mem=17   Unsigned divide AX by EA byte
F6 /7      IDIV eb       17,mem=20   Signed divide AX by EA byte (AL=Quo,AH=Rem)
*/
case 0xF6:
{
	m_instr.modrm.load();
	switch(m_instr.modrm.n) {
		case 0:
		case 1: // 1: undocumented alias
		{
			m_instr.db = fetchb();
			m_instr.fn = &CPUExecutor::TEST_eb_db;
			CYCLES(3,4);
			break;
		}
		case 2:
			m_instr.fn = &CPUExecutor::NOT_eb;
			CYCLES(2,5);
			break;
		case 3:
			m_instr.fn = &CPUExecutor::NEG_eb;
			CYCLES(2,5);
			break;
		case 4:
			m_instr.fn = &CPUExecutor::MUL_eb;
			CYCLES(13,14);
			break;
		case 5:
			m_instr.fn = &CPUExecutor::IMUL_eb;
			CYCLES(13,14);
			break;
		case 6:
			m_instr.fn = &CPUExecutor::DIV_eb;
			CYCLES(14,15);
			break;
		case 7:
			m_instr.fn = &CPUExecutor::IDIV_eb;
			CYCLES(17,18);
			break;
		default:
			illegal_opcode();
	}
	break;
}

/*
F7 /0 dw   TEST ew,dw    3,mem=6     AND immediate word into EA word for flags only
F7 /2      NOT ew        2,mem=7     Reverse each bit of EA word
F7 /3      NEG ew        2,mem=7     Two's complement negate EA word
F7 /4      MUL ew        21,mem=24   Unsigned multiply (DXAX = AX * EA word)
F7 /5      IMUL ew       21,mem=24   Signed multiply (DXAX = AX * EA word)
F7 /6      DIV ew        22,mem=25   Unsigned divide DX:AX by EA word
F7 /7      IDIV ew       25,mem=28   Signed divide DX:AX by EA word (AX=Quo,DX=Rem)
*/
case 0xF7:
{
	m_instr.modrm.load();
	switch(m_instr.modrm.n) {
		case 0:
		case 1: // 1: undocumented alias
		{
			m_instr.dw1 = fetchw();
			m_instr.fn = &CPUExecutor::TEST_ew_dw;
			CYCLES(3,4);
			break;
		}
		case 2:
			m_instr.fn = &CPUExecutor::NOT_ew;
			CYCLES(2,8);
			break;
		case 3:
			m_instr.fn = &CPUExecutor::NEG_ew;
			CYCLES(2,8);
			break;
		case 4:
			m_instr.fn = &CPUExecutor::MUL_ew;
			CYCLES(21,22);
			break;
		case 5:
			m_instr.fn = &CPUExecutor::IMUL_ew;
			CYCLES(21,22);
			break;
		case 6:
			m_instr.fn = &CPUExecutor::DIV_ew;
			CYCLES(22,23);
			break;
		case 7:
			m_instr.fn = &CPUExecutor::IDIV_ew;
			CYCLES(25,26);
			break;
		default:
			illegal_opcode();
	}
	break;
}

/* F8         CLC             2           Clear carry flag */
case 0xF8:
{
	m_instr.fn = &CPUExecutor::CLC;
	CYCLES(2,2);
	break;
}

/* F9         STC              2           Set carry flag */
case 0xF9:
{
	m_instr.fn = &CPUExecutor::STC;
	CYCLES(2,2);
	break;
}

/* FA      CLI          3       Clear interrupt flag; interrupts disabled */
case 0xFA:
{
	m_instr.fn = &CPUExecutor::CLI;
	CYCLES(3,3);
	break;
}

/* FB        STI        2      Set interrupt enable flag, interrupts enabled */
case 0xFB:
{
	m_instr.fn = &CPUExecutor::STI;
	CYCLES(2,2);
	break;
}

/* FC      CLD          2       Clear direction flag, SI and DI will increment */
case 0xFC:
{
	m_instr.fn = &CPUExecutor::CLD;
	CYCLES(2,2);
	break;
}

/* FD       STD         2        Set direction flag so SI and DI will decrement */
case 0xFD:
{
	m_instr.fn = &CPUExecutor::STD;
	CYCLES(2,2);
	break;
}

/*
FE   /0     INC eb         2,mem=7       Increment EA byte by 1
FE   /1     DEC eb         2,mem=7       Decrement EA byte by 1
*/
case 0xFE:
{
	m_instr.modrm.load();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::INC_eb;
			CYCLES(2,5);
			break;
		case 1:
			m_instr.fn = &CPUExecutor::DEC_eb;
			CYCLES(2,5);
			break;
		default:
			illegal_opcode();
			break;
	}
	break;
}

/*
FF /0     INC ew    2,mem=7    Increment EA word by 1
FF /1     DEC ew    2,mem=7    Decrement EA word by 1
FF /2     CALL ew   7,mem=11   Call near, offset absolute at EA word
FF /3     CALL ed   16,mem=29  Call inter-segment, address at EA doubleword
FF /3     CALL ed   44         Call gate, same privilege
FF /3     CALL ed   83         Call gate, more privilege, no parameters
FF /3     CALL ed   90+4X      Call gate, more privilege, X parameters
FF /3     CALL ed   180        Call via Task State Segment
FF /3     CALL ed   185        Call via task gate
FF /4     JMP ew    7,mem=11   Jump near to EA word (absolute offset)
FF /5     JMP ed    15,pm=26   Jump far (4-byte effective address in memory doubleword)
FF /5     JMP ed    41         Jump to call gate, same privilege
FF /5     JMP ed    178        Jump via Task State Segment
FF /5     JMP ed    183        Jump to task gate
FF /6     PUSH mw   5          Push memory word
*/
case 0xFF:
{
	m_instr.modrm.load();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::INC_ew;
			CYCLES(2,8);
			break;
		case 1:
			m_instr.fn = &CPUExecutor::DEC_ew;
			CYCLES(2,8);
			break;
		case 2:
			m_instr.fn = &CPUExecutor::CALL_ew;
			CYCLES(1,3);
			break;
		case 3:
			m_instr.fn = &CPUExecutor::CALL_ed;
			CYCLES(3,5);
			CYCLES_PM(7);
			break;
		case 4:
			m_instr.fn = &CPUExecutor::JMP_ew;
			CYCLES(JUMP_CYCLES,JUMP_CYCLES+2);
			break;
		case 5:
			CYCLES(JUMP_CYCLES+4,JUMP_CYCLES+4);
			CYCLES_PM(7);
			if(m_instr.modrm.mod == 3) {
				illegal_opcode();
				break;
			}
			m_instr.fn = &CPUExecutor::JMP_ed;
			break;
		case 6:
			m_instr.fn = &CPUExecutor::PUSH_mw;
			CYCLES(5,5);
			break;
		default:
			illegal_opcode();
			break;
	}
	break;
}

default:
{
	illegal_opcode();
}


} //switch

} //decode_prefix_none
