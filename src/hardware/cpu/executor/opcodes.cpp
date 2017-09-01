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
#include "machine.h"
#include "hardware/cpu/executor.h"
#include "hardware/cpu/debugger.h"

/* the parity flag (PF) indicates whether the modulo 2 sum of the low-order
 * eight bits of the operation is even (PF=O) or odd (PF=1) parity.
 */
#ifdef __SSE4_2__
#define PARITY(x) (!(popcnt(x & 0xFF) & 1))
#else
#define PARITY(x) (parity_table[x & 0xFF])
#endif

/* Bochs and DosBox compute the undefined flags for AAA and AAS differently
 * Set to true to use the DosBox version
 * Set to false to use the Bochs version (validated on P6+)
 */
#define USE_DOSBOX_ASCIIOPS false

#ifdef __MSC_VER
#  include <intrin.h>
#  define __builtin_popcount __popcnt
#endif

GCC_ATTRIBUTE(always_inline)
inline uint popcnt(uint _value)
{
#if 0
	unsigned count;
	__asm__ ("popcnt %1,%0" : "=r"(count) : "rm"(_value) : "cc");
	return count;
#else
	// the builtin is translated with the POPCNT instruction only with -mpopcnt
	// or -msse4.2 flags
	return __builtin_popcount(_value);
#endif
}

bool parity_table[256] = {
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1
};


void CPUExecutor::check_CPL_privilege(bool _mode_cond, const char *_opstr)
{
	if(_mode_cond && CPL != 0) {
		PDEBUGF(LOG_V2, LOG_CPU, "%s: privilege check failed\n", _opstr);
		throw CPUException(CPU_GP_EXC, 0);
	}
}


/*******************************************************************************
 * AAA-ASCII Adjust AL After Addition
 */

void CPUExecutor::AAA()
{
	/* according to the original Intel's 286 manual, only AF and CF are modified
	 * but it seems OF,SF,ZF,PF are also updated in a specific way (they are not
	 * undefined).
	 */
#if USE_DOSBOX_ASCIIOPS
	SET_FLAG(SF, ((REG_AL >= 0x7a) && (REG_AL <= 0xf9)));
	if(((REG_AL & 0x0f) > 9)) {
		SET_FLAG(OF,(REG_AL & 0xf0) == 0x70);
		REG_AX += 0x106;
		SET_FLAG(CF, true);
		SET_FLAG(ZF, REG_AL == 0);
		SET_FLAG(AF, true);
	} else if(FLAG_AF) {
		REG_AX += 0x106;
		SET_FLAG(CF, true);
		SET_FLAG(AF, true);
		SET_FLAG(ZF, false);
		SET_FLAG(OF, false);
	} else {
		SET_FLAG(CF, false);
		SET_FLAG(AF, false);
		SET_FLAG(ZF, REG_AL == 0);
		SET_FLAG(OF, false);
	}
	SET_FLAG(PF, PARITY(REG_AL));
	REG_AL &= 0x0f;
#else
	bool af = false;
	bool cf = false;

	if(((REG_AL & 0x0f) > 9) || FLAG_AF) {
		REG_AX += 0x106;
		af = true;
		cf = true;
	}

	REG_AL &= 0x0f;

	SET_FLAG(CF, cf);
	SET_FLAG(AF, af);
	SET_FLAG(SF, REG_AL & 0x80);
	SET_FLAG(ZF, REG_AL == 0);
	SET_FLAG(PF, PARITY(REG_AL));
#endif
}


/*******************************************************************************
 * AAD-ASCII Adjust AX Before Division
 */

void CPUExecutor::AAD()
{
	//according to the Intel's 286 manual, the immediate value is always 0x0A.
	//in reality it can be anything.
	//see http://www.rcollins.org/secrets/opcodes/AAD.html
	uint16_t tmp = REG_AL + (m_instr->ib * REG_AH);
	REG_AX = (tmp & 0xff);

	SET_FLAG(SF, REG_AL & 0x80);
	SET_FLAG(ZF, REG_AL == 0);
	SET_FLAG(PF, PARITY(REG_AL));
	SET_FLAG(CF, false);
	SET_FLAG(OF, false);
	SET_FLAG(AF, false);
}


/*******************************************************************************
 * AAM-ASCII Adjust AX After Multiply
 */

void CPUExecutor::AAM()
{
	//according to the Intel's 286 manual the immediate value is always 0x0A.
	//in reality it can be anything.
	//see http://www.rcollins.org/secrets/opcodes/AAM.html
	if(m_instr->ib == 0) {
		throw CPUException(CPU_DIV_ER_EXC, 0);
	}
	uint8_t al = REG_AL;
	REG_AH = al / m_instr->ib;
	REG_AL = al % m_instr->ib;

	SET_FLAG(SF, REG_AL & 0x80);
	SET_FLAG(ZF, REG_AL == 0);
	SET_FLAG(PF, PARITY(REG_AL));
	SET_FLAG(CF, false);
	SET_FLAG(OF, false);
	SET_FLAG(AF, false);
}


/*******************************************************************************
 * AAS-ASCII Adjust AL After Subtraction
 */

void CPUExecutor::AAS()
{
#if USE_DOSBOX_ASCIIOPS
	if((REG_AL & 0x0f) > 9) {
		SET_FLAG(SF, REG_AL > 0x85);
		REG_AX -= 0x106;
		SET_FLAG(OF, false);
		SET_FLAG(CF, true);
		SET_FLAG(AF, true);
	} else if(FLAG_AF) {
		SET_FLAG(OF, (REG_AL >= 0x80) && (REG_AL <= 0x85));
		SET_FLAG(SF, (REG_AL < 0x06) || (REG_AL > 0x85));
		REG_AX -= 0x106;
		SET_FLAG(CF, true);
		SET_FLAG(AF, true);
	} else {
		SET_FLAG(SF, REG_AL >= 0x80);
		SET_FLAG(OF, false);
		SET_FLAG(CF, false);
		SET_FLAG(AF, false);
	}
	SET_FLAG(ZF, REG_AL == 0);
	SET_FLAG(PF, PARITY(REG_AL));
	REG_AL &= 0x0F;
#else
	bool af = false;
	bool cf = false;

	if(((REG_AL & 0x0F) > 0x09) || FLAG_AF) {
		REG_AX -= 0x106;
		af = true;
		cf = true;
	}

	REG_AL &= 0x0f;

	SET_FLAG(CF, cf);
	SET_FLAG(AF, af);
	SET_FLAG(SF, REG_AL & 0x80);
	SET_FLAG(ZF, REG_AL == 0);
	SET_FLAG(PF, PARITY(REG_AL));
#endif
}


/*******************************************************************************
 * ADC/ADD-Integer Addition
 */

uint8_t CPUExecutor::ADC_b(uint8_t op1, uint8_t op2)
{
	uint8_t cf = FLAG_CF;
	uint8_t res = op1 + op2 + cf;

	SET_FLAG(OF, ((op1 ^ op2 ^ 0x80) & (res ^ op2)) & 0x80);
	SET_FLAG(SF, res & 0x80);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, ((op1 ^ op2) ^ res) & 0x10);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(CF, (res < op1) || (cf && (res == op1)));

	return res;
}

uint16_t CPUExecutor::ADC_w(uint16_t op1, uint16_t op2)
{
	uint16_t cf = FLAG_CF;
	uint16_t res = op1 + op2 + cf;

	SET_FLAG(OF, ((op1 ^ op2 ^ 0x8000) & (res ^ op2)) & 0x8000);
	SET_FLAG(SF, res & 0x8000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, ((op1 ^ op2) ^ res) & 0x10);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(CF, (res < op1) || (cf && (res == op1)));

	return res;
}

uint32_t CPUExecutor::ADC_d(uint32_t op1, uint32_t op2)
{
	uint32_t cf = FLAG_CF;
	uint32_t res = op1 + op2 + cf;

	SET_FLAG(OF, ((op1 ^ op2 ^ 0x80000000) & (res ^ op2)) & 0x80000000);
	SET_FLAG(SF, res & 0x80000000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, ((op1 ^ op2) ^ res) & 0x10);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(CF, (res < op1) || (cf && (res == op1)));

	return res;
}

void CPUExecutor::ADC_eb_rb() { store_eb(ADC_b(load_eb(), load_rb())); }
void CPUExecutor::ADC_ew_rw() { store_ew(ADC_w(load_ew(), load_rw())); }
void CPUExecutor::ADC_ed_rd() { store_ed(ADC_d(load_ed(), load_rd())); }
void CPUExecutor::ADC_rb_eb() { store_rb(ADC_b(load_rb(), load_eb())); }
void CPUExecutor::ADC_rw_ew() { store_rw(ADC_w(load_rw(), load_ew())); }
void CPUExecutor::ADC_rd_ed() { store_rd(ADC_d(load_rd(), load_ed())); }
void CPUExecutor::ADC_AL_ib() { REG_AL = ADC_b(REG_AL, m_instr->ib); }
void CPUExecutor::ADC_AX_iw() { REG_AX = ADC_w(REG_AX, m_instr->iw1); }
void CPUExecutor::ADC_EAX_id(){ REG_EAX = ADC_d(REG_EAX, m_instr->id1); }
void CPUExecutor::ADC_eb_ib() { store_eb(ADC_b(load_eb(), m_instr->ib)); }
void CPUExecutor::ADC_ew_iw() { store_ew(ADC_w(load_ew(), m_instr->iw1)); }
void CPUExecutor::ADC_ed_id() { store_ed(ADC_d(load_ed(), m_instr->id1)); }
void CPUExecutor::ADC_ew_ib() { store_ew(ADC_w(load_ew(), int8_t(m_instr->ib))); }
void CPUExecutor::ADC_ed_ib() { store_ed(ADC_d(load_ed(), int8_t(m_instr->ib))); }

uint8_t CPUExecutor::ADD_b(uint8_t op1, uint8_t op2)
{
	uint8_t res = op1 + op2;

	SET_FLAG(OF, ((op1 ^ op2 ^ 0x80) & (res ^ op2)) & 0x80);
	SET_FLAG(SF, res & 0x80);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, ((op1 ^ op2) ^ res) & 0x10);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(CF, res < op1);

	return res;
}

uint16_t CPUExecutor::ADD_w(uint16_t op1, uint16_t op2)
{
	uint16_t res = op1 + op2;

	SET_FLAG(OF, ((op1 ^ op2 ^ 0x8000) & (res ^ op2)) & 0x8000);
	SET_FLAG(SF, res & 0x8000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, ((op1 ^ op2) ^ res) & 0x10);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(CF, res < op1);

	return res;
}

uint32_t CPUExecutor::ADD_d(uint32_t op1, uint32_t op2)
{
	uint32_t res = op1 + op2;

	SET_FLAG(OF, ((op1 ^ op2 ^ 0x80000000) & (res ^ op2)) & 0x80000000);
	SET_FLAG(SF, res & 0x80000000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, ((op1 ^ op2) ^ res) & 0x10);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(CF, res < op1);

	return res;
}

void CPUExecutor::ADD_eb_rb() { store_eb(ADD_b(load_eb(), load_rb())); }
void CPUExecutor::ADD_ew_rw() { store_ew(ADD_w(load_ew(), load_rw())); }
void CPUExecutor::ADD_ed_rd() { store_ed(ADD_d(load_ed(), load_rd())); }
void CPUExecutor::ADD_rb_eb() { store_rb(ADD_b(load_rb(), load_eb())); }
void CPUExecutor::ADD_rw_ew() { store_rw(ADD_w(load_rw(), load_ew())); }
void CPUExecutor::ADD_rd_ed() { store_rd(ADD_d(load_rd(), load_ed())); }
void CPUExecutor::ADD_AL_ib() { REG_AL = ADD_b(REG_AL, m_instr->ib); }
void CPUExecutor::ADD_AX_iw() { REG_AX = ADD_w(REG_AX, m_instr->iw1); }
void CPUExecutor::ADD_EAX_id(){ REG_EAX = ADD_d(REG_EAX, m_instr->id1); }
void CPUExecutor::ADD_eb_ib() { store_eb(ADD_b(load_eb(), m_instr->ib)); }
void CPUExecutor::ADD_ew_iw() { store_ew(ADD_w(load_ew(), m_instr->iw1)); }
void CPUExecutor::ADD_ed_id() { store_ed(ADD_d(load_ed(), m_instr->id1)); }
void CPUExecutor::ADD_ew_ib() { store_ew(ADD_w(load_ew(), int8_t(m_instr->ib))); }
void CPUExecutor::ADD_ed_ib() { store_ed(ADD_d(load_ed(), int8_t(m_instr->ib))); }


/*******************************************************************************
 * AND-Logical AND
 */

uint8_t CPUExecutor::AND_b(uint8_t op1, uint8_t op2)
{
	uint8_t res = op1 & op2;

	SET_FLAG(OF, false);
	SET_FLAG(SF, res & 0x80);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(CF, false);
	SET_FLAG(AF, false); // unknown

	return res;
}

uint16_t CPUExecutor::AND_w(uint16_t op1, uint16_t op2)
{
	uint16_t res = op1 & op2;

	SET_FLAG(OF, false);
	SET_FLAG(SF, res & 0x8000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(CF, false);
	SET_FLAG(AF, false); // unknown

	return res;
}

uint32_t CPUExecutor::AND_d(uint32_t op1, uint32_t op2)
{
	uint32_t res = op1 & op2;

	SET_FLAG(OF, false);
	SET_FLAG(SF, res & 0x80000000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(CF, false);
	SET_FLAG(AF, false); // unknown

	return res;
}

void CPUExecutor::AND_eb_rb() { store_eb(AND_b(load_eb(),load_rb())); }
void CPUExecutor::AND_ew_rw() { store_ew(AND_w(load_ew(),load_rw())); }
void CPUExecutor::AND_ed_rd() { store_ed(AND_d(load_ed(),load_rd())); }
void CPUExecutor::AND_rb_eb() { store_rb(AND_b(load_rb(),load_eb())); }
void CPUExecutor::AND_rw_ew() { store_rw(AND_w(load_rw(),load_ew())); }
void CPUExecutor::AND_rd_ed() { store_rd(AND_d(load_rd(),load_ed())); }
void CPUExecutor::AND_AL_ib() { REG_AL = AND_b(REG_AL, m_instr->ib); }
void CPUExecutor::AND_AX_iw() { REG_AX = AND_w(REG_AX, m_instr->iw1); }
void CPUExecutor::AND_EAX_id(){ REG_EAX = AND_d(REG_EAX, m_instr->id1); }
void CPUExecutor::AND_eb_ib() { store_eb(AND_b(load_eb(),m_instr->ib)); }
void CPUExecutor::AND_ew_iw() { store_ew(AND_w(load_ew(),m_instr->iw1)); }
void CPUExecutor::AND_ed_id() { store_ed(AND_d(load_ed(),m_instr->id1)); }
void CPUExecutor::AND_ew_ib() { store_ew(AND_w(load_ew(), int8_t(m_instr->ib))); }
void CPUExecutor::AND_ed_ib() { store_ed(AND_d(load_ed(), int8_t(m_instr->ib))); }


/*******************************************************************************
 * ARPL-Adjust RPL Field of Selector
 */

void CPUExecutor::ARPL_ew_rw()
{
	if(!IS_PMODE()) {
		PDEBUGF(LOG_V2, LOG_CPU, "ARPL: not recognized in real or v8086 mode\n");
		throw CPUException(CPU_UD_EXC, 0);
	}

	uint16_t op1 = load_ew();
	uint16_t op2 = load_rw();

	if((op1 & 0x03) < (op2 & 0x03)) {
		op1 = (op1 & SELECTOR_RPL_MASK) | (op2 & 0x03);
		store_ew(op1);
		SET_FLAG(ZF, true);
	} else {
		SET_FLAG(ZF, false);
	}
}


/*******************************************************************************
 * BOUND-Check Array Index Against Bounds
 */

void CPUExecutor::BOUND_rw_md()
{
	int16_t op1 = int16_t(load_rw());
	uint16_t bound_min, bound_max;
	load_m1616(bound_min, bound_max);

	if(op1 < int16_t(bound_min) || op1 > int16_t(bound_max)) {
		PDEBUGF(LOG_V2,LOG_CPU, "BOUND: fails bounds test\n");
		throw CPUException(CPU_BOUND_EXC, 0);
	}
}

void CPUExecutor::BOUND_rd_mq()
{
	int32_t op1 = int32_t(load_rd());
	uint32_t bound_min, bound_max;
	load_m3232(bound_min, bound_max);

	if(op1 < int32_t(bound_min) || op1 > int32_t(bound_max)) {
		PDEBUGF(LOG_V2,LOG_CPU, "BOUND: fails bounds test\n");
		throw CPUException(CPU_BOUND_EXC, 0);
	}
}


/*******************************************************************************
 * BSF-Bit Scan Forward
 */

void CPUExecutor::BSF_rw_ew()
{
	uint16_t op2 = load_ew();

	if(op2 == 0) {
		SET_FLAG(ZF, true);
	} else {
		uint16_t mask = 0x1;
		uint16_t count = 0;
		while((op2 & mask) == 0 && mask) {
			mask <<= 1;
			count++;
		}
		store_rw(count);
		SET_FLAG(ZF, false);
		SET_FLAG(SF, count & 0x8000);
		SET_FLAG(AF, false);
		SET_FLAG(PF, PARITY(count));
		SET_FLAG(OF, false);
		SET_FLAG(CF, false);
	}
}

void CPUExecutor::BSF_rd_ed()
{
	uint32_t op2 = load_ed();

	if(op2 == 0) {
		SET_FLAG(ZF, true);
	} else {
		uint32_t mask = 0x1;
		uint32_t count = 0;
		while((op2 & mask) == 0 && mask) {
			mask <<= 1;
			count++;
		}
		store_rd(count);
		SET_FLAG(ZF, false);
		SET_FLAG(SF, count & 0x80000000);
		SET_FLAG(AF, false);
		SET_FLAG(PF, PARITY(count));
		SET_FLAG(OF, false);
		SET_FLAG(CF, false);
	}
}


/*******************************************************************************
 * BSR-Bit Scan Reverse
 */

void CPUExecutor::BSR_rw_ew()
{
	uint16_t op2 = load_ew();

	if(op2 == 0) {
		SET_FLAG(ZF, true);
	} else {
		uint16_t op1 = 15;
		while((op2 & 0x8000) == 0) {
			op1--;
			op2 <<= 1;
		}
		store_rw(op1);
		SET_FLAG(ZF, false);
		SET_FLAG(SF, op1 & 0x8000);
		SET_FLAG(AF, false);
		SET_FLAG(PF, PARITY(op1));
		SET_FLAG(OF, false);
		SET_FLAG(CF, false);
	}
}

void CPUExecutor::BSR_rd_ed()
{
	uint32_t op2 = load_ed();

	if(op2 == 0) {
		SET_FLAG(ZF, true);
	} else {
		uint32_t op1 = 31;
		while((op2 & 0x80000000) == 0) {
			op1--;
			op2 <<= 1;
		}
		store_rd(op1);
		SET_FLAG(ZF, false);
		SET_FLAG(SF, op1 & 0x80000000);
		SET_FLAG(AF, false);
		SET_FLAG(PF, PARITY(op1));
		SET_FLAG(OF, false);
		SET_FLAG(CF, false);
	}
}


/*******************************************************************************
 * BT-Bit Test
 */

uint16_t CPUExecutor::BT_ew(uint16_t _op2, bool _rmw)
{
	uint16_t op1;

	if(m_instr->modrm.mod == 3) {
		op1 = GEN_REG(m_instr->modrm.rm).word[0];
	} else {
		uint32_t disp = ((uint16_t)(_op2&0xfff0)) / 16;
		uint32_t op1_off = (this->*EA_get_offset)() + 2 * disp;
		if(_rmw) {
			op1 = read_word_rmw((this->*EA_get_segreg)(), op1_off & m_addr_mask);
		} else {
			op1 = read_word((this->*EA_get_segreg)(), op1_off & m_addr_mask);
		}
	}

	SET_FLAG(CF, (op1 >> (_op2 & 0xf)) & 1);

	return op1;
}

uint32_t CPUExecutor::BT_ed(uint32_t _op2, bool _rmw)
{
	uint32_t op1;

	if(m_instr->modrm.mod == 3) {
		op1 = GEN_REG(m_instr->modrm.rm).dword[0];
	} else {
		uint32_t disp = ((uint32_t)(_op2&0xffffffe0)) / 32;
		uint32_t op1_off = (this->*EA_get_offset)() + 4 * disp;
		if(_rmw) {
			op1 = read_dword_rmw((this->*EA_get_segreg)(), op1_off & m_addr_mask);
		} else {
			op1 = read_dword((this->*EA_get_segreg)(), op1_off & m_addr_mask);
		}
	}

	SET_FLAG(CF, (op1 >> (_op2 & 0x1f)) & 1);

	return op1;
}

void CPUExecutor::BT_ew_rw()
{
	BT_ew(load_rw(), false);
}

void CPUExecutor::BT_ed_rd()
{
	BT_ed(load_rd(), false);
}

void CPUExecutor::BT_ew_ib()
{
	uint16_t op1 = load_ew();

	SET_FLAG(CF, (op1 >> (m_instr->ib&0xf)) & 1);
}

void CPUExecutor::BT_ed_ib()
{
	uint32_t op1 = load_ed();

	SET_FLAG(CF, (op1 >> (m_instr->ib&0x1f)) & 1);
}


/*******************************************************************************
 * BTC-Bit Test and Complement
 */

void CPUExecutor::BTC_ew_rw()
{
	uint16_t op2 = load_rw();
	uint16_t op1 = BT_ew(op2, true);

	op1 ^= (1 << (op2 & 0xf));

	store_ew_rmw(op1);
}

void CPUExecutor::BTC_ed_rd()
{
	uint32_t op2 = load_rd();
	uint32_t op1 = BT_ed(op2, true);

	op1 ^= (1 << (op2 & 0x1f));

	store_ed_rmw(op1);
}

void CPUExecutor::BTC_ew_ib()
{
	uint16_t op1 = load_ew();
	uint8_t index = m_instr->ib & 0xf;

	bool cf = bool((op1 >> index) & 1);
	op1 ^= (1 << index);

	store_ew(op1);
	SET_FLAG(CF, cf);
}

void CPUExecutor::BTC_ed_ib()
{
	uint32_t op1 = load_ed();
	uint8_t index = m_instr->ib & 0x1f;

	bool cf = bool((op1 >> index) & 1);
	op1 ^= (1 << index);

	store_ed(op1);
	SET_FLAG(CF, cf);
}


/*******************************************************************************
 * BTR-Bit Test and Reset
 */

void CPUExecutor::BTR_ew_rw()
{
	uint16_t op2 = load_rw();
	uint16_t op1 = BT_ew(op2, true);

	op1 &= ~(1 << (op2 & 0xf));

	store_ew_rmw(op1);
}

void CPUExecutor::BTR_ed_rd()
{
	uint32_t op2 = load_rd();
	uint32_t op1 = BT_ed(op2, true);

	op1 &= ~(1 << (op2 & 0x1f));

	store_ed_rmw(op1);
}

void CPUExecutor::BTR_ew_ib()
{
	uint16_t op1 = load_ew();
	uint8_t index = m_instr->ib & 0xf;

	bool cf = bool((op1 >> index) & 1);
	op1 &= ~(1 << index);

	store_ew(op1);
	SET_FLAG(CF, cf);
}

void CPUExecutor::BTR_ed_ib()
{
	uint32_t op1 = load_ed();
	uint8_t index = m_instr->ib & 0x1f;

	bool cf = bool((op1 >> index) & 1);
	op1 &= ~(1 << index);

	store_ed(op1);
	SET_FLAG(CF, cf);
}


/*******************************************************************************
 * BTS-Bit Test and Set
 */

void CPUExecutor::BTS_ew_rw()
{
	uint16_t op2 = load_rw();
	uint16_t op1 = BT_ew(op2, true);

	op1 |= (1 << (op2 & 0xf));

	store_ew_rmw(op1);
}

void CPUExecutor::BTS_ed_rd()
{
	uint32_t op2 = load_rd();
	uint32_t op1 = BT_ed(op2, true);

	op1 |= (1 << (op2 & 0x1f));

	store_ed_rmw(op1);
}

void CPUExecutor::BTS_ew_ib()
{
	uint16_t op1 = load_ew();
	uint8_t index = m_instr->ib & 0xf;

	bool cf = bool((op1 >> index) & 1);
	op1 |= (1 << index);

	store_ew(op1);
	SET_FLAG(CF, cf);
}

void CPUExecutor::BTS_ed_ib()
{
	uint32_t op1 = load_ed();
	uint8_t index = m_instr->ib & 0x1f;

	bool cf = bool((op1 >> index) & 1);
	op1 |= (1 << index);

	store_ed(op1);
	SET_FLAG(CF, cf);
}


/*******************************************************************************
 * CALL-Call Procedure
 */

void CPUExecutor::CALL_rel16() { call_relative(int16_t(m_instr->iw1)); }
void CPUExecutor::CALL_rel32() { call_relative(int32_t(m_instr->id1)); }

void CPUExecutor::CALL_ew()
{
	uint16_t new_IP = load_ew();

	/* push 16 bit EA of next instruction */
	stack_push_word(REG_IP);

	branch_near(new_IP);
}

void CPUExecutor::CALL_ed()
{
	uint32_t new_EIP = load_ed();

	/* push 32 bit EA of next instruction */
	stack_push_dword(REG_EIP);

	branch_near(new_EIP);
}

void CPUExecutor::CALL_ptr1616() { call_16(m_instr->iw2, m_instr->iw1); }
void CPUExecutor::CALL_ptr1632() { call_32(m_instr->iw2, m_instr->id1); }

void CPUExecutor::CALL_m1616()
{
	uint16_t ip, cs;
	load_m1616(ip, cs);

	call_16(cs, ip);
}

void CPUExecutor::CALL_m1632()
{
	uint32_t eip; uint16_t cs;
	load_m1632(eip, cs);

	call_32(cs, eip);
}


/*******************************************************************************
 * CBW/CWD/CWDE/CDQ-Convert Byte/Word/DWord to Word/DWord/QWord
 */

void CPUExecutor::CBW()
{
	/* CBW: no flags are effected */
	REG_AX = int8_t(REG_AL);
}

void CPUExecutor::CWD()
{
	if(REG_AX & 0x8000) {
		REG_DX = 0xFFFF;
	} else {
		REG_DX = 0;
	}
}

void CPUExecutor::CWDE()
{
	REG_EAX = int16_t(REG_AX);
}

void CPUExecutor::CDQ()
{
	if(REG_EAX & 0x80000000) {
		REG_EDX = 0xFFFFFFFF;
	} else {
		REG_EDX = 0;
	}
}


/*******************************************************************************
 * CLC/CLD/CLI/CLTS-Clear Flags
 */

void CPUExecutor::CLC()
{
	SET_FLAG(CF, false);
}

void CPUExecutor::CLD()
{
	SET_FLAG(DF, false);
}

void CPUExecutor::CLI()
{
	if(!IS_RMODE() && (FLAG_IOPL < CPL)) {
		PDEBUGF(LOG_V2, LOG_CPU, "CLI: IOPL < CPL\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	SET_FLAG(IF, false);
}

void CPUExecutor::CLTS()
{
	check_CPL_privilege(!IS_RMODE(), "CLTS");
	SET_CR0BIT(TS, false);
}


/*******************************************************************************
 * CMC-Complement Carry Flag
 */

void CPUExecutor::CMC()
{
	SET_FLAG(CF, !FLAG_CF);
}


/*******************************************************************************
 * CMP-Compare Two Operands
 */

void CPUExecutor::CMP_b(uint8_t op1, uint8_t op2)
{
	uint8_t res = op1 - op2;

	SET_FLAG(OF, ((op1 ^ op2) & (op1 ^ res)) & 0x80);
	SET_FLAG(SF, res & 0x80);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, ((op1 ^ op2) ^ res) & 0x10);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(CF, op1<op2);
}

void CPUExecutor::CMP_w(uint16_t op1, uint16_t op2)
{
	uint16_t res = op1 - op2;

	SET_FLAG(OF, ((op1 ^ op2) & (op1 ^ res)) & 0x8000);
	SET_FLAG(SF, res & 0x8000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, ((op1 ^ op2) ^ res) & 0x10);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(CF, op1<op2);
}

void CPUExecutor::CMP_d(uint32_t op1, uint32_t op2)
{
	uint32_t res = op1 - op2;

	SET_FLAG(OF, ((op1 ^ op2) & (op1 ^ res)) & 0x80000000);
	SET_FLAG(SF, res & 0x80000000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, ((op1 ^ op2) ^ res) & 0x10);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(CF, op1<op2);
}

void CPUExecutor::CMP_eb_rb() { CMP_b(load_eb(), load_rb()); }
void CPUExecutor::CMP_ew_rw() { CMP_w(load_ew(), load_rw()); }
void CPUExecutor::CMP_ed_rd() { CMP_d(load_ed(), load_rd()); }
void CPUExecutor::CMP_rb_eb() { CMP_b(load_rb(), load_eb()); }
void CPUExecutor::CMP_rw_ew() { CMP_w(load_rw(), load_ew()); }
void CPUExecutor::CMP_rd_ed() { CMP_d(load_rd(), load_ed()); }
void CPUExecutor::CMP_AL_ib() { CMP_b(REG_AL, m_instr->ib); }
void CPUExecutor::CMP_AX_iw() { CMP_w(REG_AX, m_instr->iw1); }
void CPUExecutor::CMP_EAX_id(){ CMP_d(REG_EAX, m_instr->id1); }
void CPUExecutor::CMP_eb_ib() { CMP_b(load_eb(), m_instr->ib); }
void CPUExecutor::CMP_ew_iw() { CMP_w(load_ew(), m_instr->iw1); }
void CPUExecutor::CMP_ed_id() { CMP_d(load_ed(), m_instr->id1); }
void CPUExecutor::CMP_ew_ib() { CMP_w(load_ew(), int8_t(m_instr->ib)); }
void CPUExecutor::CMP_ed_ib() { CMP_d(load_ed(), int8_t(m_instr->ib)); }


/*******************************************************************************
 * CMPS/CMPSB/CMPSW/CMPSWD-Compare string operands
 */

void CPUExecutor::CMPSB_a16()
{
	uint8_t op1 = read_byte(SEG_REG(m_base_ds), REG_SI);
	uint8_t op2 = read_byte(REG_ES, REG_DI);

	CMP_b(op1, op2);

	if(FLAG_DF) {
		REG_SI -= 1;
		REG_DI -= 1;
	} else {
		REG_SI += 1;
		REG_DI += 1;
	}
}

void CPUExecutor::CMPSW_a16()
{
	uint16_t op1 = read_word(SEG_REG(m_base_ds), REG_SI);
	uint16_t op2 = read_word(REG_ES, REG_DI);

	CMP_w(op1, op2);

	if(FLAG_DF) {
		REG_SI -= 2;
		REG_DI -= 2;
	} else {
		REG_SI += 2;
		REG_DI += 2;
	}
}

void CPUExecutor::CMPSD_a16()
{
	uint32_t op1 = read_dword(SEG_REG(m_base_ds), REG_SI);
	uint32_t op2 = read_dword(REG_ES, REG_DI);

	CMP_d(op1, op2);

	if(FLAG_DF) {
		REG_SI -= 4;
		REG_DI -= 4;
	} else {
		REG_SI += 4;
		REG_DI += 4;
	}
}

void CPUExecutor::CMPSB_a32()
{
	uint8_t op1 = read_byte(SEG_REG(m_base_ds), REG_ESI);
	uint8_t op2 = read_byte(REG_ES, REG_EDI);

	CMP_b(op1, op2);

	if(FLAG_DF) {
		REG_ESI -= 1;
		REG_EDI -= 1;
	} else {
		REG_ESI += 1;
		REG_EDI += 1;
	}
}

void CPUExecutor::CMPSW_a32()
{
	uint16_t op1 = read_word(SEG_REG(m_base_ds), REG_ESI);
	uint16_t op2 = read_word(REG_ES, REG_EDI);

	CMP_w(op1, op2);

	if(FLAG_DF) {
		REG_ESI -= 2;
		REG_EDI -= 2;
	} else {
		REG_ESI += 2;
		REG_EDI += 2;
	}
}

void CPUExecutor::CMPSD_a32()
{
	uint32_t op1 = read_dword(SEG_REG(m_base_ds), REG_ESI);
	uint32_t op2 = read_dword(REG_ES, REG_EDI);

	CMP_d(op1, op2);

	if(FLAG_DF) {
		REG_ESI -= 4;
		REG_EDI -= 4;
	} else {
		REG_ESI += 4;
		REG_EDI += 4;
	}
}


/*******************************************************************************
 * DAA/DAS-Decimal Adjust AL after addition/subtraction
 */

void CPUExecutor::DAA()
{
	// WARNING: Old Intel docs are wrong!
	// Used recent (2017) version of the developer's manual.

	uint8_t al = REG_AL;
	bool cf = false, af = false;

	if(((al & 0x0F) > 0x09) || FLAG_AF) {
		cf = ((al > 0xF9) || FLAG_CF);
		REG_AL = REG_AL + 0x06;
		af = true;
	}
	if((al > 0x99) || FLAG_CF) {
		REG_AL = REG_AL + 0x60;
		cf = true;
	}
	SET_FLAG(CF, cf);
	SET_FLAG(AF, af);
	SET_FLAG(SF, REG_AL & 0x80);
	SET_FLAG(ZF, REG_AL == 0);
	SET_FLAG(PF, PARITY(REG_AL));
}

void CPUExecutor::DAS()
{
	// WARNING: Old Intel docs are wrong!
	// Used recent (2017) version of the developer's manual.

	uint8_t al = REG_AL;
	bool cf = false, af = false;

	if(((al & 0x0F) > 0x09) || FLAG_AF) {
		cf = (al < 0x06) || FLAG_CF;
		REG_AL = REG_AL - 0x06;
		af = true;
	}
	if((al > 0x99) || FLAG_CF) {
		REG_AL = REG_AL - 0x60;
		cf = true;
	}

	SET_FLAG(CF, cf);
	SET_FLAG(AF, af);
	SET_FLAG(SF, REG_AL & 0x80);
	SET_FLAG(ZF, REG_AL == 0);
	SET_FLAG(PF, PARITY(REG_AL));
}


/*******************************************************************************
 * DEC-Decrement by 1
 */

void CPUExecutor::DEC_eb()
{
	uint8_t op1 = load_eb();
	uint8_t res = op1 - 1;
	store_eb(res);

	SET_FLAG(OF, res == 0x7f);
	SET_FLAG(SF, res & 0x80);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, (res & 0x0f) == 0x0f);
	SET_FLAG(PF, PARITY(res));
}

uint16_t CPUExecutor::DEC_w(uint16_t _op1)
{
	uint16_t res = _op1 - 1;

	SET_FLAG(OF, res == 0x7fff);
	SET_FLAG(SF, res & 0x8000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, (res & 0x0f) == 0x0f);
	SET_FLAG(PF, PARITY(res));

	return res;
}

uint32_t CPUExecutor::DEC_d(uint32_t _op1)
{
	uint32_t res = _op1 - 1;

	SET_FLAG(OF, res == 0x7fffffff);
	SET_FLAG(SF, res & 0x80000000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, (res & 0x0f) == 0x0f);
	SET_FLAG(PF, PARITY(res));

	return res;
}

void CPUExecutor::DEC_ew() { store_ew(DEC_w(load_ew())); }
void CPUExecutor::DEC_ed() { store_ed(DEC_d(load_ed())); }
void CPUExecutor::DEC_rw_op() { store_rw_op(DEC_w(load_rw_op())); }
void CPUExecutor::DEC_rd_op() { store_rd_op(DEC_d(load_rd_op())); }


/*******************************************************************************
 * DIV-Unsigned Divide
 */

void CPUExecutor::DIV_eb()
{
	uint8_t op2 = load_eb();
	if(op2 == 0) {
		throw CPUException(CPU_DIV_ER_EXC, 0);
	}

	uint16_t op1 = REG_AX;

	uint16_t quotient_16 = op1 / op2;
	uint8_t remainder_8 = op1 % op2;
	uint8_t quotient_8l = quotient_16 & 0xFF;

	if(quotient_16 != quotient_8l) {
		throw CPUException(CPU_DIV_ER_EXC, 0);
	}

	/* now write quotient back to destination */
	REG_AL = quotient_8l;
	REG_AH = remainder_8;
}

void CPUExecutor::DIV_ew()
{
	uint16_t op2_16 = load_ew();
	if(op2_16 == 0) {
		throw CPUException(CPU_DIV_ER_EXC, 0);
	}

	uint32_t op1_32 = (uint32_t(REG_DX) << 16) | uint32_t(REG_AX);

	uint32_t quotient_32  = op1_32 / op2_16;
	uint16_t remainder_16 = op1_32 % op2_16;
	uint16_t quotient_16l = quotient_32 & 0xFFFF;

	if(quotient_32 != quotient_16l) {
		throw CPUException(CPU_DIV_ER_EXC, 0);
	}

	/* now write quotient back to destination */
	REG_AX = quotient_16l;
	REG_DX = remainder_16;
}

void CPUExecutor::DIV_ed()
{
	uint32_t op2_32 = load_ed();
	if(op2_32 == 0) {
		throw CPUException(CPU_DIV_ER_EXC, 0);
	}

	uint64_t op1_64 = (uint64_t(REG_EDX) << 32) | uint64_t(REG_EAX);

	uint64_t quotient_64  = op1_64 / op2_32;
	uint32_t remainder_32 = op1_64 % op2_32;
	uint32_t quotient_32l = quotient_64 & 0xFFFFFFFF;

	if(quotient_64 != quotient_32l) {
		throw CPUException(CPU_DIV_ER_EXC, 0);
	}

	/* now write quotient back to destination */
	REG_EAX = quotient_32l;
	REG_EDX = remainder_32;
}


/*******************************************************************************
 * ENTER-Make Stack Frame for Procedure Parameters
 */

void CPUExecutor::ENTER_o16()
{
	uint8_t nesting_level = m_instr->ib & 0x1F;
	uint16_t   alloc_size = m_instr->iw1;

	stack_push_word(REG_BP);
	uint16_t frame_ptr = REG_SP;

	if(REG_SS.desc.big) {
		uint32_t ebp = REG_EBP;
		if(nesting_level > 0) {
			while(--nesting_level) {
				ebp -= 2;
				uint16_t temp16 = read_word(REG_SS, ebp);
				stack_push_word(temp16);
			}
			stack_push_word(frame_ptr);
		}

		REG_ESP -= alloc_size;

		// ENTER finishes with memory write check on the final stack pointer
		seg_check(REG_SS, REG_ESP, 2, true, CPU_SS_EXC, 0);
		/* The ENTER instruction causes a page fault whenever a write using the
		 * final value of the stack pointer (within the current stack segment)
		 * would do so.
		 */
		mmu_lookup(REG_SS.desc.base + REG_ESP, 2, IS_USER_PL, true);
	} else {
		uint16_t bp = REG_BP;
		if(nesting_level > 0) {
			while(--nesting_level) {
				bp -= 2;
				uint16_t temp16 = read_word(REG_SS, bp);
				stack_push_word(temp16);
			}
			stack_push_word(frame_ptr);
		}

		REG_SP -= alloc_size;

		seg_check(REG_SS, REG_SP, 2, true, CPU_SS_EXC, 0);
		mmu_lookup(REG_SS.desc.base + REG_SP, 2, IS_USER_PL, true);
	}

	REG_BP = frame_ptr;
}

void CPUExecutor::ENTER_o32()
{
	uint8_t nesting_level = m_instr->ib & 0x1F;
	uint16_t   alloc_size = m_instr->iw1;

	stack_push_dword(REG_EBP);
	uint32_t frame_ptr = REG_ESP;

	if(REG_SS.desc.big) {
		uint32_t ebp = REG_EBP;
		if(nesting_level > 0) {
			while(--nesting_level) {
				ebp -= 4;
				uint32_t temp32 = read_dword(REG_SS, ebp);
				stack_push_dword(temp32);
			}
			stack_push_dword(frame_ptr);
		}

		REG_ESP -= alloc_size;

		// ENTER finishes with memory write check on the final stack pointer
		seg_check(REG_SS, REG_ESP, 4, true, CPU_SS_EXC, 0);
		/* The ENTER instruction causes a page fault whenever a write using the
		 * final value of the stack pointer (within the current stack segment)
		 * would do so.
		 */
		mmu_lookup(REG_SS.desc.base + REG_ESP, 4, IS_USER_PL, true);
	} else {
		uint16_t bp = REG_BP;
		if(nesting_level > 0) {
			while(--nesting_level) {
				bp -= 4;
				uint32_t temp32 = read_dword(REG_SS, bp);
				stack_push_dword(temp32);
			}
			stack_push_dword(frame_ptr);
		}

		REG_SP -= alloc_size;

		seg_check(REG_SS, REG_SP, 4, true, CPU_SS_EXC, 0);
		mmu_lookup(REG_SS.desc.base + REG_SP, 4, IS_USER_PL, true);
	}

	REG_EBP = frame_ptr;
}


/*******************************************************************************
 * FPU ESC
 * this function should be used only if there's no FPU installed (TODO?)
 */

void CPUExecutor::FPU_ESC()
{
	if(CR0_EM || CR0_TS) {
		throw CPUException(CPU_NM_EXC, 0);
	}
}


/*******************************************************************************
 * HLT-Halt
 */

void CPUExecutor::HLT()
{
	check_CPL_privilege(!IS_RMODE(), "HLT");

	if(!FLAG_IF) {
		PWARNF(LOG_CPU, "HLT instruction with IF=0!");
		PWARNF(LOG_CPU, " CS:IP=%04X:%04X\n", REG_CS.sel.value, REG_IP);
	}

	// stops instruction execution and places the processor in a
	// HALT state. An enabled interrupt, NMI, or reset will resume
	// execution. If interrupt (including NMI) is used to resume
	// execution after HLT, the saved CS:IP points to instruction
	// following HLT.
	g_cpu.enter_sleep_state(CPU_STATE_HALT);
}


/*******************************************************************************
 * IDIV-Signed Divide
 */

void CPUExecutor::IDIV_eb()
{
	int16_t op1 = int16_t(REG_AX);

	/* check MIN_INT case */
	if(op1 == int16_t(0x8000)) {
		throw CPUException(CPU_DIV_ER_EXC, 0);
	}

	int8_t op2 = int8_t(load_eb());

	if(op2 == 0) {
		throw CPUException(CPU_DIV_ER_EXC, 0);
	}

	int16_t quotient_16 = op1 / op2;
	int8_t remainder_8 = op1 % op2;
	int8_t quotient_8l = quotient_16 & 0xFF;

	if (quotient_16 != quotient_8l) {
		throw CPUException(CPU_DIV_ER_EXC, 0);
	}

	/* now write quotient back to destination */
	REG_AL = quotient_8l;
	REG_AH = remainder_8;
}

void CPUExecutor::IDIV_ew()
{
	int32_t op1_32 = int32_t((uint32_t(REG_DX) << 16) | uint32_t(REG_AX));

	/* check MIN_INT case */
	if(op1_32 == int32_t(0x80000000)) {
		throw CPUException(CPU_DIV_ER_EXC, 0);
	}

	int16_t op2_16 = int16_t(load_ew());

	if(op2_16 == 0) {
		throw CPUException(CPU_DIV_ER_EXC, 0);
	}

	int32_t quotient_32  = op1_32 / op2_16;
	int16_t remainder_16 = op1_32 % op2_16;
	int16_t quotient_16l = quotient_32 & 0xFFFF;

	if(quotient_32 != quotient_16l) {
		throw CPUException(CPU_DIV_ER_EXC, 0);
	}

	/* now write quotient back to destination */
	REG_AX = quotient_16l;
	REG_DX = remainder_16;
}

void CPUExecutor::IDIV_ed()
{
	int64_t op1_64 = int64_t((uint64_t(REG_EDX) << 32) | uint64_t(REG_EAX));

	/* check MIN_INT case */
	if(op1_64 == int64_t(0x8000000000000000LL)) {
		throw CPUException(CPU_DIV_ER_EXC, 0);
	}

	int32_t op2_32 = int32_t(load_ed());

	if(op2_32 == 0) {
		throw CPUException(CPU_DIV_ER_EXC, 0);
	}

	int64_t quotient_64  = op1_64 / op2_32;
	int32_t remainder_32 = op1_64 % op2_32;
	int32_t quotient_32l = quotient_64 & 0xFFFFFFFF;

	if(quotient_64 != quotient_32l) {
		throw CPUException(CPU_DIV_ER_EXC, 0);
	}

	/* now write quotient back to destination */
	REG_EAX = quotient_32l;
	REG_EDX = remainder_32;
}


/*******************************************************************************
 * IMUL-Signed Multiply
 */

inline static int mul_cycles_386(int _m)
{
	/* The 80386 uses an early-out multiply algorithm. The actual number of
	clocks depends on the position of the most significant bit in the
	optimizing multiplier. The optimization occurs for positive and negative
	values. To calculate the actual clocks, use	the following formula:
	Clock = if m <> 0 then max(ceiling(log₂│m│), 3) + 6 clocks
	Clock = if m = 0 then 9 clocks
	(where m is the multiplier)
	*/
	if(_m != 0) {
		return std::max(int(std::ceil(std::log2(std::abs(_m)))), 3);
	} else {
		return 3;
	}
}

void CPUExecutor::IMUL_eb()
{
	int8_t op1 = int8_t(REG_AL);
	int8_t op2 = int8_t(load_eb());

	int16_t product_16 = int16_t(op1) * int16_t(op2);
	uint8_t product_8 = (product_16 & 0xFF);

	/* now write product back to destination */
	REG_AX = product_16;

	/* IMUL r/m8: condition for clearing CF & OF:
	 *   AX = sign-extend of AL to 16 bits
	 */
	if((product_16 & 0xff80)==0xff80 || (product_16 & 0xff80)==0) {
		SET_FLAG(CF, false);
		SET_FLAG(OF, false);
	} else {
		SET_FLAG(CF, true);
		SET_FLAG(OF, true);
	}
	SET_FLAG(SF, product_8 & 0x80);
	SET_FLAG(ZF, product_8 == 0);
	SET_FLAG(AF, false);
	SET_FLAG(PF, PARITY(product_8));

	if(CPU_FAMILY == CPU_386) {
		m_instr->cycles.extra = mul_cycles_386(op2);
	}
}

void CPUExecutor::IMUL_ew()
{
	int16_t op1_16 = int16_t(REG_AX);
	int16_t op2_16 = int16_t(load_ew());

	int32_t product_32 = int32_t(op1_16) * int32_t(op2_16);
	uint16_t product_16l = (product_32 & 0xFFFF);
	uint16_t product_16h = product_32 >> 16;

	/* now write product back to destination */
	REG_AX = product_16l;
	REG_DX = product_16h;

	/* IMUL r/m16: condition for clearing CF & OF:
	 *   DX:AX = sign-extend of AX
	 */
	if(((product_32 & 0xffff8000)==0xffff8000 || (product_32 & 0xffff8000)==0)) {
		SET_FLAG(CF, false);
		SET_FLAG(OF, false);
	} else {
		SET_FLAG(CF, true);
		SET_FLAG(OF, true);
	}
	SET_FLAG(SF, product_16l & 0x8000);
	SET_FLAG(ZF, product_16l == 0);
	SET_FLAG(AF, false);
	SET_FLAG(PF, PARITY(product_16l));

	if(CPU_FAMILY == CPU_386) {
		m_instr->cycles.extra = mul_cycles_386(op2_16);
	}
}

void CPUExecutor::IMUL_ed()
{
	int32_t op1_32 = int32_t(REG_EAX);
	int32_t op2_32 = int32_t(load_ed());

	int64_t product_64 = int64_t(op1_32) * int64_t(op2_32);
	uint32_t product_32l = (product_64 & 0xFFFFFFFF);
	uint32_t product_32h = product_64 >> 32;

	/* now write product back to destination */
	REG_EAX = product_32l;
	REG_EDX = product_32h;

	/* IMUL r/m32: condition for clearing CF & OF:
	 *   EDX:EAX = sign-extend of EAX
	 */
	if(product_64 != int32_t(product_64)) {
		SET_FLAG(CF, true);
		SET_FLAG(OF, true);
	} else {
		SET_FLAG(CF, false);
		SET_FLAG(OF, false);
	}
	SET_FLAG(SF, product_32l & 0x80000000);
	SET_FLAG(ZF, product_32l == 0);
	SET_FLAG(AF, false);
	SET_FLAG(PF, PARITY(product_32l));

	if(CPU_FAMILY == CPU_386) {
		m_instr->cycles.extra = mul_cycles_386(op2_32);
	}
}

int16_t CPUExecutor::IMUL_w(int16_t _op1, int16_t _op2)
{
	int32_t  product_32  = int32_t(_op1) * int32_t(_op2);
	uint16_t product_16 = (product_32 & 0xFFFF);

	// CF and OF are cleared if the result fits in a r16
	if(product_32 != int16_t(product_32)) {
		SET_FLAG(CF, true);
		SET_FLAG(OF, true);
	} else {
		SET_FLAG(CF, false);
		SET_FLAG(OF, false);
	}
	SET_FLAG(SF, product_16 & 0x8000);
	SET_FLAG(ZF, product_16 == 0);
	SET_FLAG(AF, false);
	SET_FLAG(PF, PARITY(product_16));

	if(CPU_FAMILY == CPU_386) {
		m_instr->cycles.extra = mul_cycles_386(_op2);
	}

	return product_16;
}

int32_t CPUExecutor::IMUL_d(int32_t _op1, int32_t _op2)
{
	int64_t  product_64  = int64_t(_op1) * int64_t(_op2);
	uint32_t product_32 = uint32_t(product_64 & 0xFFFFFFFF);

	// CF and OF are cleared if the result fits in a r32
	if(product_64 != int32_t(product_64)) {
		SET_FLAG(CF, true);
		SET_FLAG(OF, true);
	} else {
		SET_FLAG(CF, false);
		SET_FLAG(OF, false);
	}
	SET_FLAG(SF, product_32 & 0x80000000);
	SET_FLAG(ZF, product_32 == 0);
	SET_FLAG(AF, false);
	SET_FLAG(PF, PARITY(product_32));

	if(CPU_FAMILY == CPU_386) {
		m_instr->cycles.extra = mul_cycles_386(_op2);
	}

	return product_32;
}

void CPUExecutor::IMUL_rw_ew()    { store_rw(IMUL_w(load_rw(), load_ew())); }
void CPUExecutor::IMUL_rd_ed()    { store_rd(IMUL_d(load_rd(), load_ed())); }
void CPUExecutor::IMUL_rw_ew_ib() { store_rw(IMUL_w(load_ew(), int8_t(m_instr->ib))); }
void CPUExecutor::IMUL_rd_ed_ib() { store_rd(IMUL_d(load_ed(), int8_t(m_instr->ib))); }
void CPUExecutor::IMUL_rw_ew_iw() { store_rw(IMUL_w(load_ew(), m_instr->iw1)); }
void CPUExecutor::IMUL_rd_ed_id() { store_rd(IMUL_d(load_ed(), m_instr->id1)); }


/*******************************************************************************
 * IN-Input from Port
 */

void CPUExecutor::IN_AL_ib()
{
	io_check(m_instr->ib, 1);
	REG_AL = g_devices.read_byte(m_instr->ib);
}

void CPUExecutor::IN_AL_DX()
{
	io_check(REG_DX, 1);
	REG_AL = g_devices.read_byte(REG_DX);
}

void CPUExecutor::IN_AX_ib()
{
	io_check(m_instr->ib, 2);
	REG_AX = g_devices.read_word(m_instr->ib);
}

void CPUExecutor::IN_AX_DX()
{
	io_check(REG_DX, 2);
	REG_AX = g_devices.read_word(REG_DX);
}

void CPUExecutor::IN_EAX_ib()
{
	io_check(m_instr->ib, 4);
	REG_EAX = g_devices.read_dword(m_instr->ib);
}

void CPUExecutor::IN_EAX_DX()
{
	io_check(REG_DX, 4);
	REG_EAX = g_devices.read_dword(REG_DX);
}


/*******************************************************************************
 * INC-Increment by 1
 */

void CPUExecutor::INC_eb()
{
	uint8_t op1 = load_eb();
	uint8_t res = op1 + 1;
	store_eb(res);

	SET_FLAG(OF, res == 0x80);
	SET_FLAG(SF, res & 0x80);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, (res & 0x0f) == 0);
	SET_FLAG(PF, PARITY(res));
}

uint16_t CPUExecutor::INC_w(uint16_t _op1)
{
	uint16_t res = _op1 + 1;

	SET_FLAG(OF, res == 0x8000);
	SET_FLAG(SF, res & 0x8000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, (res & 0x0f) == 0);
	SET_FLAG(PF, PARITY(res));

	return res;
}

uint32_t CPUExecutor::INC_d(uint32_t _op1)
{
	uint32_t res = _op1 + 1;

	SET_FLAG(OF, res == 0x80000000);
	SET_FLAG(SF, res & 0x80000000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, (res & 0x0f) == 0);
	SET_FLAG(PF, PARITY(res));

	return res;
}

void CPUExecutor::INC_ew() { store_ew(INC_w(load_ew())); }
void CPUExecutor::INC_ed() { store_ed(INC_d(load_ed())); }
void CPUExecutor::INC_rw_op() { store_rw_op(INC_w(load_rw_op())); }
void CPUExecutor::INC_rd_op() { store_rd_op(INC_d(load_rd_op())); }


/*******************************************************************************
 * INSB/INSW/INSD-Input from Port to String
 */

void CPUExecutor::INSB(uint32_t _offset)
{
	// trigger any faults before reading from I/O port
	if(m_instr->rep && m_instr->rep_first) {
		io_check(REG_DX, 1);
	}
	/* The memory operand must be addressable from the ES register; no segment
	 * override is possible.
	 */
	seg_check(REG_ES, _offset, 1, true);
	mmu_lookup(REG_ES.desc.base + _offset, 1, IS_USER_PL, true);

	uint8_t value = g_devices.read_byte(REG_DX);
	write_byte(value);
}

void CPUExecutor::INSB_a16()
{
	INSB(REG_DI);

	if(FLAG_DF) {
		REG_DI -= 1;
	} else {
		REG_DI += 1;
	}
}

void CPUExecutor::INSB_a32()
{
	INSB(REG_EDI);

	if(FLAG_DF) {
		REG_EDI -= 1;
	} else {
		REG_EDI += 1;
	}
}

void CPUExecutor::INSW(uint32_t _offset)
{
	if(m_instr->rep && m_instr->rep_first) {
		io_check(REG_DX, 2);
	}

	seg_check(REG_ES, _offset, 2, true);
	mmu_lookup(REG_ES.desc.base + _offset, 2, IS_USER_PL, true);

	uint16_t value = g_devices.read_word(REG_DX);
	write_word(value);
}

void CPUExecutor::INSW_a16()
{
	INSW(REG_DI);

	if(FLAG_DF) {
		REG_DI -= 2;
	} else {
		REG_DI += 2;
	}
}

void CPUExecutor::INSW_a32()
{
	INSW(REG_EDI);

	if(FLAG_DF) {
		REG_EDI -= 2;
	} else {
		REG_EDI += 2;
	}
}

void CPUExecutor::INSD(uint32_t _offset)
{
	if(m_instr->rep && m_instr->rep_first) {
		io_check(REG_DX, 4);
	}

	seg_check(REG_ES, _offset, 4, true);
	mmu_lookup(REG_ES.desc.base + _offset, 4, IS_USER_PL, true);

	uint32_t value = g_devices.read_dword(REG_DX);
	write_dword(value);
}

void CPUExecutor::INSD_a16()
{
	INSD(REG_DI);

	if(FLAG_DF) {
		REG_DI -= 4;
	} else {
		REG_DI += 4;
	}
}

void CPUExecutor::INSD_a32()
{
	INSD(REG_EDI);

	if(FLAG_DF) {
		REG_EDI -= 4;
	} else {
		REG_EDI += 4;
	}
}


/*******************************************************************************
 * INT/INTO-Call to Interrupt Procedure
 */

bool CPUExecutor::INT_debug(bool call, uint8_t vector, uint16_t ax, CPUCore *core, Memory *mem)
{
	const char * str = CPUDebugger::INT_decode(call, vector, ax, core, mem);
	if(str != nullptr) {
		PDEBUGF(LOG_V1, LOG_CPU, "%s\n", str);
	}
	return true;
}

void CPUExecutor::INT(uint8_t _vector, unsigned _type)
{
	uint8_t ah = REG_AH;
	uint32_t retaddr = REG_CS.desc.base + REG_EIP;

	if(INT_TRAPS) {
		std::vector<inttrap_interval_t> results;
		m_inttraps_tree.findOverlapping(_vector, _vector, results);
		if(!results.empty()) {
			bool res = false;
			auto retinfo = m_inttraps_ret.insert(
				std::pair<uint32_t, std::vector<std::function<bool()>>>(
					retaddr, std::vector<std::function<bool()>>()
				)
			).first;
			for(auto t : results) {
				res |= t.value(true, _vector, REG_AX, &g_cpucore, &g_memory);

				auto retfunc = std::bind(t.value, false, _vector, REG_AX, &g_cpucore, &g_memory);
				retinfo->second.push_back(retfunc);
			}
			if(!res) {
				return;
			}
		}
	}

	/* If it's INT 21/4Bh (LOAD AND/OR EXECUTE PROGRAM) then try to determine
	 * the program name so that it can be displayed on the GUI or reported in
	 * logs.
	 */
	if(_vector == 0x21 && ah == 0x4B) {
		const char *pname;
		try {
			uint32_t nameaddr = DBG_GET_PHYADDR(DS, REG_DX);
			pname = (char*)g_memory.get_buffer_ptr(nameaddr);
		} catch(CPUException &) {
			pname = "[unknown]";
		}
		PDEBUGF(LOG_V1, LOG_CPU, "exec %s\n", pname);
		g_machine.DOS_program_launch(pname);
		m_dos_prg.push(std::pair<uint32_t,std::string>(retaddr,pname));
		if(!CPULOG || CPULOG_INT21_EXIT_IP==-1 || IS_PMODE()) {
			g_machine.DOS_program_start(pname);
		} else {
			//find the INT exit point
			uint32_t cs = g_memory.dbg_read_word(0x21*4 + 2);
			m_dos_prg_int_exit = (cs<<4) + CPULOG_INT21_EXIT_IP;
		}
	}
	else if((_vector == 0x21 && (
			ah==0x31 || //DOS 2+ - TERMINATE AND STAY RESIDENT
			ah==0x4C    //DOS 2+ - EXIT - TERMINATE WITH RETURN CODE
		)) ||
			_vector == 0x27 //DOS 1+ - TERMINATE AND STAY RESIDENT
	)
	{
		std::string oldprg,newprg;
		if(!m_dos_prg.empty()) {
			oldprg = m_dos_prg.top().second;
			m_dos_prg.pop();
			if(!m_dos_prg.empty()) {
				newprg = m_dos_prg.top().second;
			}
		}
		g_machine.DOS_program_finish(oldprg,newprg);
		m_dos_prg_int_exit = 0;
	}

	g_cpu.interrupt(_vector, _type, false, 0);
}

void CPUExecutor::INT1()   { INT(1, CPU_PRIVILEGED_SOFTWARE_INTERRUPT); }
void CPUExecutor::INT3()   { INT(3, CPU_SOFTWARE_EXCEPTION); }
void CPUExecutor::INT_ib() { INT(m_instr->ib, CPU_SOFTWARE_INTERRUPT); }
void CPUExecutor::INTO()   { if(FLAG_OF) INT(4, CPU_SOFTWARE_EXCEPTION); }


/*******************************************************************************
 * IRET-Interrupt Return
 */

void CPUExecutor::IRET()
{
	g_cpu.unmask_event(CPU_EVENT_NMI);

	if(IS_PMODE()) {
		iret_pmode(false);
	} else {
		// real and v8086 modes
		if(IS_V8086() && (FLAG_IOPL < 3)) {
			PDEBUGF(LOG_V2, LOG_CPU, "IRET: IOPL!=3 in v8086 mode\n");
			throw CPUException(CPU_GP_EXC, 0);
		}

		uint16_t ip     = stack_pop_word();
		uint16_t cs_raw = stack_pop_word(); // #SS has higher priority
		uint16_t flags  = stack_pop_word();

		// CS LIMIT can't change when in real mode
		if(ip > REG_CS.desc.limit) {
			PDEBUGF(LOG_V2, LOG_CPU,
				"IRET: instruction pointer not within code segment limits\n");
			throw CPUException(CPU_GP_EXC, 0);
		}

		SET_CS(cs_raw);
		SET_IP(ip);

		if(CPU_FAMILY == CPU_286) {
			// in real mode IOPL and NT are always clear
			write_flags(flags,
				false, // IOPL
				true,  // IF
				false  // NT
			);
		} else {
			write_flags(flags,
				IS_RMODE(), // IOPL
				true,       // IF
				true        // NT
				);
		}
	}
	g_cpubus.invalidate_pq();
}

void CPUExecutor::IRETD()
{
	g_cpu.unmask_event(CPU_EVENT_NMI);

	if(IS_PMODE()) {
		iret_pmode(true);
	} else {
		// real and v8086 modes
		if(IS_V8086() && (FLAG_IOPL < 3)) {
			PDEBUGF(LOG_V2, LOG_CPU, "IRETD: IOPL!=3 in v8086 mode\n");
			throw CPUException(CPU_GP_EXC, 0);
		}

		uint32_t eip    = stack_pop_dword();
		uint16_t cs_raw = stack_pop_dword(); // #SS has higher priority
		uint32_t eflags = stack_pop_dword();

		// CS LIMIT can't change when in real/v8086 mode
		if(eip > REG_CS.desc.limit) {
			PDEBUGF(LOG_V2, LOG_CPU,
				"IRETD: instruction pointer not within code segment limits\n");
			throw CPUException(CPU_GP_EXC, 0);
		}

		SET_CS(cs_raw);
		SET_EIP(eip);

		// VM unchanged
		write_eflags(eflags,
			IS_RMODE(), // IOPL
			true,       // IF
			true,       // NT,
			false       // VM
			);
	}
	g_cpubus.invalidate_pq();
}


/*******************************************************************************
 * Jcc-Jump Short If Condition is Met
 */

inline void CPUExecutor::Jcc(bool _cond, int32_t _offset)
{
	if(_cond) {
		branch_relative(_offset);
	}
}

void CPUExecutor::JO_cb()  /*  70*/ { Jcc(FLAG_OF,  int8_t(m_instr->ib)); }
void CPUExecutor::JNO_cb() /*  71*/ { Jcc(!FLAG_OF, int8_t(m_instr->ib)); }
void CPUExecutor::JC_cb()  /*  72*/ { Jcc(FLAG_CF,  int8_t(m_instr->ib)); }
void CPUExecutor::JNC_cb() /*  73*/ { Jcc(!FLAG_CF, int8_t(m_instr->ib)); }
void CPUExecutor::JE_cb()  /*  74*/ { Jcc(FLAG_ZF,  int8_t(m_instr->ib)); }
void CPUExecutor::JNE_cb() /*  75*/ { Jcc(!FLAG_ZF, int8_t(m_instr->ib)); }
void CPUExecutor::JBE_cb() /*  76*/ { Jcc(FLAG_CF || FLAG_ZF, int8_t(m_instr->ib)); }
void CPUExecutor::JA_cb()  /*  77*/ { Jcc(!FLAG_CF && !FLAG_ZF, int8_t(m_instr->ib)); }
void CPUExecutor::JS_cb()  /*  78*/ { Jcc(FLAG_SF,  int8_t(m_instr->ib)); }
void CPUExecutor::JNS_cb() /*  79*/ { Jcc(!FLAG_SF, int8_t(m_instr->ib)); }
void CPUExecutor::JPE_cb() /*  7A*/ { Jcc(FLAG_PF,  int8_t(m_instr->ib)); }
void CPUExecutor::JPO_cb() /*  7B*/ { Jcc(!FLAG_PF, int8_t(m_instr->ib)); }
void CPUExecutor::JL_cb()  /*  7C*/ { Jcc((FLAG_SF!=0) != (FLAG_OF!=0), int8_t(m_instr->ib)); }
void CPUExecutor::JNL_cb() /*  7D*/ { Jcc((FLAG_SF!=0) == (FLAG_OF!=0), int8_t(m_instr->ib)); }
void CPUExecutor::JLE_cb() /*  7E*/ { Jcc(FLAG_ZF || ((FLAG_SF!=0) != (FLAG_OF!=0)), int8_t(m_instr->ib)); }
void CPUExecutor::JNLE_cb()/*  7F*/ { Jcc(!FLAG_ZF && ((FLAG_SF!=0) == (FLAG_OF!=0)), int8_t(m_instr->ib)); }

void CPUExecutor::JO_cw()  /*0F80*/ { Jcc(FLAG_OF,  int16_t(m_instr->iw1)); }
void CPUExecutor::JNO_cw() /*0F81*/ { Jcc(!FLAG_OF, int16_t(m_instr->iw1)); }
void CPUExecutor::JC_cw()  /*0F82*/ { Jcc(FLAG_CF,  int16_t(m_instr->iw1)); }
void CPUExecutor::JNC_cw() /*0F83*/ { Jcc(!FLAG_CF, int16_t(m_instr->iw1)); }
void CPUExecutor::JE_cw()  /*0F84*/ { Jcc(FLAG_ZF,  int16_t(m_instr->iw1)); }
void CPUExecutor::JNE_cw() /*0F85*/ { Jcc(!FLAG_ZF, int16_t(m_instr->iw1)); }
void CPUExecutor::JBE_cw() /*0F86*/ { Jcc(FLAG_CF || FLAG_ZF, int16_t(m_instr->iw1)); }
void CPUExecutor::JA_cw()  /*0F87*/ { Jcc(!FLAG_CF && !FLAG_ZF, int16_t(m_instr->iw1)); }
void CPUExecutor::JS_cw()  /*0F88*/ { Jcc(FLAG_SF,  int16_t(m_instr->iw1)); }
void CPUExecutor::JNS_cw() /*0F89*/ { Jcc(!FLAG_SF, int16_t(m_instr->iw1)); }
void CPUExecutor::JPE_cw() /*0F8A*/ { Jcc(FLAG_PF,  int16_t(m_instr->iw1)); }
void CPUExecutor::JPO_cw() /*0F8B*/ { Jcc(!FLAG_PF, int16_t(m_instr->iw1)); }
void CPUExecutor::JL_cw()  /*0F8C*/ { Jcc((FLAG_SF!=0) != (FLAG_OF!=0), int16_t(m_instr->iw1)); }
void CPUExecutor::JNL_cw() /*0F8D*/ { Jcc((FLAG_SF!=0) == (FLAG_OF!=0), int16_t(m_instr->iw1)); }
void CPUExecutor::JLE_cw() /*0F8E*/ { Jcc(FLAG_ZF || ((FLAG_SF!=0) != (FLAG_OF!=0)), int16_t(m_instr->iw1)); }
void CPUExecutor::JNLE_cw()/*0F8F*/ { Jcc(!FLAG_ZF && ((FLAG_SF!=0) == (FLAG_OF!=0)), int16_t(m_instr->iw1)); }

void CPUExecutor::JO_cd()  /*0F80*/ { Jcc(FLAG_OF,  int32_t(m_instr->id1)); }
void CPUExecutor::JNO_cd() /*0F81*/ { Jcc(!FLAG_OF, int32_t(m_instr->id1)); }
void CPUExecutor::JC_cd()  /*0F82*/ { Jcc(FLAG_CF,  int32_t(m_instr->id1)); }
void CPUExecutor::JNC_cd() /*0F83*/ { Jcc(!FLAG_CF, int32_t(m_instr->id1)); }
void CPUExecutor::JE_cd()  /*0F84*/ { Jcc(FLAG_ZF,  int32_t(m_instr->id1)); }
void CPUExecutor::JNE_cd() /*0F85*/ { Jcc(!FLAG_ZF, int32_t(m_instr->id1)); }
void CPUExecutor::JBE_cd() /*0F86*/ { Jcc(FLAG_CF || FLAG_ZF, int32_t(m_instr->id1)); }
void CPUExecutor::JA_cd()  /*0F87*/ { Jcc(!FLAG_CF && !FLAG_ZF, int32_t(m_instr->id1)); }
void CPUExecutor::JS_cd()  /*0F88*/ { Jcc(FLAG_SF,  int32_t(m_instr->id1)); }
void CPUExecutor::JNS_cd() /*0F89*/ { Jcc(!FLAG_SF, int32_t(m_instr->id1)); }
void CPUExecutor::JPE_cd() /*0F8A*/ { Jcc(FLAG_PF,  int32_t(m_instr->id1)); }
void CPUExecutor::JPO_cd() /*0F8B*/ { Jcc(!FLAG_PF, int32_t(m_instr->id1)); }
void CPUExecutor::JL_cd()  /*0F8C*/ { Jcc((FLAG_SF!=0) != (FLAG_OF!=0), int32_t(m_instr->id1)); }
void CPUExecutor::JNL_cd() /*0F8D*/ { Jcc((FLAG_SF!=0) == (FLAG_OF!=0), int32_t(m_instr->id1)); }
void CPUExecutor::JLE_cd() /*0F8E*/ { Jcc(FLAG_ZF || ((FLAG_SF!=0) != (FLAG_OF!=0)), int32_t(m_instr->id1)); }
void CPUExecutor::JNLE_cd()/*0F8F*/ { Jcc(!FLAG_ZF && ((FLAG_SF!=0) == (FLAG_OF!=0)), int32_t(m_instr->id1)); }

void CPUExecutor::JCXZ_cb() /*E3*/ { Jcc(REG_CX==0, int8_t(m_instr->ib)); }
void CPUExecutor::JECXZ_cb()/*E3*/ { Jcc(REG_ECX==0, int8_t(m_instr->ib)); }


/*******************************************************************************
 * JMP-Jump
 */

void CPUExecutor::JMP_rel8()  { branch_relative(int8_t(m_instr->ib)); }
void CPUExecutor::JMP_rel16() { branch_relative(int16_t(m_instr->iw1)); }
void CPUExecutor::JMP_rel32() { branch_relative(int32_t(m_instr->id1)); }
void CPUExecutor::JMP_ew()    { branch_near(load_ew()); }
void CPUExecutor::JMP_ed()    { branch_near(load_ed()); }

void CPUExecutor::JMP_ptr1616() // JMPF Ap (op.size 16)
{
	if(!IS_PMODE()) {
		branch_far(m_instr->iw2, m_instr->iw1);
	} else {
		branch_far_pmode(m_instr->iw2, m_instr->iw1);
	}
}

void CPUExecutor::JMP_m1616() // JMPF Ep (op.size 16)
{
	uint16_t disp, cs;
	load_m1616(disp, cs);

	if(!IS_PMODE()) {
		branch_far(cs, disp);
	} else {
		branch_far_pmode(cs, disp);
	}
}

void CPUExecutor::JMP_ptr1632() // JMPF Ap (op.size 32)
{
	if(!IS_PMODE()) {
		branch_far(m_instr->iw2, m_instr->id1);
	} else {
		branch_far_pmode(m_instr->iw2, m_instr->id1);
	}
}

void CPUExecutor::JMP_m1632() // JMPF Ep (op.size 32)
{
	uint32_t disp;
	uint16_t cs;
	load_m1632(disp, cs);

	if(!IS_PMODE()) {
		branch_far(cs, disp);
	} else {
		branch_far_pmode(cs, disp);
	}
}


/*******************************************************************************
 * Load Flags into AH register
 */

void CPUExecutor::LAHF()
{
	REG_AH = uint8_t(GET_FLAGS());
}


/*******************************************************************************
 * LAR-Load Access Rights Byte
 */

uint32_t CPUExecutor::LAR(uint16_t _raw_selector)
{
	uint64_t   raw_descriptor;
	Descriptor descriptor;
	Selector   selector;

	/* if selector null, clear ZF and done */
	if((_raw_selector & SELECTOR_RPL_MASK) == 0) {
		SET_FLAG(ZF, false);
		return 0;
	}

	selector = _raw_selector;

	try {
		raw_descriptor = fetch_descriptor(selector,0);
	} catch(CPUException &e) {
		//this fetch does not throw an exception
		PDEBUGF(LOG_V2, LOG_CPU, "LAR: failed to fetch descriptor\n");
		SET_FLAG(ZF, false);
		return 0;
	}

	descriptor = raw_descriptor;

	if(!descriptor.valid) {
		PDEBUGF(LOG_V2, LOG_CPU, "LAR: descriptor not valid\n");
		SET_FLAG(ZF, false);
		return 0;
	}

	/* if source selector is visible at CPL & RPL,
	 * within the descriptor table, and of type accepted by LAR instruction,
	 * then load register with segment limit and set ZF
	 */

	if(descriptor.segment) { /* normal segment */
		if(descriptor.is_code_segment() && descriptor.is_conforming()) {
			/* ignore DPL for conforming segments */
		} else {
			if(descriptor.dpl < CPL || descriptor.dpl < selector.rpl) {
				SET_FLAG(ZF, false);
				return 0;
			}
		}
	} else { /* system or gate segment */
		switch(descriptor.type) {
			case DESC_TYPE_AVAIL_286_TSS:
			case DESC_TYPE_BUSY_286_TSS:
			case DESC_TYPE_286_CALL_GATE:
			case DESC_TYPE_TASK_GATE:
			case DESC_TYPE_LDT_DESC:
			case DESC_TYPE_AVAIL_386_TSS:
			case DESC_TYPE_BUSY_386_TSS:
			case DESC_TYPE_386_CALL_GATE:
				break;
			default: /* rest not accepted types to LAR */
				PDEBUGF(LOG_V2, LOG_CPU, "LAR: not accepted descriptor type\n");
				SET_FLAG(ZF, false);
				return 0;
		}

		if(descriptor.dpl < CPL || descriptor.dpl < selector.rpl) {
			SET_FLAG(ZF, false);
			return 0;
		}
	}

	SET_FLAG(ZF, true);
	return (raw_descriptor >> 32);
}

void CPUExecutor::LAR_rw_ew()
{
	if(!IS_PMODE()) {
		PDEBUGF(LOG_V2, LOG_CPU, "LAR: not recognized in real mode\n");
		throw CPUException(CPU_UD_EXC, 0);
	}

	uint16_t raw_selector = load_ew();
	uint32_t upper_dword = LAR(raw_selector) & 0xFF00;
	if(FLAG_ZF) {
		store_rw(upper_dword);
	}
}

void CPUExecutor::LAR_rd_ew()
{
	if(!IS_PMODE()) {
		PDEBUGF(LOG_V2, LOG_CPU, "LAR: not recognized in real mode\n");
		throw CPUException(CPU_UD_EXC, 0);
	}

	uint16_t raw_selector = load_ew();
	uint32_t upper_dword = LAR(raw_selector) & 0x00FFFF00;
	if(FLAG_ZF) {
		store_rd(upper_dword);
	}
}


/*******************************************************************************
 * LEA-Load Effective Address Offset
 */

void CPUExecutor::LEA_rw_m()
{
	if(m_instr->modrm.mod == 3) {
		PDEBUGF(LOG_V2, LOG_CPU, "LEA second operand is a register\n");
		throw CPUException(CPU_UD_EXC, 0);
	}
	uint16_t offset = (this->*EA_get_offset)();
	store_rw(offset);
}

void CPUExecutor::LEA_rd_m()
{
	if(m_instr->modrm.mod == 3) {
		PDEBUGF(LOG_V2, LOG_CPU, "LEA second operand is a register\n");
		throw CPUException(CPU_UD_EXC, 0);
	}
	uint32_t offset = (this->*EA_get_offset)();
	store_rd(offset);
}


/*******************************************************************************
 * LEAVE-High Level Procedure Exit
 */

void CPUExecutor::LEAVE_o16()
{
	if(REG_SS.desc.big) {
		REG_ESP = REG_EBP;
	} else {
		REG_SP = REG_BP;
	}
	REG_BP = stack_pop_word();
}

void CPUExecutor::LEAVE_o32()
{
	if(REG_SS.desc.big) {
		REG_ESP = REG_EBP;
	} else {
		REG_SP = REG_BP;
	}
	REG_EBP = stack_pop_dword();
}


/*******************************************************************************
 * LGDT/LIDT/LLDT-Load Global/Interrupt/Local Descriptor Table Register
 */

void CPUExecutor::LDT_m(uint32_t &base_, uint16_t &limit_)
{
	check_CPL_privilege(IS_PMODE(), "LDT_m");

	SegReg & sr = (this->*EA_get_segreg)();
	uint32_t off = (this->*EA_get_offset)();

	limit_ = read_word(sr, off);
	base_ = read_dword(sr, (off+2) & m_addr_mask);
}

void CPUExecutor::LGDT_o16()
{
	uint32_t base; uint16_t limit;
	LDT_m(base, limit);
	SET_GDTR(base & 0x00FFFFFF, limit);
}

void CPUExecutor::LGDT_o32()
{
	uint32_t base; uint16_t limit;
	LDT_m(base, limit);
	SET_GDTR(base, limit);
}

void CPUExecutor::LIDT_o16()
{
	uint32_t base; uint16_t limit;
	LDT_m(base, limit);
	SET_IDTR(base & 0x00FFFFFF, limit);
}

void CPUExecutor::LIDT_o32()
{
	uint32_t base; uint16_t limit;
	LDT_m(base, limit);
	SET_IDTR(base, limit);
}

void CPUExecutor::LLDT_ew()
{
	/* protected mode */
	Descriptor  descriptor;
	Selector    selector;

	if(!IS_PMODE()) {
		PDEBUGF(LOG_V2, LOG_CPU,"LLDT: not recognized in real mode\n");
		throw CPUException(CPU_UD_EXC, 0);
	}

	if(CPL != 0) {
		PDEBUGF(LOG_V2, LOG_CPU,"LLDT: The current priveledge level is not 0\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	selector = load_ew();

	/* if selector is NULL, invalidate and done */
	if((selector.value & SELECTOR_RPL_MASK) == 0) {
		REG_LDTR.sel = selector;
		REG_LDTR.desc.valid = false;
		return;
	}

	// #GP(selector) if the selector operand does not point into GDT
	if(selector.ti != 0) {
		PDEBUGF(LOG_V2, LOG_CPU,"LLDT: selector.ti != 0\n");
		throw CPUException(CPU_GP_EXC, selector.value & SELECTOR_RPL_MASK);
	}

	/* fetch descriptor; call handles out of limits checks */
	descriptor = fetch_descriptor(selector, CPU_GP_EXC);

	/* if selector doesn't point to an LDT descriptor #GP(selector) */
	if(!descriptor.valid || descriptor.segment ||
         descriptor.type != DESC_TYPE_LDT_DESC)
	{
		PDEBUGF(LOG_V2, LOG_CPU,"LLDT: doesn't point to an LDT descriptor!\n");
		throw CPUException(CPU_GP_EXC, selector.value & SELECTOR_RPL_MASK);
	}

	/* #NP(selector) if LDT descriptor is not present */
	if(!descriptor.present) {
		PDEBUGF(LOG_V2, LOG_CPU,"LLDT: LDT descriptor not present!\n");
		throw CPUException(CPU_NP_EXC, selector.value & SELECTOR_RPL_MASK);
	}

	REG_LDTR.sel = selector;
	REG_LDTR.desc = descriptor;
}


/*******************************************************************************
 * LGS/LSS/LDS/LES/LFS-Load Full Pointer
 */

void CPUExecutor::LDS_rw_mp()
{
	uint16_t reg, seg;

	load_m1616(reg, seg);
	SET_DS(seg);
	store_rw(reg);
}

void CPUExecutor::LDS_rd_mp()
{
	uint32_t reg; uint16_t seg;

	load_m1632(reg, seg);
	SET_DS(seg);
	store_rd(reg);
}

void CPUExecutor::LSS_rw_mp()
{
	uint16_t reg, seg;

	load_m1616(reg, seg);
	SET_SS(seg);
	store_rw(reg);
}

void CPUExecutor::LSS_rd_mp()
{
	uint32_t reg; uint16_t seg;

	load_m1632(reg, seg);
	SET_SS(seg);
	store_rd(reg);
}

void CPUExecutor::LES_rw_mp()
{
	uint16_t reg, seg;

	load_m1616(reg, seg);
	SET_ES(seg);
	store_rw(reg);
}

void CPUExecutor::LES_rd_mp()
{
	uint32_t reg; uint16_t seg;

	load_m1632(reg, seg);
	SET_ES(seg);
	store_rd(reg);
}

void CPUExecutor::LFS_rw_mp()
{
	uint16_t reg, seg;

	load_m1616(reg, seg);
	SET_FS(seg);
	store_rw(reg);
}

void CPUExecutor::LFS_rd_mp()
{
	uint32_t reg; uint16_t seg;

	load_m1632(reg, seg);
	SET_FS(seg);
	store_rd(reg);
}

void CPUExecutor::LGS_rw_mp()
{
	uint16_t reg, seg;

	load_m1616(reg, seg);
	SET_GS(seg);
	store_rw(reg);
}

void CPUExecutor::LGS_rd_mp()
{
	uint32_t reg; uint16_t seg;

	load_m1632(reg, seg);
	SET_GS(seg);
	store_rd(reg);
}


/*******************************************************************************
 * LMSW-Load Machine Status Word
 */

void CPUExecutor::LMSW_ew()
{
	uint16_t msw;

	check_CPL_privilege(IS_PMODE(), "LMSW");

	msw = load_ew();

	// LMSW cannot clear PE
	if(CR0_PE) {
		msw |= CR0MASK_PE; // adjust PE to current value of 1
	}

	SET_MSW(msw);
}


/*******************************************************************************
 * LOADALL-Load registers from memory
 */

void CPUExecutor::LOADALL_286()
{
	/* Undocumented
	 * From 15-page Intel document titled "Undocumented iAPX 286 Test Instruction"
	 * http://www.rcollins.org/articles/loadall/tspec_a3_doc.html
	 */

	uint16_t word_reg;
	uint16_t desc_cache[3];
	uint32_t base,limit;

	check_CPL_privilege(IS_PMODE(), "LOADALL 286");

	PDEBUGF(LOG_V2, LOG_CPU, "LOADALL 286\n");

	word_reg = g_cpubus.mem_read<2>(0x806);
	if(CR0_PE) {
		word_reg |= CR0MASK_PE; // adjust PE to current value of 1
	}
	SET_MSW(word_reg);

	REG_TR.sel = g_cpubus.mem_read<2>(0x816);
	SET_FLAGS(g_cpubus.mem_read<2>(0x818));
	SET_IP(g_cpubus.mem_read<2>(0x81A));
	REG_LDTR.sel = g_cpubus.mem_read<2>(0x81C);
	REG_DS.sel = g_cpubus.mem_read<2>(0x81E);
	REG_SS.sel = g_cpubus.mem_read<2>(0x820);
	REG_CS.sel = g_cpubus.mem_read<2>(0x822);
	REG_ES.sel = g_cpubus.mem_read<2>(0x824);
	REG_DI = g_cpubus.mem_read<2>(0x826);
	REG_SI = g_cpubus.mem_read<2>(0x828);
	REG_BP = g_cpubus.mem_read<2>(0x82A);
	REG_SP = g_cpubus.mem_read<2>(0x82C);
	REG_BX = g_cpubus.mem_read<2>(0x82E);
	REG_DX = g_cpubus.mem_read<2>(0x830);
	REG_CX = g_cpubus.mem_read<2>(0x832);
	REG_AX = g_cpubus.mem_read<2>(0x834);

	desc_cache[0] = g_cpubus.mem_read<2>(0x836);
	desc_cache[1] = g_cpubus.mem_read<2>(0x838);
	desc_cache[2] = g_cpubus.mem_read<2>(0x83A);
	REG_ES.desc.set_from_286_cache(desc_cache);

	desc_cache[0] = g_cpubus.mem_read<2>(0x83C);
	desc_cache[1] = g_cpubus.mem_read<2>(0x83E);
	desc_cache[2] = g_cpubus.mem_read<2>(0x840);
	REG_CS.desc.set_from_286_cache(desc_cache);

	desc_cache[0] = g_cpubus.mem_read<2>(0x842);
	desc_cache[1] = g_cpubus.mem_read<2>(0x844);
	desc_cache[2] = g_cpubus.mem_read<2>(0x846);
	REG_SS.desc.set_from_286_cache(desc_cache);

	desc_cache[0] = g_cpubus.mem_read<2>(0x848);
	desc_cache[1] = g_cpubus.mem_read<2>(0x84A);
	desc_cache[2] = g_cpubus.mem_read<2>(0x84C);
	REG_DS.desc.set_from_286_cache(desc_cache);

	base  = g_cpubus.mem_read<4>(0x84E);
	limit = g_cpubus.mem_read<2>(0x852);
	SET_GDTR(base, limit);

	desc_cache[0] = g_cpubus.mem_read<2>(0x854);
	desc_cache[1] = g_cpubus.mem_read<2>(0x856);
	desc_cache[2] = g_cpubus.mem_read<2>(0x858);
	REG_LDTR.desc.set_from_286_cache(desc_cache);

	base  = g_cpubus.mem_read<4>(0x85A);
	limit = g_cpubus.mem_read<2>(0x85E);
	SET_IDTR(base, limit);

	desc_cache[0] = g_cpubus.mem_read<2>(0x860);
	desc_cache[1] = g_cpubus.mem_read<2>(0x862);
	desc_cache[2] = g_cpubus.mem_read<2>(0x864);
	REG_TR.desc.set_from_286_cache(desc_cache);

	g_cpubus.invalidate_pq();
}


/*******************************************************************************
 * LODSB/LODSW/LODSD-Load String Operand
 */

void CPUExecutor::LODSB_a16()
{
	REG_AL = read_byte(SEG_REG(m_base_ds), REG_SI);

	if(FLAG_DF) {
		REG_SI -= 1;
	} else {
		REG_SI += 1;
	}
}

void CPUExecutor::LODSB_a32()
{
	REG_AL = read_byte(SEG_REG(m_base_ds), REG_ESI);

	if(FLAG_DF) {
		REG_ESI -= 1;
	} else {
		REG_ESI += 1;
	}
}

void CPUExecutor::LODSW_a16()
{
	REG_AX = read_word(SEG_REG(m_base_ds), REG_SI);

	if(FLAG_DF) {
		REG_SI -= 2;
	} else {
		REG_SI += 2;
	}
}

void CPUExecutor::LODSW_a32()
{
	REG_AX = read_word(SEG_REG(m_base_ds), REG_ESI);

	if(FLAG_DF) {
		REG_ESI -= 2;
	} else {
		REG_ESI += 2;
	}
}

void CPUExecutor::LODSD_a16()
{
	REG_EAX = read_dword(SEG_REG(m_base_ds), REG_SI);

	if(FLAG_DF) {
		REG_SI -= 4;
	} else {
		REG_SI += 4;
	}
}

void CPUExecutor::LODSD_a32()
{
	REG_EAX = read_dword(SEG_REG(m_base_ds), REG_ESI);

	if(FLAG_DF) {
		REG_ESI -= 4;
	} else {
		REG_ESI += 4;
	}
}


/*******************************************************************************
 * LOOP/LOOPcond-Loop Control with CX Counter
 */

uint32_t CPUExecutor::LOOP(uint32_t _count)
{
	_count--;
	if(_count != 0) {
		branch_relative(int8_t(m_instr->ib));
	}
	return _count;
}

uint32_t CPUExecutor::LOOPZ(uint32_t _count)
{
	_count--;
	if(_count != 0 && FLAG_ZF) {
		branch_relative(int8_t(m_instr->ib));
	}
	return _count;
}

uint32_t CPUExecutor::LOOPNZ(uint32_t _count)
{
	_count--;
	if(_count != 0 && (FLAG_ZF==false)) {
		branch_relative(int8_t(m_instr->ib));
	}
	return _count;
}

void CPUExecutor::LOOP_a16()  { REG_CX  = LOOP(REG_CX); }
void CPUExecutor::LOOP_a32()  { REG_ECX = LOOP(REG_ECX); }
void CPUExecutor::LOOPZ_a16() { REG_CX  = LOOPZ(REG_CX); }
void CPUExecutor::LOOPZ_a32() { REG_ECX = LOOPZ(REG_ECX); }
void CPUExecutor::LOOPNZ_a16(){ REG_CX  = LOOPNZ(REG_CX); }
void CPUExecutor::LOOPNZ_a32(){ REG_ECX = LOOPNZ(REG_ECX); }


/*******************************************************************************
 * LSL-Load Segment Limit
 */

uint32_t CPUExecutor::LSL()
{
	Selector   selector;
	Descriptor descriptor;

	if(!IS_PMODE()) {
		PDEBUGF(LOG_V2, LOG_CPU, "LSL: not recognized in real mode\n");
		throw CPUException(CPU_UD_EXC, 0);
	}

	selector = load_ew();

	/* if selector null, clear ZF and done */
	if((selector.value & SELECTOR_RPL_MASK) == 0) {
		SET_FLAG(ZF, false);
		return 0;
	}

	try {
		descriptor = fetch_descriptor(selector, CPU_GP_EXC);
	} catch(CPUException &e) {
		PDEBUGF(LOG_V2, LOG_CPU, "LSL: failed to fetch descriptor\n");
		SET_FLAG(ZF, false);
		return 0;
	}

	if(descriptor.is_system_segment()) {
		switch (descriptor.type) {
			case DESC_TYPE_AVAIL_286_TSS:
			case DESC_TYPE_BUSY_286_TSS:
			case DESC_TYPE_LDT_DESC:
			case DESC_TYPE_AVAIL_386_TSS:
			case DESC_TYPE_BUSY_386_TSS:
				if(descriptor.dpl < CPL || descriptor.dpl < selector.rpl) {
					SET_FLAG(ZF, false);
					return 0;
				}
				break;
			default: /* rest not accepted types to LSL */
				SET_FLAG(ZF, false);
				return 0;
		}
	} else { // data & code segment
		if(descriptor.is_code_segment() && !descriptor.is_conforming()) {
			// non-conforming code segment
			if(descriptor.dpl < CPL || descriptor.dpl < selector.rpl) {
				SET_FLAG(ZF, false);
				return 0;
			}
		}
	}

	/* all checks pass */
	SET_FLAG(ZF, true);
	return descriptor.limit;
}

void CPUExecutor::LSL_rw_ew()
{
	uint16_t limit = LSL();
	if(FLAG_ZF) {
		store_rw(limit);
	}
}

void CPUExecutor::LSL_rd_ew()
{
	uint32_t limit = LSL();
	if(FLAG_ZF) {
		store_rd(limit);
	}
}


/*******************************************************************************
 * LTR-Load Task Register
 */

void CPUExecutor::LTR_ew()
{
	Descriptor descriptor;
	Selector   selector;

	if(!IS_PMODE()) {
		PDEBUGF(LOG_V2, LOG_CPU, "LTR: not recognized in real mode\n");
		throw CPUException(CPU_UD_EXC, 0);
	}

	if(CPL != 0) {
		PDEBUGF(LOG_V2, LOG_CPU, "LTR: The current priveledge level is not 0\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	selector = load_ew();

	if((selector.value & SELECTOR_RPL_MASK) == 0) {
		PDEBUGF(LOG_V2, LOG_CPU, "LTR: loading with NULL selector!\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	if(selector.ti) {
		PDEBUGF(LOG_V2, LOG_CPU, "LTR: selector.ti != 0\n");
		throw CPUException(CPU_GP_EXC, selector.value & SELECTOR_RPL_MASK);
	}

	/* fetch descriptor; call handles out of limits checks */
	descriptor = fetch_descriptor(selector, CPU_GP_EXC);

	/* #GP(selector) if object is not a TSS or is already busy */
	if(!descriptor.valid || descriptor.segment ||
	   (descriptor.type!=DESC_TYPE_AVAIL_286_TSS &&
	    descriptor.type!=DESC_TYPE_AVAIL_386_TSS))
	{
		PDEBUGF(LOG_V2, LOG_CPU, "LTR: doesn't point to an available TSS descriptor!\n");
		throw CPUException(CPU_GP_EXC, selector.value & SELECTOR_RPL_MASK);
	}

	/* #NP(selector) if TSS descriptor is not present */
	if(!descriptor.present) {
		PDEBUGF(LOG_V2, LOG_CPU, "LTR: TSS descriptor not present!\n");
		throw CPUException(CPU_NP_EXC, selector.value & SELECTOR_RPL_MASK);
	}

	REG_TR.sel  = selector;
	REG_TR.desc = descriptor;

	/* mark as busy */
	REG_TR.desc.type |= TSS_BUSY_BIT;
	write_byte(GET_BASE(GDTR) + selector.index*8 + 5, REG_TR.desc.get_AR());
}


/*******************************************************************************
 * MOV-Move Data
 */

void CPUExecutor::MOV_eb_rb() { store_eb(load_rb()); }
void CPUExecutor::MOV_ew_rw() { store_ew(load_rw()); }
void CPUExecutor::MOV_ed_rd() { store_ed(load_rd()); }
void CPUExecutor::MOV_rb_eb() { store_rb(load_eb()); }
void CPUExecutor::MOV_rw_ew() { store_rw(load_ew()); }
void CPUExecutor::MOV_rd_ed() { store_rd(load_ed()); }
void CPUExecutor::MOV_SR_ew() { store_sr(load_ew()); }
void CPUExecutor::MOV_AL_xb() { REG_AL = read_byte(SEG_REG(m_base_ds), m_instr->offset); }
void CPUExecutor::MOV_AX_xw() { REG_AX = read_word(SEG_REG(m_base_ds), m_instr->offset); }
void CPUExecutor::MOV_EAX_xd(){ REG_EAX = read_dword(SEG_REG(m_base_ds), m_instr->offset); }
void CPUExecutor::MOV_xb_AL() { write_byte(SEG_REG(m_base_ds), m_instr->offset, REG_AL); }
void CPUExecutor::MOV_xw_AX() { write_word(SEG_REG(m_base_ds), m_instr->offset, REG_AX); }
void CPUExecutor::MOV_xd_EAX(){ write_dword(SEG_REG(m_base_ds), m_instr->offset, REG_EAX); }
void CPUExecutor::MOV_rb_ib() { store_rb_op(m_instr->ib); }
void CPUExecutor::MOV_rw_iw() { store_rw_op(m_instr->iw1); }
void CPUExecutor::MOV_rd_id() { store_rd_op(m_instr->id1); }
void CPUExecutor::MOV_eb_ib() { store_eb(m_instr->ib); }
void CPUExecutor::MOV_ew_iw() { store_ew(m_instr->iw1); }
void CPUExecutor::MOV_ed_id() { store_ed(m_instr->id1); }

void CPUExecutor::MOV_ew_SR()
{
	store_ew(load_sr());
	if(m_instr->op32) {
		/* When the processor executes the instruction with a 32-bit general
		 * purpose register, it assumes that the 16 least-significant bits of
		 * the register are the destination or source operand.
		 * If the register is a destination operand, the resulting value in the
		 * two high-order bytes of the register is implementation dependent. For
		 * the Pentium 4, Intel Xeon, and P6 family processors, the two
		 * high-order bytes are filled with zeros; for earlier 32-bit IA-32
		 * processors, the two high order bytes are undefined.
		 *
		 * I zero-fill the upper bytes which is the behaviour of Bochs and PCjs.
		 */
		if(m_instr->modrm.mod == 3) {
			GEN_REG(m_instr->modrm.rm).word[1] = 0;
		}
	}
}

/*******************************************************************************
 * MOVSB/MOVSW/MOVSD-Move Data from String to String
 */

void CPUExecutor::MOVSB_a16()
{
	uint8_t temp = read_byte(SEG_REG(m_base_ds), REG_SI);
	write_byte(REG_ES, REG_DI, temp);

	if(FLAG_DF) {
		/* decrement SI, DI */
		REG_SI -= 1;
		REG_DI -= 1;
	} else {
		/* increment SI, DI */
		REG_SI += 1;
		REG_DI += 1;
	}
}

void CPUExecutor::MOVSW_a16()
{
	uint16_t temp = read_word(SEG_REG(m_base_ds), REG_SI);
	write_word(REG_ES, REG_DI, temp);

	if(FLAG_DF) {
		/* decrement SI, DI */
		REG_SI -= 2;
		REG_DI -= 2;
	} else {
		/* increment SI, DI */
		REG_SI += 2;
		REG_DI += 2;
	}
}

void CPUExecutor::MOVSD_a16()
{
	uint32_t temp = read_dword(SEG_REG(m_base_ds), REG_SI);
	write_dword(REG_ES, REG_DI, temp);

	if(FLAG_DF) {
		/* decrement SI, DI */
		REG_SI -= 4;
		REG_DI -= 4;
	} else {
		/* increment SI, DI */
		REG_SI += 4;
		REG_DI += 4;
	}
}

void CPUExecutor::MOVSB_a32()
{
	uint8_t temp = read_byte(SEG_REG(m_base_ds), REG_ESI);
	write_byte(REG_ES, REG_EDI, temp);

	if(FLAG_DF) {
		/* decrement ESI, EDI */
		REG_ESI -= 1;
		REG_EDI -= 1;
	} else {
		/* increment ESI, EDI */
		REG_ESI += 1;
		REG_EDI += 1;
	}
}

void CPUExecutor::MOVSW_a32()
{
	uint16_t temp = read_word(SEG_REG(m_base_ds), REG_ESI);
	write_word(REG_ES, REG_EDI, temp);

	if(FLAG_DF) {
		/* decrement ESI, EDI */
		REG_ESI -= 2;
		REG_EDI -= 2;
	} else {
		/* increment ESI, EDI */
		REG_ESI += 2;
		REG_EDI += 2;
	}
}

void CPUExecutor::MOVSD_a32()
{
	uint32_t temp = read_dword(SEG_REG(m_base_ds), REG_ESI);
	write_dword(REG_ES, REG_EDI, temp);

	if(FLAG_DF) {
		/* decrement ESI, EDI */
		REG_ESI -= 4;
		REG_EDI -= 4;
	} else {
		/* increment ESI, EDI */
		REG_ESI += 4;
		REG_EDI += 4;
	}
}


/*******************************************************************************
 * MOVSX-Move with Sign-Extend
 */

void CPUExecutor::MOVSX_rw_eb() { store_rw(int8_t(load_eb())); }
void CPUExecutor::MOVSX_rd_eb() { store_rd(int8_t(load_eb())); }
void CPUExecutor::MOVSX_rd_ew() { store_rd(int16_t(load_ew())); }


/*******************************************************************************
 * MOVZX-Move with Zero-Extend
 */

void CPUExecutor::MOVZX_rw_eb() { store_rw(load_eb()); }
void CPUExecutor::MOVZX_rd_eb() { store_rd(load_eb()); }
void CPUExecutor::MOVZX_rd_ew() { store_rd(load_ew()); }


/*******************************************************************************
 * MOV-Move to/from special registers
 */

void CPUExecutor::MOV_CR_rd()
{
	check_CPL_privilege(!IS_RMODE(), "MOV_CR_rd");
	uint32_t value = load_ed();
	switch(m_instr->modrm.r) {
		case 0: SET_CR0(value); break;
		case 2: SET_CR2(value); break;
		case 3: SET_CR3(value); break;
		default:
			break;
	}
}

void CPUExecutor::MOV_rd_CR()
{
	check_CPL_privilege(!IS_RMODE(), "MOV_rd_CR");
	store_ed(GET_CR(m_instr->modrm.r));
}

void CPUExecutor::MOV_DR_rd()
{
	check_CPL_privilege(!IS_RMODE(), "MOV_DR_rd");
	REG_DBG(m_instr->modrm.r) = load_ed();
}

void CPUExecutor::MOV_rd_DR()
{
	check_CPL_privilege(!IS_RMODE(), "MOV_rd_DR");
	store_ed(REG_DBG(m_instr->modrm.r));
}

void CPUExecutor::MOV_TR_rd()
{
	check_CPL_privilege(!IS_RMODE(), "MOV_TR_rd");
	REG_TEST(m_instr->modrm.r) = load_ed();
}

void CPUExecutor::MOV_rd_TR()
{
	check_CPL_privilege(!IS_RMODE(), "MOV_rd_TR");
	store_ed(REG_TEST(m_instr->modrm.r));
}


/*******************************************************************************
 * MUL-Unsigned Multiplication of AL / AX / EAX
 */

void CPUExecutor::MUL_eb()
{
	uint8_t op1_8 = REG_AL;
	uint8_t op2_8 = load_eb();

	uint16_t product_16 = uint16_t(op1_8) * uint16_t(op2_8);

	uint8_t product_8l = product_16 & 0xFF;
	uint8_t product_8h = product_16 >> 8;

	/* now write product back to destination */
	REG_AX = product_16;

	if(product_8h) {
		SET_FLAG(CF, true);
		SET_FLAG(OF, true);
	} else {
		SET_FLAG(CF, false);
		SET_FLAG(OF, false);
	}

	SET_FLAG(SF, product_8l & 0x80);
	SET_FLAG(ZF, product_8l == 0);
	SET_FLAG(AF, false);
	SET_FLAG(PF, PARITY(product_8l));

	if(CPU_FAMILY == CPU_386) {
		m_instr->cycles.extra = mul_cycles_386(op2_8);
	}
}

void CPUExecutor::MUL_ew()
{
	uint16_t op1_16 = REG_AX;
	uint16_t op2_16 = load_ew();

	uint32_t product_32  = uint32_t(op1_16) * uint32_t(op2_16);

	uint16_t product_16l = product_32 & 0xFFFF;
	uint16_t product_16h = product_32 >> 16;

	/* now write product back to destination */
	REG_AX = product_16l;
	REG_DX = product_16h;

	if(product_16h) {
		SET_FLAG(CF, true);
		SET_FLAG(OF, true);
	} else {
		SET_FLAG(CF, false);
		SET_FLAG(OF, false);
	}

	SET_FLAG(SF, product_16l & 0x8000);
	SET_FLAG(ZF, product_16l == 0);
	SET_FLAG(AF, false);
	SET_FLAG(PF, PARITY(product_16l));

	if(CPU_FAMILY == CPU_386) {
		m_instr->cycles.extra = mul_cycles_386(op2_16);
	}
}

void CPUExecutor::MUL_ed()
{
	uint32_t op1_32 = REG_EAX;
	uint32_t op2_32 = load_ed();

	uint64_t product_64  = uint64_t(op1_32) * uint64_t(op2_32);

	uint32_t product_32l = product_64 & 0xFFFFFFFF;
	uint32_t product_32h = product_64 >> 32;

	/* now write product back to destination */
	REG_EAX = product_32l;
	REG_EDX = product_32h;

	if(product_32h) {
		SET_FLAG(CF, true);
		SET_FLAG(OF, true);
	} else {
		SET_FLAG(CF, false);
		SET_FLAG(OF, false);
	}

	SET_FLAG(SF, product_32l & 0x80000000);
	SET_FLAG(ZF, product_32l == 0);
	SET_FLAG(AF, false);
	SET_FLAG(PF, PARITY(product_32l));

	if(CPU_FAMILY == CPU_386) {
		m_instr->cycles.extra = mul_cycles_386(op2_32);
	}
}


/*******************************************************************************
 * NEG-Two's Complement Negation
 */

void CPUExecutor::NEG_eb()
{
	uint8_t op1 = load_eb();
	uint8_t res = -(int8_t)(op1);
	store_eb(res);

	SET_FLAG(CF, op1);
	SET_FLAG(AF, op1 & 0x0f);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(SF, res & 0x80);
	SET_FLAG(OF, op1 == 0x80);
	SET_FLAG(PF, PARITY(res));
}

void CPUExecutor::NEG_ew()
{
	uint16_t op1 = load_ew();
	uint16_t res = -(int16_t)(op1);
	store_ew(res);

	SET_FLAG(CF, op1);
	SET_FLAG(AF, op1 & 0x0f);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(SF, res & 0x8000);
	SET_FLAG(OF, op1 == 0x8000);
	SET_FLAG(PF, PARITY(res));
}

void CPUExecutor::NEG_ed()
{
	uint32_t op1 = load_ed();
	uint32_t res = -(int32_t)(op1);
	store_ed(res);

	SET_FLAG(CF, op1);
	SET_FLAG(AF, op1 & 0x0f);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(SF, res & 0x80000000);
	SET_FLAG(OF, op1 == 0x80000000);
	SET_FLAG(PF, PARITY(res));
}


/*******************************************************************************
 * NOP-No OPERATION
 */

void CPUExecutor::NOP() {}


/*******************************************************************************
 * NOT-One's Complement Negation
 */

void CPUExecutor::NOT_eb()
{
	uint8_t op1 = load_eb();
	op1 = ~op1;
	store_eb(op1);
}

void CPUExecutor::NOT_ew()
{
	uint16_t op1 = load_ew();
	op1 = ~op1;
	store_ew(op1);
}

void CPUExecutor::NOT_ed()
{
	uint32_t op1 = load_ed();
	op1 = ~op1;
	store_ed(op1);
}


/*******************************************************************************
 * OR-Logical Inclusive OR
 */

uint8_t CPUExecutor::OR_b(uint8_t op1, uint8_t op2)
{
	uint8_t res = op1 | op2;

	SET_FLAG(SF, res & 0x80);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(OF, false);
	SET_FLAG(CF, false);
	SET_FLAG(AF, false); // unknown

	return res;
}

uint16_t CPUExecutor::OR_w(uint16_t op1, uint16_t op2)
{
	uint16_t res = op1 | op2;

	SET_FLAG(SF, res & 0x8000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(OF, false);
	SET_FLAG(CF, false);
	SET_FLAG(AF, false); // unknown

	return res;
}

uint32_t CPUExecutor::OR_d(uint32_t op1, uint32_t op2)
{
	uint32_t res = op1 | op2;

	SET_FLAG(SF, res & 0x80000000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(OF, false);
	SET_FLAG(CF, false);
	SET_FLAG(AF, false); // unknown

	return res;
}

void CPUExecutor::OR_eb_rb() { store_eb(OR_b(load_eb(), load_rb())); }
void CPUExecutor::OR_ew_rw() { store_ew(OR_w(load_ew(), load_rw())); }
void CPUExecutor::OR_ed_rd() { store_ed(OR_d(load_ed(), load_rd())); }
void CPUExecutor::OR_rb_eb() { store_rb(OR_b(load_rb(), load_eb())); }
void CPUExecutor::OR_rw_ew() { store_rw(OR_w(load_rw(), load_ew())); }
void CPUExecutor::OR_rd_ed() { store_rd(OR_d(load_rd(), load_ed())); }
void CPUExecutor::OR_AL_ib() { REG_AL = OR_b(REG_AL, m_instr->ib); }
void CPUExecutor::OR_AX_iw() { REG_AX = OR_w(REG_AX, m_instr->iw1); }
void CPUExecutor::OR_EAX_id(){ REG_EAX = OR_d(REG_EAX, m_instr->id1); }
void CPUExecutor::OR_eb_ib() { store_eb(OR_b(load_eb(), m_instr->ib)); }
void CPUExecutor::OR_ew_iw() { store_ew(OR_w(load_ew(), m_instr->iw1)); }
void CPUExecutor::OR_ed_id() { store_ed(OR_d(load_ed(), m_instr->id1)); }
void CPUExecutor::OR_ew_ib() { store_ew(OR_w(load_ew(), int8_t(m_instr->ib))); }
void CPUExecutor::OR_ed_ib() { store_ed(OR_d(load_ed(), int8_t(m_instr->ib))); }


/*******************************************************************************
 * OUT-Output to port
 */

void CPUExecutor::OUT_b(uint16_t _port, uint8_t _value)
{
	io_check(_port, 1);
	g_devices.write_byte(_port, _value);
}

void CPUExecutor::OUT_w(uint16_t _port, uint16_t _value)
{
	io_check(_port, 2);
	g_devices.write_word(_port, _value);
}

void CPUExecutor::OUT_d(uint16_t _port, uint32_t _value)
{
	io_check(_port, 4);
	g_devices.write_dword(_port, _value);
}

void CPUExecutor::OUT_ib_AL() { OUT_b(m_instr->ib, REG_AL); }
void CPUExecutor::OUT_ib_AX() { OUT_w(m_instr->ib, REG_AX); }
void CPUExecutor::OUT_ib_EAX(){ OUT_d(m_instr->ib, REG_EAX); }
void CPUExecutor::OUT_DX_AL() { OUT_b(REG_DX, REG_AL); }
void CPUExecutor::OUT_DX_AX() { OUT_w(REG_DX, REG_AX); }
void CPUExecutor::OUT_DX_EAX(){ OUT_d(REG_DX, REG_EAX); }


/*******************************************************************************
 * OUTSB/OUTSW/OUTSD-Output String to Port
 */

void CPUExecutor::OUTSB(uint8_t _value)
{
	if(m_instr->rep && m_instr->rep_first) {
		io_check(REG_DX, 1);
	}
	g_devices.write_byte(REG_DX, _value);
}

void CPUExecutor::OUTSB_a16()
{
	OUTSB(read_byte(SEG_REG(m_base_ds), REG_SI));

	if(FLAG_DF) {
		REG_SI -= 1;
	} else {
		REG_SI += 1;
	}
}

void CPUExecutor::OUTSB_a32()
{
	OUTSB(read_byte(SEG_REG(m_base_ds), REG_ESI));

	if(FLAG_DF) {
		REG_ESI -= 1;
	} else {
		REG_ESI += 1;
	}
}

void CPUExecutor::OUTSW(uint16_t _value)
{
	if(m_instr->rep && m_instr->rep_first) {
		io_check(REG_DX, 2);
	}
	g_devices.write_word(REG_DX, _value);
}

void CPUExecutor::OUTSW_a16()
{
	OUTSW(read_word(SEG_REG(m_base_ds), REG_SI));

	if(FLAG_DF) {
		REG_SI -= 2;
	} else {
		REG_SI += 2;
	}
}

void CPUExecutor::OUTSW_a32()
{
	OUTSW(read_word(SEG_REG(m_base_ds), REG_ESI));

	if(FLAG_DF) {
		REG_ESI -= 2;
	} else {
		REG_ESI += 2;
	}
}

void CPUExecutor::OUTSD(uint32_t _value)
{
	if(m_instr->rep && m_instr->rep_first) {
		io_check(REG_DX, 4);
	}
	g_devices.write_dword(REG_DX, _value);
}

void CPUExecutor::OUTSD_a16()
{
	OUTSD(read_dword(SEG_REG(m_base_ds), REG_SI));

	if(FLAG_DF) {
		REG_SI -= 4;
	} else {
		REG_SI += 4;
	}
}

void CPUExecutor::OUTSD_a32()
{
	OUTSD(read_dword(SEG_REG(m_base_ds), REG_ESI));

	if(FLAG_DF) {
		REG_ESI -= 4;
	} else {
		REG_ESI += 4;
	}
}


/*******************************************************************************
 * POP-Pop Operand from the Stack
 */

void CPUExecutor::POP_SR_w()
{
	SET_SR(m_instr->reg, stack_pop_word());
	if(m_instr->reg == REGI_SS) {
		/* A POP SS instruction will inhibit all interrupts, including NMI, until
		 * after the execution of the next instruction. This permits a POP SP
		 * instruction to be performed first. (cf. B-83)
		 */
		g_cpu.inhibit_interrupts(CPU_INHIBIT_INTERRUPTS_BY_MOVSS);
	}
}

void CPUExecutor::POP_SR_dw()
{
	SET_SR(m_instr->reg, stack_pop_dword());
	if(m_instr->reg == REGI_SS) {
		g_cpu.inhibit_interrupts(CPU_INHIBIT_INTERRUPTS_BY_MOVSS);
	}
}

void CPUExecutor::POP_mw()    { store_ew(stack_pop_word()); }
void CPUExecutor::POP_md()    { store_ed(stack_pop_dword()); }
void CPUExecutor::POP_rw_op() { store_rw_op(stack_pop_word()); }
void CPUExecutor::POP_rd_op() { store_rd_op(stack_pop_dword()); }


/*******************************************************************************
 * POPA/POPAD-Pop All General Registers
 */

void CPUExecutor::POPA()
{
	uint32_t sp = (REG_SS.desc.big)?REG_ESP:REG_SP;

	uint16_t di = stack_read_word(sp+0);
	uint16_t si = stack_read_word(sp+2);
	uint16_t bp = stack_read_word(sp+4);
	              stack_read_word(sp+6); //skip SP
	uint16_t bx = stack_read_word(sp+8);
	uint16_t dx = stack_read_word(sp+10);
	uint16_t cx = stack_read_word(sp+12);
	uint16_t ax = stack_read_word(sp+14);

	if(REG_SS.desc.big) {
		REG_ESP += 16;
	} else {
		REG_SP += 16;
	}

	REG_DI = di;
	REG_SI = si;
	REG_BP = bp;
	REG_BX = bx;
	REG_DX = dx;
	REG_CX = cx;
	REG_AX = ax;
}

void CPUExecutor::POPAD()
{
	uint32_t sp = (REG_SS.desc.big)?REG_ESP:REG_SP;

	uint32_t edi = stack_read_dword(sp+0);
	uint32_t esi = stack_read_dword(sp+4);
	uint32_t ebp = stack_read_dword(sp+8);
	               stack_read_dword(sp+12); //skip ESP
	uint32_t ebx = stack_read_dword(sp+16);
	uint32_t edx = stack_read_dword(sp+20);
	uint32_t ecx = stack_read_dword(sp+24);
	uint32_t eax = stack_read_dword(sp+28);

	if(REG_SS.desc.big) {
		REG_ESP += 32;
	} else {
		REG_SP += 32;
	}

	REG_EDI = edi;
	REG_ESI = esi;
	REG_EBP = ebp;
	REG_EBX = ebx;
	REG_EDX = edx;
	REG_ECX = ecx;
	REG_EAX = eax;
}


/*******************************************************************************
 * POPF/POPFD-Pop from Stack into the FLAGS or EFLAGS Register
 */

void CPUExecutor::POPF()
{
	uint16_t flags = stack_pop_word();
	write_flags(flags);
}

void CPUExecutor::POPFD()
{
	/* POPF and POPFD don't affect bit 16 & 17 of EFLAGS, so use the
	 * same write_flags as POPF
	 * TODO this works only for the 386
	 */
	uint16_t flags = uint16_t(stack_pop_dword());
	write_flags(flags);
}


/*******************************************************************************
 * PUSH-Push Operand onto the Stack
 */

void CPUExecutor::PUSH_SR_w()  { stack_push_word(SEG_REG(m_instr->reg).sel.value); }
void CPUExecutor::PUSH_SR_dw() { stack_push_dword(SEG_REG(m_instr->reg).sel.value); }
void CPUExecutor::PUSH_rw_op() { stack_push_word(load_rw_op()); }
void CPUExecutor::PUSH_rd_op() { stack_push_dword(load_rd_op()); }
void CPUExecutor::PUSH_mw()    { stack_push_word(load_ew()); }
void CPUExecutor::PUSH_md()    { stack_push_dword(load_ed()); }
void CPUExecutor::PUSH_ib_w()  { stack_push_word(int8_t(m_instr->ib)); }
void CPUExecutor::PUSH_ib_dw() { stack_push_dword(int8_t(m_instr->ib)); }
void CPUExecutor::PUSH_iw()    { stack_push_word(m_instr->iw1); }
void CPUExecutor::PUSH_id()    { stack_push_dword(m_instr->id1); }


/*******************************************************************************
 * PUSHA-Push All General Registers
 */

void CPUExecutor::PUSHA()
{
	uint32_t sp = (REG_SS.desc.big)?REG_ESP:REG_SP;

	if(!IS_PMODE()) {
		if(sp == 7 || sp == 9 || sp == 11 || sp == 13 || sp == 15) {
			throw CPUException(CPU_SEG_OVR_EXC,0);
		}
		if(sp == 1 || sp == 3 || sp == 5) {
			throw CPUShutdown("SP=1,3,5 on stack push (PUSHA)");
		}
	}

	stack_write_word(REG_AX, sp-2);
	stack_write_word(REG_CX, sp-4);
	stack_write_word(REG_DX, sp-6);
	stack_write_word(REG_BX, sp-8);
	stack_write_word(REG_SP, sp-10);
	stack_write_word(REG_BP, sp-12);
	stack_write_word(REG_SI, sp-14);
	stack_write_word(REG_DI, sp-16);

	if(REG_SS.desc.big) {
		REG_ESP -= 16;
	} else {
		REG_SP -= 16;
	}
}

void CPUExecutor::PUSHAD()
{
	uint32_t sp = (REG_SS.desc.big)?REG_ESP:REG_SP;

	if(!IS_PMODE()) {
		if(sp == 7 || sp == 9 || sp == 11 || sp == 13 || sp == 15) {
			throw CPUException(CPU_SEG_OVR_EXC,0);
		}
		if(sp == 1 || sp == 3 || sp == 5) {
			throw CPUShutdown("SP=1,3,5 on stack push (PUSHAD)");
		}
	}

	stack_write_dword(REG_EAX, sp-4);
	stack_write_dword(REG_ECX, sp-8);
	stack_write_dword(REG_EDX, sp-12);
	stack_write_dword(REG_EBX, sp-16);
	stack_write_dword(REG_ESP, sp-20);
	stack_write_dword(REG_EBP, sp-24);
	stack_write_dword(REG_ESI, sp-28);
	stack_write_dword(REG_EDI, sp-32);

	if(REG_SS.desc.big) {
		REG_ESP -= 32;
	} else {
		REG_SP -= 32;
	}
}


/*******************************************************************************
 * PUSHF/PUSHFD-Push FLAGS or EFLAGS Register onto the Stack
 */

void CPUExecutor::PUSHF()
{
	if(IS_V8086() && FLAG_IOPL < 3) {
		PDEBUGF(LOG_V2, LOG_CPU, "Push Flags: general protection in v8086 mode\n");
		throw CPUException(CPU_GP_EXC, 0);
	}
	uint16_t flags = GET_FLAGS();
	stack_push_word(flags);
}

void CPUExecutor::PUSHFD()
{
	if(IS_V8086() && FLAG_IOPL < 3) {
		PDEBUGF(LOG_V2, LOG_CPU, "Push Flags: general protection in v8086 mode\n");
		throw CPUException(CPU_GP_EXC, 0);
	}
	// VM & RF flags cleared when pushed onto stack
	uint32_t eflags = GET_EFLAGS() & ~(FMASK_RF | FMASK_VM);
	stack_push_dword(eflags);
}


/*******************************************************************************
 * RCL/RCR/ROL/ROR-Rotate Instructions
 */

uint8_t CPUExecutor::ROL_b(uint8_t _op1, uint8_t _count)
{
	if(!(_count & 0x7)) { //if _count==0 || _count>=8
		if(_count & 0x18) { //if _count==8 || _count==16 || _count==24
			SET_FLAG(CF, _op1 & 1);
			SET_FLAG(OF, (_op1 & 1) ^ (_op1 >> 7));
		}
		return _op1;
	}
	_count %= 8;

	_op1 = (_op1 << _count) | (_op1 >> (8 - _count));

	SET_FLAG(CF, _op1 & 1);
	SET_FLAG(OF, (_op1 & 1) ^ (_op1 >> 7));

	if(CPU_FAMILY <= CPU_286) {
		m_instr->cycles.extra = _count;
	}

	return _op1;
}

uint16_t CPUExecutor::ROL_w(uint16_t _op1, uint8_t _count)
{
	if(!(_count & 0xF)) { //if _count==0 || _count>=15
		if(_count & 0x10) { //if _count==16 || _count==24
			SET_FLAG(CF, _op1 & 1);
			SET_FLAG(OF, (_op1 & 1) ^ (_op1 >> 15));
		}
		return _op1;
	}
	_count %= 16;

	_op1 = (_op1 << _count) | (_op1 >> (16 - _count));

	SET_FLAG(CF, _op1 & 1);
	SET_FLAG(OF, (_op1 & 1) ^ (_op1 >> 15));

	if(CPU_FAMILY <= CPU_286) {
		m_instr->cycles.extra = _count;
	}

	return _op1;
}

uint32_t CPUExecutor::ROL_d(uint32_t _op1, uint8_t _count)
{
	_count &= 0x1F;

	if(_count == 0) {
		return _op1;
	}

	_op1 = (_op1 << _count) | (_op1 >> (32 - _count));

	bool bit0  = (_op1 & 1);
	bool bit31 = (_op1 >> 31);
	SET_FLAG(CF, bit0);
	SET_FLAG(OF, bit0 ^ bit31);

	if(CPU_FAMILY <= CPU_286) {
		m_instr->cycles.extra = _count;
	}

	return _op1;
}

void CPUExecutor::ROL_eb_ib() { store_eb(ROL_b(load_eb(), m_instr->ib)); }
void CPUExecutor::ROL_ew_ib() { store_ew(ROL_w(load_ew(), m_instr->ib)); }
void CPUExecutor::ROL_ed_ib() { store_ed(ROL_d(load_ed(), m_instr->ib)); }
void CPUExecutor::ROL_eb_1()  { store_eb(ROL_b(load_eb(), 1)); }
void CPUExecutor::ROL_ew_1()  { store_ew(ROL_w(load_ew(), 1)); }
void CPUExecutor::ROL_ed_1()  { store_ed(ROL_d(load_ed(), 1)); }
void CPUExecutor::ROL_eb_CL() { store_eb(ROL_b(load_eb(), REG_CL)); }
void CPUExecutor::ROL_ew_CL() { store_ew(ROL_w(load_ew(), REG_CL)); }
void CPUExecutor::ROL_ed_CL() { store_ed(ROL_d(load_ed(), REG_CL)); }

uint8_t CPUExecutor::ROR_b(uint8_t _op1, uint8_t _count)
{
	if(!(_count & 0x7)) { //if _count==0 || _count>=8
		if(_count & 0x18) { //if _count==8 || _count==16 || _count==24
			SET_FLAG(CF, _op1 >> 7);
			SET_FLAG(OF, (_op1 >> 7) ^ ((_op1 >> 6) & 1));
		}
		return _op1;
	}
	_count %= 8;

	_op1 = (_op1 >> _count) | (_op1 << (8 - _count));

	SET_FLAG(CF, _op1 >> 7);
	SET_FLAG(OF, (_op1 >> 7) ^ ((_op1 >> 6) & 1));

	if(CPU_FAMILY <= CPU_286) {
		m_instr->cycles.extra = _count;
	}

	return _op1;
}

uint16_t CPUExecutor::ROR_w(uint16_t _op1, uint8_t _count)
{
	if(!(_count & 0xf)) { //if _count==0 || _count>=15
		if(_count & 0x10) { //if _count==16 || _count==24
			SET_FLAG(CF, _op1 >> 15);
			SET_FLAG(OF, (_op1 >> 15) ^ ((_op1 >> 14) & 1));
		}
		return _op1;
	}
	_count %= 16;

	_op1 = (_op1 >> _count) | (_op1 << (16 - _count));

	SET_FLAG(CF, _op1 >> 15);
	SET_FLAG(OF, (_op1 >> 15) ^ ((_op1 >> 14) & 1));

	if(CPU_FAMILY <= CPU_286) {
		m_instr->cycles.extra = _count;
	}

	return _op1;
}

uint32_t CPUExecutor::ROR_d(uint32_t _op1, uint8_t _count)
{
	_count &= 0x1F;

	if(_count == 0) {
		return _op1;
	}

	_op1 = (_op1 >> _count) | (_op1 << (32 - _count));

	bool bit31 = (_op1 >> 31) & 1;
	bool bit30 = (_op1 >> 30) & 1;

	SET_FLAG(CF, bit31);
	SET_FLAG(OF, bit30 ^ bit31);

	if(CPU_FAMILY <= CPU_286) {
		m_instr->cycles.extra = _count;
	}

	return _op1;
}

void CPUExecutor::ROR_eb_ib() { store_eb(ROR_b(load_eb(), m_instr->ib)); }
void CPUExecutor::ROR_ew_ib() { store_ew(ROR_w(load_ew(), m_instr->ib)); }
void CPUExecutor::ROR_ed_ib() { store_ed(ROR_d(load_ed(), m_instr->ib)); }
void CPUExecutor::ROR_eb_1()  { store_eb(ROR_b(load_eb(), 1)); }
void CPUExecutor::ROR_ew_1()  { store_ew(ROR_w(load_ew(), 1)); }
void CPUExecutor::ROR_ed_1()  { store_ed(ROR_d(load_ed(), 1)); }
void CPUExecutor::ROR_eb_CL() { store_eb(ROR_b(load_eb(), REG_CL)); }
void CPUExecutor::ROR_ew_CL() { store_ew(ROR_w(load_ew(), REG_CL)); }
void CPUExecutor::ROR_ed_CL() { store_ed(ROR_d(load_ed(), REG_CL)); }

uint8_t CPUExecutor::RCL_b(uint8_t _op1, uint8_t _count)
{
	_count = (_count & 0x1F) % 9;

	if(_count == 0) {
		return _op1;
	}

	uint8_t res;
	uint8_t cf = FLAG_CF;

	if(_count == 1) {
		res = (_op1 << 1) | cf;
	} else {
		res = (_op1 << _count) | (cf << (_count - 1)) | (_op1 >> (9 - _count));
	}

	cf = (_op1 >> (8 - _count)) & 1;

	SET_FLAG(CF, cf);
	SET_FLAG(OF, cf ^ (res >> 7));

	if(CPU_FAMILY <= CPU_286) {
		m_instr->cycles.extra = _count;
	}

	return res;
}

uint16_t CPUExecutor::RCL_w(uint16_t _op1, uint8_t _count)
{
	_count = (_count & 0x1F) % 17;

	if(_count == 0) {
		return _op1;
	}

	uint16_t res;
	uint16_t cf = FLAG_CF;

	if(_count == 1) {
		res = (_op1 << 1) | cf;
	} else if(_count == 16) {
		res = (cf << 15) | (_op1 >> 1);
	} else { // 2..15
		res = (_op1 << _count) | (cf << (_count - 1)) | (_op1 >> (17 - _count));
	}

	cf = (_op1 >> (16 - _count)) & 1;
	SET_FLAG(CF, cf);
	SET_FLAG(OF, cf ^ (res >> 15));

	if(CPU_FAMILY <= CPU_286) {
		m_instr->cycles.extra = _count;
	}

	return res;
}

uint32_t CPUExecutor::RCL_d(uint32_t _op1, uint8_t _count)
{
	_count &= 0x1F;

	if(_count == 0) {
		return _op1;
	}

	uint32_t res;
	uint32_t cf = FLAG_CF;

	if(_count == 1) {
		res = (_op1 << 1) | cf;
	} else {
		res = (_op1 << _count) | (cf << (_count - 1)) | (_op1 >> (33 - _count));
	}

	cf = (_op1 >> (32 - _count)) & 1;
	SET_FLAG(CF, cf);
	SET_FLAG(OF, cf ^ (res >> 31));

	if(CPU_FAMILY <= CPU_286) {
		m_instr->cycles.extra = _count;
	}

	return res;
}

void CPUExecutor::RCL_eb_ib() { store_eb(RCL_b(load_eb(), m_instr->ib)); }
void CPUExecutor::RCL_ew_ib() { store_ew(RCL_w(load_ew(), m_instr->ib)); }
void CPUExecutor::RCL_ed_ib() { store_ed(RCL_d(load_ed(), m_instr->ib)); }
void CPUExecutor::RCL_eb_1()  { store_eb(RCL_b(load_eb(), 1)); }
void CPUExecutor::RCL_ew_1()  { store_ew(RCL_w(load_ew(), 1)); }
void CPUExecutor::RCL_ed_1()  { store_ed(RCL_d(load_ed(), 1)); }
void CPUExecutor::RCL_eb_CL() { store_eb(RCL_b(load_eb(), REG_CL)); }
void CPUExecutor::RCL_ew_CL() { store_ew(RCL_w(load_ew(), REG_CL)); }
void CPUExecutor::RCL_ed_CL() { store_ed(RCL_d(load_ed(), REG_CL)); }

uint8_t CPUExecutor::RCR_b(uint8_t _op1, uint8_t _count)
{
	_count = (_count & 0x1F) % 9;

	if(_count == 0) {
		return _op1;
	}

	uint8_t cf = FLAG_CF;
	uint8_t res = (_op1 >> _count) | (cf << (8 - _count)) | (_op1 << (9 - _count));

	cf = (_op1 >> (_count - 1)) & 1;
	SET_FLAG(CF, cf);
	SET_FLAG(OF, (res ^ (res << 1)) & 0x80);

	if(CPU_FAMILY <= CPU_286) {
		m_instr->cycles.extra = _count;
	}

	return res;
}

uint16_t CPUExecutor::RCR_w(uint16_t _op1, uint8_t _count)
{
	_count = (_count & 0x1f) % 17;

	if(_count == 0) {
		return _op1;
	}

	uint16_t cf = FLAG_CF;
	uint16_t res = (_op1 >> _count) | (cf << (16 - _count)) | (_op1 << (17 - _count));

	cf = (_op1 >> (_count - 1)) & 1;
	SET_FLAG(CF, cf);
	SET_FLAG(OF, (res ^ (res << 1)) & 0x8000);

	if(CPU_FAMILY <= CPU_286) {
		m_instr->cycles.extra = _count;
	}

	return res;
}

uint32_t CPUExecutor::RCR_d(uint32_t _op1, uint8_t _count)
{
	_count &= 0x1F;

	if(_count == 0) {
		return _op1;
	}

	uint32_t res;
	uint32_t cf = FLAG_CF;

	if(_count == 1) {
		res = (_op1 >> 1) | (cf << 31);
	} else {
		res = (_op1 >> _count) | (cf << (32 - _count)) | (_op1 << (33 - _count));
	}

	cf = (_op1 >> (_count - 1)) & 1;
	SET_FLAG(CF, cf);
	SET_FLAG(OF, ((res << 1) ^ res) >> 31);

	if(CPU_FAMILY <= CPU_286) {
		m_instr->cycles.extra = _count;
	}

	return res;
}

void CPUExecutor::RCR_eb_ib() { store_eb(RCR_b(load_eb(), m_instr->ib)); }
void CPUExecutor::RCR_ew_ib() { store_ew(RCR_w(load_ew(), m_instr->ib)); }
void CPUExecutor::RCR_ed_ib() { store_ed(RCR_d(load_ed(), m_instr->ib)); }
void CPUExecutor::RCR_eb_1()  { store_eb(RCR_b(load_eb(), 1)); }
void CPUExecutor::RCR_ew_1()  { store_ew(RCR_w(load_ew(), 1)); }
void CPUExecutor::RCR_ed_1()  { store_ed(RCR_d(load_ed(), 1)); }
void CPUExecutor::RCR_eb_CL() { store_eb(RCR_b(load_eb(), REG_CL)); }
void CPUExecutor::RCR_ew_CL() { store_ew(RCR_w(load_ew(), REG_CL)); }
void CPUExecutor::RCR_ed_CL() { store_ed(RCR_d(load_ed(), REG_CL)); }


/*******************************************************************************
 * RET-Return from Procedure
 */

void CPUExecutor::RET_near_o16()
{
	return_near(stack_pop_word(), m_instr->iw1);
}

void CPUExecutor::RET_near_o32()
{
	return_near(stack_pop_dword(), m_instr->iw1);
}

void CPUExecutor::RET_far_o16()
{
	if(IS_PMODE()) {
		return_far_pmode(m_instr->iw1, false);
	} else {
		uint16_t ip     = stack_pop_word();
		uint16_t cs_raw = stack_pop_word();

		return_far_rmode(cs_raw, ip, m_instr->iw1);
	}
}

void CPUExecutor::RET_far_o32()
{
	if(IS_PMODE()) {
		return_far_pmode(m_instr->iw1, true);
	} else {
		uint32_t eip    = stack_pop_dword();
		uint16_t cs_raw = stack_pop_dword(); // dword pop

		return_far_rmode(cs_raw, eip, m_instr->iw1);
	}
}


/*******************************************************************************
 * SAHF-Store AH into Flags
 */

void CPUExecutor::SAHF()
{
	uint16_t ah = REG_AH;
	SET_FLAG(SF, ah & FMASK_SF);
	SET_FLAG(ZF, ah & FMASK_ZF);
	SET_FLAG(AF, ah & FMASK_AF);
	SET_FLAG(PF, ah & FMASK_PF);
	SET_FLAG(CF, ah & FMASK_CF);
}


/*******************************************************************************
 * SALC-Set AL If Carry
 */

void CPUExecutor::SALC()
{
	//http://www.rcollins.org/secrets/opcodes/SALC.html
	PDEBUGF(LOG_V1, LOG_CPU, "SALC: undocumented opcode\n");
	if(FLAG_CF) {
		REG_AL = 0xFF;
	} else {
		REG_AL = 0;
	}
}


/*******************************************************************************
 * SAL/SAR/SHL/SHR-Shift Instructions
 */

uint8_t CPUExecutor::SHL_b(uint8_t _op1, uint8_t _count)
{
	_count &= 0x1F;

	if(_count == 0) {
		return _op1;
	}

	uint of = 0, cf = 0;
	uint8_t res;

	if(_count <= 8) {
		res = (_op1 << _count);
		cf = (_op1 >> (8 - _count)) & 0x1;
		of = cf ^ (res >> 7);
	} else {
		res = 0;
	}

	SET_FLAG(OF, of);
	SET_FLAG(CF, cf);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(SF, res & 0x80);
	SET_FLAG(AF, false);

	if(CPU_FAMILY <= CPU_286) {
		m_instr->cycles.extra = _count;
	}

	return res;
}

uint16_t CPUExecutor::SHL_w(uint16_t _op1, uint8_t _count)
{
	_count &= 0x1F;

	if(_count == 0) {
		return _op1;
	}

	uint16_t res;
	uint of = 0, cf = 0;

	if(_count <= 16) {
		res = (_op1 << _count);
		cf = (_op1 >> (16 - _count)) & 0x1;
		of = cf ^ (res >> 15);
	} else {
		res = 0;
	}

	SET_FLAG(OF, of);
	SET_FLAG(CF, cf);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(SF, res & 0x8000);
	SET_FLAG(AF, false);

	if(CPU_FAMILY <= CPU_286) {
		m_instr->cycles.extra = _count;
	}

	return res;
}

uint32_t CPUExecutor::SHL_d(uint32_t _op1, uint8_t _count)
{
	_count &= 0x1F;

	if(_count == 0) {
		return _op1;
	}

	/* count < 32, since only lower 5 bits used */
	uint32_t res = (_op1 << _count);

	bool cf = (_op1 >> (32 - _count)) & 0x1;
	bool of = cf ^ (res >> 31);
	SET_FLAG(CF, cf);
	SET_FLAG(OF, of);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(SF, res & 0x80000000);
	SET_FLAG(AF, false);

	if(CPU_FAMILY <= CPU_286) {
		m_instr->cycles.extra = _count;
	}

	return res;
}

void CPUExecutor::SAL_eb_ib() { store_eb(SHL_b(load_eb(), m_instr->ib)); }
void CPUExecutor::SAL_ew_ib() { store_ew(SHL_w(load_ew(), m_instr->ib)); }
void CPUExecutor::SAL_ed_ib() { store_ed(SHL_d(load_ed(), m_instr->ib)); }
void CPUExecutor::SAL_eb_1()  { store_eb(SHL_b(load_eb(), 1)); }
void CPUExecutor::SAL_ew_1()  { store_ew(SHL_w(load_ew(), 1)); }
void CPUExecutor::SAL_ed_1()  { store_ed(SHL_d(load_ed(), 1)); }
void CPUExecutor::SAL_eb_CL() { store_eb(SHL_b(load_eb(), REG_CL)); }
void CPUExecutor::SAL_ew_CL() { store_ew(SHL_w(load_ew(), REG_CL)); }
void CPUExecutor::SAL_ed_CL() { store_ed(SHL_d(load_ed(), REG_CL)); }

uint8_t CPUExecutor::SHR_b(uint8_t _op1, uint8_t _count)
{
	_count &= 0x1f;

	if(_count == 0) {
		return _op1;
	}

	uint8_t res = _op1 >> _count;

	SET_FLAG(OF, (((res << 1) ^ res) >> 7) & 0x1);
	SET_FLAG(CF, (_op1 >> (_count - 1)) & 0x1);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(SF, res & 0x80);
	SET_FLAG(AF, false);

	if(CPU_FAMILY <= CPU_286) {
		m_instr->cycles.extra = _count;
	}

	return res;
}

uint16_t CPUExecutor::SHR_w(uint16_t _op1, uint8_t _count)
{
	_count &= 0x1f;

	if(_count == 0) {
		return _op1;
	}

	uint16_t res = _op1 >> _count;

	SET_FLAG(OF, ((uint16_t)((res << 1) ^ res) >> 15) & 0x1);
	SET_FLAG(CF, (_op1 >> (_count - 1)) & 1);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(SF, res & 0x8000);
	SET_FLAG(AF, false);

	if(CPU_FAMILY <= CPU_286) {
		m_instr->cycles.extra = _count;
	}

	return res;
}

uint32_t CPUExecutor::SHR_d(uint32_t _op1, uint8_t _count)
{
	_count &= 0x1F;

	if(_count == 0) {
		return _op1;
	}

	uint32_t res = (_op1 >> _count);

	bool cf = (_op1 >> (_count - 1)) & 1;
	// of == result31 if count == 1 and
	// of == 0        if count >= 2
	bool of = ((res << 1) ^ res) >> 31;

	SET_FLAG(CF, cf);
	SET_FLAG(OF, of);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(SF, res & 0x80000000);
	SET_FLAG(AF, false);

	if(CPU_FAMILY <= CPU_286) {
		m_instr->cycles.extra = _count;
	}

	return res;
}

void CPUExecutor::SHR_eb_ib() { store_eb(SHR_b(load_eb(), m_instr->ib)); }
void CPUExecutor::SHR_ew_ib() { store_ew(SHR_w(load_ew(), m_instr->ib)); }
void CPUExecutor::SHR_ed_ib() { store_ed(SHR_d(load_ed(), m_instr->ib)); }
void CPUExecutor::SHR_eb_1()  { store_eb(SHR_b(load_eb(), 1)); }
void CPUExecutor::SHR_ew_1()  { store_ew(SHR_w(load_ew(), 1)); }
void CPUExecutor::SHR_ed_1()  { store_ed(SHR_d(load_ed(), 1)); }
void CPUExecutor::SHR_eb_CL() { store_eb(SHR_b(load_eb(), REG_CL)); }
void CPUExecutor::SHR_ew_CL() { store_ew(SHR_w(load_ew(), REG_CL)); }
void CPUExecutor::SHR_ed_CL() { store_ed(SHR_d(load_ed(), REG_CL)); }

uint8_t CPUExecutor::SAR_b(uint8_t _op1, uint8_t _count)
{
	_count &= 0x1F;

	if(_count == 0) {
		return _op1;
	}

	uint8_t res = ((int8_t) _op1) >> _count;

	SET_FLAG(OF, false);
	SET_FLAG(CF, (((int8_t) _op1) >> (_count - 1)) & 1);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(SF, res & 0x80);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(AF, false);

	if(CPU_FAMILY <= CPU_286) {
		m_instr->cycles.extra = _count;
	}

	return res;
}

uint16_t CPUExecutor::SAR_w(uint16_t _op1, uint8_t _count)
{
	_count &= 0x1F;

	if(_count == 0) {
		return _op1;
	}

	uint16_t res = ((int16_t) _op1) >> _count;

	SET_FLAG(OF, false);
	SET_FLAG(CF, (((int16_t) _op1) >> (_count - 1)) & 1);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(SF, res & 0x8000);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(AF, false);

	if(CPU_FAMILY <= CPU_286) {
		m_instr->cycles.extra = _count;
	}

	return res;
}

uint32_t CPUExecutor::SAR_d(uint32_t _op1, uint8_t _count)
{
	_count &= 0x1F;

	if(_count == 0) {
		return _op1;
	}
	/* count < 32, since only lower 5 bits used */
	uint32_t res = ((int32_t)_op1) >> _count;

	SET_FLAG(OF, false);
	SET_FLAG(CF, (_op1 >> (_count - 1)) & 1);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(SF, res & 0x80000000);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(AF, false);

	if(CPU_FAMILY <= CPU_286) {
		m_instr->cycles.extra = _count;
	}

	return res;
}

void CPUExecutor::SAR_eb_ib() { store_eb(SAR_b(load_eb(), m_instr->ib)); }
void CPUExecutor::SAR_ew_ib() { store_ew(SAR_w(load_ew(), m_instr->ib)); }
void CPUExecutor::SAR_ed_ib() { store_ed(SAR_d(load_ed(), m_instr->ib)); }
void CPUExecutor::SAR_eb_1()  { store_eb(SAR_b(load_eb(), 1)); }
void CPUExecutor::SAR_ew_1()  { store_ew(SAR_w(load_ew(), 1)); }
void CPUExecutor::SAR_ed_1()  { store_ed(SAR_d(load_ed(), 1)); }
void CPUExecutor::SAR_eb_CL() { store_eb(SAR_b(load_eb(), REG_CL)); }
void CPUExecutor::SAR_ew_CL() { store_ew(SAR_w(load_ew(), REG_CL)); }
void CPUExecutor::SAR_ed_CL() { store_ed(SAR_d(load_ed(), REG_CL)); }


/*******************************************************************************
 * SBB-Integer Subtraction With Borrow
 */

uint8_t CPUExecutor::SBB_b(uint8_t _op1, uint8_t _op2)
{
	uint8_t cf = FLAG_CF;
	uint8_t res = _op1 - (_op2 + cf);

	SET_FLAG(OF, ((_op1 ^ _op2) & (_op1 ^ res)) & 0x80);
	SET_FLAG(SF, res & 0x80);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, ((_op1 ^ _op2) ^ res) & 0x10);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(CF, (_op1 < res) || (cf && (_op2==0xff)));

	return res;
}

uint16_t CPUExecutor::SBB_w(uint16_t _op1, uint16_t _op2)
{
	uint16_t cf = FLAG_CF;
	uint16_t res = _op1 - (_op2 + cf);

	SET_FLAG(OF, ((_op1 ^ _op2) & (_op1 ^ res)) & 0x8000);
	SET_FLAG(SF, res & 0x8000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, ((_op1 ^ _op2) ^ res) & 0x10);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(CF, (_op1 < res) || (cf && (_op2==0xffff)));

	return res;
}

uint32_t CPUExecutor::SBB_d(uint32_t _op1, uint32_t _op2)
{
	uint32_t cf = FLAG_CF;
	uint32_t res = _op1 - (_op2 + cf);

	SET_FLAG(OF, ((_op1 ^ _op2) & (_op1 ^ res)) & 0x80000000);
	SET_FLAG(SF, res & 0x80000000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, ((_op1 ^ _op2) ^ res) & 0x10);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(CF, (_op1 < res) || (cf && (_op2==0xffffffff)));

	return res;
}

void CPUExecutor::SBB_eb_rb() { store_eb(SBB_b(load_eb(), load_rb())); }
void CPUExecutor::SBB_ew_rw() { store_ew(SBB_w(load_ew(), load_rw())); }
void CPUExecutor::SBB_ed_rd() { store_ed(SBB_d(load_ed(), load_rd())); }
void CPUExecutor::SBB_rb_eb() { store_rb(SBB_b(load_rb(), load_eb())); }
void CPUExecutor::SBB_rw_ew() { store_rw(SBB_w(load_rw(), load_ew())); }
void CPUExecutor::SBB_rd_ed() { store_rd(SBB_d(load_rd(), load_ed())); }
void CPUExecutor::SBB_AL_ib() { REG_AL = SBB_b(REG_AL, m_instr->ib); }
void CPUExecutor::SBB_AX_iw() { REG_AX = SBB_w(REG_AX, m_instr->iw1); }
void CPUExecutor::SBB_EAX_id(){REG_EAX = SBB_d(REG_EAX, m_instr->id1); }
void CPUExecutor::SBB_eb_ib() { store_eb(SBB_b(load_eb(), m_instr->ib)); }
void CPUExecutor::SBB_ew_iw() { store_ew(SBB_w(load_ew(), m_instr->iw1)); }
void CPUExecutor::SBB_ed_id() { store_ed(SBB_d(load_ed(), m_instr->id1)); }
void CPUExecutor::SBB_ew_ib() { store_ew(SBB_w(load_ew(), int8_t(m_instr->ib))); }
void CPUExecutor::SBB_ed_ib() { store_ed(SBB_d(load_ed(), int8_t(m_instr->ib))); }


/*******************************************************************************
 * SCASB/SCASW/SCASD-Compare String Data
 */

void CPUExecutor::SCASB_a16()
{
	// segment override not possible
	CMP_b(REG_AL, read_byte(REG_ES, REG_DI));

	if(FLAG_DF) {
		REG_DI -= 1;
	} else {
		REG_DI += 1;
	}
}

void CPUExecutor::SCASB_a32()
{
	CMP_b(REG_AL, read_byte(REG_ES, REG_EDI));

	if(FLAG_DF) {
		REG_EDI -= 1;
	} else {
		REG_EDI += 1;
	}
}

void CPUExecutor::SCASW_a16()
{
	CMP_w(REG_AX, read_word(REG_ES, REG_DI));

	if(FLAG_DF) {
		REG_DI -= 2;
	} else {
		REG_DI += 2;
	}
}

void CPUExecutor::SCASW_a32()
{
	CMP_w(REG_AX, read_word(REG_ES, REG_EDI));

	if(FLAG_DF) {
		REG_EDI -= 2;
	} else {
		REG_EDI += 2;
	}
}

void CPUExecutor::SCASD_a16()
{
	CMP_d(REG_EAX, read_dword(REG_ES, REG_DI));

	if(FLAG_DF) {
		REG_DI -= 4;
	} else {
		REG_DI += 4;
	}
}

void CPUExecutor::SCASD_a32()
{
	CMP_d(REG_EAX, read_dword(REG_ES, REG_EDI));

	if(FLAG_DF) {
		REG_EDI -= 4;
	} else {
		REG_EDI += 4;
	}
}


/*******************************************************************************
 * SETcc-Byte Set on Condition
 */

void CPUExecutor::SETO_eb()   /*0F90*/ { store_eb( FLAG_OF ); }
void CPUExecutor::SETNO_eb()  /*0F91*/ { store_eb(!FLAG_OF ); }
void CPUExecutor::SETB_eb()   /*0F92*/ { store_eb( FLAG_CF ); }
void CPUExecutor::SETNB_eb()  /*0F93*/ { store_eb(!FLAG_CF ); }
void CPUExecutor::SETE_eb()   /*0F94*/ { store_eb( FLAG_ZF ); }
void CPUExecutor::SETNE_eb()  /*0F95*/ { store_eb(!FLAG_ZF ); }
void CPUExecutor::SETBE_eb()  /*0F96*/ { store_eb(  FLAG_CF || FLAG_ZF ); }
void CPUExecutor::SETNBE_eb() /*0F97*/ { store_eb(!(FLAG_CF || FLAG_ZF) ); }
void CPUExecutor::SETS_eb()   /*0F98*/ { store_eb( FLAG_SF ); }
void CPUExecutor::SETNS_eb()  /*0F99*/ { store_eb(!FLAG_SF ); }
void CPUExecutor::SETP_eb()   /*0F9A*/ { store_eb( FLAG_PF ); }
void CPUExecutor::SETNP_eb()  /*0F9B*/ { store_eb(!FLAG_PF ); }
void CPUExecutor::SETL_eb()   /*0F9C*/ { store_eb(  FLAG_SF != FLAG_OF ); }
void CPUExecutor::SETNL_eb()  /*0F9D*/ { store_eb(!(FLAG_SF != FLAG_OF) ); }
void CPUExecutor::SETLE_eb()  /*0F9E*/ { store_eb(  FLAG_ZF || FLAG_SF!=FLAG_OF ); }
void CPUExecutor::SETNLE_eb() /*0F9F*/ { store_eb(!(FLAG_ZF || FLAG_SF!=FLAG_OF) ); }


/*******************************************************************************
 * SGDT/SIDT/SLDT-Store Global/Interrupt/Local Descriptor Table Register
 */

void CPUExecutor::SDT(unsigned _reg)
{
	uint16_t limit_16 = SEG_REG(_reg).desc.limit;
	uint32_t base_32  = SEG_REG(_reg).desc.base;

	if(CPU_FAMILY <= CPU_286) {
		/* Unlike to what described in the iAPX 286 Programmer's Reference Manual,
		 * the last byte is not undefined, it's always 0xFF. Windows 3.0 checks
		 * this value to detect the 80286.
		 */
		base_32 = 0xFF000000 | base_32;
	}
	/* For 32-bit CPUs, AMD documentation states that SGDT/SIDT instructions
	 * ignore any operand size prefixes and always store full 32 bits of base
	 * address (Intel documentation is wrong).
	 */

	SegReg & sr = (this->*EA_get_segreg)();
	uint32_t off = (this->*EA_get_offset)();

	write_word(sr, off, limit_16);
	write_dword(sr, (off+2) & m_addr_mask, base_32);
}

void CPUExecutor::SGDT() { SDT(REGI_GDTR); }
void CPUExecutor::SIDT() { SDT(REGI_IDTR); }

void CPUExecutor::SLDT_ew()
{
	if(!IS_PMODE()) {
		PDEBUGF(LOG_V2, LOG_CPU, "SLDT: not recognized in real mode\n");
		throw CPUException(CPU_UD_EXC, 0);
	}
	uint16_t val16 = REG_LDTR.sel.value;
	store_ew(val16);
	if(m_instr->op32 && m_instr->modrm.mod==3) {
		/* When the destination operand is a 32-bit register the high-order
		 * 16 bits of the register are cleared.
		 */
		GEN_REG(m_instr->modrm.rm).word[1] = 0;
	}
}


/*******************************************************************************
 * SHLD-Double Precision Shift Left
 */

uint16_t CPUExecutor::SHLD_w(uint16_t _op1, uint16_t _op2, uint8_t _count)
{
	uint16_t result = _op1;

	_count %= 32;

	if(_count) {
		uint32_t op1_op2 = (uint32_t(_op1) << 16) | _op2; // double formed by op1:op2
		uint32_t result_32 = op1_op2 << _count;

		// hack to act like x86 SHLD when count > 16
		if(_count > 16) {
			/* For Pentium processor, when count > 16, actually shifting op1:op2:op2 << count,
			 * it is the same as shifting op2:op2 by count-16
			 * For P6 and later, when count > 16, actually shifting op1:op2:op1 << count,
			 * which is the same as shifting op2:op1 by count-16
			 * Intel docs state that if count > operand size then result and flags are undefined,
			 * so both ways are correct.
			 *
			 * Bochs follows the P6+ behaviour.
			 * DOSBox follows the Pentium behaviour.
			 * IBMulator does the same as DOSBox.
			 */
			result_32 |= (uint32_t(_op2) << (_count - 16));
		}

		result = uint16_t(result_32 >> 16);

		SET_FLAG(ZF, result == 0);
		SET_FLAG(SF, result & 0x8000);
		SET_FLAG(PF, PARITY(result));
		bool cf = (op1_op2 >> (32 - _count)) & 1;
		bool of = cf ^ (result >> 15); // of = cf ^ result15
		SET_FLAG(CF, cf);
		SET_FLAG(OF, of);
	}

	return result;
}

uint32_t CPUExecutor::SHLD_d(uint32_t _op1, uint32_t _op2, uint8_t _count)
{
	uint32_t result = _op1;

	_count %= 32;

	if(_count) {
		result = (_op1 << _count) | (_op2 >> (32 - _count));

		SET_FLAG(ZF, result == 0);
		SET_FLAG(SF, result & 0x80000000);
		SET_FLAG(PF, PARITY(result));
		bool cf = (_op1 >> (32 - _count)) & 1;
		bool of = cf ^ (result >> 31); // of = cf ^ result31
		SET_FLAG(CF, cf);
		SET_FLAG(OF, of);
	}

	return result;
}

void CPUExecutor::SHLD_ew_rw_ib() { store_ew(SHLD_w(load_ew(), load_rw(), m_instr->ib)); }
void CPUExecutor::SHLD_ed_rd_ib() { store_ed(SHLD_d(load_ed(), load_rd(), m_instr->ib)); }
void CPUExecutor::SHLD_ew_rw_CL() { store_ew(SHLD_w(load_ew(), load_rw(), REG_CL)); }
void CPUExecutor::SHLD_ed_rd_CL() { store_ed(SHLD_d(load_ed(), load_rd(), REG_CL)); }


/*******************************************************************************
 * SHRD-Double Precision Shift Right
 */

uint16_t CPUExecutor::SHRD_w(uint16_t _op1, uint16_t _op2, uint8_t _count)
{
	uint16_t result = _op1;
	_count %= 32;
	if(_count) {
		uint32_t op2_op1 = (uint32_t(_op2) << 16) | _op1; // double formed by op2:op1
		uint32_t result_32 = op2_op1 >> _count;

		// hack to act like x86 SHRD when count > 16
		if(_count > 16) {
			/* For Pentium processor, when count > 16, actually shifting op2:op2:op1 >> count,
			 * it is the same as shifting op2:op2 by count-16
			 * For P6 and later, when count > 16, actually shifting op1:op2:op1 >> count,
			 * which is the same as shifting op1:op2 by count-16
			 * The behavior is undefined so both ways are correct.
			 *
			 * Bochs follows the P6+ behaviour.
			 * DOSBox follows the Pentium behaviour.
			 * IBMulator does the same as DOSBox.
			 */
			result_32 |= (uint32_t(_op2) << (32 - _count));
		}

		result = uint16_t(result_32);

		SET_FLAG(ZF, result == 0);
		SET_FLAG(SF, result & 0x8000);
		SET_FLAG(PF, PARITY(result));
		bool cf = (_op1 >> (_count - 1)) & 1;
		bool of = (((result << 1) ^ result) >> 15) & 1; // of = result14 ^ result15
		SET_FLAG(CF, cf);
		SET_FLAG(OF, of);
	}

	return result;
}

uint32_t CPUExecutor::SHRD_d(uint32_t _op1, uint32_t _op2, uint8_t _count)
{
	uint32_t result = _op1;

	_count %= 32;

	if(_count) {
		result = (_op2 << (32 - _count)) | (_op1 >> _count);

		SET_FLAG(ZF, result == 0);
		SET_FLAG(SF, result & 0x80000000);
		SET_FLAG(PF, PARITY(result));
		bool cf = (_op1 >> (_count - 1)) & 1;
		bool of = ((result << 1) ^ result) >> 31; // of = result30 ^ result31
		SET_FLAG(CF, cf);
		SET_FLAG(OF, of);
	}

	return result;
}

void CPUExecutor::SHRD_ew_rw_ib() { store_ew(SHRD_w(load_ew(), load_rw(), m_instr->ib)); }
void CPUExecutor::SHRD_ed_rd_ib() { store_ed(SHRD_d(load_ed(), load_rd(), m_instr->ib)); }
void CPUExecutor::SHRD_ew_rw_CL() { store_ew(SHRD_w(load_ew(), load_rw(), REG_CL)); }
void CPUExecutor::SHRD_ed_rd_CL() { store_ed(SHRD_d(load_ed(), load_rd(), REG_CL)); }


/*******************************************************************************
 * SMSW-Store Machine Status Word
 */

void CPUExecutor::SMSW_ew()
{
	uint16_t msw = GET_MSW();
	store_ew(msw);
	if(m_instr->op32 && m_instr->modrm.mod==3) {
		/* When the destination operand is a 32-bit register the high-order
		 * 16 bits of the register are cleared.
		 */
		GEN_REG(m_instr->modrm.rm).word[1] = 0;
	}
}


/*******************************************************************************
 * STC/STD/STI-Set Carry/Direction/Interrupt Flag
 */

void CPUExecutor::STC()
{
	SET_FLAG(CF, true);
}

void CPUExecutor::STD()
{
	SET_FLAG(DF, true);
}

void CPUExecutor::STI()
{
	if(IS_PMODE() && (CPL > FLAG_IOPL)) {
		PDEBUGF(LOG_V2, LOG_CPU, "STI: CPL > IOPL in protected mode\n");
		throw CPUException(CPU_GP_EXC, 0);
    }
	if(!FLAG_IF) {
		SET_FLAG(IF, true);
		g_cpu.inhibit_interrupts(CPU_INHIBIT_INTERRUPTS);
	}
}


/*******************************************************************************
 * STOSB/STOSW/STOSD-Store String Data
 */

void CPUExecutor::STOSB_a16()
{
	//no segment override is possible.
	write_byte(REG_ES, REG_DI, REG_AL);

	if(FLAG_DF) {
		REG_DI -= 1;
	} else {
		REG_DI += 1;
	}
}

void CPUExecutor::STOSB_a32()
{
	//no segment override is possible.
	write_byte(REG_ES, REG_EDI, REG_AL);

	if(FLAG_DF) {
		REG_EDI -= 1;
	} else {
		REG_EDI += 1;
	}
}

void CPUExecutor::STOSW_a16()
{
	//no segment override is possible.
	write_word(REG_ES, REG_DI, REG_AX);

	if(FLAG_DF) {
		REG_DI -= 2;
	} else {
		REG_DI += 2;
	}
}

void CPUExecutor::STOSW_a32()
{
	//no segment override is possible.
	write_word(REG_ES, REG_EDI, REG_AX);

	if(FLAG_DF) {
		REG_EDI -= 2;
	} else {
		REG_EDI += 2;
	}
}

void CPUExecutor::STOSD_a16()
{
	//no segment override is possible.
	write_dword(REG_ES, REG_DI, REG_EAX);

	if(FLAG_DF) {
		REG_DI -= 4;
	} else {
		REG_DI += 4;
	}
}

void CPUExecutor::STOSD_a32()
{
	//no segment override is possible.
	write_dword(REG_ES, REG_EDI, REG_EAX);

	if(FLAG_DF) {
		REG_EDI -= 4;
	} else {
		REG_EDI += 4;
	}
}


/*******************************************************************************
 * STR-Store Task Register
 */

void CPUExecutor::STR_ew()
{
	if(!IS_PMODE()) {
		PDEBUGF(LOG_V2, LOG_CPU, "STR: not recognized in real mode\n");
		throw CPUException(CPU_UD_EXC, 0);
	}
	uint16_t val = REG_TR.sel.value;
	store_ew(val);
	if(m_instr->op32 && m_instr->modrm.mod==3) {
		/* When the destination operand is a 32-bit register the high-order
		 * 16 bits of the register are cleared.
		 */
		GEN_REG(m_instr->modrm.rm).word[1] = 0;
	}
}


/*******************************************************************************
 * SUB-Integer Subtraction
 */

uint8_t CPUExecutor::SUB_b(uint8_t _op1, uint8_t _op2)
{
	uint8_t res = _op1 - _op2;

	SET_FLAG(OF, ((_op1 ^ _op2) & (_op1 ^ res)) & 0x80);
	SET_FLAG(SF, res & 0x80);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, ((_op1 ^ _op2) ^ res) & 0x10);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(CF, _op1 < _op2);

	return res;
}

uint16_t CPUExecutor::SUB_w(uint16_t _op1, uint16_t _op2)
{
	uint16_t res = _op1 - _op2;

	SET_FLAG(OF, ((_op1 ^ _op2) & (_op1 ^ res)) & 0x8000);
	SET_FLAG(SF, res & 0x8000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, ((_op1 ^ _op2) ^ res) & 0x10);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(CF, _op1 < _op2);

	return res;
}

uint32_t CPUExecutor::SUB_d(uint32_t _op1, uint32_t _op2)
{
	uint32_t res = _op1 - _op2;

	SET_FLAG(OF, ((_op1 ^ _op2) & (_op1 ^ res)) & 0x80000000);
	SET_FLAG(SF, res & 0x80000000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, ((_op1 ^ _op2) ^ res) & 0x10);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(CF, _op1 < _op2);

	return res;
}

void CPUExecutor::SUB_eb_rb() { store_eb(SUB_b(load_eb(), load_rb())); }
void CPUExecutor::SUB_ew_rw() { store_ew(SUB_w(load_ew(), load_rw())); }
void CPUExecutor::SUB_ed_rd() { store_ed(SUB_d(load_ed(), load_rd())); }
void CPUExecutor::SUB_rb_eb() { store_rb(SUB_b(load_rb(), load_eb())); }
void CPUExecutor::SUB_rw_ew() { store_rw(SUB_w(load_rw(), load_ew())); }
void CPUExecutor::SUB_rd_ed() { store_rd(SUB_d(load_rd(), load_ed())); }
void CPUExecutor::SUB_AL_ib() { REG_AL = SUB_b(REG_AL, m_instr->ib); }
void CPUExecutor::SUB_AX_iw() { REG_AX = SUB_w(REG_AX, m_instr->iw1); }
void CPUExecutor::SUB_EAX_id(){REG_EAX = SUB_d(REG_EAX, m_instr->id1); }
void CPUExecutor::SUB_eb_ib() { store_eb(SUB_b(load_eb(), m_instr->ib)); }
void CPUExecutor::SUB_ew_iw() { store_ew(SUB_w(load_ew(), m_instr->iw1)); }
void CPUExecutor::SUB_ed_id() { store_ed(SUB_d(load_ed(), m_instr->id1)); }
void CPUExecutor::SUB_ew_ib() { store_ew(SUB_w(load_ew(), int8_t(m_instr->ib))); }
void CPUExecutor::SUB_ed_ib() { store_ed(SUB_d(load_ed(), int8_t(m_instr->ib))); }


/*******************************************************************************
 * TEST-Logical Compare
 */

void CPUExecutor::TEST_b(uint8_t _value1, uint8_t _value2)
{
	uint8_t res = _value1 & _value2;

	SET_FLAG(OF, false);
	SET_FLAG(CF, false);
	SET_FLAG(SF, res & 0x80);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(AF, false); // unknown
}

void CPUExecutor::TEST_w(uint16_t _value1, uint16_t _value2)
{
	uint16_t res = _value1 & _value2;

	SET_FLAG(OF, false);
	SET_FLAG(CF, false);
	SET_FLAG(SF, res & 0x8000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(AF, false); // unknown
}

void CPUExecutor::TEST_d(uint32_t _value1, uint32_t _value2)
{
	uint32_t res = _value1 & _value2;

	SET_FLAG(OF, false);
	SET_FLAG(CF, false);
	SET_FLAG(SF, res & 0x80000000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(AF, false); // unknown
}

void CPUExecutor::TEST_eb_rb() { TEST_b(load_eb(), load_rb()); }
void CPUExecutor::TEST_ew_rw() { TEST_w(load_ew(), load_rw()); }
void CPUExecutor::TEST_ed_rd() { TEST_d(load_ed(), load_rd()); }
void CPUExecutor::TEST_AL_ib() { TEST_b(REG_AL, m_instr->ib); }
void CPUExecutor::TEST_AX_iw() { TEST_w(REG_AX, m_instr->iw1); }
void CPUExecutor::TEST_EAX_id(){ TEST_d(REG_EAX, m_instr->id1); }
void CPUExecutor::TEST_eb_ib() { TEST_b(load_eb(), m_instr->ib); }
void CPUExecutor::TEST_ew_iw() { TEST_w(load_ew(), m_instr->iw1); }
void CPUExecutor::TEST_ed_id() { TEST_d(load_ed(), m_instr->id1); }


/*******************************************************************************
 * VERR,VERW-Verify a Segment for Reading or Writing
 */

void CPUExecutor::VERR_ew()
{
	Descriptor descriptor;
	Selector   selector;

	if(!IS_PMODE()) {
		PDEBUGF(LOG_V2, LOG_CPU, "VERR: not recognized in real mode\n");
		throw CPUException(CPU_UD_EXC, 0);
	}

	selector = load_ew();

	/* if selector null, clear ZF and done */
	if((selector.value & SELECTOR_RPL_MASK) == 0) {
		PDEBUGF(LOG_V2, LOG_CPU, "VERR: null selector\n");
	    SET_FLAG(ZF, false);
	    return;
	}

	try {
		descriptor = fetch_descriptor(selector,0);
	} catch(CPUException &e) {
	    PDEBUGF(LOG_V2, LOG_CPU, "VERR: not within descriptor table\n");
	    SET_FLAG(ZF, false);
	    return;
	}

	/* If source selector is visible at CPL & RPL,
	 * within the descriptor table, and of type accepted by VERR instruction,
	 * then load register with segment limit and set ZF
	 */

	if(!descriptor.segment) {
		PDEBUGF(LOG_V2, LOG_CPU, "VERR: system descriptor\n");
		SET_FLAG(ZF, false);
		return;
	}

	if(!descriptor.valid) {
	    PDEBUGF(LOG_V2, LOG_CPU, "VERR: valid bit cleared\n");
		SET_FLAG(ZF, false);
		return;
	}

	// normal data/code segment
	if(descriptor.is_code_segment()) {
		// ignore DPL for readable conforming segments
		if(descriptor.is_conforming() && descriptor.is_readable()) {
			PDEBUGF(LOG_V2, LOG_CPU, "VERR: conforming code, OK\n");
			SET_FLAG(ZF, true);
			return;
	    }
	    if(!descriptor.is_readable()) {
	    	PDEBUGF(LOG_V2, LOG_CPU, "VERR: code not readable\n");
			SET_FLAG(ZF, false);
			return;
	    }
	    // readable, non-conforming code segment
	    if((descriptor.dpl<CPL) || (descriptor.dpl<selector.rpl)) {
	    	PDEBUGF(LOG_V2, LOG_CPU, "VERR: non-conforming code not withing priv level\n");
	    	SET_FLAG(ZF, false);
	    } else {
	    	SET_FLAG(ZF, true);
	    }
	} else {
		// data segment
		if((descriptor.dpl<CPL) || (descriptor.dpl<selector.rpl)) {
			PDEBUGF(LOG_V2, LOG_CPU, "VERR: data seg not withing priv level\n");
			SET_FLAG(ZF, false);
	    } else {
	    	SET_FLAG(ZF, true);
	    }
	}
}

void CPUExecutor::VERW_ew()
{
	Descriptor descriptor;
	Selector   selector;

	if(!IS_PMODE()) {
		PDEBUGF(LOG_V2, LOG_CPU, "VERW: not recognized in real mode\n");
		throw CPUException(CPU_UD_EXC, 0);
	}

	selector = load_ew();

	/* if selector null, clear ZF and done */
	if((selector.value & SELECTOR_RPL_MASK) == 0) {
		PDEBUGF(LOG_V2, LOG_CPU, "VERW: null selector\n");
		SET_FLAG(ZF, false);
		return;
	}

	/* If source selector is visible at CPL & RPL,
	 * within the descriptor table, and of type accepted by VERW instruction,
	 * then load register with segment limit and set ZF
	 */

	try {
		descriptor = fetch_descriptor(selector,0);
	} catch(CPUException &e) {
		PDEBUGF(LOG_V2, LOG_CPU, "VERW: not within descriptor table\n");
		SET_FLAG(ZF, false);
		return;
	}

	// rule out system segments & code segments
	if(descriptor.is_system_segment() || descriptor.is_code_segment()) {
		PDEBUGF(LOG_V2, LOG_CPU, "VERW: system seg or code\n");
		SET_FLAG(ZF, false);
		return;
	}

	if(!descriptor.valid) {
		PDEBUGF(LOG_V2, LOG_CPU, "VERW: valid bit cleared\n");
		SET_FLAG(ZF, false);
		return;
	}

	// data segment
	if(descriptor.is_writeable()) {
		if((descriptor.dpl < CPL) || (descriptor.dpl < selector.rpl)) {
			PDEBUGF(LOG_V2, LOG_CPU, "VERW: writable data seg not within priv level\n");
			SET_FLAG(ZF, false);
		} else {
			SET_FLAG(ZF, true);
		}
	} else {
		PDEBUGF(LOG_V2, LOG_CPU, "VERW: data seg not writable\n");
		SET_FLAG(ZF, false);
	}
}


/*******************************************************************************
 * WAIT-Wait Until BUSY Pin Is Inactive (HIGH)
 */

void CPUExecutor::WAIT()
{
/* TODO fpu support?
#NM if task switch flag in MSW is set. #MF if 80287 has detected an
unmasked numeric error.
*/
	//checks also MP
	if(CR0_TS && CR0_MP) {
		throw CPUException(CPU_NM_EXC, 0);
	}
}


/*******************************************************************************
 * XCHG-Exchange Memory/Register with Register
 */

void CPUExecutor::XCHG_eb_rb()
{
	uint8_t eb = load_eb();
	uint8_t rb = load_rb();
	store_eb(rb);
	store_rb(eb);
}

void CPUExecutor::XCHG_ew_rw()
{
	uint16_t ew = load_ew();
	uint16_t rw = load_rw();
	store_ew(rw);
	store_rw(ew);
}

void CPUExecutor::XCHG_ed_rd()
{
	uint32_t ed = load_ed();
	uint32_t rd = load_rd();
	store_ed(rd);
	store_rd(ed);
}

void CPUExecutor::XCHG_AX_rw()
{
	uint16_t ax = REG_AX;
	REG_AX = GEN_REG(m_instr->reg).word[0];
	GEN_REG(m_instr->reg).word[0] = ax;
}

void CPUExecutor::XCHG_EAX_rd()
{
	uint32_t eax = REG_EAX;
	REG_EAX = GEN_REG(m_instr->reg).dword[0];
	GEN_REG(m_instr->reg).dword[0] = eax;
}


/*******************************************************************************
 * XLATB-Table Look-up Translation
 */

void CPUExecutor::XLATB_a16()
{
	REG_AL = read_byte(SEG_REG(m_base_ds), uint16_t(REG_BX + REG_AL));
}

void CPUExecutor::XLATB_a32()
{
	REG_AL = read_byte(SEG_REG(m_base_ds), (REG_EBX + REG_AL));
}


/*******************************************************************************
 * XOR-Logical Exclusive OR
 */

uint8_t CPUExecutor::XOR_b(uint8_t _op1, uint8_t _op2)
{
	uint8_t res = _op1 ^ _op2;

	SET_FLAG(CF, false);
	SET_FLAG(OF, false);
	SET_FLAG(SF, res & 0x80);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(AF, false); // unknown

	return res;
}

uint16_t CPUExecutor::XOR_w(uint16_t _op1, uint16_t _op2)
{
	uint16_t res = _op1 ^ _op2;

	SET_FLAG(CF, false);
	SET_FLAG(OF, false);
	SET_FLAG(SF, res & 0x8000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(AF, false); // unknown

	return res;
}

uint32_t CPUExecutor::XOR_d(uint32_t _op1, uint32_t _op2)
{
	uint32_t res = _op1 ^ _op2;

	SET_FLAG(CF, false);
	SET_FLAG(OF, false);
	SET_FLAG(SF, res & 0x80000000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(AF, false); // unknown

	return res;
}

void CPUExecutor::XOR_rb_eb() { store_rb(XOR_b(load_rb(), load_eb())); }
void CPUExecutor::XOR_rw_ew() { store_rw(XOR_w(load_rw(), load_ew())); }
void CPUExecutor::XOR_rd_ed() { store_rd(XOR_d(load_rd(), load_ed())); }
void CPUExecutor::XOR_eb_rb() { store_eb(XOR_b(load_eb(), load_rb())); }
void CPUExecutor::XOR_ew_rw() { store_ew(XOR_w(load_ew(), load_rw())); }
void CPUExecutor::XOR_ed_rd() { store_ed(XOR_d(load_ed(), load_rd())); }
void CPUExecutor::XOR_AL_ib() { REG_AL = XOR_b(REG_AL, m_instr->ib); }
void CPUExecutor::XOR_AX_iw() { REG_AX = XOR_w(REG_AX, m_instr->iw1); }
void CPUExecutor::XOR_EAX_id(){REG_EAX = XOR_d(REG_EAX, m_instr->id1); }
void CPUExecutor::XOR_eb_ib() { store_eb(XOR_b(load_eb(), m_instr->ib)); }
void CPUExecutor::XOR_ew_iw() { store_ew(XOR_w(load_ew(), m_instr->iw1)); }
void CPUExecutor::XOR_ed_id() { store_ed(XOR_d(load_ed(), m_instr->id1)); }
void CPUExecutor::XOR_ew_ib() { store_ew(XOR_w(load_ew(), int8_t(m_instr->ib))); }
void CPUExecutor::XOR_ed_ib() { store_ed(XOR_d(load_ed(), int8_t(m_instr->ib))); }

