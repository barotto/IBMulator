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
#include "core.h"
#include "mmu.h"
#include "hardware/cpu.h"
#include "hardware/memory.h"
#include <cstring>

CPUCore g_cpucore;


void CPUCore::reset()
{
	/* The RESET signal initializes the CPU in Real-Address mode. The first
	 * instruction fetch cycle following reset will be from the physical address
	 * formed by CS.base and EIP. This location will normally contain a JMP
	 * instruction to the actual beginning of the system bootstrap program.
	 *
	 * CPU type  CS sel   CS base   CS limit    EIP
	 *     8086    FFFF     FFFF0       FFFF   0000
	 *     286     F000    FF0000       FFFF   FFF0
	 *     386+    F000  FFFF0000       FFFF   FFF0
	 */

	memset(m_genregs, 0, sizeof(GenReg)*8);
	memset(m_segregs, 0, sizeof(SegReg)*10);

	m_eflags = 0x00000002;
	m_cr[0] = 0x0;
	m_cr[2] = 0x0;
	m_cr[3] = 0x0;
	m_eip = 0x0000FFF0;

	load_segment_defaults(REG_CS, 0xF000);
	load_segment_defaults(REG_DS, 0x0000);
	load_segment_defaults(REG_SS, 0x0000);
	load_segment_defaults(REG_ES, 0x0000);
	load_segment_defaults(REG_FS, 0x0000);
	load_segment_defaults(REG_GS, 0x0000);

	load_segment_defaults(REG_LDTR, 0x0000);
	load_segment_defaults(REG_TR, 0x0000);

	set_IDTR(0x000000, 0x03FF);
	set_GDTR(0x000000, 0x0000);

	for(int r=0; r<4; r++) {
		m_dr[r] = 0;
	}
	m_dr[6] = 0xFFFF1FF0;
	m_dr[7] = 0x00000400;
	for(int r=0; r<8; r++) {
		m_tr[r] = 0;
	}

	switch(CPU_FAMILY) {
		case CPU_286:
			m_cr[0] |= CR0MASK_RES286;
			REG_CS.desc.base = 0xFF0000;
			break;
		case CPU_386:
			m_genregs[REGI_EDX].dword[0] = CPU_SIGNATURE;
			m_cr[0] |= (CR0MASK_RES386 | CR0MASK_ET);
			REG_CS.desc.base = 0xFFFF0000;
			break;
		default:
			PERRF_ABORT(LOG_CPU, "unsupported CPU family\n");
	}

	handle_mode_change();
}

#define CPUCORE_STATE_NAME "CPUCore"

void CPUCore::save_state(StateBuf &_state) const
{
	static_assert(std::is_pod<CPUCore>::value, "CPUCore must be POD");
	StateHeader h;
	h.name = CPUCORE_STATE_NAME;
	h.data_size = sizeof(CPUCore);
	_state.write(this,h);
}

void CPUCore::restore_state(StateBuf &_state)
{
	StateHeader h;
	h.name = CPUCORE_STATE_NAME;
	h.data_size = sizeof(CPUCore);
	_state.read(this,h);
}

void CPUCore::handle_mode_change()
{
	if(m_cr[0] & CR0MASK_PE) {
		if(m_eflags & FMASK_VM) {
			CPL = 3;
			PDEBUGF(LOG_V2, LOG_CPU, "now in V8086 mode\n");
		} else {
			PDEBUGF(LOG_V2, LOG_CPU, "now in Protected mode\n");
		}
	} else {
		// CS segment in real mode always allows full access
		m_segregs[REGI_CS].desc.set_AR(
			SEG_SEGMENT |
			SEG_PRESENT |
			SEG_READWRITE |
			SEG_ACCESSED);
		CPL = 0;
		PDEBUGF(LOG_V2, LOG_CPU, "now in Real mode\n");
	}
}

void CPUCore::load_segment_real(SegReg & _segreg, uint16_t _value)
{
	/* According to Intel, each time any segment register is loaded in real mode,
	 * the base address is calculated as 16 times the segment value, while the
	 * access rights and size limit attributes are given fixed, "real-mode
	 * compatible" values. This is not true. In fact, only the CS descriptor caches
	 * for the 286, 386, and 486 get loaded with fixed values each time the segment
	 * register is loaded.
	 * (http://www.rcollins.org/ddj/Aug98/Aug98.html)
	 */
	// in real mode CS register is loaded with load_segment_defaults()
	assert(!_segreg.is(REG_CS));

	_segreg.sel.value = _value;
	_segreg.desc.base = uint32_t(_value) << 4;

	if(is_rmode()) {
		// in real mode the current privilege level is always 0
		_segreg.sel.cpl = 0;
		_segreg.desc.present = true;
		_segreg.desc.segment = true;
		_segreg.desc.valid = true;
	} else {
		// v8086 mode
		_segreg.sel.cpl = 3;
		_segreg.desc.set_AR(SEG_SEGMENT|SEG_PRESENT|SEG_READWRITE|SEG_ACCESSED);
		_segreg.desc.dpl = 3;
		_segreg.desc.limit = 0xFFFF;
		_segreg.desc.page_granular = false;
		_segreg.desc.big = false;
	}
}

void CPUCore::load_segment_defaults(SegReg & _segreg, uint16_t _value)
{
	_segreg.sel.value = _value;
	if(is_rmode()) {
		_segreg.sel.cpl = 0;
		_segreg.desc.dpl = 0;
	} else {
		// V8086
		assert(_segreg.is(REG_CS));
		_segreg.sel.cpl = 3;
		_segreg.desc.dpl = 3;
	}
	_segreg.desc.base = uint32_t(_value) << 4;
	_segreg.desc.limit = 0xFFFF;
	_segreg.desc.page_granular = false;
	_segreg.desc.big = false;
	if(_segreg.is(REG_LDTR)) {
		_segreg.desc.set_AR(
			SEG_PRESENT |
			DESC_TYPE_LDT_DESC
		);
	} else if(_segreg.is(REG_TR)) {
		_segreg.desc.set_AR(
			SEG_PRESENT |
			DESC_TYPE_BUSY_386_TSS
		);
	} else {
		_segreg.desc.set_AR(
			SEG_SEGMENT |
			SEG_PRESENT |
			SEG_ACCESSED |
			SEG_READWRITE
		);
	}
}

void CPUCore::load_segment_protected(SegReg & _segreg, uint16_t _value)
{
	/*chapters 6,7*/

	Selector selector;
	Descriptor descriptor;

	if(_segreg.is(REG_SS)) {

		if ((_value & SELECTOR_RPL_MASK) == 0) {
			PDEBUGF(LOG_V2, LOG_CPU, "load_segment_protected(SS): null selector\n");
			throw CPUException(CPU_GP_EXC, _value & SELECTOR_RPL_MASK);
		}

		selector = _value;

		/* selector's RPL must be equal to CPL, else #GP(selector) */
		if(selector.rpl != CPL) {
			PDEBUGF(LOG_V2, LOG_CPU, "load_segment_protected(SS): rpl != CPL\n");
			throw CPUException(CPU_GP_EXC, _value & SELECTOR_RPL_MASK);
		}

		descriptor = g_cpuexecutor.fetch_descriptor(selector, CPU_GP_EXC);

		if(!descriptor.valid) {
			PDEBUGF(LOG_V2, LOG_CPU,"load_segment_protected(SS): not valid\n");
			throw CPUException(CPU_GP_EXC, _value & SELECTOR_RPL_MASK);
		}
		/* AR byte must indicate a writable data segment else #GP(selector) */
		if(!descriptor.is_data_segment() || !descriptor.is_writeable()) {
			PDEBUGF(LOG_V2, LOG_CPU, "load_segment_protected(SS): not writable data segment\n");
			throw CPUException(CPU_GP_EXC, _value & SELECTOR_RPL_MASK);
		}
		/* DPL in the AR byte must equal CPL else #GP(selector) */
		if(descriptor.dpl != CPL) {
			PDEBUGF(LOG_V2, LOG_CPU,"load_segment_protected(SS): dpl != CPL\n");
			throw CPUException(CPU_GP_EXC, _value & SELECTOR_RPL_MASK);
		}
		/* segment must be marked PRESENT else #SS(selector) */
		if(!descriptor.present) {
			PDEBUGF(LOG_V2, LOG_CPU,"load_segment_protected(SS): not present\n");
			throw CPUException(CPU_SS_EXC, _value & SELECTOR_RPL_MASK);
		}

		/* set accessed bit */
		g_cpuexecutor.touch_segment(selector, descriptor);

		/* all done and well, load the register */
		REG_SS.desc = descriptor;
		REG_SS.sel = selector;

	} else if(_segreg.is(REG_DS) || _segreg.is(REG_ES)
	        || _segreg.is(REG_FS)|| _segreg.is(REG_GS)) {

		if((_value & SELECTOR_RPL_MASK) == 0) {
			/* null selector */
			_segreg.sel  = _value;
			_segreg.desc = 0;
			_segreg.desc.set_AR(SEG_SEGMENT); /* data/code segment */
			_segreg.desc.valid = false; /* invalidate null selector */
			return;
		}

		selector   = _value;
		descriptor = g_cpuexecutor.fetch_descriptor(selector, CPU_GP_EXC);

		if(descriptor.valid==0) {
			PDEBUGF(LOG_V2, LOG_CPU,"load_segment_protected(%s, 0x%04x): invalid segment\n",
					_segreg.to_string(), _value);
			throw CPUException(CPU_GP_EXC, _value & SELECTOR_RPL_MASK);
		}

		/* AR byte must indicate a writable data segment else #GP(selector) */
		if(  descriptor.is_system_segment() ||
			(descriptor.is_code_segment() && !(descriptor.is_readable())
		  )
		) {
			PDEBUGF(LOG_V2, LOG_CPU, "load_segment_protected(%s, 0x%04x): not data or readable code (AR=0x%02X)\n",
					_segreg.to_string(), _value, descriptor.get_AR());
			throw CPUException(CPU_GP_EXC, _value & SELECTOR_RPL_MASK);
		}

		/* If data or non-conforming code, then both the RPL and the CPL
		 * must be less than or equal to DPL in AR byte else #GP(selector) */
		if(descriptor.is_data_segment() || !descriptor.is_conforming()) {
			if((selector.rpl > descriptor.dpl) || (CPL > descriptor.dpl)) {
				PDEBUGF(LOG_V2, LOG_CPU, "load_segment_protected(%s, 0x%04x): RPL & CPL must be <= DPL\n",
						_segreg.to_string(), _value);
				throw CPUException(CPU_GP_EXC, _value & SELECTOR_RPL_MASK);
			}
		}

		/* segment must be marked PRESENT else #NP(selector) */
		if(!descriptor.present) {
			PDEBUGF(LOG_V2, LOG_CPU,"load_segment_protected(%s, 0x%04x): segment not present\n",
					_segreg.to_string(), _value);
			throw CPUException(CPU_NP_EXC, _value & SELECTOR_RPL_MASK);
		}

		/* set accessed bit */
		g_cpuexecutor.touch_segment(selector, descriptor);

		/* all done and well, load the register */
		_segreg.desc = descriptor;
		_segreg.sel = selector;

	} else {

		PERRF_ABORT(LOG_CPU, "load_segment_protected(): invalid register!\n");

	}
}

void CPUCore::check_CS(uint16_t selector, Descriptor &descriptor, uint8_t rpl, uint8_t cpl)
{
	// descriptor AR byte must indicate code segment else #GP(selector)
	if(!descriptor.valid || !descriptor.is_code_segment()) {
		PDEBUGF(LOG_V2, LOG_CPU,"check_CS(0x%04x): not a valid code segment\n", selector);
		throw CPUException(CPU_GP_EXC, selector & SELECTOR_RPL_MASK);
	}

	// if non-conforming, code segment descriptor DPL must = CPL else #GP(selector)
	if(!descriptor.is_conforming()) {
		if(descriptor.dpl != cpl) {
			PDEBUGF(LOG_V2, LOG_CPU,"check_CS(0x%04x): non-conforming code seg descriptor dpl != cpl, dpl=%d, cpl=%d\n",
					selector, descriptor.dpl, cpl);
			throw CPUException(CPU_GP_EXC, selector & SELECTOR_RPL_MASK);
		}

		/* RPL of destination selector must be <= CPL else #GP(selector) */
		if(rpl > cpl) {
			PDEBUGF(LOG_V2, LOG_CPU,"check_CS(0x%04x): non-conforming code seg selector rpl > cpl, rpl=%d, cpl=%d\n",
					selector, rpl, cpl);
			throw CPUException(CPU_GP_EXC, selector & SELECTOR_RPL_MASK);
		}
	}
	// if conforming, then code segment descriptor DPL must <= CPL else #GP(selector)
	else {
		if(descriptor.dpl > cpl) {
			PDEBUGF(LOG_V2, LOG_CPU,"check_CS(0x%04x): conforming code seg descriptor dpl > cpl, dpl=%d, cpl=%d\n",
					selector, descriptor.dpl, cpl);
			throw CPUException(CPU_GP_EXC, selector & SELECTOR_RPL_MASK);
		}
	}

	// code segment must be present else #NP(selector)
	if(!descriptor.present) {
		PDEBUGF(LOG_V2, LOG_CPU,"check_CS(0x%04x): code segment not present\n", selector);
		throw CPUException(CPU_NP_EXC, selector & SELECTOR_RPL_MASK);
	}
}

void CPUCore::set_CS(Selector &selector, Descriptor &descriptor, uint8_t cpl)
{
	// Add cpl to the selector value.
	selector.value = (selector.value & SELECTOR_RPL_MASK) | cpl;

	g_cpuexecutor.touch_segment(selector, descriptor);

	REG_CS.sel = selector;
	REG_CS.desc = descriptor;
	REG_CS.sel.cpl = cpl;

	//the pq must be invalidated by the caller!
}

void CPUCore::set_SS(Selector &selector, Descriptor &descriptor, uint8_t cpl)
{
	// Add cpl to the selector value.
	selector.value = (selector.value & SELECTOR_RPL_MASK) | cpl;

	if((selector.value & SELECTOR_RPL_MASK) != 0) {
		g_cpuexecutor.touch_segment(selector, descriptor);
	}

	REG_SS.sel = selector;
	REG_SS.desc = descriptor;
	REG_SS.sel.cpl = cpl;
}

void CPUCore::set_FLAGS(uint16_t _val)
{
	uint16_t f16 = uint16_t(m_eflags);
	// bit1 is fixed 1
	m_eflags = (_val & FMASK_VALID) | (m_eflags & 0x30000) | 2;
	if(m_eflags & FMASK_TF) {
		g_cpu.set_async_event();
	}
	if((f16 ^ _val) & FMASK_IF) {
		g_cpu.interrupt_mask_change();
	}
}

void CPUCore::set_EFLAGS(uint32_t _val)
{
	uint32_t f32 = m_eflags;
	// bit1 is fixed 1
	m_eflags = (_val & FMASK_VALID) | 2;
	if(m_eflags & FMASK_TF) {
		g_cpu.set_async_event();
	}
	if((f32 ^ _val) & FMASK_IF) {
		g_cpu.interrupt_mask_change();
	}
	if((m_cr[0]&CR0MASK_PE) && ((f32 ^ _val)&FMASK_VM)) {
		handle_mode_change();
	}
	if(!(f32 & FMASK_RF) && (_val & FMASK_RF)) {
		// TODO why?
		g_cpubus.invalidate_pq();
	}
}

void CPUCore::set_TF(bool _val)
{
	if(_val) {
		m_eflags |= FMASK_TF;
		g_cpu.set_async_event();
	} else {
		m_eflags &= ~FMASK_TF;
	}
}

void CPUCore::set_IF(bool _val)
{
	set_flag(FBITN_IF, _val);
	g_cpu.interrupt_mask_change();
}

void CPUCore::set_VM(bool _val)
{
	if(CPU_FAMILY>=CPU_386 && bool(m_eflags&FMASK_VM)!=_val) {
		set_flag(FBITN_VM, _val);
		handle_mode_change();
	}
}

void CPUCore::set_RF(bool _val)
{
	set_flag(FBITN_RF, _val);
	if(_val) {
		// TODO why?
		g_cpubus.invalidate_pq();
	}
}

void CPUCore::set_CR0(uint32_t _cr0)
{
	// set reserved bits
	switch(CPU_FAMILY) {
		case CPU_286:
			_cr0 |= CR0MASK_RES286;
			break;
		case CPU_386:
			_cr0 |= CR0MASK_RES386;
			/* CR0.ET may be toggled on the 80386 DX, while it is hardwired to 1 on
			 * the 80386 SX (same difference between the 80486 DX and SX)
			 */
			if((CPU_SIGNATURE&CPU_SIG_386SX) == CPU_SIG_386SX) {
				_cr0 |= CR0MASK_ET;
			}
			break;
		default:
			PERRF_ABORT(LOG_CPU, "unsupported CPU family\n");
	}

	if((_cr0&CR0MASK_PG) && !(_cr0&CR0MASK_PE)) {
		PDEBUGF(LOG_V2, LOG_CPU, "attempt to set CR0.PG with CR0.PE cleared\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	uint32_t oldcr0 = m_cr[0];
	m_cr[0] = _cr0;
	bool PE_changed = (oldcr0 ^ _cr0) & CR0MASK_PE;
	bool PG_changed = (oldcr0 ^ _cr0) & CR0MASK_PG;
	if(PE_changed) {
		handle_mode_change();
	}
	if(PG_changed) {
		if(_cr0&CR0MASK_PG) {
			PDEBUGF(LOG_V2, LOG_CPU, "Paging enabled, CR3=%08X\n", REG_CR3);
			g_cpubus.enable_paging(true);
		} else {
			PDEBUGF(LOG_V2, LOG_CPU, "Paging disabled\n");
			g_cpubus.enable_paging(false);
		}
	}
	if(CPU_FAMILY >= CPU_386 && (PE_changed || PG_changed)) {
		// Modification of PG,PE flushes TLB cache according to docs.
		g_cpummu.TLB_flush();
	}
}

void CPUCore::set_CR3(uint32_t _cr3)
{
	m_cr[3] = _cr3;
	g_cpummu.TLB_flush();
}

uint32_t CPUCore::match_x86_code_breakpoint(uint32_t _linear_addr)
{
	/* Instruction breakpoint addresses must have a length specification of 1
	 * byte (the LENn field is set to 00). Code breakpoints for other operand
	 * sizes are undefined. The processor recognizes an instruction breakpoint
	 * address only when it points to the first byte of an instruction. If the
	 * instruction has prefixes, the breakpoint address must point to the first
	 * prefix.
	 */
	uint32_t debug_trap = 0;
	for(int n=0; n<4; n++) {
		if((dr7_rw(n) == 0x0) && _linear_addr == m_dr[n]) {
			// If any enabled breakpoints matched, then we need to set status
			// bits for all breakpoints
			debug_trap |= (1 << n);
			if(dr7_enabled(n)) {
				debug_trap |= CPU_DEBUG_TRAP_HIT;
			}
		}
	}
	return debug_trap;
}

void CPUCore::match_x86_data_breakpoint(uint32_t _laddr0, unsigned _size, unsigned _rw)
{
	/* DR0-DR3 and the LENn fields for each breakpoint define a range of
	 * sequential byte addresses for a data breakpoint (or I/O for 586+). The
	 * LENn fields permit specification of a 1-, 2-, or 4-byte range (also
	 * 8-byte for P4+), beginning at the linear address specified in the
	 * corresponding debug register (DRn). Two-byte ranges must be aligned on
	 * word boundaries; 4-byte ranges must be aligned on doubleword boundaries.
	 * I/O addresses are zero-extended. These requirements are enforced by the
	 * processor; it uses LENn field bits to mask the lower address bits in the
	 * debug registers. Unaligned data or I/O breakpoint addresses do not yield
	 * valid results.
	 */
	static uint32_t mask[4] = {
		0x0, // 00b = 1-byte length.
		0x1, // 01b = 2-byte length.
		0x7, // 10b = Undefined (or 8 byte length for P4+).
		0x3  // 11b = 4-byte length.
	};

	uint32_t laddr1 = _laddr0 + (_size - 1);
	uint32_t debug_trap = 0;

	for(unsigned n=0; n<4; n++) {
		if(dr7_rw(n) != _rw) {
			continue;
		}
		/* A breakpoint is triggered if any of the bytes participating in an
		 * access is within the range defined by a breakpoint address register
		 * and its LENn field.
		 */
		uint32_t start = m_dr[n] & ~mask[dr7_len(n)];
		uint32_t end = start + mask[dr7_len(n)];
		if((_laddr0 <= end) && (start <= laddr1)) {
			// If any enabled breakpoints matched, then we need to set status
			// bits for all breakpoints
			debug_trap |= (1 << n);
			if(dr7_enabled(n)) {
				debug_trap |= CPU_DEBUG_TRAP_HIT;
			}
		}
	}
	if(debug_trap & CPU_DEBUG_TRAP_HIT) {
		g_cpu.set_debug_trap(debug_trap | CPU_DEBUG_TRAP_DATA);
		// The processor generates the exception after it executes the
		// instruction that made the access, so these breakpoint condition
		// causes a trap-class exception to be generated.
		g_cpu.set_async_event();
	}
}

uint32_t CPUCore::dbg_get_phyaddr(uint32_t _linaddr, Memory *_memory) const
{
	if(_memory == nullptr) {
		_memory = &g_memory;
	}
	if(is_paging()) {
		return CPUMMU::dbg_translate_linear(_linaddr, m_cr[3]&0xFFFFF000, _memory);
	} else {
		return _linaddr;
	}
}

uint32_t CPUCore::dbg_get_phyaddr(unsigned _segidx, uint32_t _offset, Memory *_memory) const
{
	if(_memory == nullptr) {
		_memory = &g_memory;
	}
	if(is_paging()) {
		return CPUMMU::dbg_translate_linear(get_linaddr(_segidx, _offset), m_cr[3]&0xFFFFF000, _memory);
	} else {
		return get_linaddr(_segidx, _offset);
	}
}

const char *SegReg::to_string() const
{
	if (is(REG_ES)) return("ES");
	else if (is(REG_CS)) return("CS");
	else if (is(REG_SS)) return("SS");
	else if (is(REG_DS)) return("DS");
	else if (is(REG_FS)) return("FS");
	else if (is(REG_GS)) return("GS");
	else { return "??"; }
}

void SegReg::validate()
{
	if(desc.dpl < CPL) {
		// invalidate if data or non-conforming code segment
		if(desc.valid==0 || desc.segment==false ||
        desc.is_data_segment() || !desc.is_conforming())
		{
			sel.value = 0;
			desc.valid = false;
		}
	}
}
