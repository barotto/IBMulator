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

#ifndef IBMULATOR_CPU_MMU_H
#define IBMULATOR_CPU_MMU_H

class Memory;
class CPUMMU;
extern CPUMMU g_cpummu;


#define TLB_SIZE 1024 // number of entries in the TLB

#define LPF_MASK           0xFFFFF000
#define LPF_OF(laddr)      ((laddr) & LPF_MASK)
#define PAGE_OFFSET(laddr) (uint32_t(laddr) & 0xFFF)


class CPUMMU
{
private:
	typedef struct {
		uint32_t lpf;   // linear page frame
		uint32_t ppf;   // physical page frame
		unsigned protection;
	} TLBEntry;

	TLBEntry m_TLB[TLB_SIZE];

public:
	uint32_t TLB_lookup(uint32_t _linear, unsigned _len, bool _user, bool _write);
	void TLB_check(uint32_t _linear, bool _user, bool _write);
	void TLB_flush();
	static uint32_t dbg_translate_linear(uint32_t _linear_addr, uint32_t _pdbr, Memory *_memory);

private:
	inline unsigned TLB_index(uint32_t _lpf, unsigned _len) const {
		return (((_lpf + _len) & ((TLB_SIZE-1) << 12)) >> 12);
	}

	void TLB_miss(uint32_t _linear, TLBEntry *_tlbent, bool _user, bool _write);
	void page_check(unsigned _protection, uint32_t _linear, bool _user, bool _write);
	void page_fault(unsigned _fault, uint32_t _linear, bool _user, bool _write);
};

#endif
