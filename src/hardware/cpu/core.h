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

#ifndef IBMULATOR_CPU_CORE_H
#define IBMULATOR_CPU_CORE_H

#include "selector.h"
#include "descriptor.h"
#include "statebuf.h"

class Memory;
class CPUCore;
extern CPUCore g_cpucore;

/* EFLAGS REGISTER:

              CARRY───S─────────────────────────────────────────┐
             PARITY───S───────────────────────────────────┐     │
   AUXILLIARY CARRY───S─────────────────────────────┐     │     │
               ZERO───S───────────────────────┐     │     │     │
               SIGN───S────────────────────┐  │     │     │     │
           OVERFLOW───S────────┐           │  │     │     │     │
                               │           │  │     │     │     │
    31     18 17 16 15 14 13 12│11 10  9  8│ 7│ 6  5│ 4  3│ 2  1│ 0
   ╔════════╤══╤══╤══╤══╤══╤══╤══╤══╤══╤══╤══╤══╤══╤══╤══╤══╤══╤══╗
   ║ 0 .. 0 │VM│RF│ 0│NT│IO PL│OF│DF│IF│TF│SF│ZF│ 0│AF│ 0│PF│ 1│CF║
   ╚════════╧══╧══╧══╧══╧══╧══╧══╧══╧══╧══╧══╧══╧══╧══╧══╧══╧══╧══╝
              │  │     │   │       │  │  │
              │  │     │   │       │  │  └───────S───TRAP FLAG
              │  │     │   │       │  └──────────X───INTERRUPT ENABLE
              │  │     │   │       └─────────────C───DIRECTION FLAG
              │  │     │   └─────────────────────X───I/O PRIVILEGE LEVEL
              │  │     └─────────────────────────X───NESTED TASK FLAG
              │  └───────────────────────────────X───RESUME FLAG (386)
              └──────────────────────────────────X───VIRTUAL 8086 MODE (386)

    S = STATUS FLAG, C = CONTROL FLAG, X = SYSTEM FLAG
*/
#define FBITN_CF   0
#define FBITN_PF   2
#define FBITN_AF   4
#define FBITN_ZF   6
#define FBITN_SF   7
#define FBITN_TF   8
#define FBITN_IF   9
#define FBITN_DF   10
#define FBITN_OF   11
#define FBITN_IOPL 12
#define FBITN_NT   14
#define FBITN_RF   16
#define FBITN_VM   17

#define FMASK_CF   (1 << FBITN_CF)  // 0 CARRY
#define FMASK_PF   (1 << FBITN_PF)  // 2 PARITY
#define FMASK_AF   (1 << FBITN_AF)  // 4 AUXILLIARY CARRY
#define FMASK_ZF   (1 << FBITN_ZF)  // 6 ZERO
#define FMASK_SF   (1 << FBITN_SF)  // 7 SIGN
#define FMASK_TF   (1 << FBITN_TF)  // 8 TRAP FLAG
#define FMASK_IF   (1 << FBITN_IF)  // 9 INTERRUPT ENABLE
#define FMASK_DF   (1 << FBITN_DF)  // 10 DIRECTION FLAG
#define FMASK_OF   (1 << FBITN_OF)  // 11 OVERFLOW
#define FMASK_IOPL (3 << FBITN_IOPL)// 12-13 I/O PRIVILEGE LEVEL
#define FMASK_NT   (1 << FBITN_NT)  // 14 NESTED TASK FLAG
#define FMASK_RF   (1 << FBITN_RF)  // 16 RESUME FLAG
#define FMASK_VM   (1 << FBITN_VM)  // 17 VIRTUAL 8086 MODE

#define FMASK_FLAGS  0xFFFF
#define FMASK_EFLAGS 0x3FFFF
#define FMASK_VALID  0x00037FD5 // only supported bits for EFLAGS register

#define GET_FLAG(NAME)     g_cpucore.get_EFLAGS(FMASK_ ## NAME)
#define SET_FLAG(NAME,VAL) g_cpucore.set_##NAME (VAL)
#define GET_FLAGS()        g_cpucore.get_FLAGS(FMASK_FLAGS)
#define GET_EFLAGS()       g_cpucore.get_EFLAGS(FMASK_EFLAGS)
#define SET_FLAGS(VAL)     g_cpucore.set_FLAGS(VAL)
#define SET_EFLAGS(VAL)    g_cpucore.set_EFLAGS(VAL)

#define FLAG_CF   (GET_FLAG(CF) >> FBITN_CF)
#define FLAG_PF   (GET_FLAG(PF) >> FBITN_PF)
#define FLAG_AF   (GET_FLAG(AF) >> FBITN_AF)
#define FLAG_ZF   (GET_FLAG(ZF) >> FBITN_ZF)
#define FLAG_SF   (GET_FLAG(SF) >> FBITN_SF)
#define FLAG_TF   (GET_FLAG(TF) >> FBITN_TF)
#define FLAG_IF   (GET_FLAG(IF) >> FBITN_IF)
#define FLAG_DF   (GET_FLAG(DF) >> FBITN_DF)
#define FLAG_OF   (GET_FLAG(OF) >> FBITN_OF)
#define FLAG_IOPL (GET_FLAG(IOPL) >> FBITN_IOPL)
#define FLAG_NT   (GET_FLAG(NT) >> FBITN_NT)
#define FLAG_RF   (GET_FLAG(RF) >> FBITN_RF)
#define FLAG_VM   (GET_FLAG(VM) >> FBITN_VM)


// Control registers
/*
31                23                15                7               0
╔═════════════════╪═════════════════╪════════╦════════╪═════════════════╗
║                                            ║                          ║
║    PAGE DIRECTORY BASE REGISTER (PDBR)     ║         RESERVED         ║CR3
╟────────────────────────────────────────────╨──────────────────────────╢
║                                                                       ║
║                       PAGE FAULT LINEAR ADDRESS                       ║CR2
╟─┬───────────────────────────────────────────────────────────┬─┬─┬─┬─┬─╢
║P│                                                           │E│T│E│M│P║
║G│                              RESERVED                     │T│S│M│P│E║CR0
╚═╧═══════════════╪═════════════════╪═════════════════╪═══════╧═╧═╧═╧═╧═╝
*/

#define CR0BIT_PE 0  // Protected mode enable
#define CR0BIT_MP 1  // Monitor processor extension
#define CR0BIT_EM 2  // Emulate processor extension
#define CR0BIT_TS 3  // Task switched
#define CR0BIT_ET 4  // Extension Type
#define CR0BIT_PG 31 // Paging

#define CR0MASK_PE (1 << CR0BIT_PE)
#define CR0MASK_MP (1 << CR0BIT_MP)
#define CR0MASK_EM (1 << CR0BIT_EM)
#define CR0MASK_TS (1 << CR0BIT_TS)
#define CR0MASK_ET (1 << CR0BIT_ET)
#define CR0MASK_PG (1 << CR0BIT_PG)

#define CR0MASK_ALL 0x8000001F
#define CR0MASK_MSW 0x0000000F

#define GET_CR(REGI)         g_cpucore.ctl_reg(REGI)
#define GET_CR0(BIT)         g_cpucore.get_CR0(CR0MASK_ ## BIT)
#define SET_CR0(VAL)         g_cpucore.set_CR0(VAL)
#define SET_CR0BIT(BIT,VAL)  g_cpucore.set_CR0(CR0BIT_ ## BIT, VAL)
#define SET_CR2(VAL)         g_cpucore.set_CR2(VAL)
#define SET_CR3(VAL)         g_cpucore.set_CR3(VAL)

#define CR0_PE (GET_CR0(PE) >> CR0BIT_PE)
#define CR0_MP (GET_CR0(MP) >> CR0BIT_MP)
#define CR0_EM (GET_CR0(EM) >> CR0BIT_EM)
#define CR0_TS (GET_CR0(TS) >> CR0BIT_TS)
#define CR0_ET (GET_CR0(ET) >> CR0BIT_ET)
#define CR0_PG (GET_CR0(PG) >> CR0BIT_PG)

#define GET_MSW()      (uint16_t(g_cpucore.get_CR0(CR0MASK_MSW)))
#define SET_MSW(_msw_) (g_cpucore.set_CR0((g_cpucore.get_CR0(CR0MASK_ALL) & ~CR0MASK_MSW) | _msw_))

#define REG_CR0 g_cpucore.ctl_reg(0)
#define REG_CR2 g_cpucore.ctl_reg(2)
#define REG_CR3 g_cpucore.ctl_reg(3)
#define PDBR (REG_CR3 & 0xFFFFF000)

#define IS_PMODE()  g_cpucore.is_pmode()
#define IS_V8086()  g_cpucore.is_v8086()
#define IS_RMODE()  g_cpucore.is_rmode()
#define IS_PAGING() g_cpucore.is_paging()


enum GenRegIndex8 {
	REGI_AL = 0,
	REGI_CL = 1,
	REGI_DL = 2,
	REGI_BL = 3,
	REGI_AH = 4,
	REGI_CH = 5,
	REGI_DH = 6,
	REGI_BH = 7
};

enum GenRegIndex32 {
	REGI_EAX = 0,
	REGI_ECX = 1,
	REGI_EDX = 2,
	REGI_EBX = 3,
	REGI_ESP = 4,
	REGI_EBP = 5,
	REGI_ESI = 6,
	REGI_EDI = 7
};

enum SegRegIndex {
	REGI_ES = 0,
	REGI_CS = 1,
	REGI_SS = 2,
	REGI_DS = 3,
	REGI_FS = 4,
	REGI_GS = 5,

	REGI_TR,
	REGI_IDTR,
	REGI_LDTR,
	REGI_GDTR
};

#define REGI_NONE 0xFF


#define REG_AL  g_cpucore.gen_reg(REGI_EAX).byte[0]
#define REG_AH  g_cpucore.gen_reg(REGI_EAX).byte[1]
#define REG_AX  g_cpucore.gen_reg(REGI_EAX).word[0]
#define REG_EAX g_cpucore.gen_reg(REGI_EAX).dword[0]
#define REG_BL  g_cpucore.gen_reg(REGI_EBX).byte[0]
#define REG_BH  g_cpucore.gen_reg(REGI_EBX).byte[1]
#define REG_BX  g_cpucore.gen_reg(REGI_EBX).word[0]
#define REG_EBX g_cpucore.gen_reg(REGI_EBX).dword[0]
#define REG_CL  g_cpucore.gen_reg(REGI_ECX).byte[0]
#define REG_CH  g_cpucore.gen_reg(REGI_ECX).byte[1]
#define REG_CX  g_cpucore.gen_reg(REGI_ECX).word[0]
#define REG_ECX g_cpucore.gen_reg(REGI_ECX).dword[0]
#define REG_DL  g_cpucore.gen_reg(REGI_EDX).byte[0]
#define REG_DH  g_cpucore.gen_reg(REGI_EDX).byte[1]
#define REG_DX  g_cpucore.gen_reg(REGI_EDX).word[0]
#define REG_EDX g_cpucore.gen_reg(REGI_EDX).dword[0]
#define REG_SI  g_cpucore.gen_reg(REGI_ESI).word[0]
#define REG_ESI g_cpucore.gen_reg(REGI_ESI).dword[0]
#define REG_DI  g_cpucore.gen_reg(REGI_EDI).word[0]
#define REG_EDI g_cpucore.gen_reg(REGI_EDI).dword[0]
#define REG_SP  g_cpucore.gen_reg(REGI_ESP).word[0]
#define REG_ESP g_cpucore.gen_reg(REGI_ESP).dword[0]
#define REG_BP  g_cpucore.gen_reg(REGI_EBP).word[0]
#define REG_EBP g_cpucore.gen_reg(REGI_EBP).dword[0]

#define REG_IP (uint16_t(g_cpucore.get_EIP() & 0xFFFF))
#define SET_IP(value) g_cpucore.set_EIP(value & 0xFFFF)
#define RESTORE_IP g_cpucore.restore_EIP
#define REG_EIP g_cpucore.get_EIP()
#define SET_EIP(value) g_cpucore.set_EIP(value)
#define RESTORE_EIP g_cpucore.restore_EIP

#define SET_CS g_cpucore.set_CS
#define SET_SS g_cpucore.set_SS
#define SET_ES g_cpucore.set_ES
#define SET_DS g_cpucore.set_DS
#define SET_FS g_cpucore.set_FS
#define SET_GS g_cpucore.set_GS
#define SET_IDTR(base,limit) g_cpucore.set_IDTR(base,limit)
#define SET_GDTR(base,limit) g_cpucore.set_GDTR(base,limit)
#define SET_SR g_cpucore.set_SR

#define REG_ES g_cpucore.seg_reg(REGI_ES)
#define REG_DS g_cpucore.seg_reg(REGI_DS)
#define REG_SS g_cpucore.seg_reg(REGI_SS)
#define REG_CS g_cpucore.seg_reg(REGI_CS)
#define REG_FS g_cpucore.seg_reg(REGI_FS)
#define REG_GS g_cpucore.seg_reg(REGI_GS)
#define REG_TR g_cpucore.seg_reg(REGI_TR)
#define REG_LDTR g_cpucore.seg_reg(REGI_LDTR)

#define SEG_REG(idx) g_cpucore.seg_reg(idx)
#define GEN_REG(idx) g_cpucore.gen_reg(idx)

#define GET_BASE(S)	 g_cpucore.get_seg_base(REGI_ ## S)
#define GET_LIMIT(S) g_cpucore.get_seg_limit(REGI_ ## S)

#define GET_PHYADDR(SEG,OFF) g_cpucore.get_phyaddr(REGI_ ## SEG , OFF)
#define GET_LINADDR(SEG,OFF) g_cpucore.get_linaddr(REGI_ ## SEG , OFF)

#define IP_CHAIN_SIZE 10

#define CPL g_cpucore.get_CPL()
#define IS_USER_PL (CPL == 3)

#define REG_DBG(NUM) g_cpucore.dbg_reg(NUM)
#define REG_TEST(NUM) g_cpucore.test_reg(NUM)


union GenReg
{
	uint32_t dword[1];
	uint16_t word[2];
	uint8_t  byte[4];
};

struct SegReg
{
	Selector sel;
	Descriptor desc;

	inline bool is(const SegReg & _segreg) {
		return (this==&_segreg);
	}

	void validate();
	const char *to_string();
};


class CPUCore
{
protected:

	// general registers
	GenReg m_genregs[8];

	// segment registers and TR, IDTR, LDTR, and GDTR for convenience
	SegReg m_segregs[10];

	// status and control registers
	uint32_t m_eflags;
	uint32_t m_eip, m_prev_eip;
	uint32_t m_cr[4];
	uint32_t m_dr[8];
	uint32_t m_tr[8];


	inline void load_segment_register(SegReg & _segreg, uint16_t _value)
	{
		if(is_pmode()) {
			load_segment_protected(_segreg, _value);
		} else {
			load_segment_real(_segreg, _value, false);
		}
	}
	void load_segment_real(SegReg & _segreg, uint16_t _value, bool _defaults);
	void load_segment_protected(SegReg & _segreg, uint16_t _value);

	inline void set_flag(uint8_t _flagnum, bool _val) {
		m_eflags = (m_eflags &~ (1<<_flagnum)) | ((_val)<<_flagnum);
	}

	uint32_t translate_linear(uint32_t _linear_addr) const;
	void handle_mode_change();

public:

	void reset();

	inline GenReg & gen_reg(uint8_t idx) { assert(idx<8); return m_genregs[idx]; }
	inline SegReg & seg_reg(uint8_t idx) { assert(idx<10); return m_segregs[idx]; }
	inline uint32_t ctl_reg(uint8_t idx) { assert(idx<4); return m_cr[idx]; }
	inline uint32_t & dbg_reg(uint8_t idx) { assert(idx<8); return m_dr[idx]; }
	inline uint32_t & test_reg(uint8_t idx) { assert(idx<8); return m_tr[idx]; }

	//only real mode:
	inline void set_CS(uint16_t _val) {
		assert(!is_pmode());
		load_segment_real(m_segregs[REGI_CS], _val, true);
	}
	//only protected mode
	void set_CS(Selector &sel, Descriptor &desc, uint8_t cpl);
	static void check_CS(uint16_t selector, Descriptor &descriptor, uint8_t rpl, uint8_t cpl);
	static void check_CS(Selector &selector, Descriptor &descriptor, uint8_t rpl, uint8_t cpl)
		{ CPUCore::check_CS(selector.value, descriptor, rpl, cpl); }
	inline void set_DS(uint16_t _val) { load_segment_register(m_segregs[REGI_DS], _val); }
	inline void set_SS(uint16_t _val) { load_segment_register(m_segregs[REGI_SS], _val); }
	void set_SS(Selector &sel, Descriptor &desc, uint8_t cpl);
	inline void set_ES(uint16_t _val) { load_segment_register(m_segregs[REGI_ES], _val); }
	inline void set_FS(uint16_t _val) { load_segment_register(m_segregs[REGI_FS], _val); }
	inline void set_GS(uint16_t _val) { load_segment_register(m_segregs[REGI_GS], _val); }
	inline void set_SR(uint8_t _idx, uint16_t _val) {
		assert(_idx <= REGI_GS);
		if(_idx == REGI_CS) {
			set_CS(_val);
		} else {
			load_segment_register(m_segregs[_idx], _val);
		}
	}
	inline void set_IDTR(uint32_t _base, uint32_t _limit) {
		m_segregs[REGI_IDTR].desc.base = _base;
		m_segregs[REGI_IDTR].desc.limit = _limit;
	}
	inline void set_GDTR(uint32_t _base, uint32_t _limit) {
		m_segregs[REGI_GDTR].desc.base = _base;
		m_segregs[REGI_GDTR].desc.limit = _limit;
	}

	inline void set_EIP(uint32_t _val) {
		m_prev_eip = m_eip;
		m_eip = _val;
	}
	inline uint32_t get_EIP() const { return m_eip; }
	inline void restore_EIP() { m_eip = m_prev_eip; }

	inline uint16_t get_FLAGS(uint16_t _mask) const { return (uint16_t(m_eflags) & _mask); }
	inline uint32_t get_EFLAGS(uint32_t _mask) const { return (m_eflags & _mask); }

	       void set_FLAGS(uint16_t _val);
	       void set_EFLAGS(uint32_t _val);
	inline void set_CF(bool _val) { set_flag(FBITN_CF,_val); }
	inline void set_PF(bool _val) { set_flag(FBITN_PF,_val); }
	inline void set_AF(bool _val) { set_flag(FBITN_AF,_val); }
	inline void set_ZF(bool _val) { set_flag(FBITN_ZF,_val); }
	inline void set_SF(bool _val) { set_flag(FBITN_SF,_val); }
	       void set_TF(bool _val);
	       void set_IF(bool _val);
	inline void set_DF(bool _val) { set_flag(FBITN_DF,_val); }
	inline void set_OF(bool _val) { set_flag(FBITN_OF,_val); }
	inline void set_IOPL(uint16_t _val) {
		m_eflags &= ~(3 << 12);
		m_eflags |= ((3 & _val) << 12);
	}
	inline void set_NT(bool _val) { set_flag(FBITN_NT,_val); }
	       void set_VM(bool _val);
	       void set_RF(bool _val);

	       void set_CR0(uint32_t _cr0);
	inline void set_CR0(uint8_t _flagnum, bool _val) {
		set_CR0((m_cr[0] &~ (1<<_flagnum)) | ((_val)<<_flagnum));
	}
	inline uint32_t get_CR0(uint32_t _cr0) const {
		return (m_cr[0] & _cr0);
	}
	inline void set_CR2(uint32_t _cr2) { m_cr[2] = _cr2; }
	       void set_CR3(uint32_t _cr3);

	inline bool is_rmode() const { return !(m_cr[0] & CR0MASK_PE); }
	inline bool is_pmode() const { return (m_cr[0] & CR0MASK_PE) && !(m_eflags & FMASK_VM); }
	inline bool is_v8086() const { return (m_eflags & FMASK_VM); }
	inline bool is_paging() const { return (m_cr[0] & CR0MASK_PG); }

	/*
	 * From the 80286 to the Pentium, all Intel processors derive their current
	 * privilege level (CPL) from the SS access rights. The CPL is loaded from
	 * the SS descriptor table entry when the SS register is loaded.
	 * The undocumented LOADALL instruction (or system-management mode RSM
	 * instruction) can be used to manipulate the SS descriptor-cache access
	 * rights, thereby directly manipulating the CPL of the microprocessors.
	 * (http://www.rcollins.org/ddj/Aug98/Aug98.html)
	 */
	inline uint8_t & get_CPL() { return m_segregs[REGI_CS].sel.cpl; }

	inline uint32_t get_seg_base(unsigned _segidx) const { return m_segregs[_segidx].desc.base; }
	inline uint32_t get_seg_limit(unsigned _segidx) const { return m_segregs[_segidx].desc.limit; }

	inline uint32_t get_linaddr(unsigned _segidx, uint32_t _offset) const { return get_seg_base(_segidx) + _offset; }
	       uint32_t get_phyaddr(unsigned _segidx, uint32_t _offset, Memory *_memory=nullptr) const;

	// convenience funcs used by CPUDebugger
	inline uint8_t  get_AL() const  { return m_genregs[REGI_EAX].byte[0]; }
	inline uint8_t  get_AH() const  { return m_genregs[REGI_EAX].byte[1]; }
	inline uint16_t get_AX() const  { return m_genregs[REGI_EAX].word[0]; }
	inline uint32_t get_EAX() const { return m_genregs[REGI_EAX].dword[0]; }
	inline uint8_t  get_BL() const  { return m_genregs[REGI_EBX].byte[0]; }
	inline uint8_t  get_BH() const  { return m_genregs[REGI_EBX].byte[1]; }
	inline uint16_t get_BX() const  { return m_genregs[REGI_EBX].word[0]; }
	inline uint32_t get_EBX() const { return m_genregs[REGI_EBX].dword[0]; }
	inline uint8_t  get_CL() const  { return m_genregs[REGI_ECX].byte[0]; }
	inline uint8_t  get_CH() const  { return m_genregs[REGI_ECX].byte[1]; }
	inline uint16_t get_CX() const  { return m_genregs[REGI_ECX].word[0]; }
	inline uint32_t get_ECX() const { return m_genregs[REGI_ECX].dword[0]; }
	inline uint8_t  get_DL() const  { return m_genregs[REGI_EDX].byte[0]; }
	inline uint8_t  get_DH() const  { return m_genregs[REGI_EDX].byte[1]; }
	inline uint16_t get_DX() const  { return m_genregs[REGI_EDX].word[0]; }
	inline uint32_t get_EDX() const { return m_genregs[REGI_EDX].dword[0]; }
	inline uint16_t get_SI() const  { return m_genregs[REGI_ESI].word[0]; }
	inline uint32_t get_ESI() const { return m_genregs[REGI_ESI].dword[0]; }
	inline uint16_t get_BP() const  { return m_genregs[REGI_EBP].word[0]; }
	inline uint32_t get_EBP() const { return m_genregs[REGI_EBP].dword[0]; }
	inline uint16_t get_DI() const  { return m_genregs[REGI_EDI].word[0]; }
	inline uint32_t get_EDI() const { return m_genregs[REGI_EDI].dword[0]; }
	inline uint16_t get_SP() const  { return m_genregs[REGI_ESP].word[0]; }
	inline uint32_t get_ESP() const { return m_genregs[REGI_ESP].dword[0]; }
	inline SegReg & get_CS() { return m_segregs[REGI_CS]; }
	inline SegReg & get_DS() { return m_segregs[REGI_DS]; }
	inline SegReg & get_SS() { return m_segregs[REGI_SS]; }
	inline SegReg & get_ES() { return m_segregs[REGI_ES]; }
	inline SegReg & get_FS() { return m_segregs[REGI_FS]; }
	inline SegReg & get_GS() { return m_segregs[REGI_GS]; }
	inline SegReg & get_TR() { return m_segregs[REGI_TR]; }
	inline SegReg & get_LDTR() { return m_segregs[REGI_LDTR]; }

	void save_state(StateBuf &_state) const;
	void restore_state(StateBuf &_state);
};


#endif
