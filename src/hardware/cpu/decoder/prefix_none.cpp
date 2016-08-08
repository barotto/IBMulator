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


void CPUDecoder::prefix_none(uint8_t _opcode, unsigned &ctb_idx_, unsigned &ctb_op_)
{
	ctb_op_ = _opcode;
	ctb_idx_ = CTB_IDX_NONE;

switch(_opcode) {

/* 00 /r      ADD eb,rb   Add byte register into EA byte */
case 0x00:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::ADD_eb_rb;
	break;
}

/* 01 /r     ADD ew,rw   Add word register into EA word */
case 0x01:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::ADD_ew_rw;
	break;
}

/* 02 /r      ADD rb,eb   Add EA byte into byte register */
case 0x02:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::ADD_rb_eb;
	break;
}

/* 03 /r      ADD rw,ew   Add EA word into word register */
case 0x03:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::ADD_rw_ew;
	break;
}

/* 04 db      ADD AL,db   Add immediate byte into AL */
case 0x04:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::ADD_AL_db;
	break;
}

/* 05 dw      ADD AX,dw   Add immediate word into AX */
case 0x05:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::ADD_AX_dw;
	break;
}

/* 06         PUSH ES      Push ES */
case 0x06:
{
	m_instr.fn = &CPUExecutor::PUSH_ES;
	break;
}

/* 07          POP ES      Pop top of stack into ES */
case 0x07:
{
	m_instr.fn = &CPUExecutor::POP_ES;
	break;
}

/* 08 /r      OR eb,rb       Logical-OR byte register into EA byte */
case 0x08:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::OR_eb_rb;
	break;
}

/* 09 /r      OR ew,rw      Logical-OR word register into EA word */
case 0x09:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::OR_ew_rw;
	break;
}

/* 0A /r      OR rb,eb       Logical-OR EA byte into byte register */
case 0x0A:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::OR_rb_eb;
	break;
}

/* 0B /r      OR rw,ew       Logical-OR EA word into word register */
case 0x0B:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::OR_rw_ew;
	break;
}

/* 0C db      OR AL,db       Logical-OR immediate byte into AL */
case 0x0C:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::OR_AL_db;
	break;
}

/* 0D dw      OR AX,dw       Logical-OR immediate word into AX */
case 0x0D:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::OR_AX_dw;
	break;
}

/* 0E         PUSH CS      Push CS */
case 0x0E:
{
	m_instr.fn = &CPUExecutor::PUSH_CS;
	break;
}

/* 0F 2-byte opcode prefix */

/* 10 /r      ADC eb,rb   Add with carry byte register into EA byte */
case 0x10:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::ADC_eb_rb;
	break;
}

/* 11 /r      ADC ew,rw   Add with carry word register into EA word */
case 0x11:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::ADC_ew_rw;
	break;
}

/* 12 /r      ADC rb,eb   Add with carry EA byte into byte register */
case 0x12:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::ADC_rb_eb;
	break;
}

/* 13 /r      ADC rw,ew   Add with carry EA word into word register */
case 0x13:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::ADC_rw_ew;
	break;
}

/* 14 db      ADC AL,db   Add with carry immediate byte into AL */
case 0x14:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::ADC_AL_db;
	break;
}

/* 15 dw      ADC AX,dw   Add with carry immediate word into AX */
case 0x15:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::ADC_AX_dw;
	break;
}

/* 16         PUSH SS      Push SS */
case 0x16:
{
	m_instr.fn = &CPUExecutor::PUSH_SS;
	break;
}

/* 17          POP SS       Pop top of stack into SS */
case 0x17:
{
	m_instr.fn = &CPUExecutor::POP_SS;
	break;
}

/* 18 /r       SBB eb,rb    Subtract with borrow byte register from EA byte */
case 0x18:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::SBB_eb_rb;
	break;
}

/* 19 /r       SBB ew,rw    Subtract with borrow word register from EA word */
case 0x19:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::SBB_ew_rw;
	break;
}

/* 1A /r       SBB rb,eb    Subtract with borrow EA byte from byte register */
case 0x1A:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::SBB_rb_eb;
	break;
}

/* 1B /r       SBB rw,ew    Subtract with borrow EA word from word register */
case 0x1B:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::SBB_rw_ew;
	break;
}

/* 1C db       SBB AL,db    Subtract with borrow imm. byte from AL */
case 0x1C:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::SBB_AL_db;
	break;
}

/* 1D dw       SBB AX,dw    Subtract with borrow imm. word from AX */
case 0x1D:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::SBB_AX_dw;
	break;
}

/* 1E         PUSH DS      Push DS */
case 0x1E:
{
	m_instr.fn = &CPUExecutor::PUSH_DS;
	break;
}

/* 1F          POP DS       Pop top of stack into DS */
case 0x1F:
{
	m_instr.fn = &CPUExecutor::POP_DS;
	break;
}

/* 20 /r      AND eb,rb     Logical-AND byte register into EA byte */
case 0x20:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::AND_eb_rb;
	break;
}

/* 21 /r      AND ew,rw     Logical-AND word register into EA word */
case 0x21:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::AND_ew_rw;
	break;
}

/* 22 /r      AND rb,eb     Logical-AND EA byte into byte register */
case 0x22:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::AND_rb_eb;
	break;
}

/* 23 /r      AND rw,ew     Logical-AND EA word into word register */
case 0x23:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::AND_rw_ew;
	break;
}

/* 24 db      AND AL,db     Logical-AND immediate byte into AL */
case 0x24:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::AND_AL_db;
	break;
}

/* 25 dw      AND AX,dw     Logical-AND immediate word into AX */
case 0x25:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::AND_AX_dw;
	break;
}

/* 26  seg ovr prefix (ES) */

/* 27      DAA            Decimal adjust AL after addition */
case 0x27:
{
	m_instr.fn = &CPUExecutor::DAA;
	break;
}

/* 28 /r      SUB eb,rb     Subtract byte register from EA byte */
case 0x28:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::SUB_eb_rb;
	break;
}

/* 29 /r      SUB ew,rw     Subtract word register from EA word */
case 0x29:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::SUB_ew_rw;
	break;
}

/* 2A /r      SUB rb,eb      Subtract EA byte from byte register */
case 0x2A:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::SUB_rb_eb;
	break;
}

/* 2B /r      SUB rw,ew      Subtract EA word from word register */
case 0x2B:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::SUB_rw_ew;
	break;
}

/* 2C db      SUB AL,db      Subtract immediate byte from AL */
case 0x2C:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::SUB_AL_db;
	break;
}

/* 2D dw      SUB AX,dw      Subtract immediate word from AX */
case 0x2D:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::SUB_AX_dw;
	break;
}

/* 2E  seg ovr prefix (CS) */

/* 2F        DAS             Decimal adjust AL after subtraction */
case 0x2F:
{
	m_instr.fn = &CPUExecutor::DAS;
	break;
}

/* 30 /r     XOR eb,rb   Exclusive-OR byte register into EA byte */
case 0x30:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::XOR_eb_rb;
	break;
}

/* 31 /r     XOR ew,rw   Exclusive-OR word register into EA word */
case 0x31:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::XOR_ew_rw;
	break;
}

/* 32 /r     XOR rb,eb   Exclusive-OR EA byte into byte register */
case 0x32:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::XOR_rb_eb;
	break;
}

/* 33 /r     XOR rw,ew   Exclusive-OR EA word into word register */
case 0x33:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::XOR_rw_ew;
	break;
}

/* 34 db     XOR AL,db   Exclusive-OR immediate byte into AL */
case 0x34:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::XOR_AL_db;
	break;
}

/* 35 dw     XOR AX,dw   Exclusive-OR immediate word into AX */
case 0x35:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::XOR_AX_dw;
	break;
}

/* 36  seg ovr prefix (SS) */

/* 37        AAA         ASCII adjust AL after addition */
case 0x37:
{
	m_instr.fn = &CPUExecutor::AAA;
	break;
}

/* 38 /r        CMP eb,rb      Compare byte register from EA byte */
case 0x38:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::CMP_eb_rb;
	break;
}

/* 39 /r        CMP ew,rw     Compare word register from EA word */
case 0x39:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::CMP_ew_rw;
	break;
}

/* 3A /r        CMP rb,eb     Compare EA byte from byte register*/
case 0x3A:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::CMP_rb_eb;
	break;
}

/* 3B /r        CMP rw,ew     Compare EA word from word register */
case 0x3B:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::CMP_rw_ew;
	break;
}

/* 3C db        CMP AL,db      Compare immediate byte from AL */
case 0x3C:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::CMP_AL_db;
	break;
}

/* 3D dw        CMP AX,dw       Compare immediate word from AX */
case 0x3D:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::CMP_AX_dw;
	break;
}

/* 3E  seg ovr prefix (DS) */

/* 3F        AAS       ASCII adjust AL after subtraction */
case 0x3F:
{
	m_instr.fn = &CPUExecutor::AAS;
	break;
}

/* 40+ rw     INC rw    Increment word register by 1 */
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
	break;
}

/* 48+ rw     DEC rw      Decrement word register by 1 */
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
	break;
}

/*  50+ rw     PUSH rw     Push word register */
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
	break;
}

/* 58+ rw     POP rw        Pop top of stack into word register */
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
	break;
}

/* 60        PUSHA      Push in order: AX,CX,DX,BX,original SP,BP,SI,DI */
case 0x60:
{
	m_instr.fn = &CPUExecutor::PUSHA;
	break;
}

/* 61        POPA         Pop in order: DI,SI,BP,SP,BX,DX,CX,AX */
case 0x61:
{
	m_instr.fn = &CPUExecutor::POPA ;
	break;
}

/* 62 /r      BOUND rw,md     INT 5 if rw not within bounds */
case 0x62:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::BOUND_rw_md ;
	break;
}

/* 63 /r    ARPL ew,rw   Adjust RPL of EA word not less than RPL of rw */
case 0x63:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::ARPL_ew_rw;
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

/* 68  dw     PUSH dw      Push immediate word */
case 0x68:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::PUSH_dw;
	break;
}

/* 69 /r dw   IMUL rw,ew,dw     Signed multiply (rw = EA word * imm. word) */
case 0x69:
{
	m_instr.modrm.load();
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::IMUL_rw_ew_dw;
	break;
}

/* 6A  db     PUSH db      Push immediate sign-extended byte*/
case 0x6A:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::PUSH_db;
	break;
}

/* 6B /r db   IMUL rw,ew,db    Signed multiply (rw = EA word * imm. byte) */
case 0x6B:
{
	m_instr.modrm.load();
	m_instr.dw1 = int8_t(fetchb());
	m_instr.fn = &CPUExecutor::IMUL_rw_ew_dw;
	break;
}

/* 6C      INSB           Input byte from port DX into ES:[DI] */
case 0x6C:
{
	m_instr.rep = m_rep;
	m_instr.fn = &CPUExecutor::INSB;
	break;
}

/* 6D      INSW           Input word from port DX into ES:[DI] */
case 0x6D:
{
	m_instr.rep = m_rep;
	m_instr.fn = &CPUExecutor::INSW;
	break;
}

/* 6E      OUTSB          Output byte DS:[SI] to port number DX */
case 0x6E:
{
	m_instr.rep = m_rep;
	m_instr.fn = &CPUExecutor::OUTSB;
	break;
}

/* 6F      OUTSW          Output word DS:[SI] to port number DX */
case 0x6F:
{
	m_instr.rep = m_rep;
	m_instr.fn = &CPUExecutor::OUTSW;
	break;
}

/* 70  cb     JO cb      Jump short if overflow (OF=1) */
case 0x70:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JO_cb;
	break;
}

/* 71  cb     JNO cb     Jump short if notoverflow (OF=0) */
case 0x71:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JNO_cb;
	break;
}

/* 72  cb     JC cb      Jump short if carry (CF=1) */
case 0x72:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JC_cb;
	break;
}

/* 73  cb     JNC cb     Jump short if not carry (CF=0) */
case 0x73:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JNC_cb;
	break;
}

/* 74  cb     JE cb      Jump short if equal (ZF=1) */
case 0x74:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JE_cb;
	break;
}

/* 75  cb     JNE cb     Jump short if not equal (ZF=0) */
case 0x75:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JNE_cb;
	break;
}

/* 76  cb     JBE cb     Jump short if below or equal (CF=1 or ZF=1) */
case 0x76:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JBE_cb;
	break;
}

/* 77  cb     JA cb      Jump short if above (CF=0 and ZF=0) */
case 0x77:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JA_cb;
	break;
}

/* 78  cb     JS cb      Jump short if sign (SF=1) */
case 0x78:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JS_cb;
	break;
}

/* 79  cb     JNS cb     Jump short if not sign (SF=0) */
case 0x79:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JNS_cb;
	break;
}

/* 7A  cb     JPE cb     Jump short if parity even (PF=1) */
case 0x7A:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JPE_cb;
	break;
}

/* 7B  cb     JPO cb     Jump short if parity odd (PF=0) */
case 0x7B:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JPO_cb;
	break;
}

/* 7C  cb     JL cb      Jump short if less (SF/=OF) */
case 0x7C:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JL_cb;
	break;
}

/* 7D  cb     JNL cb     Jump short if not less (SF=OF) */
case 0x7D:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JNL_cb;
	break;
}

/* 7E  cb     JLE cb     Jump short if less or equal (ZF=1 or SF/=OF) */
case 0x7E:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JLE_cb;
	break;
}

/* 7F  cb     JNLE cb    Jump short if not less/equal (ZF=0 and SF=OF) */
case 0x7F:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JNLE_cb;
	break;
}

/*
80 /0 db   ADD eb,db    Add immediate byte into EA byte
80 /1 db   OR  eb,db    Logical-OR immediate byte  into EA byte
80 /2 db   ADC eb,db    Add with carry immediate byte into EA byte
80 /3 db   SBB eb,db    Subtract with borrow imm. byte from EA byte
80 /4 db   AND eb,db    Logical-AND immediate byte into EA byte
80 /5 db   SUB eb,db    Subtract immediate byte from EA byte
80 /6 db   XOR eb,db    Exclusive-OR immediate byte into EA byte
80 /7 db   CMP eb,db    Compare immediate byte from EA byte
*/
case 0x80:
case 0x82:
{
	m_instr.modrm.load();
	m_instr.db = fetchb();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::ADD_eb_db;
			break;
		case 1:
			m_instr.fn = &CPUExecutor::OR_eb_db;
			break;
		case 2:
			m_instr.fn = &CPUExecutor::ADC_eb_db;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::SBB_eb_db;
			break;
		case 4:
			m_instr.fn = &CPUExecutor::AND_eb_db;
			break;
		case 5:
			m_instr.fn = &CPUExecutor::SUB_eb_db;
			break;
		case 6:
			m_instr.fn = &CPUExecutor::XOR_eb_db;
			break;
		case 7:
			m_instr.fn = &CPUExecutor::CMP_eb_db;
			break;
		default:
			illegal_opcode();
	}
	ctb_op_ = m_instr.modrm.n;
	ctb_idx_ = CTB_IDX_80;
	break;
}

/*
81 /0 dw   ADD ew,dw    Add immediate word into EA word
81 /1 dw   OR  ew,dw    Logical-OR immediate word into EA word
81 /2 dw   ADC ew,dw    Add with carry immediate word into EA word
81 /3 dw   SBB ew,dw    Subtract with borrow imm. word from EA word
81 /4 dw   AND ew,dw    Logical-AND immediate word into EA word
81 /5 dw   SUB ew,dw    Subtract immediate word from EA word
81 /6 dw   XOR ew,dw    Exclusive-OR immediate word into EA word
81 /7 dw   CMP ew,dw    Compare immediate word from EA word
*/
case 0x81:
{
	m_instr.modrm.load();
	m_instr.dw1 = fetchw();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::ADD_ew_dw;
			break;
		case 1:
			m_instr.fn = &CPUExecutor::OR_ew_dw;
			break;
		case 2:
			m_instr.fn = &CPUExecutor::ADC_ew_dw;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::SBB_ew_dw;
			break;
		case 4:
			m_instr.fn = &CPUExecutor::AND_ew_dw;
			break;
		case 5:
			m_instr.fn = &CPUExecutor::SUB_ew_dw;
			break;
		case 6:
			m_instr.fn = &CPUExecutor::XOR_ew_dw;
			break;
		case 7:
			m_instr.fn = &CPUExecutor::CMP_ew_dw;
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
83 /0 db   ADD ew,db    Add immediate byte into EA word
83 /1 db   OR  ew,db    Logical-OR immediate byte into EA word (undocumented!)
83 /2 db   ADC ew,db    Add with carry immediate byte into EA word
83 /3 db   SBB ew,db    Subtract with borrow imm. byte from EA word
83 /4 db   AND ew,db    Logical-AND immediate byte into EA word (undocumented!)
83 /5 db   SUB ew,db    Subtract immediate byte from EA word
83 /6 db   XOR ew,db    Exclusive-OR immediate byte into EA word (undocumented!)
83 /7 db   CMP ew,db    Compare immediate byte from EA word
*/
case 0x83:
{
	m_instr.modrm.load();
	m_instr.db = fetchb();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::ADD_ew_db;
			break;
		case 1:
			m_instr.fn = &CPUExecutor::OR_ew_db;
			break;
		case 2:
			m_instr.fn = &CPUExecutor::ADC_ew_db;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::SBB_ew_db;
			break;
		case 4:
			m_instr.fn = &CPUExecutor::AND_ew_db;
			break;
		case 5:
			m_instr.fn = &CPUExecutor::SUB_ew_db;
			break;
		case 6:
			m_instr.fn = &CPUExecutor::XOR_ew_db;
			break;
		case 7:
			m_instr.fn = &CPUExecutor::CMP_ew_db;
			break;
		default:
			// according to the Intel's 286 user manual and http://ref.x86asm.net
			// 1,4,6 should be 386+ only, but the PS/1 BIOS uses them, so
			// they are clearly 286 opcodes too.
			illegal_opcode();
	}
	ctb_op_ = m_instr.modrm.n;
	ctb_idx_ = CTB_IDX_83;
	break;
}

/* 84 /r      TEST eb,rb    AND byte register into EA byte for flags only */
case 0x84:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::TEST_eb_rb;
	break;
}

/* 85 /r      TEST ew,rw    AND word register into EA word for flags only */
case 0x85:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::TEST_ew_rw;
	break;
}

/* 86 /r     XCHG eb,rb     Exchange byte register with EA byte */
case 0x86:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::XCHG_eb_rb;
	break;
}

/* 87 /r     XCHG ew,rw     Exchange word register with EA word */
case 0x87:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::XCHG_ew_rw;
	break;
}

/* 88 /r      MOV eb,rb   Move byte register into EA byte */
case 0x88:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::MOV_eb_rb;
	break;
}

/* 89 /r      MOV ew,rw   Move word register into EA word */
case 0x89:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::MOV_ew_rw;
	break;
}

/* 8A /r      MOV rb,eb   Move EA byte into byte register */
case 0x8A:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::MOV_rb_eb;
	break;
}

/* 8B /r      MOV rw,ew   Move EA word into word register */
case 0x8B:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::MOV_rw_ew;
	break;
}

/*
8C /0      MOV ew,ES      Move ES into EA word
8C /1      MOV ew,CS      Move CS into EA word
8C /2      MOV ew,SS      Move SS into EA word
8C /3      MOV ew,DS      Move DS into EA word
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
	break;
}

/* 8D /r    LEA rw,m       Calculate EA offset given by m, place in rw */
case 0x8D:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::LEA_rw_m;
	break;
}

/*
8E /0      MOV ES,mw    Move memory word into ES
8E /0      MOV ES,rw    Move word register into ES
8E /2      MOV SS,mw    Move memory word into SS
8E /2      MOV SS,rw    Move word register into SS
8E /3      MOV DS,mw    Move memory word into DS
8E /3      MOV DS,rw    Move word register into DS
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
	break;
}

/*
8F /0       POP mw          Pop top of stack into memory word
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
	break;
}

/* 90            NOP            No OPERATION */
case 0x90:
{
	m_instr.fn = &CPUExecutor::NOP;
	break;
}

/* 90+ rw    XCHG AX,rw     Exchange word register with AX */
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
	break;
}

/* 98       CBW           Convert byte into word (AH = top bit of AL) */
case 0x98:
{
	m_instr.fn = &CPUExecutor::CBW;
	break;
}

/* 99       CWD            Convert word to doubleword (DX:AX = AX) */
case 0x99:
{
	m_instr.fn = &CPUExecutor::CWD;
	break;
}

/* 9A cd     CALL cd       Call inter-segment, immediate 4-byte address */
case 0x9A:
{
	m_instr.dw1 = fetchw();
	m_instr.dw2 = fetchw();
	m_instr.fn = &CPUExecutor::CALL_cd;
	break;
}

/* 9B        WAIT           Wait until BUSY pin is inactive (HIGH) */
case 0x9B:
{
	m_instr.fn = &CPUExecutor::WAIT;
	break;
}

/* 9C         PUSHF         Push flags register */
case 0x9C:
{
	m_instr.fn = &CPUExecutor::PUSHF;
	break;
}

/* 9D         POPF           Pop top of stack into flags register */
case 0x9D:
{
	m_instr.fn = &CPUExecutor::POPF ;
	break;
}

/* 9E - SAHF     Store AH into flags */
case 0x9E:
{
	m_instr.fn = &CPUExecutor::SAHF;
	break;
}

/* 9F - LAHF     Load flags into AH */
case 0x9F:
{
	m_instr.fn = &CPUExecutor::LAHF;
	break;
}

/* A0 dw      MOV AL,xb    Move byte variable (offset dw) into AL */
case 0xA0:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::MOV_AL_xb;
	break;
}

/* A1 dw      MOV AX,xw      Move word variable (offset dw) into AX */
case 0xA1:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::MOV_AX_xw;
	break;
}

/* A2 dw      MOV xb,AL       Move AL into byte variable (offset dw) */
case 0xA2:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::MOV_xb_AL;
	break;
}

/* A3 dw      MOV xw,AX       Move AX into word register (offset dw) */
case 0xA3:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::MOV_xw_AX;
	break;
}

/* A4        MOVSB         Move byte DS:[SI] to ES:[DI] */
case 0xA4:
{
	m_instr.rep = m_rep;
	m_instr.fn = &CPUExecutor::MOVSB;
	break;
}

/* A5        MOVSW         Move word DS:[SI] to ES:[DI] */
case 0xA5:
{
	m_instr.rep = m_rep;
	m_instr.fn = &CPUExecutor::MOVSW;
	break;
}

/* A6        CMPSB         Compare bytes ES:[DI] from DS:[SI] */
case 0xA6:
{
	m_instr.rep = m_rep;
	m_instr.rep_zf = true;
	m_instr.fn = &CPUExecutor::CMPSB;
	break;
}

/* A7        CMPSW         Compare words ES:[DI] from DS:[SI] */
case 0xA7:
{
	m_instr.rep = m_rep;
	m_instr.rep_zf = true;
	m_instr.fn = &CPUExecutor::CMPSW;
	break;
}

/* A8 db      TEST AL,db    AND immediate byte into AL for flags only */
case 0xA8:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::TEST_AL_db;
	break;
}

/* A9 dw      TEST AX,dw    AND immediate word into AX for flags only */
case 0xA9:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::TEST_AX_dw;
	break;
}

/* AA       STOSB           Store AL to byte ES:[DI], advance DI */
case 0xAA:
{
	m_instr.rep = m_rep;
	m_instr.fn = &CPUExecutor::STOSB;
	break;
}

/* AB       STOSW           Store AX to word ES:[DI], advance DI */
case 0xAB:
{
	m_instr.rep = m_rep;
	m_instr.fn = &CPUExecutor::STOSW;
	break;
}

/* AC         LODSB            Load byte DS:[SI] into AL */
case 0xAC:
{
	m_instr.rep = m_rep;
	m_instr.fn = &CPUExecutor::LODSB;
	break;
}

/* AD         LODSW             Load word DS:[SI] into AX */
case 0xAD:
{
	m_instr.rep = m_rep;
	m_instr.fn = &CPUExecutor::LODSW;
	break;
}

/*  AE       SCASB         Compare bytes AL - ES:[DI], advance DI */
case 0xAE:
{
	m_instr.rep = m_rep;
	m_instr.rep_zf = true;
	m_instr.fn = &CPUExecutor::SCASB;
	break;
}

/*  AF       SCASW         Compare words AX - ES:[DI], advance DI */
case 0xAF:
{
	m_instr.rep = m_rep;
	m_instr.rep_zf = true;
	m_instr.fn = &CPUExecutor::SCASW;
	break;
}

/* B0+ rb db - MOV rb,db    Move imm byte into byte reg */
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
	break;
}

/* B8+ rw dw - MOV rw,dw   Move imm w into w reg */
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
	break;
}

/*
C0 /0 db  ROL eb,db    Rotate 8-bit EA byte left db times
C0 /1 db  ROR eb,db    Rotate 8-bit EA byte right db times
C0 /2 db  RCL eb,db    Rotate 9-bits (CF, EA byte) left db times
C0 /3 db  RCR eb,db    Rotate 9-bits (CF, EA byte) right db times
C0 /4 db  SAL eb,db    Multiply EA byte by 2, db times
C0 /5 db  SHR eb,db    Unsigned divide EA byte by 2, db times
C0 /7 db  SAR eb,db    Signed divide EA byte by 2, db times
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
	ctb_op_ = m_instr.modrm.n;
	ctb_idx_ = CTB_IDX_C0;
	break;
}

/*
C1 /0 db  ROL ew,db    Rotate 16-bit EA word left db times
C1 /1 db  ROR ew,db    Rotate 16-bit EA word right db times
C1 /2 db  RCL ew,db    Rotate 17-bits (CF, EA word) left db times
C1 /3 db  RCR ew,db    Rotate 17-bits (CF, EA word) right db times
C1 /4 db  SAL ew,db    Multiply EA word by 2, db times
C1 /5 db  SHR ew,db    Unsigned divide EA word by 2, db times
C1 /7 db  SAR ew,db    Signed divide EA word by 2, db times
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
	ctb_op_ = m_instr.modrm.n;
	ctb_idx_ = CTB_IDX_C1;
	break;
}

/* C2 dw   RET dw      RET (near), same privilege, pop dw bytes pushed before Call*/
case 0xC2:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::RET_near;
	break;
}

/* C3      RET          Return to near caller, same privilege */
case 0xC3:
{
	m_instr.dw1 = 0;
	m_instr.fn = &CPUExecutor::RET_near;
	break;
}

/* C4 /r    LES rw,ed    Load EA doubleword into ES and word register */
case 0xC4:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::LES_rw_ed;
	if(m_instr.modrm.mod_is_reg()) {
		illegal_opcode();
	}
	break;
}

/* C5 /r    LDS rw,ed    Load EA doubleword into DS and word register */
case 0xC5:
{
	m_instr.modrm.load();
	m_instr.fn = &CPUExecutor::LDS_rw_ed;
	if(m_instr.modrm.mod_is_reg()) {
		illegal_opcode();
	}
	break;
}

/* C6 /0 db   MOV eb,db   Move immediate byte into EA byte */
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
	break;
}

/* C7 /0 dw   MOV ew,dw   Move immediate word into EA word */
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
	break;
}

/* C8 dw db   ENTER dw,db   Make stack frame for procedure parameters */
case 0xC8:
{
	m_instr.dw1 = fetchw();
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::ENTER;
	break;
}

/* C9         LEAVE      Set SP to BP, then POP BP */
case 0xC9:
{
	m_instr.fn = &CPUExecutor::LEAVE;
	break;
}

/* CA dw   RET dw       RET (far), pop dw bytes */
case 0xCA:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::RET_far;
	break;
}

/* CB      RET          Return to far caller */
case 0xCB:
{
	m_instr.dw1 = 0;
	m_instr.fn = &CPUExecutor::RET_far;
	break;
}

/* CC      INT 3         Interrupt 3 (trap to debugger) */
case 0xCC:
{
	m_instr.fn = &CPUExecutor::INT3;
	break;
}

/* CD db   INT db        Interrupt numbered by immediate byte */
case 0xCD:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::INT_db;
	break;
}

/* CE      INTO      Interrupt 4  */
case 0xCE:
{
	m_instr.fn = &CPUExecutor::INTO;
	break;
}

/* CF        IRET    Interrupt return (far return and pop flags) */
case 0xCF:
{
	m_instr.fn = &CPUExecutor::IRET;
	break;
}

/*
D0 /0     ROL eb,1     Rotate 8-bit EA byte left once
D0 /1     ROR eb,1     Rotate 8-bit EA byte right once
D0 /2     RCL eb,1     Rotate 9-bits (CF, EA byte) left once
D0 /3     RCR eb,1     Rotate 9-bits (CF, EA byte) right once
D0 /4     SAL eb,1     Multiply EA byte by 2, once
D0 /5     SHR eb,1     Unsigned divide EA byte by 2, once
D0 /7     SAR eb,1     Signed divide EA byte by 2, once
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
	ctb_op_ = m_instr.modrm.n;
	ctb_idx_ = CTB_IDX_D0;
	break;
}

/*
D1 /0     ROL ew,1    Rotate 16-bit EA word left once
D1 /1     ROR ew,1    Rotate 16-bit EA word right once
D1 /2     RCL ew,1    Rotate 17-bits (CF, EA word) left once
D1 /3     RCR ew,1    Rotate 17-bits (CF, EA word) right once
D1 /4     SAL ew,1    Multiply EA word by 2, once
D1 /5     SHR ew,1    Unsigned divide EA word by 2, once
D1 /7     SAR ew,1    Signed divide EA word by 2, once
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
D2 /7     SAR eb,CL    Signed divide EA byte by 2, CL times
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
	ctb_op_ = m_instr.modrm.n;
	ctb_idx_ = CTB_IDX_D2;
	break;
}

/*
D3 /0     ROL ew,CL    Rotate 16-bit EA word left CL times
D3 /1     ROR ew,CL    Rotate 16-bit EA word right CL times
D3 /2     RCL ew,CL    Rotate 17-bits (CF, EA word) left CL times
D3 /3     RCR ew,CL    Rotate 17-bits (CF, EA word) right CL times
D3 /4     SAL ew,CL    Multiply EA word by 2, CL times
D3 /5     SHR ew,CL    Unsigned divide EA word by 2, CL times
D3 /7     SAR ew,CL    Signed divide EA word by 2, CL times
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
	ctb_op_ = m_instr.modrm.n;
	ctb_idx_ = CTB_IDX_D3;
	break;
}

/* D4 db      AAM        ASCII adjust AX after multiply */
case 0xD4:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::AAM;
	break;
}

/* D5 db      AAD         ASCII adjust AX before division */
case 0xD5:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::AAD;
	break;
}

/* D6         SALC        Set AL If Carry */
case 0xD6:
{
	m_instr.fn = &CPUExecutor::SALC;
	break;
}

/* D7        XLATB        Set AL to memory byte DS:[BX + unsigned AL] */
case 0xD7:
{
	m_instr.fn = &CPUExecutor::XLATB;
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
	break;
}

/* E0 cb    LOOPNZ cb     DEC CX; jump short if CX<>0 and ZF=0 */
case 0xE0:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::LOOPNZ;
	break;
}

/* E1 cb    LOOPZ cb      DEC CX; jump short if CX<>0 and zero (ZF=1) */
case 0xE1:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::LOOPZ;
	break;
}

/* E2 cb    LOOP cb       DEC CX; jump short if CX<>0 */
case 0xE2:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::LOOP;
	break;
}

/* E3  cb     JCXZ cb    Jump short if CX register is zero */
case 0xE3:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JCXZ_cb;
	break;
}

/* E4 db     IN AL,db     Input byte from immediate port into AL */
case 0xE4:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::IN_AL_db;
	break;
}

/* E5 db     IN AX,db      Input word from immediate port into AX */
case 0xE5:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::IN_AX_db;
	break;
}

/* E6 db    OUT db,AL     Output byte AL to immediate port number db */
case 0xE6:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::OUT_db_AL;
	break;
}

/* E7 db    OUT db,AX     Output word AX to immediate port number db */
case 0xE7:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::OUT_db_AX;
	break;
}

/* E8 cw    CALL cw       Call near, offset relative to next instruction */
case 0xE8:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::CALL_cw;
	break;
}

/* E9 cw   JMP cw   ump near */
case 0xE9:
{
	m_instr.dw1 = fetchw();
	m_instr.fn = &CPUExecutor::JMP_cw;
	break;
}

/* EA cd   JMP cd   Jump far/task/call/tss */
case 0xEA:
{
	m_instr.dw1 = fetchw();
	m_instr.dw2 = fetchw();
	m_instr.fn = &CPUExecutor::JMP_cd;
	break;
}

/* EB   cb     JMP cb         ump short */
case 0xEB:
{
	m_instr.db = fetchb();
	m_instr.fn = &CPUExecutor::JMP_cb;
	break;
}

/* EC        IN AL,DX       Input byte from port DX into AL */
case 0xEC:
{
	m_instr.fn = &CPUExecutor::IN_AL_DX;
	break;
}

/* ED        IN AX,DX       Input word from port DX into AX */
case 0xED:
{
	m_instr.fn = &CPUExecutor::IN_AX_DX;
	break;
}

/* EE       OUT DX,AL       Output byte AL to port number DX */
case 0xEE:
{
	m_instr.fn = &CPUExecutor::OUT_DX_AL;
	break;
}

/* EF       OUT DX,AX     Output word AX to port number DX */
case 0xEF:
{
	m_instr.fn = &CPUExecutor::OUT_DX_AX;
	break;
}

/* F0  LOCK prefix */

/* F1 prefix, does not generate #UD; ICEBP on 386+ */

/* F2 REP/REPE prefix */

/* F3 REPNE prefix */

/* F4        HLT            Halt */
case 0xF4:
{
	m_instr.fn = &CPUExecutor::HLT;
	break;
}

/* F5         CMC            Complement carry flag */
case 0xF5:
{
	m_instr.fn = &CPUExecutor::CMC;
	break;
}

/*
F6 /0 db   TEST eb,db    AND immediate byte into EA byte for flags only
F6 /2      NOT eb        Reverse each bit of EA byte
F6 /3      NEG eb        Two's complement negate EA byte
F6 /4      MUL eb        Unsigned multiply (AX = AL * EA byte)
F6 /5      IMUL eb       Signed multiply (AX = AL * EA byte)
F6 /6      DIV eb        Unsigned divide AX by EA byte
F6 /7      IDIV eb       Signed divide AX by EA byte (AL=Quo,AH=Rem)
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
			break;
		}
		case 2:
			m_instr.fn = &CPUExecutor::NOT_eb;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::NEG_eb;
			break;
		case 4:
			m_instr.fn = &CPUExecutor::MUL_eb;
			break;
		case 5:
			m_instr.fn = &CPUExecutor::IMUL_eb;
			break;
		case 6:
			m_instr.fn = &CPUExecutor::DIV_eb;
			break;
		case 7:
			m_instr.fn = &CPUExecutor::IDIV_eb;
			break;
		default:
			illegal_opcode();
	}
	ctb_op_ = m_instr.modrm.n;
	ctb_idx_ = CTB_IDX_F6;
	break;
}

/*
F7 /0 dw   TEST ew,dw    AND immediate word into EA word for flags only
F7 /2      NOT ew        Reverse each bit of EA word
F7 /3      NEG ew        Two's complement negate EA word
F7 /4      MUL ew        Unsigned multiply (DXAX = AX * EA word)
F7 /5      IMUL ew       Signed multiply (DXAX = AX * EA word)
F7 /6      DIV ew        Unsigned divide DX:AX by EA word
F7 /7      IDIV ew       Signed divide DX:AX by EA word (AX=Quo,DX=Rem)
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
			break;
		}
		case 2:
			m_instr.fn = &CPUExecutor::NOT_ew;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::NEG_ew;
			break;
		case 4:
			m_instr.fn = &CPUExecutor::MUL_ew;
			break;
		case 5:
			m_instr.fn = &CPUExecutor::IMUL_ew;
			break;
		case 6:
			m_instr.fn = &CPUExecutor::DIV_ew;
			break;
		case 7:
			m_instr.fn = &CPUExecutor::IDIV_ew;
			break;
		default:
			illegal_opcode();
	}
	ctb_op_ = m_instr.modrm.n;
	ctb_idx_ = CTB_IDX_F7;
	break;
}

/* F8         CLC            Clear carry flag */
case 0xF8:
{
	m_instr.fn = &CPUExecutor::CLC;
	break;
}

/* F9         STC             Set carry flag */
case 0xF9:
{
	m_instr.fn = &CPUExecutor::STC;
	break;
}

/* FA      CLI          Clear interrupt flag; interrupts disabled */
case 0xFA:
{
	m_instr.fn = &CPUExecutor::CLI;
	break;
}

/* FB        STI        Set interrupt enable flag, interrupts enabled */
case 0xFB:
{
	m_instr.fn = &CPUExecutor::STI;
	break;
}

/* FC      CLD          Clear direction flag, SI and DI will increment */
case 0xFC:
{
	m_instr.fn = &CPUExecutor::CLD;
	break;
}

/* FD       STD         Set direction flag so SI and DI will decrement */
case 0xFD:
{
	m_instr.fn = &CPUExecutor::STD;
	break;
}

/*
FE   /0     INC eb         Increment EA byte by 1
FE   /1     DEC eb         Decrement EA byte by 1
*/
case 0xFE:
{
	m_instr.modrm.load();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::INC_eb;
			break;
		case 1:
			m_instr.fn = &CPUExecutor::DEC_eb;
			break;
		default:
			illegal_opcode();
			break;
	}
	ctb_op_ = m_instr.modrm.n;
	ctb_idx_ = CTB_IDX_FE;
	break;
}

/*
FF /0     INC ew    Increment EA word by 1
FF /1     DEC ew    Decrement EA word by 1
FF /2     CALL ew   Call near, offset absolute at EA word
FF /3     CALL ed   Call inter-segment, address at EA doubleword
FF /4     JMP ew    Jump near to EA word (absolute offset)
FF /5     JMP ed    Jump far (4-byte effective address in memory doubleword)
FF /6     PUSH mw   Push memory word
*/
case 0xFF:
{
	m_instr.modrm.load();
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::INC_ew;
			break;
		case 1:
			m_instr.fn = &CPUExecutor::DEC_ew;
			break;
		case 2:
			m_instr.fn = &CPUExecutor::CALL_ew;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::CALL_ed;
			break;
		case 4:
			m_instr.fn = &CPUExecutor::JMP_ew;
					break;
		case 5:
			if(m_instr.modrm.mod == 3) {
				illegal_opcode();
				break;
			}
			m_instr.fn = &CPUExecutor::JMP_ed;
			break;
		case 6:
			m_instr.fn = &CPUExecutor::PUSH_mw;
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

} //decode_prefix_none
