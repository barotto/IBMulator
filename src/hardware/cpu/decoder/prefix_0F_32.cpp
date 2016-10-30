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

#define PREFIX_0F return prefix_0F(_opcode, ctb_idx_, ctb_op_)

void CPUDecoder::prefix_0F_32(uint8_t _opcode, unsigned &ctb_idx_, unsigned &ctb_op_)
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
case 0x00: PREFIX_0F;

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
	m_instr.modrm.load(m_instr.addr32);
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::SGDT_o32;
			break;
		case 1:
			m_instr.fn = &CPUExecutor::SIDT_o32;
			break;
		case 2:
			m_instr.fn = &CPUExecutor::LGDT_o32;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::LIDT_o32;
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

/* 0F 02 /r   LAR rd,ew    Load: high(rd)= Access Rights byte, selector ew */
case 0x02:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::LAR_rd_ew;
	break;
}

/* 0F 03 /r     LSL rd,ew   Load: rd = Segment Limit, selector ew */
case 0x03:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::LSL_rd_ew;
	break;
}

case 0x05: /* 0F 05      286 LOADALL            Load CPU registers from memory */
case 0x06: /* 0F 06      CLTS                   Clear task switched flag */
case 0x07: /* 0F 07      386 LOADALL            Load CPU registers from memory */
case 0x20: /* 0F 20 /r   MOV r32,CR0/CR2/CR3    Move (control register) to (register) */
case 0x21: /* 0F 21 /r   MOV r32,DRx            Move (debug register) to (register) */
case 0x22: /* 0F 22 /r   MOV CR0/CR2/CR3,r32    Move (register) to (control register)  */
case 0x23: /* 0F 23 /r   MOV DR0 -- 3,r32       Move (register) to (debug register) */
case 0x24: /* 0F 24 /r   MOV r32,TR6/TR7        Move (test register) to (register) */
case 0x26: /* 0F 26 /r   MOV TR6/TR7,r32        Move (register) to (test register) */
	PREFIX_0F;

/* 0F 80 cd    JO rel32         Jump near if overflow (OF=1)                    */
case 0x80:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::JO_cd;
	break;
}

/* 0F 81 cd   JNO  rel32       Jump near if not overflow (OF=0)                */
case 0x81:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::JNO_cd;
	break;
}

/* 0F 82 cd   JC   rel32       Jump near if carry (CF=1)                       */
case 0x82:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::JC_cd;
	break;
}

/* 0F 83 cd   JNC  rel32       Jump near if not carry (CF=0)                   */
case 0x83:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::JNC_cd;
	break;
}

/* 0F 84 cd   JE   rel32       Jump near if equal (ZF=1)                       */
case 0x84:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::JE_cd;
	break;
}

/* 0F 85 cd   JNE  rel32       Jump near if not equal (ZF=0)                   */
case 0x85:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::JNE_cd;
	break;
}

/* 0F 86 cd   JBE  rel32       Jump near if below or equal (CF=1 or ZF=1)      */
case 0x86:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::JBE_cd;
	break;
}

/* 0F 87 cd   JA   rel32       Jump near if above (CF=0 and ZF=0)              */
case 0x87:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::JA_cd;
	break;
}

/* 0F 88 cd   JS   rel32       Jump near if sign (SF=1)                        */
case 0x88:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::JS_cd;
	break;
}

/* 0F 89 cd   JNS  rel32       Jump near if not sign (SF=0)                    */
case 0x89:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::JNS_cd;
	break;
}

/* 0F 8A cd   JPE  rel32       Jump near if parity even (PF=1)                 */
case 0x8A:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::JPE_cd;
	break;
}

/* 0F 8B cd   JPO  rel32       Jump near if parity odd (PF=0)                  */
case 0x8B:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::JPO_cd;
	break;
}

/* 0F 8C cd   JL   rel32       Jump near if less (SF<>OF)                      */
case 0x8C:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::JL_cd;
	break;
}

/* 0F 8D cd   JNL  rel32       Jump near if not less (SF=OF)                   */
case 0x8D:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::JNL_cd;
	break;
}

/* 0F 8E cd   JLE  rel32       Jump near if less or equal (ZF=1 or SF<>OF)    */
case 0x8E:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::JLE_cd;
	break;
}

/* 0F 8F cd   JNLE rel32       Jump near if not less or equal (ZF=0 and SF=OF) */
case 0x8F:
{
	m_instr.id1 = fetchdw();
	m_instr.fn = &CPUExecutor::JNLE_cd;
	break;
}

case 0x90: /* 0F 90   SETO r/m8     Set byte if overflow (OF=1)                    */
case 0x91: /* 0F 91   SETNO r/m8    Set byte if not overflow (OF=0)                */
case 0x92: /* 0F 92   SETB r/m8     Set byte if below (CF=1)                       */
case 0x93: /* 0F 93   SETNB r/m8    Set byte if not below (CF=0)                   */
case 0x94: /* 0F 94   SETE r/m8     Set byte if equal (ZF=1)                       */
case 0x95: /* 0F 95   SETNE r/m8    Set byte if not equal (ZF=0)                   */
case 0x96: /* 0F 96   SETBE r/m8    Set byte if below or equal (CF=1 or ZF=1)      */
case 0x97: /* 0F 97   SETNBE r/m8   Set byte if not below or equal (CF=0 and ZF=0) */
case 0x98: /* 0F 98   SETS r/m8     Set byte if sign (SF=1)                        */
case 0x99: /* 0F 99   SETNS r/m8    Set byte if not sign (SF=0)                    */
case 0x9A: /* 0F 9A   SETP r/m8     Set byte if parity (PF=1)                      */
case 0x9B: /* 0F 9B   SETNP r/m8    Set byte if not parity (PF=0)                  */
case 0x9C: /* 0F 9C   SETL r/m8     Set byte if less (SF<>OF)                      */
case 0x9D: /* 0F 9D   SETNL r/m8    Set byte if not less (SF=OF)                   */
case 0x9E: /* 0F 9E   SETLE r/m8    Set byte if less or equal (ZF=1 or SF<>OF)     */
case 0x9F: /* 0F 9F   SETNLE r/m8   Set byte if not less or equal (ZF=0 and SF=OF) */
	PREFIX_0F;

/* 0F A0           PUSH FS                  Push FS                                       */
case 0xA0:
{
	m_instr.reg = REGI_FS;
	m_instr.fn = &CPUExecutor::PUSH_SR_dw;
	break;
}

/* 0F A1           POP FS                   Pop top of stack into FS                      */
case 0xA1:
{
	m_instr.reg = REGI_FS;
	m_instr.fn = &CPUExecutor::POP_SR_dw;
	break;
}

/* 0F A3           BT r/m32,r32             Save bit in carry flag                        */
case 0xA3:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::BT_ed_rd;
	break;
}

/* 0F A4           SHLD r/m32,r32,imm8      r/m32 gets SHL of r/m32 concatenated with r32 */
case 0xA4:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.ib = fetchb();
	m_instr.fn = &CPUExecutor::SHLD_ed_rd_ib;
	break;
}

/* 0F A5           SHLD r/m32,r32,CL        r/m32 gets SHL of r/m32 concatenated with r32 */
case 0xA5:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::SHLD_ed_rd_CL;
	break;
}

/* OF A8           PUSH GS                  Push GS                                       */
case 0xA8:
{
	m_instr.reg = REGI_GS;
	m_instr.fn = &CPUExecutor::PUSH_SR_dw;
	break;
}

/* 0F A9           POP GS                   Pop top of stack into GS                      */
case 0xA9:
{
	m_instr.reg = REGI_GS;
	m_instr.fn = &CPUExecutor::POP_SR_dw;
	break;
}

/* 0F AB           BTS r/m32,r32            Save bit in carry flag and set                */
case 0xAB:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::BTS_ed_rd;
	break;
}

/* 0F AC           SHRD r/m32,r32,imm8      r/m32 gets SHR of r/m32 concatenated with r32 */
case 0xAC:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.ib = fetchb();
	m_instr.fn = &CPUExecutor::SHRD_ed_rd_ib;
	break;
}

/* 0F AD           SHRD r/m32,r32,CL        r/m32 gets SHR of r/m32 concatenated with r32 */
case 0xAD:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::SHRD_ed_rd_CL;
	break;
}

/* 0F AF /r        IMUL r32,r/m32           dword register = dword register * r/m dword      */
case 0xAF:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::IMUL_rd_ed;
	break;
}

/* 0F B2 /r        LSS r32,m16:32           Load SS:r32 with pointer from memory          */
case 0xB2:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::LSS_rd_mp;
	break;
}

/* 0F B3           BTR r/m32,r32            Save bit in carry flag and reset              */
case 0xB3:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::BTR_ed_rd;
	break;
}

/* 0F B4 /r        LFS r32,m16:32           Load FS:r32 with pointer from memory          */
case 0xB4:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::LFS_rd_mp;
	break;
}

/* 0F B5 /r        LGS r32,m16:32           Load GS:r32 with pointer from memory          */
case 0xB5:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::LGS_rd_mp;
	break;
}

/* 0F B6 /r        MOVZX r32,r/m8           Move byte to dword with zero-extend            */
case 0xB6:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::MOVZX_rd_eb;
	break;
}

/* 0F B7 /r        MOVZX r32,r/m16          Move word to dword reg with zero-extend */
case 0xB7:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::MOVZX_rd_ew;
	break;
}

/* 0F BA /4  ib    BT r/m32,imm8            Save bit in carry flag                        */
/* 0F BA /5  ib    BTS r/m32,imm8           Save bit in carry flag and set                */
/* 0F BA /6  ib    BTR r/m32,imm8           Save bit in carry flag and reset              */
/* 0F BA /7  ib    BTC r/m32,imm8           Save bit in carry flag and complement         */
case 0xBA:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.ib = fetchb();
	switch(m_instr.modrm.n) {
		case 4:
			m_instr.fn = &CPUExecutor::BT_ed_ib;
			break;
		case 5:
			m_instr.fn = &CPUExecutor::BTS_ed_ib;
			break;
		case 6:
			m_instr.fn = &CPUExecutor::BTR_ed_ib;
			break;
		case 7:
			m_instr.fn = &CPUExecutor::BTC_ed_ib;
			break;
		default:
			illegal_opcode();
			break;
	}
	ctb_op_ = m_instr.modrm.n;
	ctb_idx_ = CTB_IDX_0FBA;
	break;
}

/* 0F BB           BTC r/m32,r32            Save bit in carry flag and complement         */
case 0xBB:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::BTC_ed_rd;
	break;
}

/* 0F BC           BSF r32,r/m32            Bit scan forward on r/m dword                  */
case 0xBC:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::BSF_rd_ed;
	break;
}

/* 0F BD           BSR r32,r/m32            Bit scan reverse on r/m dword                  */
case 0xBD:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::BSR_rd_ed;
	break;
}

/* 0F BE /r        MOVSX r32,r/m8           Move byte to dword with sign-extend            */
case 0xBE:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::MOVSX_rd_eb;
	break;
}

/* 0F BF /r        MOVSX r32,r/m16          Move word to dword, sign-extend */
case 0xBF:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::MOVSX_rd_ew;
	break;
}

default:
{
	illegal_opcode();
}

} //switch

}
