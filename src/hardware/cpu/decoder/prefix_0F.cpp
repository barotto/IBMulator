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
	m_instr.modrm.load(m_instr.addr32);
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
	m_instr.modrm.load(m_instr.addr32);
	switch(m_instr.modrm.n) {
		case 0:
			m_instr.fn = &CPUExecutor::SGDT_o16;
			break;
		case 1:
			m_instr.fn = &CPUExecutor::SIDT_o16;
			break;
		case 2:
			m_instr.fn = &CPUExecutor::LGDT_o16;
			break;
		case 3:
			m_instr.fn = &CPUExecutor::LIDT_o16;
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
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::LAR_rw_ew;
	break;
}

/* 0F 03 /r     LSL rw,ew   Load: rw = Segment Limit, selector ew */
case 0x03:
{
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::LSL_rw_ew;
	break;
}

/*  0F 05      286 LOADALL        Load CPU registers from memory (286) */
case 0x05:
{
	if(CPU_FAMILY == CPU_286) {
		m_instr.fn = &CPUExecutor::LOADALL_286;
	} else {
		illegal_opcode();
	}
	break;
}

/*  0F 06      CLTS            Clear task switched flag */
case 0x06:
{
	m_instr.fn = &CPUExecutor::CLTS;
	break;
}

/*  0F 07      386 LOADALL        Load CPU registers from memory (386) */
case 0x07:
{
	if(CPU_FAMILY == CPU_386) {
		//TODO
		//m_instr.fn = &CPUExecutor::LOADALL_386;
		PERRF_ABORT(LOG_CPU, "LOADALL 386 not implemented\n");
		illegal_opcode();
	} else {
		illegal_opcode();
	}
	break;
}

/* 0F 20 /r   MOV r32,CR0/CR2/CR3      Move (control register) to (register) */
case 0x20:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	/* For the MOVs from/to CRx/DRx/TRx, mod=00b/01b/10b is aliased to 11b. */
	m_instr.modrm.mod = 3;
	m_instr.fn = &CPUExecutor::MOV_rd_CR;
	break;
}

/* 0F 21 /r   MOV r32,DRx         Move (debug register) to (register) */
case 0x21:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.modrm.mod = 3;
	m_instr.fn = &CPUExecutor::MOV_rd_DR;
	break;
}

/* 0F 22 /r   MOV CR0/CR2/CR3,r32      Move (register) to (control register)  */
case 0x22:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.modrm.mod = 3;
	m_instr.fn = &CPUExecutor::MOV_CR_rd;
	break;
}

/* 0F 23 /r        MOV DR0 -- 3,r32         Move (register) to (debug register) */
case 0x23:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.modrm.mod = 3;
	m_instr.fn = &CPUExecutor::MOV_DR_rd;
	break;
}

/* 0F 24 /r        MOV r32,TR6/TR7          Move (test register) to (register) */
case 0x24:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.modrm.mod = 3;
	m_instr.fn = &CPUExecutor::MOV_rd_TR;
	break;
}

/* 0F 26 /r        MOV TR6/TR7,r32          Move (register) to (test register) */
case 0x26:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.modrm.mod = 3;
	m_instr.fn = &CPUExecutor::MOV_TR_rd;
	break;
}

/* 0F 80     cw JO rel16         Jump near if overflow (OF=1)                    */
case 0x80:
{
	ILLEGAL_286
	m_instr.iw1 = fetchw();
	m_instr.fn = &CPUExecutor::JO_cw;
	break;
}

/* 0F 81 cw   JNO  rel16       Jump near if not overflow (OF=0)                */
case 0x81:
{
	ILLEGAL_286
	m_instr.iw1 = fetchw();
	m_instr.fn = &CPUExecutor::JNO_cw;
	break;
}

/* 0F 82 cw   JC   rel16       Jump near if carry (CF=1)                       */
case 0x82:
{
	ILLEGAL_286
	m_instr.iw1 = fetchw();
	m_instr.fn = &CPUExecutor::JC_cw;
	break;
}

/* 0F 83 cw   JNC  rel16       Jump near if not carry (CF=0)                   */
case 0x83:
{
	ILLEGAL_286
	m_instr.iw1 = fetchw();
	m_instr.fn = &CPUExecutor::JNC_cw;
	break;
}

/* 0F 84 cw   JE   rel16       Jump near if equal (ZF=1)                       */
case 0x84:
{
	ILLEGAL_286
	m_instr.iw1 = fetchw();
	m_instr.fn = &CPUExecutor::JE_cw;
	break;
}

/* 0F 85 cw   JNE  rel16       Jump near if not equal (ZF=0)                   */
case 0x85:
{
	ILLEGAL_286
	m_instr.iw1 = fetchw();
	m_instr.fn = &CPUExecutor::JNE_cw;
	break;
}

/* 0F 86 cw   JBE  rel16       Jump near if below or equal (CF=1 or ZF=1)      */
case 0x86:
{
	ILLEGAL_286
	m_instr.iw1 = fetchw();
	m_instr.fn = &CPUExecutor::JBE_cw;
	break;
}

/* 0F 87 cw   JA   rel16       Jump near if above (CF=0 and ZF=0)              */
case 0x87:
{
	ILLEGAL_286
	m_instr.iw1 = fetchw();
	m_instr.fn = &CPUExecutor::JA_cw;
	break;
}

/* 0F 88 cw   JS   rel16       Jump near if sign (SF=1)                        */
case 0x88:
{
	ILLEGAL_286
	m_instr.iw1 = fetchw();
	m_instr.fn = &CPUExecutor::JS_cw;
	break;
}

/* 0F 89 cw   JNS  rel16       Jump near if not sign (SF=0)                    */
case 0x89:
{
	ILLEGAL_286
	m_instr.iw1 = fetchw();
	m_instr.fn = &CPUExecutor::JNS_cw;
	break;
}

/* 0F 8A cw   JPE  rel16       Jump near if parity even (PF=1)                 */
case 0x8A:
{
	ILLEGAL_286
	m_instr.iw1 = fetchw();
	m_instr.fn = &CPUExecutor::JPE_cw;
	break;
}

/* 0F 8B cw   JPO  rel16       Jump near if parity odd (PF=0)                  */
case 0x8B:
{
	ILLEGAL_286
	m_instr.iw1 = fetchw();
	m_instr.fn = &CPUExecutor::JPO_cw;
	break;
}

/* 0F 8C cw   JL   rel16       Jump near if less (SF<>OF)                      */
case 0x8C:
{
	ILLEGAL_286
	m_instr.iw1 = fetchw();
	m_instr.fn = &CPUExecutor::JL_cw;
	break;
}

/* 0F 8D cw   JNL  rel16       Jump near if not less (SF=OF)                   */
case 0x8D:
{
	ILLEGAL_286
	m_instr.iw1 = fetchw();
	m_instr.fn = &CPUExecutor::JNL_cw;
	break;
}

/* 0F 8E cw   JLE  rel16       Jump near if less or equal (ZF=1 and SF<>OF)    */
case 0x8E:
{
	ILLEGAL_286
	m_instr.iw1 = fetchw();
	m_instr.fn = &CPUExecutor::JLE_cw;
	break;
}

/* 0F 8F cw   JNLE rel16       Jump near if not less or equal (ZF=0 and SF=OF) */
case 0x8F:
{
	ILLEGAL_286
	m_instr.iw1 = fetchw();
	m_instr.fn = &CPUExecutor::JNLE_cw;
	break;
}

/* 0F 90      SETO r/m8        Set byte if overflow (OF=1)                    */
case 0x90:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::SETO_eb;
	break;
}

/* 0F 91      SETNO r/m8       Set byte if not overflow (OF=0)                */
case 0x91:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::SETNO_eb;
	break;
}

/* 0F 92      SETB r/m8        Set byte if below (CF=1)                       */
case 0x92:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::SETB_eb;
	break;
}

/* 0F 93      SETNB r/m8       Set byte if not below (CF=0)                   */
case 0x93:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::SETNB_eb;
	break;
}

/* 0F 94      SETE r/m8        Set byte if equal (ZF=1)                       */
case 0x94:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::SETE_eb;
	break;
}

/* 0F 95      SETNE r/m8       Set byte if not equal (ZF=0)                   */
case 0x95:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::SETNE_eb;
	break;
}

/* 0F 96      SETBE r/m8       Set byte if below or equal (CF=1 or (ZF=1)     */
case 0x96:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::SETBE_eb;
	break;
}

/* 0F 97      SETNBE r/m8      Set byte if not below or equal (CF=0 and ZF=0) */
case 0x97:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::SETNBE_eb;
	break;
}

/* 0F 98      SETS r/m8        Set byte if sign (SF=1)                        */
case 0x98:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::SETS_eb;
	break;
}

/* 0F 99      SETNS r/m8       Set byte if not sign (SF=0)                    */
case 0x99:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::SETNS_eb;
	break;
}

/* 0F 9A      SETP r/m8        Set byte if parity (PF=1)                      */
case 0x9A:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::SETP_eb;
	break;
}

/* 0F 9B      SETNP r/m8       Set byte if not parity (PF=0)                  */
case 0x9B:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::SETNP_eb;
	break;
}

/* 0F 9C      SETL r/m8        Set byte if less (SF<>OF)                      */
case 0x9C:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::SETL_eb;
	break;
}

/* 0F 9D      SETNL r/m8       Set byte if not less (SF=OF)                   */
case 0x9D:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::SETNL_eb;
	break;
}

/* 0F 9E      SETLE r/m8       Set byte if less or equal (ZF=1 and SF<>OF)    */
case 0x9E:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::SETLE_eb;
	break;
}

/* 0F 9F      SETNLE r/m8      Set byte if not less or equal (ZF=1 and SF<>OF)*/
case 0x9F:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::SETNLE_eb;
	break;
}

/* 0F A0           PUSH FS                  Push FS                                       */
case 0xA0:
{
	ILLEGAL_286
	m_instr.reg = REGI_FS;
	m_instr.fn = &CPUExecutor::PUSH_SR_w;
	break;
}

/* 0F A1           POP FS                   Pop top of stack into FS                      */
case 0xA1:
{
	ILLEGAL_286
	m_instr.reg = REGI_FS;
	m_instr.fn = &CPUExecutor::POP_SR_w;
	break;
}

/* 0F A3           BT r/m16,r16             Save bit in carry flag                        */
case 0xA3:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::BT_ew_rw;
	break;
}

/* 0F A4           SHLD r/m16,r16,imm8      r/m16 gets SHL of r/m16 concatenated with r16 */
case 0xA4:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.ib = fetchb();
	m_instr.fn = &CPUExecutor::SHLD_ew_rw_ib;
	break;
}

/* 0F A5           SHLD r/m16,r16,CL        r/m16 gets SHL of r/m16 concatenated with r16 */
case 0xA5:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::SHLD_ew_rw_CL;
	break;
}

/* OF A8           PUSH GS                  Push GS                                       */
case 0xA8:
{
	ILLEGAL_286
	m_instr.reg = REGI_GS;
	m_instr.fn = &CPUExecutor::PUSH_SR_w;
	break;
}

/* 0F A9           POP GS                   Pop top of stack into GS                      */
case 0xA9:
{
	ILLEGAL_286
	m_instr.reg = REGI_GS;
	m_instr.fn = &CPUExecutor::POP_SR_w;
	break;
}

/* 0F AB           BTS r/m16,r16            Save bit in carry flag and set                */
case 0xAB:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::BTS_ew_rw;
	break;
}

/* 0F AC           SHRD r/m16,r16,imm8      r/m16 gets SHR of r/m16 concatenated with r16 */
case 0xAC:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.ib = fetchb();
	m_instr.fn = &CPUExecutor::SHRD_ew_rw_ib;
	break;
}

/* 0F AD           SHRD r/m16,r16,CL        r/m16 gets SHR of r/m16 concatenated with r16 */
case 0xAD:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::SHRD_ew_rw_CL;
	break;
}

/* 0F AF /r        IMUL r16,r/m16           word register = word register * r/m word      */
case 0xAF:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::IMUL_rw_ew;
	break;
}

/* 0F B2 /r        LSS r16,m16:16           Load SS:r16 with pointer from memory          */
case 0xB2:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::LSS_rw_mp;
	break;
}

/* 0F B3           BTR r/m16,r16            Save bit in carry flag and reset              */
case 0xB3:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::BTR_ew_rw;
	break;
}

/* 0F B4 /r        LFS r16,m16:16           Load FS:r16 with pointer from memory          */
case 0xB4:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::LFS_rw_mp;
	break;
}

/* 0F B5 /r        LGS r16,m16:16           Load GS:r16 with pointer from memory          */
case 0xB5:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::LGS_rw_mp;
	break;
}

/* 0F B6 /r        MOVZX r16,r/m8           Move byte to word with zero-extend            */
case 0xB6:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::MOVZX_rw_eb;
	break;
}

/* 0F B7 /r        MOV r16,r/m16          Move word to word reg */
case 0xB7:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::MOV_rw_ew;
	break;
}

/* 0F BA /4  ib    BT r/m16,imm8            Save bit in carry flag                        */
/* 0F BA /5  ib    BTS r/m16,imm8           Save bit in carry flag and set                */
/* 0F BA /6  ib    BTR r/m16,imm8           Save bit in carry flag and reset              */
/* 0F BA /7  ib    BTC r/m16,imm8           Save bit in carry flag and complement         */
case 0xBA:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.ib = fetchb();
	switch(m_instr.modrm.n) {
		case 4:
			m_instr.fn = &CPUExecutor::BT_ew_ib;
			break;
		case 5:
			m_instr.fn = &CPUExecutor::BTS_ew_ib;
			break;
		case 6:
			m_instr.fn = &CPUExecutor::BTR_ew_ib;
			break;
		case 7:
			m_instr.fn = &CPUExecutor::BTC_ew_ib;
			break;
		default:
			illegal_opcode();
			break;
	}
	ctb_op_ = m_instr.modrm.n;
	ctb_idx_ = CTB_IDX_0FBA;
	break;
}

/* 0F BB           BTC r/m16,r16            Save bit in carry flag and complement         */
case 0xBB:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::BTC_ew_rw;
	break;
}

/* 0F BC           BSF r16,r/m16            Bit scan forward on r/m word                  */
case 0xBC:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::BSF_rw_ew;
	break;
}

/* 0F BD           BSR r16,r/m16            Bit scan reverse on r/m word                  */
case 0xBD:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::BSR_rw_ew;
	break;
}

/* 0F BE /r        MOVSX r16,r/m8           Move byte to word with sign-extend            */
case 0xBE:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::MOVSX_rw_eb;
	break;
}

/* 0F BF /r        MOV r16,r/m16          Move word to word reg */
case 0xBF:
{
	ILLEGAL_286
	m_instr.modrm.load(m_instr.addr32);
	m_instr.fn = &CPUExecutor::MOV_rw_ew;
	break;
}

default:
{
	illegal_opcode();
}

} //switch

} //decode_prefix_0F
