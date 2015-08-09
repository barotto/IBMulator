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
#include "machine.h"
#include "cpu.h"
#include "core.h"
#include "executor.h"
#include "bus.h"
#include "hardware/devices.h"
#include "debugger.h"
#include <cstring>

CPUExecutor g_cpuexecutor;

/* the parity flag (PF) indicates whether the modulo 2 sum of the low-order
 * eight bits of the operation is even (PF=O) or odd (PF= 1) parity.
 */
#define PARITY(x) (!(popcnt(x & 0xFF) & 1))

#ifdef __MSC_VER
#  include <intrin.h>
#  define __builtin_popcount __popcnt
#endif

GCC_ATTRIBUTE(always_inline)
inline uint popcnt(uint _value)
{
#if 0
	uint count;
    __asm__ ("popcnt %1,%0" : "=r"(count) : "rm"(_value) : "cc");
    return count;
#else
    return __builtin_popcount(_value);
#endif
}


CPUExecutor::CPUExecutor()
:
m_dos_prg_int_exit(0)
{
	//register_INT_trap(0x00, 0xFF, &CPUExecutor::INT_debug);
	register_INT_trap(0x13, 0x13, &CPUExecutor::INT_debug);
	register_INT_trap(0x21, 0x21, &CPUExecutor::INT_debug);
}

void CPUExecutor::reset(uint _signal)
{
	m_instr = nullptr;
	m_base_ds = REGI_DS;
	m_base_ss = REGI_SS;

	if(_signal == MACHINE_HARD_RESET || _signal == MACHINE_POWER_ON) {
		m_inttraps_ret.clear();
		while(!m_dos_prg.empty()) {
			m_dos_prg.pop();
		}
	}
}

inline SegReg & CPUExecutor::EA_get_segreg()
{
	switch(m_instr->modrm.rm) {
		case 0:
		case 1:
		case 4:
		case 5:
		case 7:
			return SEG_REG(m_base_ds);
		case 2:
		case 3:
			return SEG_REG(m_base_ss);
		case 6:
			if(m_instr->modrm.mod==0)
				return SEG_REG(m_base_ds);
			return SEG_REG(m_base_ss);
	}

	ASSERT(false);

	//keep compiler happy, but you really don't want to end here!
	return REG_DS;
}

uint16_t CPUExecutor::EA_get_offset()
{
	uint16_t disp = m_instr->modrm.disp;
	switch(m_instr->modrm.rm) {
		case 0:
			return (REG_BX + REG_SI + disp);
		case 1:
			return (REG_BX + REG_DI + disp);
		case 2:
			return (REG_BP + REG_SI + disp);
		case 3:
			return (REG_BP + REG_DI + disp);
		case 4:
			return (REG_SI + disp);
		case 5:
			return (REG_DI + disp);
		case 6:
			if(m_instr->modrm.mod==0)
				return disp;
			return (REG_BP + disp);
		case 7:
			return (REG_BX + disp);
	}
	return 0;
}

uint8_t CPUExecutor::load_eb()
{
	if(m_instr->modrm.mod == 3) {
		if(m_instr->modrm.rm < 4) {
			return g_cpucore.gen_reg(m_instr->modrm.rm).byte[LO_INDEX];
		}
		return g_cpucore.gen_reg(m_instr->modrm.rm-4).byte[HI_INDEX];
	}
	return read_byte(EA_get_segreg(), EA_get_offset());
}

uint8_t CPUExecutor::load_rb()
{
	if(m_instr->modrm.r < 4) {
		return g_cpucore.gen_reg(m_instr->modrm.r).byte[LO_INDEX];
	}
	return g_cpucore.gen_reg(m_instr->modrm.r-4).byte[HI_INDEX];
}

uint16_t CPUExecutor::load_ew()
{
	if(m_instr->modrm.mod == 3) {
		return g_cpucore.gen_reg(m_instr->modrm.rm).word[0];
	}
	return read_word(EA_get_segreg(), EA_get_offset());
}

uint16_t CPUExecutor::load_rw()
{
	return g_cpucore.gen_reg(m_instr->modrm.r).word[0];
}

void CPUExecutor::load_ed(uint16_t &w1_, uint16_t &w2_)
{
	SegReg & sr = EA_get_segreg();
	uint16_t off = EA_get_offset();

	w1_ = read_word(sr, off);
	w2_ = read_word(sr, off+2);
}

void CPUExecutor::store_eb(uint8_t _value)
{
	if(m_instr->modrm.mod == 3) {
		if(m_instr->modrm.rm < 4) {
			g_cpucore.gen_reg(m_instr->modrm.rm).byte[LO_INDEX] = _value;
			return;
		}
		g_cpucore.gen_reg(m_instr->modrm.rm-4).byte[HI_INDEX] = _value;
		return;
	}
	write_byte(EA_get_segreg(), EA_get_offset(), _value);
}

void CPUExecutor::store_rb(uint8_t _value)
{
	if(m_instr->modrm.r < 4) {
		g_cpucore.gen_reg(m_instr->modrm.r).byte[LO_INDEX] = _value;
	} else {
		g_cpucore.gen_reg(m_instr->modrm.r-4).byte[HI_INDEX] = _value;
	}
}

void CPUExecutor::store_rb_op(uint8_t _value)
{
	if(m_instr->reg < 4) {
		g_cpucore.gen_reg(m_instr->reg).byte[LO_INDEX] = _value;
	} else {
		g_cpucore.gen_reg(m_instr->reg-4).byte[HI_INDEX] = _value;
	}
}

void CPUExecutor::store_ew(uint16_t _value)
{
	if(m_instr->modrm.mod == 3) {
		g_cpucore.gen_reg(m_instr->modrm.rm).word[0] = _value;
		return;
	}
	write_word(EA_get_segreg(), EA_get_offset(), _value);
}

void CPUExecutor::store_rw(uint16_t _value)
{
	g_cpucore.gen_reg(m_instr->modrm.r).word[0] = _value;
}

void CPUExecutor::store_rw_op(uint16_t _value)
{
	g_cpucore.gen_reg(m_instr->reg).word[0] = _value;
}

void CPUExecutor::write_flags(uint16_t _flags,
		bool _change_IOPL, bool _change_IF, bool _change_NT)
{
	// Build a mask of the following bits:
	// x,NT,IOPL,OF,DF,IF,TF,SF,ZF,x,AF,x,PF,x,CF
	uint16_t changeMask = 0x0dd5;

#if(1)
	/* Bochs enables this code only if cpu level >=3 (i386) but I suspect it's a
	* bug. no one checks the 286 emulation.
	*/
	if(_change_NT)
	  changeMask |= FMASK_NT;     // NT is modified as requested.
	if(_change_IOPL)
		changeMask |= FMASK_IOPL; // IOPL is modified as requested.
#endif

	if(_change_IF)
		changeMask |= FMASK_IF;

	// Screen out changing of any unsupported bits.
	changeMask &= FMASK_VALID;

	uint16_t new_flags = (GET_FLAGS() & ~changeMask) | (_flags & changeMask);
	SET_FLAGS(new_flags);
}

void CPUExecutor::read_check_pmode(SegReg & _seg, uint16_t _offset, uint _len)
{
	ASSERT(_len!=0);
	uint8_t vector = _seg.is(REG_SS)?CPU_SS_EXC:CPU_GP_EXC;
	if(!_seg.desc.valid) {
		PDEBUGF(LOG_V2, LOG_CPU, "read_check_pmode(): segment not valid\n");
		throw CPUException(vector, 0);
	}
	if(uint32_t(_offset)+_len-1 > _seg.desc.limit) {
		PDEBUGF(LOG_V2, LOG_CPU, "read_check_pmode(): segment limit violation\n");
		throw CPUException(vector, 0);
	}
}

void CPUExecutor::write_check_pmode(SegReg & _seg, uint16_t _offset, uint _len)
{
	ASSERT(_len!=0);
	uint8_t vector = _seg.is(REG_SS)?CPU_SS_EXC:CPU_GP_EXC;
	if(!_seg.desc.valid) {
		PDEBUGF(LOG_V2, LOG_CPU, "write_check_pmode(): segment not valid\n");
		throw CPUException(vector, 0);
	}
	if(uint32_t(_offset)+_len-1 > _seg.desc.limit) {
		PDEBUGF(LOG_V2, LOG_CPU, "write_check_pmode(): segment limit violation\n");
		throw CPUException(vector, 0);
	}
	if(!_seg.desc.is_data_segment_writeable()) {
		PDEBUGF(LOG_V2, LOG_CPU, "write_check_pmode(): segment not writeable\n");
		throw CPUException(CPU_GP_EXC, 0);
	}
}

void CPUExecutor::read_check_rmode(SegReg & /*_seg*/, uint16_t _offset, uint _len)
{
	if(_len>1 && _offset==0xFFFF) {
		throw CPUException(CPU_SEG_OVR_EXC, 0);
	}
}

void CPUExecutor::write_check_rmode(SegReg & /*_seg*/, uint16_t _offset, uint _len)
{
	if(_len>1 && _offset==0xFFFF) {
		throw CPUException(CPU_SEG_OVR_EXC, 0);
	}
}

uint8_t CPUExecutor::read_byte(SegReg &_seg, uint16_t _offset)
{
	if(IS_PMODE()) {
		read_check_pmode(_seg, _offset, 1);
	} else {
		read_check_rmode(_seg, _offset, 1);
	}
	return g_cpubus.mem_read_byte(_seg.desc.base + uint32_t(_offset));
}

uint16_t CPUExecutor::read_word(SegReg &_seg, uint16_t _offset)
{
	if(IS_PMODE()) {
		read_check_pmode(_seg, _offset, 2);
	} else {
		read_check_rmode(_seg, _offset, 2);
	}
	return g_cpubus.mem_read_word(_seg.desc.base + uint32_t(_offset));
}

uint32_t CPUExecutor::read_dword(SegReg &_seg, uint16_t _offset)
{
	if(IS_PMODE()) {
		read_check_pmode(_seg, _offset, 4);
	} else {
		read_check_rmode(_seg, _offset, 4);
	}
	return g_cpubus.mem_read_dword(_seg.desc.base + uint32_t(_offset));
}

void CPUExecutor::write_byte(SegReg &_seg, uint16_t _offset, uint8_t _data)
{
	if(IS_PMODE()) {
		write_check_pmode(_seg, _offset, 1);
	} else {
		write_check_rmode(_seg, _offset, 1);
	}
	g_cpubus.mem_write_byte(_seg.desc.base + uint32_t(_offset), _data);
}

void CPUExecutor::write_word(SegReg &_seg, uint16_t _offset, uint16_t _data)
{
	if(IS_PMODE()) {
		write_check_pmode(_seg, _offset, 2);
	} else {
		write_check_rmode(_seg, _offset, 2);
	}
	g_cpubus.mem_write_word(_seg.desc.base + uint32_t(_offset), _data);
}

uint8_t CPUExecutor::read_byte_nocheck(SegReg &_seg, uint16_t _offset)
{
	return g_cpubus.mem_read_byte(_seg.desc.base + uint32_t(_offset));
}

uint16_t CPUExecutor::read_word_nocheck(SegReg &_seg, uint16_t _offset)
{
	return g_cpubus.mem_read_word(_seg.desc.base + uint32_t(_offset));
}

void CPUExecutor::write_byte_nocheck(SegReg &_seg, uint16_t _offset, uint8_t _data)
{
	g_cpubus.mem_write_byte(_seg.desc.base + uint32_t(_offset), _data);
}

void CPUExecutor::write_word_nocheck(SegReg &_seg, uint16_t _offset, uint16_t _data)
{
	g_cpubus.mem_write_word(_seg.desc.base + uint32_t(_offset), _data);
}

void CPUExecutor::write_word_pmode(SegReg &_seg, uint16_t _offset, uint16_t _data,
		uint8_t _exc, uint16_t _errcode)
{
	//see access32.cc/write_virtual_word_32
	//and access32.cc/write_new_stack_word_32
	//this function doesn't call write_virtual_checks()
	//TODO?
	if(!_seg.desc.valid) {
		PERRF(LOG_CPU, "write_word_pmode(): segment not valid\n");
		throw CPUException(_exc, _errcode);
	}
	if(!_seg.desc.is_data_segment_writeable()) {
		PDEBUGF(LOG_V2, LOG_CPU, "write_word_pmode(): segment not writeable\n");
		throw CPUException(CPU_GP_EXC, 0);
	}
	if(uint32_t(_offset)+1 <= _seg.desc.limit) {
		uint32_t addr = _seg.desc.base + _offset;
		g_cpubus.mem_write_word(addr,_data);
		return;
	} else {
		PERRF(LOG_CPU, "write_word_pmode(): segment limit violation\n");
		throw CPUException(_exc, _errcode);
	}
}

void CPUExecutor::write_word_pmode(SegReg &_seg, uint16_t _offset, uint16_t _data)
{
    uint8_t exc = _seg.is(REG_SS)?CPU_SS_EXC:CPU_GP_EXC;
    uint16_t errcode = _seg.sel.rpl != CPL ? (_seg.sel.value & SELECTOR_RPL_MASK) : 0;
    write_word_pmode(_seg, _offset, _data, exc, errcode);
}

uint16_t CPUExecutor::read_word_pmode(SegReg & _seg, uint16_t _offset, uint8_t _exc,
		uint16_t _errcode)
{
	//see access32.cc/read_virtual_word_32
	//this function doesn't call read_virtual_checks()
	//TODO?
	if(!_seg.desc.valid) {
		PDEBUGF(LOG_V2, LOG_CPU, "read_word_pmode(): segment not valid\n");
		throw CPUException(_exc, _errcode);
	}

    if(uint32_t(_offset)+1 <= _seg.desc.limit) {
    	uint32_t addr = _seg.desc.base + _offset;
    	uint16_t value = g_cpubus.mem_read_word(addr);
    	return value;
    } else {
    	PDEBUGF(LOG_V2, LOG_CPU, "read_word_pmode(): segment limit violation\n");
		throw CPUException(_exc, _errcode);
    }
}

void CPUExecutor::stack_push(uint16_t _value)
{
	if(REG_SP == 1) {
		throw CPUShutdown("insufficient stack space on push");
	}
	REG_SP -= 2;
	write_word(REG_SS, REG_SP, _value);
}

uint16_t CPUExecutor::stack_pop()
{
	uint16_t value = read_word(REG_SS, REG_SP);
	REG_SP += 2;

	return value;
}

void CPUExecutor::stack_push_pmode(uint16_t _value)
{
	if(REG_SP == 1) {
		PDEBUGF(LOG_V2, LOG_CPU, "stack_push_pmode(): insufficient stack space\n");
		throw CPUException(CPU_SS_EXC, 0);
	}
	write_word_pmode(REG_SS, (REG_SP - 2), _value, CPU_SS_EXC, 0);
	REG_SP -= 2;
}

uint16_t CPUExecutor::stack_pop_pmode()
{
	uint16_t value = read_word_pmode(REG_SS, REG_SP, CPU_SS_EXC, 0);
	REG_SP += 2;

	return value;
}

uint16_t CPUExecutor::stack_read(uint16_t _offset)
{
	if(IS_PMODE()) {
		read_check_pmode(REG_SS, _offset, 2);
	}
	return g_cpubus.mem_read_word(GET_PHYADDR(SS, _offset));
}

void CPUExecutor::stack_write(uint16_t _offset, uint16_t _data)
{
	if(IS_PMODE()) {
		write_check_pmode(REG_SS, _offset, 2);
	}
	g_cpubus.mem_write_word(GET_PHYADDR(SS, _offset), _data);
}

void CPUExecutor::execute(Instruction * _instr)
{
	m_instr = _instr;

	uint32_t old_ip = REG_IP;

	SET_IP(REG_IP + m_instr->size);

	if(INT_TRAPS) {
		auto ret = m_inttraps_ret.find(m_instr->csip);
		if(ret != m_inttraps_ret.end()) {
			for(auto fn : ret->second) {
				fn();
			}
			m_inttraps_ret.erase(ret);
		}
	}

	if(CPULOG && m_dos_prg_int_exit) {
		if(m_instr->csip == m_dos_prg_int_exit) {
			//logging starts at the next instruction
			g_machine.DOS_program_start(m_dos_prg.top().second);
		}
	}

	if(!m_instr->valid) {
		illegal_opcode();
	}
	if(m_instr->size > CPU_MAX_INSTR_SIZE) {
		/*
		 * When the CPU detects an instruction that is illegal due to being
		 * greater than 10 bytes in length, it generates an exception
		 * #13 (General Protection Violation)
		 * [80286 ARPL and Overlength Instructions, 15 October 1984]
		 */
		throw CPUException(CPU_GP_EXC, 0);
	}
	if(old_ip + m_instr->size > GET_LIMIT(CS)) {
		PERRF(LOG_CPU, "CS limit violation!\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	if(m_instr->seg != REGI_NONE) {
		m_base_ds = m_instr->seg;
		m_base_ss = m_instr->seg;
	} else {
		m_base_ds = REGI_DS;
		m_base_ss = REGI_SS;
	}

	if(m_instr->rep) {
		/* 1. Check the CX register. If it is zero, exit the iteration and move
		 * to the next instruction.
		 */
		if(REG_CX == 0) {
			//REP finished and IP points to the next instr.
			return;
		}
		/* 2. Acknowledge any pending interrupts.
		 * this is done in the CPU::step()
		 * TODO so it checks CX after interrupts; is it really a relevant difference?
		 */
		try {
			/* 3. Perform the string operation once.
			 */
			m_instr->fn(g_cpuexecutor);
		} catch(CPUException &e) {
			//TODO an exception occurred during the instr execution. what should i do?
			RESTORE_IP();
			throw;
		}
		/* 4. Decrement CX by 1; no flags are modified. */
		REG_CX -= 1;

		/* 5. If the string operation is SCAS or CMPS, check the zero flag.
		 * If the repeat condition does not hold, then exit the iteration and
		 * move to the next instruction. Exit if the prefix is REPE and ZF=O
		 * (the last comparison was not equal), or if the prefix is REPNE and
		 * ZF=1 (the last comparison was equal).
		 */
		if(m_instr->rep_zf) {
			if((m_instr->rep_equal && !FLAG_ZF) || (!m_instr->rep_equal && FLAG_ZF))
			{
				//REP finished and IP points to the next instr.
				return;
			}
		}
		/* 6. Go to step 1 for the next iteration. */
		//REP not finished so back up
		RESTORE_IP();

	} else {

		m_instr->fn(g_cpuexecutor);

	}
}

void CPUExecutor::illegal_opcode()
{
	char buf[CPU_MAX_INSTR_SIZE * 2 + 1];
	char * writecode = buf;
	uint i=0;
	while(i<(m_instr->size) && i<CPU_MAX_INSTR_SIZE) {
		sprintf(writecode, "%02X", m_instr->bytes[i++]);
		writecode += 2;
	}
	PDEBUGF(LOG_V2, LOG_CPU, "Illegal opcode: %s\n", buf);
	throw CPUException(CPU_UD_EXC, 0);
}

void CPUExecutor::get_SS_SP_from_TSS(unsigned pl, uint16_t &ss_, uint16_t &sp_)
{
	if(!REG_TR.desc.valid)
		PERRF_ABORT(LOG_CPU, "get_SS_ESP_from_TSS: TR invalid\n");

	if(!(REG_TR.desc.type!=DESC_TYPE_AVAIL_TSS || REG_TR.desc.type!=DESC_TYPE_BUSY_TSS)) {
		PERRF_ABORT(LOG_CPU, "get_SS_ESP_from_TSS: TR is bogus type (%u)", REG_TR.desc.type);
	}

	uint32_t TSSstackaddr = 4 * pl + 2;
	if((TSSstackaddr+3) > REG_TR.desc.limit) {
		PDEBUGF(LOG_V2, LOG_CPU, "get_SS_SP_from_TSS: TSSstackaddr > TSS.LIMIT\n");
		throw CPUException(CPU_TS_EXC, REG_TR.sel.value & SELECTOR_RPL_MASK);
	}
	ss_ = g_cpubus.mem_read_word(REG_TR.desc.base + TSSstackaddr + 2);
	sp_ = g_cpubus.mem_read_word(REG_TR.desc.base + TSSstackaddr);
}

void CPUExecutor::interrupt(uint8_t _vector)
{
	/* In Real Address Mode, the interrupt table can be accessed directly at
	 * physical memory location 0 through 1023.
	 * (cfr. 5-4)
	 *
	 * When an interrupt occurs in Real Address Mode, the 8086 performs the
	 * following sequence of steps. First, the FLAGS register, as well as the
	 * old values of CS and IP, are pushed onto the stack. The IF and TF flag
	 * bits are cleared. The vector number is then used to read the address of
	 * the interrupt service routine from the interrupt table. Execution begins
	 * at this address.
	 * The IRET instruction at the end of the interrupt service routine will
	 * reverse these steps before transferring control to the program that was
	 * interrupted.
	 * (cfr. 5-5)
	 */

	if((_vector*4+2+1) > GET_LIMIT(IDTR)) {
		/* Interrupt Table Limit Too Small (Interrupt 8). This interrupt will
		 * occur if the limit of the interrupt vector table was changed from
		 * 3FFH by the LIDT instruction and an interrupt whose vector is outside
		 * the limit occurs. The saved value of CS:IP will point to the first
		 * byte of the instruction that caused the interrupt or that was ready
		 * to execute before an external interrupt occurred. No error code is
		 * pushed.
		 * (cfr. 5-7)
		 */
		PERRF(LOG_CPU, "real mode interrupt vector > IDT limit\n");
		throw CPUException(CPU_IDT_LIMIT_EXC, 0);
	}
	stack_push(GET_FLAGS());
	stack_push(REG_CS.sel.value);
	stack_push(REG_IP);

	uint32_t addr = _vector * 4;
	uint16_t new_ip = g_cpubus.mem_read_word(addr);
	uint16_t cs_selector = g_cpubus.mem_read_word(addr+2);

	SET_CS(cs_selector);
	SET_IP(new_ip);

	SET_FLAG(IF, false);
	SET_FLAG(TF, false);

	g_cpubus.invalidate_pq();
}

void CPUExecutor::interrupt_pmode(uint8_t vector, bool soft_int,
		bool push_error, uint16_t error_code)
{
	Selector   cs_selector;
	Descriptor gate_descriptor, cs_descriptor;

	Selector   tss_selector;
	Descriptor tss_descriptor;

	uint16_t gate_dest_selector;
	uint32_t gate_dest_offset;

	// interrupt vector must be within IDT table limits,
	// else #GP(vector*8 + 2 + EXT)
	if((vector*8 + 7) > GET_LIMIT(IDTR)) {
		PDEBUGF(LOG_V2,LOG_CPU,
			"interrupt(): vector must be within IDT table limits, IDT.limit = 0x%x\n",
			GET_LIMIT(IDTR));
		throw CPUException(CPU_GP_EXC, vector*8 + 2);
	}

	gate_descriptor = g_cpubus.mem_read_qword(GET_BASE(IDTR) + vector*8);

	if(!gate_descriptor.valid || gate_descriptor.segment) {
		PDEBUGF(LOG_V2,LOG_CPU,
				"interrupt(): gate descriptor is not valid sys seg (vector=0x%02x)\n",
				vector);
		throw CPUException(CPU_GP_EXC, vector*8 + 2);
	}

	// descriptor AR byte must indicate interrupt gate, trap gate,
	// or task gate, else #GP(vector*8 + 2 + EXT)
	switch(gate_descriptor.type) {
		case DESC_TYPE_TASK_GATE:
		case DESC_TYPE_INTR_GATE:
		case DESC_TYPE_TRAP_GATE:
			break;
		default:
			PDEBUGF(LOG_V1,LOG_CPU, "interrupt(): gate.type(%u) != {5,6,7}\n",
					(unsigned) gate_descriptor.type);
			throw CPUException(CPU_GP_EXC, vector*8 + 2);
	}

	// if software interrupt, then gate descripor DPL must be >= CPL,
	// else #GP(vector * 8 + 2 + EXT)
	if(soft_int && gate_descriptor.dpl < CPL) {
		PDEBUGF(LOG_V1,LOG_CPU, "interrupt(): soft_int && (gate.dpl < CPL)\n");
		throw CPUException(CPU_GP_EXC, vector*8 + 2);
	}

	// Gate must be present, else #NP(vector * 8 + 2 + EXT)
	if(!gate_descriptor.present) {
		PDEBUGF(LOG_V1,LOG_CPU, "interrupt(): gate not present\n");
		throw CPUException(CPU_NP_EXC, vector*8 + 2);
	}

	switch(gate_descriptor.type) {
		case DESC_TYPE_TASK_GATE:
			// examine selector to TSS, given in task gate descriptor
			tss_selector = gate_descriptor.selector;
			// must specify global in the local/global bit,
			//      else #GP(TSS selector)
			if(tss_selector.ti) {
				PDEBUGF(LOG_V1,LOG_CPU,
					"interrupt(): tss_selector.ti=1 from gate descriptor - #GP(tss_selector)\n");
				throw CPUException(CPU_GP_EXC, tss_selector.value & SELECTOR_RPL_MASK);
			}

			// index must be within GDT limits, else #TS(TSS selector)
			try {
				tss_descriptor = g_cpucore.fetch_descriptor(tss_selector, CPU_GP_EXC);
			} catch(CPUException &e) {
				PDEBUGF(LOG_V1,LOG_CPU, "interrupt_pmode: bad tss_selector fetch\n");
				throw;
			}

			// AR byte must specify available TSS,
			//   else #GP(TSS selector)
			if(tss_descriptor.valid==0 || tss_descriptor.segment) {
				PDEBUGF(LOG_V1,LOG_CPU,
					"interrupt(): TSS selector points to invalid or bad TSS - #GP(tss_selector)\n");
				throw CPUException(CPU_GP_EXC, tss_selector.value & SELECTOR_RPL_MASK);
			}

			if(tss_descriptor.type != DESC_TYPE_AVAIL_TSS) {
				PDEBUGF(LOG_V1,LOG_CPU,
					"interrupt(): TSS selector points to bad TSS - #GP(tss_selector)\n");
				throw CPUException(CPU_GP_EXC, tss_selector.value & SELECTOR_RPL_MASK);
			}

			// TSS must be present, else #NP(TSS selector)
			if(!tss_descriptor.present) {
				PDEBUGF(LOG_V1,LOG_CPU, "interrupt(): TSS descriptor.p == 0\n");
				throw CPUException(CPU_NP_EXC, tss_selector.value & SELECTOR_RPL_MASK);
			}

			// switch tasks with nesting to TSS
			switch_tasks(tss_selector, tss_descriptor, CPU_TASK_FROM_INT,
					push_error, error_code);
			return;

		case DESC_TYPE_INTR_GATE:
		case DESC_TYPE_TRAP_GATE:
			gate_dest_selector = gate_descriptor.selector;
			gate_dest_offset   = gate_descriptor.offset;

			// examine CS selector and descriptor given in gate descriptor
			// selector must be non-null else #GP(EXT)
			if((gate_dest_selector & SELECTOR_RPL_MASK) == 0) {
				PDEBUGF(LOG_V1,LOG_CPU,"int_trap_gate(): selector null\n");
				throw CPUException(CPU_GP_EXC, 0);
			}
			cs_selector = gate_dest_selector;

			// selector must be within its descriptor table limits
			// else #GP(selector+EXT)
			try {
				cs_descriptor = g_cpucore.fetch_descriptor(cs_selector, CPU_GP_EXC);
			} catch(CPUException &e) {
				PDEBUGF(LOG_V1,LOG_CPU, "interrupt_pmode: bad cs_selector fetch\n");
				throw;
			}

			// descriptor AR byte must indicate code seg
			// and code segment descriptor DPL<=CPL, else #GP(selector+EXT)
			if(!cs_descriptor.valid || !cs_descriptor.segment ||
				!(cs_descriptor.type & SEG_TYPE_EXECUTABLE) || cs_descriptor.dpl > CPL)
			{
				PDEBUGF(LOG_V1,LOG_CPU, "interrupt(): not accessible or not code segment cs=0x%04x\n",
						cs_selector.value);
				throw CPUException(CPU_GP_EXC, cs_selector.value & SELECTOR_RPL_MASK);
			}

			// segment must be present, else #NP(selector + EXT)
			if(!cs_descriptor.present) {
				PDEBUGF(LOG_V1,LOG_CPU,"interrupt(): segment not present\n");
				throw CPUException(CPU_NP_EXC, cs_selector.value & SELECTOR_RPL_MASK);
			}

			// if code segment is non-conforming and DPL < CPL then
			// INTERRUPT TO INNER PRIVILEGE
			if(!(cs_descriptor.type & SEG_TYPE_CONFORMING) && cs_descriptor.dpl < CPL)
			{
				uint16_t old_SS, old_CS, SS_for_cpl_x;
				uint16_t SP_for_cpl_x, old_IP, old_SP;
				Descriptor ss_descriptor;
				Selector   ss_selector;
				PDEBUGF(LOG_V2, LOG_CPU, "interrupt(): INTERRUPT TO INNER PRIVILEGE\n");

				// check selector and descriptor for new stack in current TSS
				get_SS_SP_from_TSS(cs_descriptor.dpl, SS_for_cpl_x, SP_for_cpl_x);

				// Selector must be non-null else #TS(EXT)
				if((SS_for_cpl_x & SELECTOR_RPL_MASK) == 0) {
					PDEBUGF(LOG_V1,LOG_CPU, "interrupt(): SS selector null\n");
					throw CPUException(CPU_TS_EXC, 0); /* TS(ext) */
				}

				// selector index must be within its descriptor table limits
				// else #TS(SS selector + EXT)
				ss_selector = SS_for_cpl_x;

				// fetch 2 dwords of descriptor; call handles out of limits checks
				try {
					ss_descriptor = g_cpucore.fetch_descriptor(ss_selector, CPU_TS_EXC);
				} catch(CPUException &e) {
					PDEBUGF(LOG_V1,LOG_CPU, "interrupt_pmode: bad ss_selector fetch\n");
					throw;
				}

				// selector rpl must = dpl of code segment,
				// else #TS(SS selector + ext)
				if(ss_selector.rpl != cs_descriptor.dpl) {
					PDEBUGF(LOG_V1,LOG_CPU, "interrupt(): SS.rpl != CS.dpl\n");
					throw CPUException(CPU_TS_EXC, SS_for_cpl_x & SELECTOR_RPL_MASK);
				}

				// stack seg DPL must = DPL of code segment,
				// else #TS(SS selector + ext)
				if(ss_descriptor.dpl != cs_descriptor.dpl) {
					PDEBUGF(LOG_V1,LOG_CPU, "interrupt(): SS.dpl != CS.dpl\n");
					throw CPUException(CPU_TS_EXC, SS_for_cpl_x & SELECTOR_RPL_MASK);
				}

				// descriptor must indicate writable data segment,
				// else #TS(SS selector + EXT)
				if(!ss_descriptor.valid || !ss_descriptor.segment ||
					(ss_descriptor.type & SEG_TYPE_EXECUTABLE) ||
					!(ss_descriptor.type & SEG_TYPE_WRITABLE))
				{
					PDEBUGF(LOG_V1,LOG_CPU,"interrupt(): SS is not writable data segment\n");
					throw CPUException(CPU_TS_EXC, SS_for_cpl_x & SELECTOR_RPL_MASK);
				}

				// seg must be present, else #SS(SS selector + ext)
				if(!ss_descriptor.present) {
					PDEBUGF(LOG_V1,LOG_CPU, "interrupt(): SS not present\n");
					throw CPUException(CPU_SS_EXC, SS_for_cpl_x & SELECTOR_RPL_MASK);
				}

				// IP must be within CS segment boundaries, else #GP(0)
				if(gate_dest_offset > cs_descriptor.limit) {
					PDEBUGF(LOG_V1,LOG_CPU,"interrupt(): gate IP > CS.limit\n");
					throw CPUException(CPU_GP_EXC, 0);
				}

				old_SP = REG_SP;
				old_SS = REG_SS.sel.value;
				old_IP = REG_IP;
				old_CS = REG_CS.sel.value;

				// Prepare new stack segment
				SegReg new_stack;
				new_stack.sel = ss_selector;
				new_stack.desc = ss_descriptor;
				new_stack.sel.rpl = cs_descriptor.dpl;
				// add cpl to the selector value
				new_stack.sel.value =
					(new_stack.sel.value & SELECTOR_RPL_MASK) | new_stack.sel.rpl;

				uint16_t temp_SP = SP_for_cpl_x;

				// int/trap gate
				// push long pointer to old stack onto new stack
				uint8_t exc = CPU_SS_EXC;
				uint16_t errcode =
					new_stack.sel.rpl != CPL ? (new_stack.sel.value & SELECTOR_RPL_MASK) : 0;
				write_word_pmode(new_stack, temp_SP-2,  old_SS, exc, errcode);
				write_word_pmode(new_stack, temp_SP-4,  old_SP, exc, errcode);
				write_word_pmode(new_stack, temp_SP-6,  GET_FLAGS(), exc, errcode);
				write_word_pmode(new_stack, temp_SP-8,  old_CS, exc, errcode);
				write_word_pmode(new_stack, temp_SP-10, old_IP, exc, errcode);
				temp_SP -= 10;

				if(push_error) {
					temp_SP -= 2;
					write_word_pmode(new_stack, temp_SP, error_code, exc, errcode);
				}

				// load new CS:IP values from gate
				// set CPL to new code segment DPL
				// set RPL of CS to CPL
				SET_CS(cs_selector, cs_descriptor, cs_descriptor.dpl);
				//IP is set below...

				// load new SS:SP values from TSS
				SET_SS(ss_selector, ss_descriptor, cs_descriptor.dpl);
				REG_SP = temp_SP;
			}
			else
			{
				PDEBUGF(LOG_V2,LOG_CPU, "interrupt(): INTERRUPT TO SAME PRIVILEGE\n");

				// IP must be in CS limit else #GP(0)
				if(gate_dest_offset > cs_descriptor.limit) {
					PDEBUGF(LOG_V1,LOG_CPU,"interrupt(): IP > CS descriptor limit\n");
					throw CPUException(CPU_GP_EXC, 0);
				}

				// push flags onto stack
				// push current CS selector onto stack
				// push return IP onto stack
				stack_push(GET_FLAGS());
				stack_push(REG_CS.sel.value);
				stack_push(REG_IP);
				if(push_error) {
					stack_push(error_code);
				}

				// load CS:IP from gate
				// load CS descriptor
				// set the RPL field of CS to CPL
				SET_CS(cs_selector, cs_descriptor, CPL);
			}

			SET_IP(gate_dest_offset);

			/* The difference between a trap and an interrupt gate is whether
			 * the interrupt enable flag is to be cleared or not. An interrupt
			 * gate specifies a procedure that enters with interrupts disabled
			 * (i.e., with the interrupt enable flag cleared); entry via a trap
			 * gate leaves the interrupt enable status unchanged.
			 */
			if(gate_descriptor.type == DESC_TYPE_INTR_GATE) {
				SET_FLAG(IF,false);
			}

			/* The NT flag is always cleared (after the old NT state is saved on
			 * the stack) when an interrupt uses these gates.
			 */
			SET_FLAG(NT,false);
			SET_FLAG(TF,false);

			g_cpubus.invalidate_pq();

			break;

		default:
			PERRF_ABORT(LOG_CPU,"bad descriptor type in interrupt()!\n");
			break;
	}
}

void CPUExecutor::switch_tasks_load_selector(SegReg &_seg, uint8_t _cs_rpl)
{
	Descriptor descriptor;

	// NULL selector is OK, will leave cache invalid
	if((_seg.sel.value & SELECTOR_RPL_MASK) != 0) {
		try {
			descriptor = g_cpucore.fetch_descriptor(_seg.sel, CPU_TS_EXC);
		} catch (CPUException &e) {
			PERRF(LOG_CPU,"switch_tasks(%s): bad selector fetch\n", _seg.to_string());
			throw;
		}

		/* AR byte must indicate data or readable code segment else #TS(selector) */
		if(descriptor.segment==0 ||
			( (descriptor.type & SEG_TYPE_EXECUTABLE) && !(descriptor.type & SEG_TYPE_READABLE) )
		) {
			PERRF(LOG_CPU,"switch_tasks(%s): not data or readable code\n", _seg.to_string());
			throw CPUException(CPU_TS_EXC, _seg.sel.value & SELECTOR_RPL_MASK);
		}

		/* If data or non-conforming code, then both the RPL and the CPL
		 * must be less than or equal to DPL in AR byte else #GP(selector) */
		if(!(descriptor.type & SEG_TYPE_EXECUTABLE) || !(descriptor.type & SEG_TYPE_CONFORMING)) {
			if((_seg.sel.rpl > descriptor.dpl) || (_cs_rpl > descriptor.dpl)) {
				PERRF(LOG_CPU,"switch_tasks(%s): RPL & CPL must be <= DPL\n", _seg.to_string());
				throw CPUException(CPU_TS_EXC, _seg.sel.value & SELECTOR_RPL_MASK);
			}
		}

		if(descriptor.present == false) {
			PERRF(LOG_CPU,"switch_tasks(%s): descriptor not present\n", _seg.to_string());
			throw CPUException(CPU_TS_EXC, _seg.sel.value & SELECTOR_RPL_MASK);
		}

		g_cpucore.touch_segment(_seg.sel, descriptor);

		// All checks pass, fill in shadow cache
		_seg.desc = descriptor;
	}
}

void CPUExecutor::switch_tasks(Selector &selector, Descriptor &descriptor,
		unsigned source, bool push_error, uint16_t error_code)
{
	uint32_t obase32; // base address of old TSS
	uint32_t nbase32; // base address of new TSS
	uint16_t raw_cs_selector, raw_ss_selector, raw_ds_selector,
		raw_es_selector, raw_ldt_selector;
	//uint16_t trap_word;
	Descriptor cs_descriptor, ss_descriptor, ldt_descriptor;
	uint32_t old_TSS_limit, new_TSS_limit;
	uint32_t newAX, newCX, newDX, newBX;
	uint32_t newSP, newBP, newSI, newDI;
	uint32_t newFLAGS, newIP;

	PDEBUGF(LOG_V2,LOG_CPU,"TASKING: ENTER\n");

	// Discard any traps and inhibits for new context; traps will
	// resume upon return.
	g_cpu.clear_inhibit_mask();
	g_cpu.clear_debug_trap();

	// STEP 1: The following checks are made before calling task_switch(),
	//         for JMP & CALL only. These checks are NOT made for exceptions,
	//         interrupts & IRET.
	//
	//   1) TSS DPL must be >= CPL
	//   2) TSS DPL must be >= TSS selector RPL
	//   3) TSS descriptor is not busy.

	// STEP 2: The processor performs limit-checking on the target TSS
	//         to verify that the TSS limit is greater than or equal to 2Bh.

	const uint32_t new_TSS_max = 0x2B;
	const uint32_t old_TSS_max = 0x29;

	nbase32 = (uint32_t) descriptor.base;
	new_TSS_limit = descriptor.limit;

	if(new_TSS_limit < new_TSS_max) {
		PERRF(LOG_CPU,"switch_tasks(): new TSS limit < %d\n", new_TSS_max);
		throw CPUException(CPU_TS_EXC, selector.value & SELECTOR_RPL_MASK);
	}

	obase32 = GET_BASE(TR);        // old TSS.base
	old_TSS_limit = GET_LIMIT(TR);

	if(old_TSS_limit < old_TSS_max) {
		PERRF(LOG_CPU,"switch_tasks(): old TSS limit < %d\n", old_TSS_max);
		throw CPUException(CPU_TS_EXC, REG_TR.sel.value & SELECTOR_RPL_MASK);
	}

	if(obase32 == nbase32) {
		PWARNF(LOG_CPU, "switch_tasks(): switching to the same TSS!\n");
	}

	// Privilege and busy checks done in CALL, JUMP, INT, IRET

	// Step 3: If JMP or IRET, clear busy bit in old task TSS descriptor,
	//         otherwise leave set.

	// effect on Busy bit of old task
	if(source == CPU_TASK_FROM_JUMP || source == CPU_TASK_FROM_IRET) {
		// Bit is cleared
		uint32_t addr = GET_BASE(GDTR) + REG_TR.sel.index*8 + 5;
		uint8_t ar = g_cpubus.mem_read_byte(addr);
		ar &= ~0x2;
		g_cpubus.mem_write_byte(addr,ar);
	}

	// STEP 4: If the task switch was initiated with an IRET instruction,
	//         clears the NT flag in a temporarily saved EFLAGS image;
	//         if initiated with a CALL or JMP instruction, an exception, or
	//         an interrupt, the NT flag is left unchanged.

	uint16_t oldFLAGS = GET_FLAGS();

	/* if moving to busy task, clear NT bit */
	if(descriptor.type == DESC_TYPE_BUSY_TSS) {
		oldFLAGS &= ~FMASK_NT;
	}

	// STEP 5: Save the current task state in the TSS. Up to this point,
	//         any exception that occurs aborts the task switch without
	//         changing the processor state.

	/* save current machine state in old task's TSS */
    g_cpubus.mem_write_word(uint32_t(obase32 + 14), REG_IP);
    g_cpubus.mem_write_word(uint32_t(obase32 + 16), oldFLAGS);
    g_cpubus.mem_write_word(uint32_t(obase32 + 18), REG_AX);
    g_cpubus.mem_write_word(uint32_t(obase32 + 20), REG_CX);
    g_cpubus.mem_write_word(uint32_t(obase32 + 22), REG_DX);
    g_cpubus.mem_write_word(uint32_t(obase32 + 24), REG_BX);
    g_cpubus.mem_write_word(uint32_t(obase32 + 26), REG_SP);
    g_cpubus.mem_write_word(uint32_t(obase32 + 28), REG_BP);
    g_cpubus.mem_write_word(uint32_t(obase32 + 30), REG_SI);
    g_cpubus.mem_write_word(uint32_t(obase32 + 32), REG_DI);
    g_cpubus.mem_write_word(uint32_t(obase32 + 34), REG_ES.sel.value);
    g_cpubus.mem_write_word(uint32_t(obase32 + 36), REG_CS.sel.value);
    g_cpubus.mem_write_word(uint32_t(obase32 + 38), REG_SS.sel.value);
    g_cpubus.mem_write_word(uint32_t(obase32 + 40), REG_DS.sel.value);

	// effect on link field of new task
	if(source == CPU_TASK_FROM_CALL || source == CPU_TASK_FROM_INT) {
		// set to selector of old task's TSS
		g_cpubus.mem_write_word(nbase32, REG_TR.sel.value);
	}

	// STEP 6: The new-task state is loaded from the TSS
	newIP    = g_cpubus.mem_read_word(uint32_t(nbase32 + 14));
	newFLAGS = g_cpubus.mem_read_word(uint32_t(nbase32 + 16));

	// incoming TSS:
	newAX = g_cpubus.mem_read_word(uint32_t(nbase32 + 18));
	newCX = g_cpubus.mem_read_word(uint32_t(nbase32 + 20));
	newDX = g_cpubus.mem_read_word(uint32_t(nbase32 + 22));
	newBX = g_cpubus.mem_read_word(uint32_t(nbase32 + 24));
	newSP = g_cpubus.mem_read_word(uint32_t(nbase32 + 26));
	newBP = g_cpubus.mem_read_word(uint32_t(nbase32 + 28));
	newSI = g_cpubus.mem_read_word(uint32_t(nbase32 + 30));
	newDI = g_cpubus.mem_read_word(uint32_t(nbase32 + 32));
	raw_es_selector  = g_cpubus.mem_read_word(uint32_t(nbase32 + 34));
	raw_cs_selector  = g_cpubus.mem_read_word(uint32_t(nbase32 + 36));
	raw_ss_selector  = g_cpubus.mem_read_word(uint32_t(nbase32 + 38));
	raw_ds_selector  = g_cpubus.mem_read_word(uint32_t(nbase32 + 40));
	raw_ldt_selector = g_cpubus.mem_read_word(uint32_t(nbase32 + 42));

	// Step 7: If CALL, interrupt, or JMP, set busy flag in new task's
	//         TSS descriptor.  If IRET, leave set.

	if(source != CPU_TASK_FROM_IRET) {
		// set the new task's busy bit
		uint32_t addr = GET_BASE(GDTR) + (selector.index*8) + 5;
		uint8_t ar = g_cpubus.mem_read_byte(addr);
		ar |= 0x2;
		g_cpubus.mem_write_byte(addr, ar);
	}

	//
	// Commit point.  At this point, we commit to the new
	// context.  If an unrecoverable error occurs in further
	// processing, we complete the task switch without performing
	// additional access and segment availablility checks and
	// generate the appropriate exception prior to beginning
	// execution of the new task.
	//

	// Step 8: Load the task register with the segment selector and
	//         descriptor for the new task TSS.

	REG_TR.sel  = selector;
	REG_TR.desc = descriptor;
	REG_TR.desc.type |= 2; // mark TSS in TR as busy

	// Step 9: Set TS flag

	SET_MSW(MSW_TS, true);

	// Step 10: If call or interrupt, set the NT flag in the eflags
	//          image stored in new task's TSS.  If IRET or JMP,
	//          NT is restored from new TSS eflags image. (no change)

	// effect on NT flag of new task
	if(source == CPU_TASK_FROM_CALL || source == CPU_TASK_FROM_INT) {
		newFLAGS |= FMASK_NT; // NT flag is set
	}

	// Step 11: Load the new task (dynamic) state from new TSS.
	//          Any errors associated with loading and qualification of
	//          segment descriptors in this step occur in the new task's
	//          context.  State loaded here includes LDTR,
	//          FLAGS, IP, general purpose registers, and segment
	//          descriptor parts of the segment registers.

	SET_IP(newIP);

	REG_AX = newAX;
	REG_CX = newCX;
	REG_DX = newDX;
	REG_BX = newBX;
	REG_SP = newSP;
	REG_BP = newBP;
	REG_SI = newSI;
	REG_DI = newDI;

	SET_FLAGS(newFLAGS);

	// Fill in selectors for all segment registers.  If errors
	// occur later, the selectors will at least be loaded.
	REG_CS.sel   = raw_cs_selector;
	REG_SS.sel   = raw_ss_selector;
	REG_DS.sel   = raw_ds_selector;
	REG_ES.sel   = raw_es_selector;
	REG_LDTR.sel = raw_ldt_selector;

	// Start out with invalid descriptor, fill in with
	// values only as they are validated
	REG_LDTR.desc.valid = false;
	REG_CS.desc.valid   = false;
	REG_SS.desc.valid   = false;
	REG_DS.desc.valid   = false;
	REG_ES.desc.valid   = false;

	unsigned save_CPL = CPL;
	/* set CPL to 3 to force a privilege level change and stack switch if SS
	 is not properly loaded */
	CPL = 3;

	// LDTR
	if(REG_LDTR.sel.ti) {
		// LDT selector must be in GDT
		PINFOF(LOG_V2,LOG_CPU,"switch_tasks(exception after commit point): bad LDT selector TI=1\n");
		throw CPUException(CPU_TS_EXC, raw_ldt_selector & SELECTOR_RPL_MASK);
	}

	if((raw_ldt_selector & SELECTOR_RPL_MASK) != 0) {
		try {
			ldt_descriptor = g_cpucore.fetch_descriptor(REG_LDTR.sel, CPU_TS_EXC);
		} catch(CPUException &e) {
			PERRF(LOG_CPU, "switch_tasks(exception after commit point): bad LDT fetch\n");
			throw;
		}

		// LDT selector of new task is valid, else #TS(new task's LDT)
		if(ldt_descriptor.valid == false ||
			ldt_descriptor.type != DESC_TYPE_LDT_DESC ||
			ldt_descriptor.segment)
		{
			PERRF(LOG_CPU, "switch_tasks(exception after commit point): bad LDT segment\n");
			throw CPUException(CPU_TS_EXC, raw_ldt_selector & SELECTOR_RPL_MASK);
		}

		// LDT of new task is present in memory, else #TS(new tasks's LDT)
		if(ldt_descriptor.present == false) {
			PERRF(LOG_CPU, "switch_tasks(exception after commit point): LDT not present\n");
			throw CPUException(CPU_TS_EXC, raw_ldt_selector & SELECTOR_RPL_MASK);
		}

		// All checks pass, fill in LDTR shadow cache
		REG_LDTR.desc = ldt_descriptor;

	} else {
		// NULL LDT selector is OK, leave cache invalid
	}

	// SS
	if((raw_ss_selector & SELECTOR_RPL_MASK) != 0) {
		try {
			ss_descriptor = g_cpucore.fetch_descriptor(REG_SS.sel, CPU_TS_EXC);
		} catch(CPUException &e) {
			PERRF(LOG_CPU, "switch_tasks(exception after commit point): bad SS fetch\n");
			throw;
		}

		// SS selector must be within its descriptor table limits else #TS(SS)
		// SS descriptor AR byte must must indicate writable data segment,
		// else #TS(SS)
		if(!ss_descriptor.valid || !ss_descriptor.segment ||
           (ss_descriptor.is_code_segment()) ||
          !(ss_descriptor.type & SEG_TYPE_READWRITE))
		{
			PERRF(LOG_CPU,"switch_tasks(exception after commit point): SS not valid or writeable segment\n");
			throw CPUException(CPU_TS_EXC, raw_ss_selector & SELECTOR_RPL_MASK);
		}

		//
		// Stack segment is present in memory, else #SS(new stack segment)
		//
		if(ss_descriptor.present == false) {
			PERRF(LOG_CPU,"switch_tasks(exception after commit point): SS not present\n");
			throw CPUException(CPU_SS_EXC, raw_ss_selector & SELECTOR_RPL_MASK);
		}

		// Stack segment DPL matches CS.RPL, else #TS(new stack segment)
		if(ss_descriptor.dpl != REG_CS.sel.rpl) {
			PERRF(LOG_CPU,"switch_tasks(exception after commit point): SS.rpl != CS.RPL\n");
			throw CPUException(CPU_TS_EXC, raw_ss_selector & SELECTOR_RPL_MASK);
		}

		// Stack segment DPL matches selector RPL, else #TS(new stack segment)
		if(ss_descriptor.dpl != REG_SS.sel.rpl) {
			PERRF(LOG_CPU,"switch_tasks(exception after commit point): SS.dpl != SS.rpl\n");
			throw CPUException(CPU_TS_EXC, raw_ss_selector & SELECTOR_RPL_MASK);
		}

		g_cpucore.touch_segment(REG_SS.sel, ss_descriptor);

		// All checks pass, fill in cache
		REG_SS.desc = ss_descriptor;
	} else {
		// SS selector is valid, else #TS(new stack segment)
		PERRF(LOG_CPU,"switch_tasks(exception after commit point): SS NULL\n");
		throw CPUException(CPU_TS_EXC, raw_ss_selector & SELECTOR_RPL_MASK);
    }

	CPL = save_CPL;

	switch_tasks_load_selector(REG_DS, REG_CS.sel.rpl);
    switch_tasks_load_selector(REG_ES, REG_CS.sel.rpl);

	// if new selector is not null then perform following checks:
	//    index must be within its descriptor table limits else #TS(selector)
	//    AR byte must indicate data or readable code else #TS(selector)
	//    if data or non-conforming code then:
	//      DPL must be >= CPL else #TS(selector)
	//      DPL must be >= RPL else #TS(selector)
	//    AR byte must indicate PRESENT else #NP(selector)
	//    load cache with new segment descriptor and set valid bit

	// CS
    if((raw_cs_selector & SELECTOR_RPL_MASK) != 0) {
		try {
			cs_descriptor = g_cpucore.fetch_descriptor(REG_CS.sel, CPU_TS_EXC);
		} catch(CPUException &e) {
			PERRF(LOG_CPU, "switch_tasks(exception after commit point): bad CS fetch\n");
			throw;
		}

		// CS descriptor AR byte must indicate code segment else #TS(CS)
		if(!cs_descriptor.valid || !cs_descriptor.segment ||
			cs_descriptor.is_data_segment())
		{
			PERRF(LOG_CPU,"switch_tasks(exception after commit point): CS not valid executable seg\n");
			throw CPUException(CPU_TS_EXC, raw_cs_selector & SELECTOR_RPL_MASK);
		}

		// if non-conforming then DPL must equal selector RPL else #TS(CS)
		if (cs_descriptor.is_code_segment_non_conforming() && cs_descriptor.dpl != REG_CS.sel.rpl)
		{
			PERRF(LOG_CPU,"switch_tasks(exception after commit point): non-conforming: CS.dpl!=CS.RPL\n");
			throw CPUException(CPU_TS_EXC, raw_cs_selector & SELECTOR_RPL_MASK);
		}

		// if conforming then DPL must be <= selector RPL else #TS(CS)
		if(cs_descriptor.is_code_segment_conforming() && cs_descriptor.dpl > REG_CS.sel.rpl) {
			PERRF(LOG_CPU,"switch_tasks(exception after commit point): conforming: CS.dpl>RPL\n");
			throw CPUException(CPU_TS_EXC, raw_cs_selector & SELECTOR_RPL_MASK);
		}

		// Code segment is present in memory, else #NP(new code segment)
		if(!cs_descriptor.present) {
			PERRF(LOG_CPU,"switch_tasks(exception after commit point): CS.p==0\n");
			throw CPUException(CPU_NP_EXC, raw_cs_selector & SELECTOR_RPL_MASK);
		}

		g_cpucore.touch_segment(REG_CS.sel, cs_descriptor);

		// All checks pass, fill in shadow cache
		REG_CS.desc = cs_descriptor;

    } else {
    	// If new cs selector is null #TS(CS)
    	PERRF(LOG_CPU,"switch_tasks(exception after commit point): CS NULL\n");
    	throw CPUException(CPU_TS_EXC, raw_cs_selector & SELECTOR_RPL_MASK);
    }

	//
	// Step 12: Begin execution of new task.
	//
    PDEBUGF(LOG_V2, LOG_CPU, "TASKING: LEAVE\n");


    // push error code onto stack
    if(push_error) {
    	stack_push_pmode(error_code);
    }

    // instruction pointer must be in CS limit, else #GP(0)
    if(REG_IP > REG_CS.desc.limit) {
    	PERRF(LOG_CPU,"switch_tasks: IP > CS.limit\n");
    	throw CPUException(CPU_GP_EXC, 0);
    }

    g_cpubus.invalidate_pq();
}

void CPUExecutor::task_gate(Selector &selector, Descriptor &gate_descriptor, unsigned source)
{
	Selector   tss_selector;
	Descriptor tss_descriptor;

	// task gate must be present else #NP(gate selector)
	if(!gate_descriptor.present) {
		PERRF(LOG_CPU,"task_gate: task gate not present");
		throw CPUException(CPU_NP_EXC, selector.value & SELECTOR_RPL_MASK);
	}

	// examine selector to TSS, given in Task Gate descriptor
	// must specify global in the local/global bit else #GP(TSS selector)
	tss_selector = gate_descriptor.selector;

	if(tss_selector.ti) {
		PERRF(LOG_CPU,"task_gate: tss_selector.ti=1\n");
		throw CPUException(CPU_GP_EXC, tss_selector.value & SELECTOR_RPL_MASK);
	}

	// index must be within GDT limits else #GP(TSS selector)
	tss_descriptor = g_cpucore.fetch_descriptor(tss_selector, CPU_GP_EXC);

	if(tss_descriptor.valid==0 || tss_descriptor.segment) {
		PERRF(LOG_CPU,"task_gate: TSS selector points to bad TSS\n");
		throw CPUException(CPU_GP_EXC, tss_selector.value & SELECTOR_RPL_MASK);
	}
	// descriptor AR byte must specify available TSS
	//   else #GP(TSS selector)
	if(tss_descriptor.type != DESC_TYPE_AVAIL_TSS) {
		PERRF(LOG_CPU,"task_gate: TSS selector points to bad TSS\n");
		throw CPUException(CPU_GP_EXC, tss_selector.value & SELECTOR_RPL_MASK);
	}

	// task state segment must be present, else #NP(tss selector)
	if(!tss_descriptor.present) {
		PERRF(LOG_CPU,"task_gate: TSS descriptor.p == 0\n");
		throw CPUException(CPU_NP_EXC, tss_selector.value & SELECTOR_RPL_MASK);
	}

	// SWITCH_TASKS _without_ nesting to TSS
	switch_tasks(tss_selector, tss_descriptor, source);
}

void CPUExecutor::call_gate(Descriptor &gate_descriptor)
{
	Selector   cs_selector;
	Descriptor cs_descriptor;

	// examine code segment selector in call gate descriptor
	PDEBUGF(LOG_V2, LOG_CPU, "call_protected: call gate\n");

	cs_selector     = gate_descriptor.selector;
	uint16_t new_IP = gate_descriptor.offset;

	// selector must not be null else #GP(0)
	if((cs_selector.value & SELECTOR_RPL_MASK) == 0) {
		PDEBUGF(LOG_V2, LOG_CPU, "call_protected: selector in gate null\n");
		throw CPUException(CPU_GP_EXC, 0);
	}
	// selector must be within its descriptor table limits,
	//   else #GP(code segment selector)
	cs_descriptor = g_cpucore.fetch_descriptor(cs_selector, CPU_GP_EXC);

	// AR byte of selected descriptor must indicate code segment,
	//   else #GP(code segment selector)
	// DPL of selected descriptor must be <= CPL,
	// else #GP(code segment selector)
	if(!cs_descriptor.valid || !cs_descriptor.segment ||
		cs_descriptor.is_data_segment() || cs_descriptor.dpl > CPL)
	{
		PDEBUGF(LOG_V2, LOG_CPU, "call_protected: selected descriptor is not code\n");
		throw CPUException(CPU_GP_EXC, cs_selector.value & SELECTOR_RPL_MASK);
	}

	// code segment must be present else #NP(selector)
	if(!cs_descriptor.present) {
		PDEBUGF(LOG_V2, LOG_CPU, "call_protected: code segment not present!\n");
		throw CPUException(CPU_NP_EXC, cs_selector.value & SELECTOR_RPL_MASK);
	}

	// CALL GATE TO MORE PRIVILEGE
	// if non-conforming code segment and DPL < CPL then
	if(cs_descriptor.is_code_segment_non_conforming() && (cs_descriptor.dpl < CPL)) {
		uint16_t SS_for_cpl_x;
		uint16_t SP_for_cpl_x;
		Selector   ss_selector;
		Descriptor ss_descriptor;
		uint16_t   return_SS, return_CS;
		uint16_t   return_SP, return_IP;

		PDEBUGF(LOG_V2, LOG_CPU, "CALL GATE TO MORE PRIVILEGE LEVEL\n");

		// get new SS selector for new privilege level from TSS
		get_SS_SP_from_TSS(cs_descriptor.dpl, SS_for_cpl_x, SP_for_cpl_x);

		// check selector & descriptor for new SS:
		// selector must not be null, else #TS(0)
		if((SS_for_cpl_x & SELECTOR_RPL_MASK) == 0) {
			PDEBUGF(LOG_V2, LOG_CPU, "call_protected: new SS null\n");
			throw CPUException(CPU_TS_EXC, 0);
		}

		// selector index must be within its descriptor table limits,
		//   else #TS(SS selector)
		ss_selector   = SS_for_cpl_x;
		ss_descriptor = g_cpucore.fetch_descriptor(ss_selector, CPU_TS_EXC);

		// selector's RPL must equal DPL of code segment,
		//   else #TS(SS selector)
		if(ss_selector.rpl != cs_descriptor.dpl) {
			PDEBUGF(LOG_V2, LOG_CPU, "call_protected: SS selector.rpl != CS descr.dpl\n");
			throw CPUException(CPU_TS_EXC, SS_for_cpl_x & SELECTOR_RPL_MASK);
		}

		// stack segment DPL must equal DPL of code segment,
		//   else #TS(SS selector)
		if(ss_descriptor.dpl != cs_descriptor.dpl) {
			PDEBUGF(LOG_V2, LOG_CPU, "call_protected: SS descr.rpl != CS descr.dpl\n");
			throw CPUException(CPU_TS_EXC, SS_for_cpl_x & SELECTOR_RPL_MASK);
		}

		// descriptor must indicate writable data segment,
		//   else #TS(SS selector)
		if(!ss_descriptor.valid || !ss_descriptor.segment ||
			ss_descriptor.is_code_segment() ||
			!ss_descriptor.is_data_segment_writeable())
		{
			PDEBUGF(LOG_V2, LOG_CPU, "call_protected: ss descriptor is not writable data seg\n");
			throw CPUException(CPU_TS_EXC, SS_for_cpl_x & SELECTOR_RPL_MASK);
		}

		// segment must be present, else #SS(SS selector)
		if(!ss_descriptor.present) {
			PDEBUGF(LOG_V2, LOG_CPU, "call_protected: ss descriptor not present\n");
			throw CPUException(CPU_SS_EXC, SS_for_cpl_x & SELECTOR_RPL_MASK);
		}

		// get word count from call gate, mask to 5 bits
		unsigned param_count = gate_descriptor.word_count & 0x1f;

		// save return SS:eSP to be pushed on new stack
		return_SS = REG_SS.sel.value;
		return_SP = REG_SP;

		// save return CS:IP to be pushed on new stack
		return_CS = REG_CS.sel.value;
		return_IP = REG_IP;

		// Prepare new stack segment
		SegReg new_stack;
		new_stack.sel     = ss_selector;
		new_stack.desc    = ss_descriptor;
		new_stack.sel.rpl = cs_descriptor.dpl;
		// add cpl to the selector value
		new_stack.sel.value = (new_stack.sel.value & SELECTOR_RPL_MASK) | new_stack.sel.rpl;

		/* load new SS:SP value from TSS */
		uint16_t temp_SP = SP_for_cpl_x;

		// push pointer of old stack onto new stack
		uint16_t errcode = new_stack.sel.rpl != CPL ? (new_stack.sel.value & SELECTOR_RPL_MASK) : 0;
		write_word_pmode(new_stack, temp_SP-2, return_SS, CPU_SS_EXC, errcode);
		write_word_pmode(new_stack, temp_SP-4, return_SP, CPU_SS_EXC, errcode);
		temp_SP -= 4;

		for(unsigned n = param_count; n>0; n--) {
			temp_SP -= 2;
			uint32_t addr = GET_PHYADDR(SS, return_SP + (n-1)*2);
			uint16_t param = g_cpubus.mem_read_word(addr);
			write_word_pmode(new_stack, temp_SP, param, CPU_SS_EXC, errcode);
		}
		// push return address onto new stack
		write_word_pmode(new_stack, temp_SP-2, return_CS, CPU_SS_EXC, errcode);
		write_word_pmode(new_stack, temp_SP-4, return_IP, CPU_SS_EXC, errcode);
		temp_SP -= 4;

		REG_SP = temp_SP;

		// new eIP must be in code segment limit else #GP(0)
		if(new_IP > cs_descriptor.limit) {
			PDEBUGF(LOG_V2, LOG_CPU, "call_protected: IP not within CS limits\n");
			throw CPUException(CPU_GP_EXC, 0);
		}

		/* load SS descriptor */
		SET_SS(ss_selector, ss_descriptor, cs_descriptor.dpl);

		/* load new CS:IP value from gate */
		/* load CS descriptor */
		/* set CPL to stack segment DPL */
		/* set RPL of CS to CPL */
		SET_CS(cs_selector, cs_descriptor, cs_descriptor.dpl);
		SET_IP(new_IP);

		g_cpubus.invalidate_pq();
	}
	else   // CALL GATE TO SAME PRIVILEGE
	{
		PDEBUGF(LOG_V2, LOG_CPU, "CALL GATE TO SAME PRIVILEGE\n");

		// call gate 16bit, push return address onto stack
		stack_push(REG_CS.sel.value);
		stack_push(REG_IP);

		// load CS:IP from gate
		// load code segment descriptor into CS register
		// set RPL of CS to CPL
		branch_far(cs_selector, cs_descriptor, new_IP, CPL);
	}
}

void CPUExecutor::branch_far(Selector &selector, Descriptor &descriptor, uint16_t ip, uint8_t cpl)
{
	/* instruction pointer must be in code segment limit else #GP(0) */
	if(ip > descriptor.limit) {
		PERRF(LOG_CPU, "branch_far: IP > descriptor limit\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	/* Load CS:IP from destination pointer */
	SET_CS(selector, descriptor, cpl);
	SET_IP(ip);
	g_cpubus.invalidate_pq();
}

void CPUExecutor::branch_far(uint16_t _sel, uint16_t _disp)
{
	// CS LIMIT can't change when in real mode
	if(_disp > GET_LIMIT(CS)) {
		PDEBUGF(LOG_V2,LOG_CPU, "branch_far: offset outside of CS limits\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	SET_CS(_sel);
	SET_IP(_disp);
	g_cpubus.invalidate_pq();
}

void CPUExecutor::branch_near(uint16_t new_IP)
{
	// check always, not only in protected mode
	if(new_IP > GET_LIMIT(CS)) {
		PDEBUGF(LOG_V2,LOG_CPU,"branch_near: offset outside of CS limits\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	SET_IP(new_IP);
	g_cpubus.invalidate_pq();
}

void CPUExecutor::call_protected(uint16_t cs_raw, uint16_t disp)
{
	Selector   cs_selector;
	Descriptor cs_descriptor;

	/* new cs selector must not be null, else #GP(0) */
	if((cs_raw & SELECTOR_RPL_MASK) == 0) {
		PDEBUGF(LOG_V2, LOG_CPU,"call_protected: CS selector null\n");
		throw CPUException(CPU_GP_EXC, 0);
	}
	cs_selector = cs_raw;

	// check new CS selector index within its descriptor limits,
	// else #GP(new CS selector)
	try {
		cs_descriptor = g_cpucore.fetch_descriptor(cs_selector, CPU_GP_EXC);
	} catch(CPUException &e) {
		PDEBUGF(LOG_V2, LOG_CPU, "call_protected: descriptor fetch error\n");
		throw;
	}

	// examine AR byte of selected descriptor for various legal values
	if(!cs_descriptor.valid) {
		PDEBUGF(LOG_V2, LOG_CPU,"call_protected: invalid CS descriptor\n");
		throw CPUException(CPU_GP_EXC, cs_raw & SELECTOR_RPL_MASK);
	}

	if(cs_descriptor.segment) {  // normal segment

		CPUCore::check_CS(cs_raw, cs_descriptor, SELECTOR_RPL(cs_raw), CPL);

		uint16_t temp_SP = REG_SP;

		write_word_pmode(REG_SS, temp_SP - 2, REG_CS.sel.value);
		write_word_pmode(REG_SS, temp_SP - 4, REG_IP);
		temp_SP -= 4;

		// load code segment descriptor into CS cache
		// load CS with new code segment selector
		// set RPL of CS to CPL
		branch_far(cs_selector, cs_descriptor, disp, CPL);

		REG_SP = temp_SP;

		return;

	} else { // gate & special segment

		Descriptor  gate_descriptor = cs_descriptor;
		Selector    gate_selector = cs_selector;

		// descriptor DPL must be >= CPL else #GP(gate selector)
		if (gate_descriptor.dpl < CPL) {
			PDEBUGF(LOG_V2, LOG_CPU,"call_protected: descriptor.dpl < CPL\n");
			throw CPUException(CPU_GP_EXC, cs_raw & SELECTOR_RPL_MASK);
		}

		// descriptor DPL must be >= gate selector RPL else #GP(gate selector)
		if(gate_descriptor.dpl < gate_selector.rpl) {
			PDEBUGF(LOG_V2, LOG_CPU,"call_protected: descriptor.dpl < selector.rpl\n");
			throw CPUException(CPU_GP_EXC, cs_raw & SELECTOR_RPL_MASK);
		}

		switch (gate_descriptor.type) {
			case DESC_TYPE_AVAIL_TSS:
				PDEBUGF(LOG_V2, LOG_CPU, "call_protected: available TSS\n");
				if (!gate_descriptor.valid || gate_selector.ti) {
					PDEBUGF(LOG_V2, LOG_CPU,"call_protected: call bad TSS selector!\n");
					throw CPUException(CPU_GP_EXC, cs_raw & SELECTOR_RPL_MASK);
				}

				// TSS must be present, else #NP(TSS selector)
				if(!gate_descriptor.present) {
					PDEBUGF(LOG_V2, LOG_CPU,"call_protected: call not present TSS !\n");
					throw CPUException(CPU_NP_EXC, cs_raw & SELECTOR_RPL_MASK);
				}

				// SWITCH_TASKS _without_ nesting to TSS
				switch_tasks(gate_selector, gate_descriptor, CPU_TASK_FROM_CALL);
				return;

			case DESC_TYPE_TASK_GATE:
				task_gate(gate_selector, gate_descriptor, CPU_TASK_FROM_CALL);
				return;

			case DESC_TYPE_CALL_GATE:
				// gate descriptor must be present else #NP(gate selector)
				if(!gate_descriptor.present) {
					PDEBUGF(LOG_V2, LOG_CPU,"call_protected: gate not present\n");
					throw CPUException(CPU_NP_EXC, cs_raw & SELECTOR_RPL_MASK);
				}
				call_gate(gate_descriptor);
				return;

			default: // can't get here
				PDEBUGF(LOG_V2, LOG_CPU,"call_protected(): gate.type(%u) unsupported\n",
						(unsigned) gate_descriptor.type);
				throw CPUException(CPU_GP_EXC, cs_raw & SELECTOR_RPL_MASK);
		}
	}
}

void CPUExecutor::register_INT_trap(uint8_t _lo_vec, uint8_t _hi_vec, inttrap_fun_t _fn)
{
	m_inttraps_intervals.push_back(inttrap_interval_t(_lo_vec, _hi_vec, _fn));
	m_inttraps_tree = inttrap_intervalTree_t(m_inttraps_intervals);
}


/*******************************************************************************
 * AAA-ASCII Adjust AL After Addition
 */

void CPUExecutor::AAA()
{
	/* according to the original Intel's 286 manual, only AF and CF are modified
	 * but it seems OF,SF,ZF,PF are also updated in a specific way (they are not
	 * undefined).
	 * used the dosbox algo.
	 */
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
}


/*******************************************************************************
 * AAD-ASCII Adjust AX Before Division
 */

void CPUExecutor::AAD(uint8_t imm)
{
	//according to the Intel's 286 manual, the immediate value is always 0x0A.
	//in reality it can be anything.
	//see http://www.rcollins.org/secrets/opcodes/AAD.html
	uint16_t tmp = REG_AL + (imm * REG_AH);
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

void CPUExecutor::AAM(uint8_t imm)
{
	//according to the Intel's 286 manual the immediate value is always 0x0A.
	//in reality it can be anything.
	//see http://www.rcollins.org/secrets/opcodes/AAM.html
	if(imm == 0) {
		throw CPUException(CPU_DIV_ER_EXC, 0);
	}
	uint8_t al = REG_AL;
	REG_AH = al / imm;
	REG_AL = al % imm;

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

void CPUExecutor::ADC_eb_rb() { store_eb(ADC_b(load_eb(), load_rb())); }
void CPUExecutor::ADC_ew_rw() { store_ew(ADC_w(load_ew(), load_rw())); }
void CPUExecutor::ADC_rb_eb() { store_rb(ADC_b(load_rb(), load_eb())); }
void CPUExecutor::ADC_rw_ew() { store_rw(ADC_w(load_rw(), load_ew())); }
void CPUExecutor::ADC_AL_db(uint8_t imm) { REG_AL = ADC_b(REG_AL, imm); }
void CPUExecutor::ADC_AX_dw(uint16_t imm){ REG_AX = ADC_w(REG_AX, imm); }
void CPUExecutor::ADC_eb_db(uint8_t imm) { store_eb(ADC_b(load_eb(), imm)); }
void CPUExecutor::ADC_ew_dw(uint16_t imm){ store_ew(ADC_w(load_ew(), imm)); }
void CPUExecutor::ADC_ew_db(uint8_t imm) { store_ew(ADC_w(load_ew(), int8_t(imm))); }

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

void CPUExecutor::ADD_eb_rb()  { store_eb(ADD_b(load_eb(), load_rb())); }
void CPUExecutor::ADD_ew_rw()  { store_ew(ADD_w(load_ew(), load_rw())); }
void CPUExecutor::ADD_rb_eb()  { store_rb(ADD_b(load_rb(), load_eb())); }
void CPUExecutor::ADD_rw_ew()  { store_rw(ADD_w(load_rw(), load_ew())); }
void CPUExecutor::ADD_AL_db(uint8_t imm) { REG_AL = ADD_b(REG_AL, imm); }
void CPUExecutor::ADD_AX_dw(uint16_t imm){ REG_AX = ADD_w(REG_AX, imm); }
void CPUExecutor::ADD_eb_db(uint8_t imm) { store_eb(ADD_b(load_eb(), imm)); }
void CPUExecutor::ADD_ew_dw(uint16_t imm){ store_ew(ADD_w(load_ew(), imm)); }
void CPUExecutor::ADD_ew_db(uint8_t imm) { store_ew(ADD_w(load_ew(), int8_t(imm))); }


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

void CPUExecutor::AND_eb_rb() { store_eb(AND_b(load_eb(),load_rb())); }
void CPUExecutor::AND_ew_rw() { store_ew(AND_w(load_ew(),load_rw())); }
void CPUExecutor::AND_rb_eb() { store_rb(AND_b(load_rb(),load_eb())); }
void CPUExecutor::AND_rw_ew() { store_rw(AND_w(load_rw(),load_ew())); }
void CPUExecutor::AND_AL_db(uint8_t imm) { REG_AL = AND_b(REG_AL, imm); }
void CPUExecutor::AND_AX_dw(uint16_t imm){ REG_AX = AND_w(REG_AX, imm); }
void CPUExecutor::AND_eb_db(uint8_t imm) { store_eb(AND_b(load_eb(),imm)); }
void CPUExecutor::AND_ew_dw(uint16_t imm){ store_ew(AND_w(load_ew(),imm)); }
void CPUExecutor::AND_ew_db(uint8_t imm) { store_ew(AND_w(load_ew(), int8_t(imm))); }


/*******************************************************************************
 * ARPL-Adjust RPL Field of Selector
 */

void CPUExecutor::ARPL_ew_rw()
{
	if(!IS_PMODE()) {
		PDEBUGF(LOG_V2, LOG_CPU, "ARPL: not recognized in real mode\n");
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
	load_ed(bound_min, bound_max);

	if(op1 < int16_t(bound_min) || op1 > int16_t(bound_max)) {
		PDEBUGF(LOG_V2,LOG_CPU, "BOUND: fails bounds test\n");
		throw CPUException(CPU_BOUND_EXC, 0);
	}
}


/*******************************************************************************
 * CALL-Call Procedure
 */

void CPUExecutor::CALL_cw(uint16_t cw)
{
	/* push 16 bit EA of next instruction */
	stack_push(REG_IP);

	uint16_t new_IP = REG_IP + cw;
	branch_near(new_IP);
}

void CPUExecutor::CALL_ew()
{
	/* push 16 bit EA of next instruction */
	stack_push(REG_IP);

	uint16_t new_IP = load_ew();
	branch_near(new_IP);
}

void CPUExecutor::CALL_cd(uint16_t newip, uint16_t newcs)
{
	if(IS_PMODE()) {
		call_protected(newcs, newip);
		return;
	}

	//REAL mode
	stack_push(REG_CS.sel.value);
	stack_push(REG_IP);

	// CS LIMIT can't change when in real mode
	if(newip > GET_LIMIT(CS)) {
		PDEBUGF(LOG_V2, LOG_CPU, "CALL_cd: instruction pointer not within code segment limits\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	SET_CS(newcs);
	SET_IP(newip);
	g_cpubus.invalidate_pq();
}

void CPUExecutor::CALL_ed()
{
	uint16_t newip, newcs;
	load_ed(newip, newcs);

	CALL_cd(newip, newcs);
}


/*******************************************************************************
 * CBW-Convert Byte into Word
 */

void CPUExecutor::CBW()
{
	/* CBW: no flags are effected */
	REG_AX = int8_t(REG_AL);
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
	if(IS_PMODE() && (FLAG_IOPL < CPL)) {
		PDEBUGF(LOG_V2, LOG_CPU, "CLI: IOPL < CPL in protected mode\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	SET_FLAG(IF, false);
}

void CPUExecutor::CLTS()
{
	// CPL is always 0 in real mode
	if(IS_PMODE() && CPL != 0) {
		PDEBUGF(LOG_V2, LOG_CPU, "CLTS: priveledge check failed\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	SET_MSW(MSW_TS, false);
}


/*******************************************************************************
 * CMC-Complement Carry Flag
 */

void CPUExecutor::CMC()
{
	SET_FLAG(CF, !FLAG_CF);
}


/*******************************************************************************
 * CWD-Convert Word to Doubleword
 */

void CPUExecutor::CWD()
{
	if(REG_AX & 0x8000) {
		REG_DX = 0xFFFF;
	} else {
		REG_DX = 0;
	}
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

void CPUExecutor::CMP_eb_rb() { CMP_b(load_eb(), load_rb()); }
void CPUExecutor::CMP_ew_rw() { CMP_w(load_ew(), load_rw()); }
void CPUExecutor::CMP_rb_eb() { CMP_b(load_rb(), load_eb()); }
void CPUExecutor::CMP_rw_ew() { CMP_w(load_rw(), load_ew()); }
void CPUExecutor::CMP_AL_db(uint8_t imm) { CMP_b(REG_AL, imm); }
void CPUExecutor::CMP_AX_dw(uint16_t imm){ CMP_w(REG_AX, imm); }
void CPUExecutor::CMP_eb_db(uint8_t imm) { CMP_b(load_eb(), imm); }
void CPUExecutor::CMP_ew_dw(uint16_t imm){ CMP_w(load_ew(), imm); }
void CPUExecutor::CMP_ew_db(uint8_t imm) { CMP_w(load_ew(), int8_t(imm)); }


/*******************************************************************************
 * CMPS/CMPSB/CMPSW-Compare string operands
 */

void CPUExecutor::CMPSB()
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

void CPUExecutor::CMPSW()
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


/*******************************************************************************
 * DAA/DAS-Decimal Adjust AL after addition/subtraction
 */

void CPUExecutor::DAA()
{
	if(((REG_AL & 0x0F) > 9) || FLAG_AF) {
		REG_AL += 6;
		SET_FLAG(AF, true);
	} else {
		SET_FLAG(AF, false);
	}
	if((REG_AL > 0x9F) || FLAG_CF) {
		REG_AL += 0x60;
		SET_FLAG(CF, true);
	} else {
		SET_FLAG(CF, false);
	}
	SET_FLAG(SF, REG_AL & 0x80);
	SET_FLAG(ZF, REG_AL == 0);
	SET_FLAG(PF, PARITY(REG_AL));
}

void CPUExecutor::DAS()
{
	if(((REG_AL & 0x0F) > 9) || FLAG_AF) {
		REG_AL -= 6;
		SET_FLAG(AF, true);
	} else {
		SET_FLAG(AF, false);
	}
	if((REG_AL > 0x9F) || FLAG_CF) {
		REG_AL -= 0x60;
		SET_FLAG(CF, true);
	} else {
		SET_FLAG(CF, false);
	}
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

void CPUExecutor::DEC_ew()
{
	uint16_t op1 = load_ew();
	uint16_t res = op1 - 1;
	store_ew(res);

	SET_FLAG(OF, res == 0x7fff);
	SET_FLAG(SF, res & 0x8000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, (res & 0x0f) == 0x0f);
	SET_FLAG(PF, PARITY(res));
}

void CPUExecutor::DEC_rw()
{
	uint16_t op1 = GEN_REG(m_instr->reg).word[0];
	uint16_t res = op1 - 1;
	GEN_REG(m_instr->reg).word[0] = res;

	SET_FLAG(OF, res == 0x7fff);
	SET_FLAG(SF, res & 0x8000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, (res & 0x0f) == 0x0f);
	SET_FLAG(PF, PARITY(res));
}


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


/*******************************************************************************
 * ENTER-Make Stack Frame for Procedure Parameters
 */

void CPUExecutor::ENTER(uint16_t bytes, uint8_t level)
{
	level &= 0x1F;

	stack_push(REG_BP);

	uint16_t frame_ptr16 = REG_SP;
	uint16_t bp = REG_BP;

	if (level > 0) {
		/* do level-1 times */
		while(--level) {
			bp -= 2;
			uint16_t temp16 = read_word_nocheck(REG_SS, bp);
			stack_push(temp16);
		}

		/* push(frame pointer) */
		stack_push(frame_ptr16);
	}

	REG_SP -= bytes;

	// ENTER finishes with memory write check on the final stack pointer
	// the memory is touched but no write actually occurs
	// emulate it by doing RMW read access from SS:SP
	//read_RMW_virtual_word_32(REG_SS, SP); TODO?
	//according to the intel docs the only exc is #SS(0) if SP were to go outside
	//of the stack limit (already checked in stack_push())

	REG_BP = frame_ptr16;
}


/*******************************************************************************
 * FPU ESC
 * this function should be used only if there's no FPU installed (TODO?)
 */

void CPUExecutor::FPU_ESC()
{
	if(GET_MSW(MSW_EM) || GET_MSW(MSW_TS)) {
		throw CPUException(CPU_NM_EXC, 0);
	}
}


/*******************************************************************************
 * HLT-Halt
 */

void CPUExecutor::HLT()
{
	// CPL is always 0 in real mode
	if(IS_PMODE() && CPL != 0) { //pmode
		PDEBUGF(LOG_V2,LOG_CPU,
			"HLT: pmode priveledge check failed, CPL=%d\n", CPL);
		throw CPUException(CPU_GP_EXC, 0);
	}

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


/*******************************************************************************
 * IMUL-Signed Multiply
 */

void CPUExecutor::IMUL_eb()
{
	int8_t op1 = int8_t(REG_AL);
	int8_t op2 = int8_t(load_eb());

	int16_t product_16 = op1 * op2;

	/* now write product back to destination */
	REG_AX = product_16;

	/* IMUL r/m8: condition for clearing CF & OF:
	 *   AX = sign-extend of AL to 16 bits
	 */
	if((product_16 & 0xff80)==0xff80 || (product_16 & 0xff80)==0x0000) {
		SET_FLAG(CF, false);
		SET_FLAG(OF, false);
	} else {
		SET_FLAG(CF, true);
		SET_FLAG(OF, true);
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
	if(((product_32 & 0xffff8000)==0xffff8000 || (product_32 & 0xffff8000)==0x0000)) {
		SET_FLAG(CF, false);
		SET_FLAG(OF, false);
	} else {
		SET_FLAG(CF, true);
		SET_FLAG(OF, true);
	}
}

void CPUExecutor::IMUL_rw_ew_dw(uint16_t imm16)
{
	int16_t op2_16 = int16_t(load_ew());
	int16_t op3_16 = int16_t(imm16);

	int32_t product_32  = op2_16 * op3_16;
	uint16_t product_16 = (product_32 & 0xFFFF);

	/* now write product back to destination */
	store_rw(product_16);

	/* IMUL r16,r/m16,imm16: condition for clearing CF & OF:
	 * Carry and overflow are set to 0 if the result fits in a signed word
	 * (between -32768 and +32767, inclusive); they are set to 1 otherwise.
	 */
	if((product_32 >= -32768) && (product_32 <= 32767)) {
		SET_FLAG(CF, false);
		SET_FLAG(OF, false);
	} else {
		SET_FLAG(CF, true);
		SET_FLAG(OF, true);
	}
}



/*******************************************************************************
 * Input from Port
 */

void CPUExecutor::IN_AL_db(uint8_t port)
{
	if(IS_PMODE() && (CPL > FLAG_IOPL)) {
		/* #GP(O) if the current privilege level is bigger (has less privilege)
		 * than IOPL; which is the privilege level found in the flags register.
		 */
		PDEBUGF(LOG_V2, LOG_CPU, "IN_AL_db: I/O access not allowed!\n");
		throw CPUException(CPU_GP_EXC, 0);
	}
	REG_AL = g_devices.read_byte(port);
}

void CPUExecutor::IN_AL_DX()
{
	if(IS_PMODE() && (CPL > FLAG_IOPL)) {
	    PDEBUGF(LOG_V2, LOG_CPU, "IN_AL_DX: I/O access not allowed!\n");
	    throw CPUException(CPU_GP_EXC, 0);
	}
	REG_AL = g_devices.read_byte(REG_DX);
}

void CPUExecutor::IN_AX_db(uint8_t port)
{
	if(IS_PMODE() && (CPL > FLAG_IOPL)) {
	    PDEBUGF(LOG_V2, LOG_CPU, "IN_AX_db: I/O access not allowed!\n");
	    throw CPUException(CPU_GP_EXC, 0);
	}
	REG_AX = g_devices.read_word(port);
}

void CPUExecutor::IN_AX_DX()
{
	if(IS_PMODE() && (CPL > FLAG_IOPL)) {
	    PDEBUGF(LOG_V2, LOG_CPU, "IN_AX_DX: I/O access not allowed!\n");
	    throw CPUException(CPU_GP_EXC, 0);
	}
	REG_AX = g_devices.read_word(REG_DX);
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

void CPUExecutor::INC_ew()
{
	uint16_t op1 = load_ew();
	uint16_t res = op1 + 1;
	store_ew(res);

	SET_FLAG(OF, res == 0x8000);
	SET_FLAG(SF, res & 0x8000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, (res & 0x0f) == 0);
	SET_FLAG(PF, PARITY(res));
}

void CPUExecutor::INC_rw()
{
	uint16_t op1 = GEN_REG(m_instr->reg).word[0];
	uint16_t res = op1 + 1;
	GEN_REG(m_instr->reg).word[0] = res;

	SET_FLAG(OF, res == 0x8000);
	SET_FLAG(SF, res & 0x8000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, (res & 0x0f) == 0);
	SET_FLAG(PF, PARITY(res));
}


/*******************************************************************************
 * INSB/INSW-Input from Port to String
 */

void CPUExecutor::INSB()
{
	// trigger any segment faults before reading from IO port
	if(IS_PMODE()) {
		if(CPL > FLAG_IOPL) {
			PDEBUGF(LOG_V2, LOG_CPU, "INSB: I/O access not allowed!\n");
			throw CPUException(CPU_GP_EXC, 0);
		}
		write_check_pmode(REG_ES, REG_DI, 1);
	} else {
		write_check_rmode(REG_ES, REG_DI, 1);
	}

	uint8_t value = g_devices.read_byte(REG_DX);

	/*
	The memory operand must be addressable from the ES register; no segment override is
	possible.
	*/
	write_byte_nocheck(REG_ES, REG_DI, value);

	if(FLAG_DF) {
		REG_DI -= 1;
	} else {
		REG_DI += 1;
	}
}

void CPUExecutor::INSW()
{
	// trigger any segment faults before reading from IO port
	if(IS_PMODE()) {
		if(CPL > FLAG_IOPL) {
			PDEBUGF(LOG_V2, LOG_CPU, "INSW: I/O access not allowed!\n");
			throw CPUException(CPU_GP_EXC, 0);
		}
		write_check_pmode(REG_ES, REG_DI, 2);
	} else {
		write_check_rmode(REG_ES, REG_DI, 2);
	}

	uint16_t value = g_devices.read_word(REG_DX);

	write_word_nocheck(REG_ES, REG_DI, value);

	if(FLAG_DF) {
		REG_DI -= 2;
	} else {
		REG_DI += 2;
	}
}


/*******************************************************************************
 * INT/INTO-Call to Interrupt Procedure
 */

bool CPUExecutor::INT_debug(bool call, uint8_t vector, uint16_t ax, CPUCore *core, Memory *mem)
{
	const char * str = CPUDebugger::INT_decode(call, vector, ax, core, mem);
	if(str != NULL) {
		PINFOF(LOG_V1, LOG_CPU, "%s\n", str);
	}
	return true;
}

void CPUExecutor::INT(uint8_t _vector, unsigned _type)
{
	uint8_t ah = REG_AH;
	uint32_t retaddr = GET_PHYADDR(CS, REG_IP);

	if(INT_TRAPS) {
		std::vector<inttrap_interval_t> results;
		m_inttraps_tree.findOverlapping(_vector, _vector, results);
		if(!results.empty()) {
			bool res = false;
			auto retinfo = m_inttraps_ret.insert(
				pair<uint32_t, std::vector<std::function<bool()>>>(
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

	//DOS 2+ - EXEC - LOAD AND/OR EXECUTE PROGRAM
	if(_vector == 0x21 && ah == 0x4B) {
		char * pname = (char*)g_memory.get_phy_ptr(GET_PHYADDR(DS, REG_DX));
		PDEBUGF(LOG_V1, LOG_CPU, "exec %s\n", pname);
		g_machine.DOS_program_launch(pname);
		m_dos_prg.push(std::pair<uint32_t,std::string>(retaddr,pname));
		//can the cpu be in pmode?
		if(!CPULOG || CPULOG_INT21_EXIT_IP==-1 || IS_PMODE()) {
			g_machine.DOS_program_start(pname);
		} else {
			//find the INT exit point
			uint32_t cs = g_cpubus.mem_read_word(0x21*4 + 2);
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

void CPUExecutor::INT3() { INT(3, CPU_SOFTWARE_EXCEPTION); }
void CPUExecutor::INT_db(uint8_t vector) { INT(vector, CPU_SOFTWARE_INTERRUPT); }
void CPUExecutor::INTO() { if(FLAG_OF) INT(4, CPU_SOFTWARE_EXCEPTION); }


/*******************************************************************************
 * IRET-Interrupt Return
 */

void CPUExecutor::IRET()
{
	g_cpu.unmask_event(CPU_EVENT_NMI);

	if(IS_PMODE()) {
		IRET_pmode();
	} else {
		uint16_t ip     = stack_pop();
		uint16_t cs_raw = stack_pop(); // #SS has higher priority
		uint16_t flags  = stack_pop();

		// CS LIMIT can't change when in real mode
		if(ip > REG_CS.desc.limit) {
			PDEBUGF(LOG_V2, LOG_CPU,
				"IRET: instruction pointer not within code segment limits\n");
			throw CPUException(CPU_GP_EXC, 0);
		}
		SET_CS(cs_raw);
		SET_IP(ip);
		write_flags(flags,false,true,false);
	}
	g_cpubus.invalidate_pq();
}

void CPUExecutor::IRET_pmode()
{
	Selector cs_selector, ss_selector;
	Descriptor cs_descriptor, ss_descriptor;

	if(FLAG_NT)   /* NT = 1: RETURN FROM NESTED TASK */
	{
		/* what's the deal with NT ? */
		Selector   link_selector;
		Descriptor tss_descriptor;

		PDEBUGF(LOG_V2, LOG_CPU, "IRET: nested task return\n");

		if(!REG_TR.desc.valid)
			PERRF_ABORT(LOG_CPU, "IRET: TR not valid!\n");

		// examine back link selector in TSS addressed by current TR
		link_selector = g_cpubus.mem_read_word(REG_TR.desc.base);

		// must specify global, else #TS(new TSS selector)
		if(link_selector.ti) {
			PDEBUGF(LOG_V2, LOG_CPU, "IRET: link selector.ti=1\n");
			throw CPUException(CPU_TS_EXC, link_selector.value & SELECTOR_RPL_MASK);
		}

		// index must be within GDT limits, else #TS(new TSS selector)
		tss_descriptor = g_cpucore.fetch_descriptor(link_selector, CPU_TS_EXC);

		if(!tss_descriptor.valid || tss_descriptor.segment) {
			PDEBUGF(LOG_V2, LOG_CPU, "IRET: TSS selector points to bad TSS\n");
			throw CPUException(CPU_TS_EXC, link_selector.value & SELECTOR_RPL_MASK);
		}
		// AR byte must specify TSS, else #TS(new TSS selector)
		// new TSS must be busy, else #TS(new TSS selector)
		if(tss_descriptor.type != DESC_TYPE_BUSY_TSS) {
			PDEBUGF(LOG_V2, LOG_CPU, "IRET: TSS selector points to bad TSS\n");
			throw CPUException(CPU_TS_EXC, link_selector.value & SELECTOR_RPL_MASK);
		}

		// TSS must be present, else #NP(new TSS selector)
		if(!tss_descriptor.present) {
			PDEBUGF(LOG_V2, LOG_CPU, "IRET: task descriptor.p == 0\n");
			throw CPUException(CPU_NP_EXC, link_selector.value & SELECTOR_RPL_MASK);
		}

		// switch tasks (without nesting) to TSS specified by back link selector
		switch_tasks(link_selector, tss_descriptor, CPU_TASK_FROM_IRET);
		return;
	}

	/* NT = 0: INTERRUPT RETURN ON STACK */
	const unsigned top_nbytes_same = 6;
	uint16_t new_sp, new_ip = 0, new_flags = 0;

	/*
	* SS     SP+8
	* SP     SP+6
	* -----------
	* FLAGS  SP+4
	* CS     SP+2
	* IP     SP+0
	*/

	new_flags   = stack_read(REG_SP + 4);
    cs_selector = stack_read(REG_SP + 2);
    new_ip      = stack_read(REG_SP + 0);

	// return CS selector must be non-null, else #GP(0)
	if((cs_selector.value & SELECTOR_RPL_MASK) == 0) {
		PDEBUGF(LOG_V2, LOG_CPU, "IRET: return CS selector null\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	// selector index must be within descriptor table limits,
	// else #GP(return selector)
	cs_descriptor = g_cpucore.fetch_descriptor(cs_selector, CPU_GP_EXC);

	// return CS selector RPL must be >= CPL, else #GP(return selector)
	if(cs_selector.rpl < CPL) {
		PDEBUGF(LOG_V2, LOG_CPU, "iret: return selector RPL < CPL\n");
		throw CPUException(CPU_GP_EXC, cs_selector.value & SELECTOR_RPL_MASK);
	}

	// check code-segment descriptor
	CPUCore::check_CS(cs_selector.value,cs_descriptor,0,cs_selector.rpl);

	if(cs_selector.rpl == CPL) { /* INTERRUPT RETURN TO SAME LEVEL */

		/* top 6 bytes on stack must be within limits, else #SS(0) */
		/* satisfied above */

		/* load CS-cache with new code segment descriptor */
		branch_far(cs_selector, cs_descriptor, new_ip, cs_selector.rpl);

		/* load flags with third word on stack */
		write_flags(new_flags, CPL==0, CPL<=FLAG_IOPL);

		/* increment stack by 6 */
		REG_SP += top_nbytes_same;
		return;

	} else { /* INTERRUPT RETURN TO OUTER PRIVILEGE LEVEL */

		/*
		 * SS     SP+8
		 * SP     SP+6
		 * FLAGS  SP+4
		 * CS     SP+2
		 * IP     SP+0
		 */

		/* examine return SS selector and associated descriptor */

		ss_selector = stack_read(REG_SP + 8);

		/* selector must be non-null, else #GP(0) */
		if((ss_selector.value & SELECTOR_RPL_MASK) == 0) {
			PDEBUGF(LOG_V2, LOG_CPU, "IRET: SS selector null\n");
			throw CPUException(CPU_GP_EXC, 0);
		}

		/* selector RPL must = RPL of return CS selector,
		 * else #GP(SS selector) */
		if(ss_selector.rpl != cs_selector.rpl) {
			PDEBUGF(LOG_V2, LOG_CPU, "IRET: SS.rpl != CS.rpl\n");
			throw CPUException(CPU_GP_EXC, ss_selector.value & SELECTOR_RPL_MASK);
		}

		/* selector index must be within its descriptor table limits,
		 * else #GP(SS selector) */
		ss_descriptor = g_cpucore.fetch_descriptor(ss_selector, CPU_GP_EXC);

		/* AR byte must indicate a writable data segment,
		 * else #GP(SS selector) */
		if(!ss_descriptor.valid || !ss_descriptor.segment ||
			ss_descriptor.is_code_segment() ||
			!ss_descriptor.is_data_segment_writeable()
		)
		{
			PDEBUGF(LOG_V2, LOG_CPU, "iret: SS AR byte not writable or code segment\n");
			throw CPUException(CPU_GP_EXC, ss_selector.value & SELECTOR_RPL_MASK);
		}

		/* stack segment DPL must equal the RPL of the return CS selector,
		 * else #GP(SS selector) */
		if(ss_descriptor.dpl != cs_selector.rpl) {
			PDEBUGF(LOG_V2, LOG_CPU, "iret: SS.dpl != CS selector RPL\n");
			throw CPUException(CPU_GP_EXC, ss_selector.value & SELECTOR_RPL_MASK);
		}

		/* SS must be present, else #NP(SS selector) */
		if(!ss_descriptor.present) {
			PDEBUGF(LOG_V2, LOG_CPU, "IRET: SS not present!\n");
			throw CPUException(CPU_NP_EXC, ss_selector.value & SELECTOR_RPL_MASK);
		}

		new_ip    = stack_read(REG_SP + 0);
		new_flags = stack_read(REG_SP + 4);
		new_sp    = stack_read(REG_SP + 6);

		bool change_IF = (CPL <= FLAG_IOPL);
		bool change_IOPL = (CPL == 0);

		/* load CS:EIP from stack */
		/* load the CS-cache with CS descriptor */
		/* set CPL to the RPL of the return CS selector */
		branch_far(cs_selector, cs_descriptor, new_ip, cs_selector.rpl);

		// IF only changed if (prev_CPL <= FLAGS.IOPL)
		// IOPL only changed if prev_CPL == 0
		write_flags(new_flags, change_IOPL, change_IF);

		// load SS:SP from stack
		// load the SS-cache with SS descriptor
		SET_SS(ss_selector, ss_descriptor, cs_selector.rpl);
		REG_SP = new_sp;

		REG_ES.validate();
		REG_DS.validate();
	}
}


/*******************************************************************************
 * Jcond-Jump Short If Condition Met
 */

void CPUExecutor::JA_cb(int8_t disp)  { if(!FLAG_CF && !FLAG_ZF){branch_near(REG_IP + disp);} }
void CPUExecutor::JBE_cb(int8_t disp) { if(FLAG_CF || FLAG_ZF)  {branch_near(REG_IP + disp);} }
void CPUExecutor::JC_cb(int8_t disp)  { if(FLAG_CF) {branch_near(REG_IP + disp);} }
void CPUExecutor::JNC_cb(int8_t disp) { if(!FLAG_CF){branch_near(REG_IP + disp);} }
void CPUExecutor::JE_cb(int8_t disp)  { if(FLAG_ZF) {branch_near(REG_IP + disp);} }
void CPUExecutor::JNE_cb(int8_t disp) { if(!FLAG_ZF){branch_near(REG_IP + disp);} }
void CPUExecutor::JO_cb(int8_t disp)  { if(FLAG_OF) {branch_near(REG_IP + disp);} }
void CPUExecutor::JNO_cb(int8_t disp) { if(!FLAG_OF){branch_near(REG_IP + disp);} }
void CPUExecutor::JPE_cb(int8_t disp) { if(FLAG_PF) {branch_near(REG_IP + disp);} }
void CPUExecutor::JPO_cb(int8_t disp) { if(!FLAG_PF){branch_near(REG_IP + disp);} }
void CPUExecutor::JS_cb(int8_t disp)  { if(FLAG_SF) {branch_near(REG_IP + disp);} }
void CPUExecutor::JNS_cb(int8_t disp) { if(!FLAG_SF){branch_near(REG_IP + disp);} }
void CPUExecutor::JL_cb(int8_t disp)  { if((FLAG_SF!=0) != (FLAG_OF!=0)) {branch_near(REG_IP + disp);} }
void CPUExecutor::JNL_cb(int8_t disp) { if((FLAG_SF!=0) == (FLAG_OF!=0)) {branch_near(REG_IP + disp);} }
void CPUExecutor::JLE_cb(int8_t disp) { if(FLAG_ZF || ((FLAG_SF!=0) != (FLAG_OF!=0))) {branch_near(REG_IP + disp);} }
void CPUExecutor::JNLE_cb(int8_t disp){ if(!FLAG_ZF && ((FLAG_SF!=0) == (FLAG_OF!=0))) {branch_near(REG_IP + disp);} }
void CPUExecutor::JCXZ_cb(int8_t disp){ if(REG_CX==0) {branch_near(REG_IP + disp);} }


/*******************************************************************************
 * JMP-Jump
 */

void CPUExecutor::JMP_pmode(uint16_t _cs, uint16_t _disp)
{
	//see jmp_far.cc/jump_protected

	Descriptor  descriptor;
	Selector    selector;

	/* destination selector is not null else #GP(0) */
	if((_cs & SELECTOR_RPL_MASK) == 0) {
		PDEBUGF(LOG_V2, LOG_CPU,"JMP_far_pmode: cs == 0\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	selector = _cs;

	/* destination selector index is within its descriptor table
	 * limits else #GP(selector)
	 */
	descriptor = g_cpucore.fetch_descriptor(selector, CPU_GP_EXC);

	/* examine AR byte of destination selector for legal values: */
	if(descriptor.segment) {
		CPUCore::check_CS(selector, descriptor, selector.rpl, CPL);
		branch_far(selector, descriptor, _disp, CPL);
		return;
	} else {
		// call gate DPL must be >= CPL else #GP(gate selector)
		if(descriptor.dpl < CPL) {
			PDEBUGF(LOG_V2, LOG_CPU,"JMP_pmode: call gate.dpl < CPL\n");
			throw CPUException(CPU_GP_EXC, _cs & SELECTOR_RPL_MASK);
		}

		// call gate DPL must be >= gate selector RPL else #GP(gate selector)
		if(descriptor.dpl < selector.rpl) {
			PDEBUGF(LOG_V2, LOG_CPU,"JMP_pmode: call gate.dpl < selector.rpl\n");
			throw CPUException(CPU_GP_EXC, _cs & SELECTOR_RPL_MASK);
		}

		switch(descriptor.type) {
			case DESC_TYPE_AVAIL_TSS:
				PDEBUGF(LOG_V2,LOG_CPU,"JMP_pmode: jump to TSS\n");

				if(!descriptor.valid || selector.ti) {
					PDEBUGF(LOG_V2, LOG_CPU,"JMP_pmode: jump to bad TSS selector\n");
					throw CPUException(CPU_GP_EXC, _cs & SELECTOR_RPL_MASK);
				}

				// TSS must be present, else #NP(TSS selector)
				if(!descriptor.present) {
					PDEBUGF(LOG_V2, LOG_CPU,"JMP_pmode: jump to not present TSS\n");
					throw CPUException(CPU_NP_EXC, _cs & SELECTOR_RPL_MASK);
				}

				// SWITCH_TASKS _without_ nesting to TSS
				switch_tasks(selector, descriptor, CPU_TASK_FROM_JUMP);
				return;
			case DESC_TYPE_TASK_GATE:
				task_gate(selector, descriptor, CPU_TASK_FROM_JUMP);
				return;
			case DESC_TYPE_CALL_GATE:
				JMP_call_gate(selector, descriptor);
				return;
			default:
				PDEBUGF(LOG_V2, LOG_CPU,"JMP_pmode: gate type %u unsupported\n", descriptor.type);
				throw CPUException(CPU_GP_EXC, _cs & SELECTOR_RPL_MASK);
		}
	}
}

void CPUExecutor::JMP_call_gate(Selector &selector, Descriptor &gate_descriptor)
{
	Selector   gate_cs_selector;
	Descriptor gate_cs_descriptor;

	// task gate must be present else #NP(gate selector)
	if(!gate_descriptor.present) {
		PERRF(LOG_CPU,"JMP_call_gate: call gate not present!\n");
		throw CPUException(CPU_NP_EXC, selector.value & SELECTOR_RPL_MASK);
	}

	gate_cs_selector = gate_descriptor.selector;

	// examine selector to code segment given in call gate descriptor
	// selector must not be null, else #GP(0)
	if((gate_cs_selector.value & SELECTOR_RPL_MASK) == 0) {
		PERRF(LOG_CPU,"JMP_call_gate: CS selector null\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	// selector must be within its descriptor table limits else #GP(CS selector)
	gate_cs_descriptor = g_cpucore.fetch_descriptor(gate_cs_selector, CPU_GP_EXC);

	// check code-segment descriptor
	CPUCore::check_CS(gate_cs_selector, gate_cs_descriptor, 0, CPL);

	uint16_t newIP = gate_descriptor.offset;
	branch_far(gate_cs_selector, gate_cs_descriptor, newIP, CPL);
}

void CPUExecutor::JMP_ew()
{
	uint16_t newip = load_ew();
	branch_near(newip);
}

void CPUExecutor::JMP_ed()
{
	uint16_t disp, cs;
	load_ed(disp, cs);

	if(!IS_PMODE()) {
		branch_far(cs, disp);
	} else {
		JMP_pmode(cs, disp);
	}
}

void CPUExecutor::JMP_cb(int8_t offset)
{
	uint16_t new_IP = REG_IP + offset;
	branch_near(new_IP);
}

void CPUExecutor::JMP_cw(uint16_t offset)
{
	branch_near(REG_IP + offset);
}

void CPUExecutor::JMP_cd(uint16_t selector, uint16_t offset)
{
	if(!IS_PMODE()) {
		branch_far(selector, offset);
	} else {
		JMP_pmode(selector, offset);
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

void CPUExecutor::LAR_rw_ew()
{
	Descriptor descriptor;
	Selector   selector;

	if(!IS_PMODE()) {
		PDEBUGF(LOG_V2, LOG_CPU, "LAR: not recognized in real mode\n");
		throw CPUException(CPU_UD_EXC, 0);
	}

	selector = load_ew();

	/* if selector null, clear ZF and done */
	if((selector.value & SELECTOR_RPL_MASK) == 0) {
		SET_FLAG(ZF, false);
		return;
	}

	try {
		descriptor = g_cpucore.fetch_descriptor(selector,0);
	} catch(CPUException &e) {
		//this fetch does not throw an exception
		PDEBUGF(LOG_V2, LOG_CPU, "LAR: failed to fetch descriptor\n");
		SET_FLAG(ZF, false);
		return;
	}

	if(!descriptor.valid) {
		PDEBUGF(LOG_V2, LOG_CPU, "LAR: descriptor not valid\n");
		SET_FLAG(ZF, false);
		return;
	}

	/* if source selector is visible at CPL & RPL,
	 * within the descriptor table, and of type accepted by LAR instruction,
	 * then load register with segment limit and set ZF
	 */

	if(descriptor.segment) { /* normal segment */
		if(descriptor.is_code_segment() && descriptor.is_code_segment_conforming()) {
			/* ignore DPL for conforming segments */
		} else {
			if(descriptor.dpl < CPL || descriptor.dpl < selector.rpl) {
				SET_FLAG(ZF, false);
				return;
			}
		}
	} else { /* system or gate segment */
		switch(descriptor.type) {
			case DESC_TYPE_AVAIL_TSS:
			case DESC_TYPE_BUSY_TSS:
			case DESC_TYPE_CALL_GATE:
			case DESC_TYPE_TASK_GATE:
			case DESC_TYPE_LDT_DESC:
				break;
			default: /* rest not accepted types to LAR */
				PDEBUGF(LOG_V2, LOG_CPU, "LAR: not accepted descriptor type\n");
				SET_FLAG(ZF, false);
				return;
		}

		if(descriptor.dpl < CPL || descriptor.dpl < selector.rpl) {
			SET_FLAG(ZF, false);
			return;
		}
	}

	SET_FLAG(ZF, true);
	uint16_t value = uint16_t(descriptor.ar)<<8;
	store_rw(value);
}


/*******************************************************************************
 * LDS/LES-Load Doubleword Pointer
 */

void CPUExecutor::LDS_rw_ed()
{
	uint16_t reg, ds;
	load_ed(reg, ds);

	SET_DS(ds);
	store_rw(reg);
}

void CPUExecutor::LES_rw_ed()
{
	uint16_t reg, es;
	load_ed(reg, es);

	SET_ES(es);
	store_rw(reg);
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
	uint16_t offset = EA_get_offset();
	store_rw(offset);
}


/*******************************************************************************
 * LEAVE-High Level Procedure Exit
 */

void CPUExecutor::LEAVE()
{
	REG_SP = REG_BP;
	REG_BP = stack_pop();
}


/*******************************************************************************
 * LGDT/LIDT/LLDT-Load Global/Interrupt/Local Descriptor Table Register
 */

void CPUExecutor::LGDT()
{
	// CPL is always 0 is real mode
	if(IS_PMODE() && CPL != 0) {
		PDEBUGF(LOG_V2, LOG_CPU, "LGDT: CPL != 0 causes #GP\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	SegReg & sr = EA_get_segreg();
	uint16_t off = EA_get_offset();

	uint16_t limit = read_word(sr, off);
	uint32_t base = read_dword(sr, off+2) & 0x00ffffff;

	SET_GDTR(base, limit);
}

void CPUExecutor::LIDT()
{
	// CPL is always 0 is real mode
	if(IS_PMODE() && CPL != 0) {
		PDEBUGF(LOG_V2, LOG_CPU, "LIDT: CPL != 0 causes #GP\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	SegReg & sr = EA_get_segreg();
	uint16_t off = EA_get_offset();

	uint16_t limit = read_word(sr, off);
	uint32_t base = read_dword(sr, off+2) & 0x00ffffff;

	if(limit==0) {
		PDEBUGF(LOG_V2, LOG_CPU, "LIDT: base 0x%06X, limit 0x%04X\n", base, limit);
	}

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
	descriptor = g_cpucore.fetch_descriptor(selector, CPU_GP_EXC);

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
 * LMSW-Load Machine Status Word
 */

void CPUExecutor::LMSW_ew()
{
	uint16_t msw;

	// CPL is always 0 in real mode
	if(IS_PMODE() && CPL != 0) {
		PDEBUGF(LOG_V2, LOG_CPU, "LMSW: CPL!=0\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	msw = load_ew();

	// LMSW cannot clear PE
	if(GET_MSW(MSW_PE)) {
		msw |= MSW_PE; // adjust PE bit to current value of 1
	} else if(msw & MSW_PE) {
		PDEBUGF(LOG_V2, LOG_CPU, "now in Protected Mode\n");
	}

	SET_MSW(msw);
}


/*******************************************************************************
 * LOADALL-Load registers from memory
 */

void CPUExecutor::LOADALL()
{
	/* Undocumented
	 * From 15-page Intel document titled "Undocumented iAPX 286 Test Instruction"
	 * http://www.rcollins.org/articles/loadall/tspec_a3_doc.html
	 */

	uint16_t word_reg;
	uint16_t desc_cache[3];
	uint32_t base,limit;

	if(IS_PMODE() && (CPL != 0)) {
		PDEBUGF(LOG_V2, LOG_CPU, "LOADALL: CPL != 0 causes #GP\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	PDEBUGF(LOG_V2, LOG_CPU, "LOADALL\n");

	word_reg = g_cpubus.mem_read_word(0x806);
	if(GET_MSW(MSW_PE)) {
		word_reg |= MSW_PE; // adjust PE bit to current value of 1
	}
	SET_MSW(word_reg);

	REG_TR.sel = g_cpubus.mem_read_word(0x816);
	SET_FLAGS(g_cpubus.mem_read_word(0x818));
	SET_IP(g_cpubus.mem_read_word(0x81A));
	REG_LDTR.sel = g_cpubus.mem_read_word(0x81C);
	REG_DS.sel = g_cpubus.mem_read_word(0x81E);
	REG_SS.sel = g_cpubus.mem_read_word(0x820);
	REG_CS.sel = g_cpubus.mem_read_word(0x822);
	REG_ES.sel = g_cpubus.mem_read_word(0x824);
	REG_DI = g_cpubus.mem_read_word(0x826);
	REG_SI = g_cpubus.mem_read_word(0x828);
	REG_BP = g_cpubus.mem_read_word(0x82A);
	REG_SP = g_cpubus.mem_read_word(0x82C);
	REG_BX = g_cpubus.mem_read_word(0x82E);
	REG_DX = g_cpubus.mem_read_word(0x830);
	REG_CX = g_cpubus.mem_read_word(0x832);
	REG_AX = g_cpubus.mem_read_word(0x834);

	desc_cache[0] = g_cpubus.mem_read_word(0x836);
	desc_cache[1] = g_cpubus.mem_read_word(0x838);
	desc_cache[2] = g_cpubus.mem_read_word(0x83A);
	REG_ES.desc.set_from_cache(desc_cache);

	desc_cache[0] = g_cpubus.mem_read_word(0x83C);
	desc_cache[1] = g_cpubus.mem_read_word(0x83E);
	desc_cache[2] = g_cpubus.mem_read_word(0x840);
	REG_CS.desc.set_from_cache(desc_cache);

	desc_cache[0] = g_cpubus.mem_read_word(0x842);
	desc_cache[1] = g_cpubus.mem_read_word(0x844);
	desc_cache[2] = g_cpubus.mem_read_word(0x846);
	REG_SS.desc.set_from_cache(desc_cache);

	desc_cache[0] = g_cpubus.mem_read_word(0x848);
	desc_cache[1] = g_cpubus.mem_read_word(0x84A);
	desc_cache[2] = g_cpubus.mem_read_word(0x84C);
	REG_DS.desc.set_from_cache(desc_cache);

	base  = g_cpubus.mem_read_dword(0x84E);
	limit = g_cpubus.mem_read_word(0x852);
	SET_GDTR(base, limit);

	desc_cache[0] = g_cpubus.mem_read_word(0x854);
	desc_cache[1] = g_cpubus.mem_read_word(0x856);
	desc_cache[2] = g_cpubus.mem_read_word(0x858);
	REG_LDTR.desc.set_from_cache(desc_cache);

	base  = g_cpubus.mem_read_dword(0x85A);
	limit = g_cpubus.mem_read_word(0x85E);
	SET_IDTR(base, limit);

	desc_cache[0] = g_cpubus.mem_read_word(0x860);
	desc_cache[1] = g_cpubus.mem_read_word(0x862);
	desc_cache[2] = g_cpubus.mem_read_word(0x864);
	REG_TR.desc.set_from_cache(desc_cache);

	g_cpubus.invalidate_pq();
}


/*******************************************************************************
 * LODSB/LODSW-Load String Operand
 */

void CPUExecutor::LODSB()
{
	REG_AL = read_byte(SEG_REG(m_base_ds), REG_SI);

	if(FLAG_DF) {
		REG_SI -= 1;
	} else {
		REG_SI += 1;
	}
}

void CPUExecutor::LODSW()
{
	REG_AX = read_word(SEG_REG(m_base_ds), REG_SI);

	if(FLAG_DF) {
		REG_SI -= 2;
	} else {
		REG_SI += 2;
	}
}


/*******************************************************************************
 * LOOP/LOOPcond-Loop Control with CX Counter
 */

void CPUExecutor::LOOP(int8_t disp)
{
	uint16_t count = REG_CX;

	count--;
	if(count != 0) {
		uint16_t new_IP = REG_IP + disp;
		branch_near(new_IP);
	}

    REG_CX = count;
}

void CPUExecutor::LOOPZ(int8_t disp)
{
	uint16_t count = REG_CX;

	count--;
	if(count != 0 && FLAG_ZF) {
		uint16_t new_IP = REG_IP + disp;
		branch_near(new_IP);
	}

	REG_CX = count;
}

void CPUExecutor::LOOPNZ(int8_t disp)
{
	uint16_t count = REG_CX;

	count--;
	if(count != 0 && (FLAG_ZF==false)) {
		uint16_t new_IP = REG_IP + disp;
		branch_near(new_IP);
	}

	REG_CX = count;
}


/*******************************************************************************
 * LSL-Load Segment Limit
 */

void CPUExecutor::LSL_rw_ew()
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
	    return;
	}

	try {
		descriptor = g_cpucore.fetch_descriptor(selector, CPU_GP_EXC);
	} catch(CPUException &e) {
		PDEBUGF(LOG_V2, LOG_CPU, "LSL: failed to fetch descriptor\n");
	    SET_FLAG(ZF, false);
	    return;
	}

	if(!descriptor.segment) { // system segment
		switch (descriptor.type) {
			case DESC_TYPE_AVAIL_TSS:
			case DESC_TYPE_BUSY_TSS:
			case DESC_TYPE_LDT_DESC:
				if(descriptor.dpl < CPL || descriptor.dpl < selector.rpl) {
					SET_FLAG(ZF, false);
					return;
				}
				break;
			default: /* rest not accepted types to LSL */
				SET_FLAG(ZF, false);
				return;
		}
	} else { // data & code segment
		if(descriptor.is_code_segment_non_conforming()) {
			// non-conforming code segment
			if(descriptor.dpl < CPL || descriptor.dpl < selector.rpl) {
				SET_FLAG(ZF, false);
				return;
			}
		}
	}

	/* all checks pass */
	SET_FLAG(ZF, true);
	store_rw(descriptor.limit);
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
	descriptor = g_cpucore.fetch_descriptor(selector, CPU_GP_EXC);

	/* #GP(selector) if object is not a TSS or is already busy */
	if(!descriptor.valid || descriptor.segment || descriptor.type != DESC_TYPE_AVAIL_TSS)
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
	REG_TR.desc.type = DESC_TYPE_BUSY_TSS;
	g_cpubus.mem_write_byte(GET_BASE(GDTR) + selector.index*8 + 5, REG_TR.desc.get_AR());
}


/*******************************************************************************
 * MOV-Move Data
 */

void CPUExecutor::MOV_eb_rb()
{
	store_eb(load_rb());
}

void CPUExecutor::MOV_ew_rw()
{
	store_ew(load_rw());
}

void CPUExecutor::MOV_rb_eb()
{
	store_rb(load_eb());
}

void CPUExecutor::MOV_rw_ew()
{
	store_rw(load_ew());
}

void CPUExecutor::MOV_rb_db(uint8_t db)
{
	store_rb_op(db);
}

void CPUExecutor::MOV_rw_dw(uint16_t dw)
{
	store_rw_op(dw);
}

void CPUExecutor::MOV_ew_ES()
{
	store_ew(REG_ES.sel.value);
}

void CPUExecutor::MOV_ew_CS()
{
	store_ew(REG_CS.sel.value);
}

void CPUExecutor::MOV_ew_SS()
{
	store_ew(REG_SS.sel.value);
}

void CPUExecutor::MOV_ew_DS()
{
	store_ew(REG_DS.sel.value);
}

void CPUExecutor::MOV_ES_ew()
{
	uint16_t value = load_ew();
	SET_ES(value);
}

void CPUExecutor::MOV_SS_ew()
{
	uint16_t value = load_ew();
	SET_SS(value);
    /* Any move into SS will inhibit all interrupts until after the execution
     * of the next instruction.
     */
	g_cpu.inhibit_interrupts(CPU_INHIBIT_INTERRUPTS_BY_MOVSS);
}

void CPUExecutor::MOV_DS_ew()
{
	uint16_t value = load_ew();
	SET_DS(value);
}

void CPUExecutor::MOV_AL_xb(uint16_t dw)
{
	REG_AL = read_byte(SEG_REG(m_base_ds), dw);
}

void CPUExecutor::MOV_AX_xw(uint16_t dw)
{
	REG_AX = read_word(SEG_REG(m_base_ds), dw);
}

void CPUExecutor::MOV_xb_AL(uint16_t dw)
{
	write_byte(SEG_REG(m_base_ds), dw, REG_AL);
}

void CPUExecutor::MOV_xw_AX(uint16_t dw)
{
	write_word(SEG_REG(m_base_ds), dw, REG_AX);
}

void CPUExecutor::MOV_eb_db(uint8_t data)
{
	store_eb(data);
}

void CPUExecutor::MOV_ew_dw(uint16_t data)
{
	store_ew(data);
}


/*******************************************************************************
 * MOVSB/MOVSW-Move Data from String to String
 */

void CPUExecutor::MOVSB()
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

void CPUExecutor::MOVSW()
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


/*******************************************************************************
 * MUL-Unsigned Multiplication of AL or AX
 */

void CPUExecutor::MUL_eb()
{
	uint8_t op1_8 = REG_AL;
	uint8_t op2_8 = load_eb();

	uint16_t product_16 = uint16_t(op1_8) * uint16_t(op2_8);

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

	return res;
}

void CPUExecutor::OR_eb_rb() { store_eb(OR_b(load_eb(), load_rb())); }
void CPUExecutor::OR_ew_rw() { store_ew(OR_w(load_ew(), load_rw())); }
void CPUExecutor::OR_rb_eb() { store_rb(OR_b(load_rb(), load_eb())); }
void CPUExecutor::OR_rw_ew() { store_rw(OR_w(load_rw(), load_ew())); }
void CPUExecutor::OR_AL_db(uint8_t imm) { REG_AL = OR_b(REG_AL, imm); }
void CPUExecutor::OR_AX_dw(uint16_t imm){ REG_AX = OR_w(REG_AX, imm); }
void CPUExecutor::OR_eb_db(uint8_t imm) { store_eb(OR_b(load_eb(), imm)); }
void CPUExecutor::OR_ew_dw(uint16_t imm){ store_ew(OR_w(load_ew(), imm)); }
void CPUExecutor::OR_ew_db(uint8_t imm) { store_ew(OR_w(load_ew(), int8_t(imm))); }


/*******************************************************************************
 * OUT-Output to port
 */

void CPUExecutor::OUT_b(uint16_t _port, uint8_t _value)
{
	if(IS_PMODE() && (CPL > FLAG_IOPL)) {
	    PDEBUGF(LOG_V2, LOG_CPU, "OUT_b: I/O access not allowed!\n");
	    throw CPUException(CPU_GP_EXC, 0);
	}

	g_devices.write_byte(_port, _value);
}

void CPUExecutor::OUT_w(uint16_t _port, uint16_t _value)
{
	if(IS_PMODE() && (CPL > FLAG_IOPL)) {
	    PDEBUGF(LOG_V2, LOG_CPU, "OUT_w: I/O access not allowed!\n");
	    throw CPUException(CPU_GP_EXC, 0);
	}

	g_devices.write_word(_port, _value);
}

void CPUExecutor::OUT_db_AL(uint8_t port) { OUT_b(port, REG_AL); }
void CPUExecutor::OUT_db_AX(uint8_t port) { OUT_w(port, REG_AX); }
void CPUExecutor::OUT_DX_AL() { OUT_b(REG_DX, REG_AL); }
void CPUExecutor::OUT_DX_AX() { OUT_w(REG_DX, REG_AX); }


/*******************************************************************************
 * OUTSB/OUTSW-Output String to Port
 */

void CPUExecutor::OUTSB()
{
	uint8_t value = read_byte(SEG_REG(m_base_ds), REG_SI);

	OUT_b(REG_DX, value);

	if(FLAG_DF) {
		REG_SI -= 1;
	} else {
		REG_SI += 1;
	}
}

void CPUExecutor::OUTSW()
{
	uint16_t value = read_word(SEG_REG(m_base_ds), REG_SI);

	OUT_w(REG_DX, value);

	if(FLAG_DF) {
		REG_SI -= 2;
	} else {
		REG_SI += 2;
	}
}


/*******************************************************************************
 * POP-Pop a Word from the Stack
 */

void CPUExecutor::POP_DS()
{
	uint16_t selector = stack_pop();
	SET_DS(selector);
}

void CPUExecutor::POP_ES()
{
	uint16_t selector = stack_pop();
	SET_ES(selector);
}

void CPUExecutor::POP_SS()
{
	uint16_t selector = stack_pop();
	SET_SS(selector);

	/*
	A POP SS instruction will inhibit all interrupts, including NMI, until
	after the execution of the next instruction. This permits a POP SP
	instruction to be performed first. (cf. B-83)
	*/
	g_cpu.inhibit_interrupts(CPU_INHIBIT_INTERRUPTS_BY_MOVSS);
}

void CPUExecutor::POP_mw()
{
	uint16_t val = stack_pop();
	store_ew(val);
}

void CPUExecutor::POP_rw()
{
	store_rw_op(stack_pop());
}


/*******************************************************************************
 * POPA-Pop All General Registers
 */

void CPUExecutor::POPA()
{
	uint16_t temp_SP = REG_SP;

	REG_DI = stack_read(temp_SP + 0);
	REG_SI = stack_read(temp_SP + 2);
	REG_BP = stack_read(temp_SP + 4);
	//REG_SP = stack_read(temp_SP + 6); skip SP
	REG_BX = stack_read(temp_SP + 8);
	REG_DX = stack_read(temp_SP + 10);
	REG_CX = stack_read(temp_SP + 12);
	REG_AX = stack_read(temp_SP + 14);

	REG_SP += 16;
}


/*******************************************************************************
 * POPF-Pop from Stack into the Flags Register
 */

void CPUExecutor::POPF()
{
	uint16_t flags = stack_pop();

	if(IS_PMODE()) {
		write_flags(flags,
			(CPL == 0),         // IOPL
			(CPL <= FLAG_IOPL), // IF
			true                // NT
		);
	} else {
		write_flags(flags,
			false, // IOPL
			true,  // IF
			false  // NT
		);
	}
}


/*******************************************************************************
 * PUSH-Push a Word onto the Stack
 */

void CPUExecutor::PUSH_ES()
{
	stack_push(REG_ES.sel.value);
}

void CPUExecutor::PUSH_CS()
{
	stack_push(REG_CS.sel.value);
}

void CPUExecutor::PUSH_SS()
{
	stack_push(REG_SS.sel.value);
}

void CPUExecutor::PUSH_DS()
{
	stack_push(REG_DS.sel.value);
}

void CPUExecutor::PUSH_rw()
{
	/* The 80286 PUSH SP instruction pushes the value of SP as it existed before
	 * the instruction. This differs from the 8086, which pushes the new
	 * (decremented by 2) value.
	 */
	stack_push(GEN_REG(m_instr->reg).word[0]);
}

void CPUExecutor::PUSH_mw()
{
	stack_push(load_ew());
}

void CPUExecutor::PUSH_dw(uint16_t imm)
{
	stack_push(imm);
}

void CPUExecutor::PUSH_db(uint8_t imm)
{
	stack_push(int8_t(imm));
}


/*******************************************************************************
 * PUSHA-Push All General Registers
 */

void CPUExecutor::PUSHA()
{
	uint16_t temp_SP  = REG_SP;

	if(!IS_PMODE()) {
		if(temp_SP == 7 || temp_SP == 9 || temp_SP == 11 || temp_SP == 13 || temp_SP == 15) {
			throw CPUException(CPU_SEG_OVR_EXC,0);
		}
		if(temp_SP == 1 || temp_SP == 3 || temp_SP == 5) {
			throw CPUShutdown("SP=1,3,5 on stack push (PUSHA)");
		}
	}

	stack_write(temp_SP -  2, REG_AX);
	stack_write(temp_SP -  4, REG_CX);
	stack_write(temp_SP -  6, REG_DX);
	stack_write(temp_SP -  8, REG_BX);
	stack_write(temp_SP - 10, temp_SP);
	stack_write(temp_SP - 12, REG_BP);
	stack_write(temp_SP - 14, REG_SI);
	stack_write(temp_SP - 16, REG_DI);
	REG_SP -= 16;
}


/*******************************************************************************
 * PUSHF-Push Flags Register onto the Stack
 */

void CPUExecutor::PUSHF()
{
	uint16_t flags = GET_FLAGS();
	stack_push(flags);
}


/*******************************************************************************
 * RCL/RCR/ROL/ROR-Rotate Instructions
 */

uint8_t CPUExecutor::ROL_b(uint8_t _value, uint8_t _times)
{
	if(!(_times & 0x7)) { //if _times==0 || _times>=8
		if(_times & 0x18) { //if times==8 || _times==16 || _times==24
			SET_FLAG(CF, _value & 1);
			SET_FLAG(OF, (_value & 1) ^ (_value >> 7));
		}
		return _value;
	}
	_times %= 8;

	m_instr->cycles.extra = _times;

	_value = (_value << _times) | (_value >> (8 - _times));

	SET_FLAG(CF, _value & 1);
	SET_FLAG(OF, (_value & 1) ^ (_value >> 7));

	return _value;
}

uint16_t CPUExecutor::ROL_w(uint16_t _value, uint8_t _times)
{
	if(!(_times & 0xF)) { //if _times==0 || _times>=15
		if(_times & 0x10) { //if _times==16 || _times==24
			SET_FLAG(CF, _value & 1);
			SET_FLAG(OF, (_value & 1) ^ (_value >> 15));
		}
		return _value;
	}
	_times %= 16;

	m_instr->cycles.extra = _times;

	_value = (_value << _times) | (_value >> (16 - _times));

	SET_FLAG(CF, _value & 1);
	SET_FLAG(OF, (_value & 1) ^ (_value >> 15));

	return _value;
}

void CPUExecutor::ROL_eb_db(uint8_t times) { store_eb(ROL_b(load_eb(), times)); }
void CPUExecutor::ROL_ew_db(uint8_t times) { store_ew(ROL_w(load_ew(), times)); }
void CPUExecutor::ROL_eb_1() { store_eb(ROL_b(load_eb(), 1)); }
void CPUExecutor::ROL_ew_1() { store_ew(ROL_w(load_ew(), 1)); }
void CPUExecutor::ROL_eb_CL(){ store_eb(ROL_b(load_eb(), REG_CL)); }
void CPUExecutor::ROL_ew_CL(){ store_ew(ROL_w(load_ew(), REG_CL)); }

uint8_t CPUExecutor::ROR_b(uint8_t _value, uint8_t _times)
{
	if(!(_times & 0x7)) { //if _times==0 || _times>=8
		if(_times & 0x18) { //if times==8 || _times==16 || _times==24
			SET_FLAG(CF, _value >> 7);
			SET_FLAG(OF, (_value >> 7) ^ ((_value >> 6) & 1));
		}
		return _value;
	}
	_times %= 8;

	m_instr->cycles.extra = _times;

	_value = (_value >> _times) | (_value << (8 - _times));

	SET_FLAG(CF, _value >> 7);
	SET_FLAG(OF, (_value >> 7) ^ ((_value >> 6) & 1));

	return _value;
}

uint16_t CPUExecutor::ROR_w(uint16_t _value, uint8_t _times)
{
	if(!(_times & 0xf)) { //if _times==0 || _times>=15
		if(_times & 0x10) { //if _times==16 || _times==24
			SET_FLAG(CF, _value >> 15);
			SET_FLAG(OF, (_value >> 15) ^ ((_value >> 14) & 1));
		}
		return _value;
	}
	_times %= 16;

	m_instr->cycles.extra = _times;

	_value = (_value >> _times) | (_value << (16 - _times));

	SET_FLAG(CF, _value >> 15);
	SET_FLAG(OF, (_value >> 15) ^ ((_value >> 14) & 1));

	return _value;
}

void CPUExecutor::ROR_eb_db(uint8_t times) { store_eb(ROR_b(load_eb(), times)); }
void CPUExecutor::ROR_ew_db(uint8_t times) { store_ew(ROR_w(load_ew(), times)); }
void CPUExecutor::ROR_eb_1() { store_eb(ROR_b(load_eb(), 1)); }
void CPUExecutor::ROR_ew_1() { store_ew(ROR_w(load_ew(), 1)); }
void CPUExecutor::ROR_eb_CL(){ store_eb(ROR_b(load_eb(), REG_CL)); }
void CPUExecutor::ROR_ew_CL(){ store_ew(ROR_w(load_ew(), REG_CL)); }

uint8_t CPUExecutor::RCL_b(uint8_t _value, uint8_t _times)
{
	_times = (_times & 0x1F) % 9;

	if(_times == 0) {
		return _value;
	}

	m_instr->cycles.extra = _times;

	uint8_t res;
	uint8_t cf = FLAG_CF;

	if(_times == 1) {
		res = (_value << 1) | cf;
	} else {
		res = (_value << _times) | (cf << (_times - 1)) | (_value >> (9 - _times));
	}

	cf = (_value >> (8-_times)) & 1;

	SET_FLAG(CF, cf);
	SET_FLAG(OF, cf ^ (res >> 7));

	return res;
}

uint16_t CPUExecutor::RCL_w(uint16_t _value, uint8_t _times)
{
	_times = (_times & 0x1F) % 17;

	if(_times == 0) {
		return _value;
	}

	m_instr->cycles.extra = _times;

	uint16_t res;
	uint16_t cf = FLAG_CF;

	if(_times == 1) {
		res = (_value << 1) | cf;
	} else if(_times == 16) {
		res = (cf << 15) | (_value >> 1);
	} else { // 2..15
		res = (_value << _times) | (cf << (_times - 1)) | (_value >> (17 - _times));
	}

	cf = (_value >> (16-_times)) & 1;
	SET_FLAG(CF, cf);
	SET_FLAG(OF, cf ^ (res >> 15));

	return res;
}

void CPUExecutor::RCL_eb_db(uint8_t times) { store_eb(RCL_b(load_eb(), times)); }
void CPUExecutor::RCL_ew_db(uint8_t times) { store_ew(RCL_w(load_ew(), times)); }
void CPUExecutor::RCL_eb_1() { store_eb(RCL_b(load_eb(), 1)); }
void CPUExecutor::RCL_ew_1() { store_ew(RCL_w(load_ew(), 1)); }
void CPUExecutor::RCL_eb_CL(){ store_eb(RCL_b(load_eb(), REG_CL)); }
void CPUExecutor::RCL_ew_CL(){ store_ew(RCL_w(load_ew(), REG_CL)); }

uint8_t CPUExecutor::RCR_b(uint8_t _value, uint8_t _times)
{
	_times = (_times & 0x1F) % 9;

	if(_times == 0) {
		return _value;
	}

	m_instr->cycles.extra = _times;

	uint8_t cf = FLAG_CF;
	uint8_t res = (_value >> _times) | (cf << (8-_times)) | (_value << (9-_times));

	cf = (_value >> (_times - 1)) & 1;
	SET_FLAG(CF, cf);
	SET_FLAG(OF, (res ^ (res << 1)) & 0x80);

	return res;
}

uint16_t CPUExecutor::RCR_w(uint16_t _value, uint8_t _times)
{
	_times = (_times & 0x1f) % 17;

	if(_times == 0) {
		return _value;
	}

	m_instr->cycles.extra = _times;

	uint16_t cf = FLAG_CF;
	uint16_t res = (_value >> _times) | (cf << (16-_times)) | (_value << (17-_times));

	cf = (_value >> (_times - 1)) & 1;
	SET_FLAG(CF, cf);
	SET_FLAG(OF, (res ^ (res << 1)) & 0x8000);

	return res;
}

void CPUExecutor::RCR_eb_db(uint8_t times) { store_eb(RCR_b(load_eb(), times)); }
void CPUExecutor::RCR_ew_db(uint8_t times) { store_ew(RCR_w(load_ew(), times)); }
void CPUExecutor::RCR_eb_1() { store_eb(RCR_b(load_eb(), 1)); }
void CPUExecutor::RCR_ew_1() { store_ew(RCR_w(load_ew(), 1)); }
void CPUExecutor::RCR_eb_CL(){ store_eb(RCR_b(load_eb(), REG_CL)); }
void CPUExecutor::RCR_ew_CL(){ store_ew(RCR_w(load_ew(), REG_CL)); }


/*******************************************************************************
 * RET-Return from Procedure
 */

void CPUExecutor::RET_near(uint16_t popbytes)
{
	uint16_t return_IP = stack_pop();

	if(return_IP > REG_CS.desc.limit) {
		PDEBUGF(LOG_V2, LOG_CPU, "RET_near: offset outside of CS limits\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	SET_IP(return_IP);
	REG_SP += popbytes;

	g_cpubus.invalidate_pq();
}

void CPUExecutor::RET_far(uint16_t popbytes)
{
	if(IS_PMODE()) {
		return_protected(popbytes);
		return;
	}

	uint16_t ip     = stack_pop();
	uint16_t cs_raw = stack_pop();

	// CS.LIMIT can't change when in real mode
	if(ip > REG_CS.desc.limit) {
		PDEBUGF(LOG_V2, LOG_CPU,
				"RET_far: instruction pointer not within code segment limits\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	SET_CS(cs_raw);
	SET_IP(ip);
	REG_SP += popbytes;

	g_cpubus.invalidate_pq();
}

void CPUExecutor::return_protected(uint16_t pop_bytes)
{
	Selector cs_selector, ss_selector;
	Descriptor cs_descriptor, ss_descriptor;
	const uint32_t stack_param_offset = 4;
	uint32_t return_IP, return_SP, temp_SP;

	/* + 6+N*2: SS
	 * + 4+N*2: SP
	 *          parm N
	 *          parm 3
	 *          parm 2
	 * + 4:     parm 1
	 * + 2:     CS
	 * + 0:     IP
	 */

	temp_SP = REG_SP;

	return_IP   = stack_read(temp_SP);
	cs_selector = stack_read(temp_SP + 2);

	// selector must be non-null else #GP(0)
	if((cs_selector.value & SELECTOR_RPL_MASK) == 0) {
		PDEBUGF(LOG_V2, LOG_CPU, "return_protected: CS selector null\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	// selector index must be within its descriptor table limits,
	// else #GP(selector)
	cs_descriptor = g_cpucore.fetch_descriptor(cs_selector, CPU_GP_EXC);

	// return selector RPL must be >= CPL, else #GP(return selector)
	if(cs_selector.rpl < CPL) {
		PDEBUGF(LOG_V2, LOG_CPU, "return_protected: CS.rpl < CPL\n");
		throw CPUException(CPU_GP_EXC, cs_selector.value & SELECTOR_RPL_MASK);
	}

	// descriptor AR byte must indicate code segment, else #GP(selector)
	// check code-segment descriptor
	CPUCore::check_CS(cs_selector.value, cs_descriptor, 0, cs_selector.rpl);

	// if return selector RPL == CPL then
	// RETURN TO SAME PRIVILEGE LEVEL
	if(cs_selector.rpl == CPL) {
		PDEBUGF(LOG_V2, LOG_CPU, "return_protected: return to SAME PRIVILEGE LEVEL\n");
		branch_far(cs_selector, cs_descriptor, return_IP, CPL);
		REG_SP += stack_param_offset + pop_bytes;
	}
	/* RETURN TO OUTER PRIVILEGE LEVEL */
	else {
		/* + 6+N*2: SS
		 * + 4+N*2: SP
		 *          parm N
		 *          parm 3
		 *          parm 2
		 * + 4:     parm 1
		 * + 2:     CS
		 * + 0:     IP
		 */

		PDEBUGF(LOG_V2, LOG_CPU, "return_protected: return to OUTER PRIVILEGE LEVEL\n");
		return_SP   = stack_read(temp_SP + 4 + pop_bytes);
		ss_selector = stack_read(temp_SP + 6 + pop_bytes);

		if((ss_selector.value & SELECTOR_RPL_MASK) == 0) {
			PDEBUGF(LOG_V2, LOG_CPU, "return_protected: SS selector null\n");
			throw CPUException(CPU_GP_EXC, 0);
		}

		// selector index must be within its descriptor table limits,
		// else #GP(selector)
		ss_descriptor = g_cpucore.fetch_descriptor(ss_selector, CPU_GP_EXC);

		// selector RPL must = RPL of the return CS selector,
		// else #GP(selector)
		if(ss_selector.rpl != cs_selector.rpl) {
			PDEBUGF(LOG_V2, LOG_CPU, "return_protected: ss.rpl != cs.rpl\n");
			throw CPUException(CPU_GP_EXC, ss_selector.value & SELECTOR_RPL_MASK);
		}

		// descriptor AR byte must indicate a writable data segment,
		// else #GP(selector)
		if(!ss_descriptor.valid || !ss_descriptor.segment ||
			ss_descriptor.is_code_segment() ||
			!ss_descriptor.is_data_segment_writeable())
		{
			PDEBUGF(LOG_V2, LOG_CPU, "return_protected: SS.AR byte not writable data\n");
			throw CPUException(CPU_GP_EXC, ss_selector.value & SELECTOR_RPL_MASK);
		}

		// descriptor dpl must == RPL of the return CS selector,
		// else #GP(selector)
		if(ss_descriptor.dpl != cs_selector.rpl) {
			PDEBUGF(LOG_V2, LOG_CPU, "return_protected: SS.dpl != cs.rpl\n");
			throw CPUException(CPU_GP_EXC, ss_selector.value & SELECTOR_RPL_MASK);
		}

		// segment must be present else #SS(selector)
		if(!ss_descriptor.present) {
			PDEBUGF(LOG_V2, LOG_CPU, "return_protected: ss.present == 0\n");
			throw CPUException(CPU_SS_EXC, ss_selector.value & SELECTOR_RPL_MASK);
		}

		branch_far(cs_selector, cs_descriptor, return_IP, cs_selector.rpl);

		if((ss_selector.value & SELECTOR_RPL_MASK) != 0) {
			// load SS:RSP from stack
			// load the SS-cache with SS descriptor
			SET_SS(ss_selector, ss_descriptor, cs_selector.rpl);
		}

		REG_SP  = (uint16_t)(return_SP + pop_bytes);

		/* check ES, DS for validity */
		REG_ES.validate();
		REG_DS.validate();
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

uint8_t CPUExecutor::SHL_b(uint8_t _value, uint8_t _times)
{
	_times &= 0x1F;

	if(_times == 0) {
		return _value;
	}

	m_instr->cycles.extra = _times;

	uint of = 0, cf = 0;
	uint8_t res;

	if(_times <= 8) {
		res = (_value << _times);
		cf = (_value >> (8 - _times)) & 0x1;
		of = cf ^ (res >> 7);
	} else {
		res = 0;
	}

	SET_FLAG(OF, of);
	SET_FLAG(CF, cf);

	SET_FLAG(ZF, res == 0);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(SF, res & 0x80);

	return res;
}

uint16_t CPUExecutor::SHL_w(uint16_t _value, uint8_t _times)
{
	_times &= 0x1F;

	if(_times == 0) {
		return _value;
	}

	m_instr->cycles.extra = _times;

	uint16_t res;
	uint of = 0, cf = 0;

	if(_times <= 16) {
		res = (_value << _times);
		cf = (_value >> (16 - _times)) & 0x1;
		of = cf ^ (res >> 15);
	} else {
		res = 0;
	}

	SET_FLAG(OF, of);
	SET_FLAG(CF, cf);

	SET_FLAG(ZF, res == 0);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(SF, res & 0x8000);

	return res;
}

void CPUExecutor::SAL_eb_db(uint8_t data) { store_eb(SHL_b(load_eb(), data)); }
void CPUExecutor::SAL_ew_db(uint8_t data) { store_ew(SHL_w(load_ew(), data));  }
void CPUExecutor::SAL_eb_1() { store_eb(SHL_b(load_eb(), 1)); }
void CPUExecutor::SAL_ew_1() { store_ew(SHL_w(load_ew(), 1)); }
void CPUExecutor::SAL_eb_CL(){ store_eb(SHL_b(load_eb(), REG_CL)); }
void CPUExecutor::SAL_ew_CL(){ store_ew(SHL_w(load_ew(), REG_CL)); }

uint8_t CPUExecutor::SHR_b(uint8_t _value, uint8_t _times)
{
	_times &= 0x1f;

	if(_times == 0) {
		return _value;
	}

	m_instr->cycles.extra = _times;

	uint8_t res = _value >> _times;

	SET_FLAG(OF, (((res << 1) ^ res) >> 7) & 0x1);
	SET_FLAG(CF, (_value >> (_times - 1)) & 0x1);

	SET_FLAG(ZF, res == 0);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(SF, res & 0x80);

	return res;
}

uint16_t CPUExecutor::SHR_w(uint16_t _value, uint8_t _times)
{
	_times &= 0x1f;

	if(_times == 0) {
		return _value;
	}

	m_instr->cycles.extra = _times;

	uint16_t res = _value >> _times;

	SET_FLAG(OF, ((uint16_t)((res << 1) ^ res) >> 15) & 0x1);
	SET_FLAG(CF, (_value >> (_times - 1)) & 1);

	SET_FLAG(ZF, res == 0);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(SF, res & 0x8000);

	return res;
}

void CPUExecutor::SHR_eb_db(uint8_t data) { store_eb(SHR_b(load_eb(), data)); }
void CPUExecutor::SHR_ew_db(uint8_t data) { store_ew(SHR_w(load_ew(), data)); }
void CPUExecutor::SHR_eb_1() { store_eb(SHR_b(load_eb(), 1)); }
void CPUExecutor::SHR_ew_1() { store_ew(SHR_w(load_ew(), 1)); }
void CPUExecutor::SHR_eb_CL(){ store_eb(SHR_b(load_eb(), REG_CL)); }
void CPUExecutor::SHR_ew_CL(){ store_ew(SHR_w(load_ew(), REG_CL)); }

uint8_t CPUExecutor::SAR_b(uint8_t _value, uint8_t _times)
{
	_times &= 0x1F;

	if(_times == 0) {
		return _value;
	}

	m_instr->cycles.extra = _times;

	uint8_t res = ((int8_t) _value) >> _times;

	SET_FLAG(OF, false);
	SET_FLAG(CF, (((int8_t) _value) >> (_times - 1)) & 1);

	SET_FLAG(ZF, res == 0);
	SET_FLAG(SF, res & 0x80);
	SET_FLAG(PF, PARITY(res));

	return res;
}

uint16_t CPUExecutor::SAR_w(uint16_t _value, uint8_t _times)
{
	_times &= 0x1F;

	if(_times == 0) {
		return _value;
	}

	m_instr->cycles.extra = (_value)?_times:0;

	uint16_t res = ((int16_t) _value) >> _times;

	SET_FLAG(OF, false);
	SET_FLAG(CF, (((int16_t) _value) >> (_times - 1)) & 1);

	SET_FLAG(ZF, res == 0);
	SET_FLAG(SF, res & 0x8000);
	SET_FLAG(PF, PARITY(res));

	return res;
}

void CPUExecutor::SAR_eb_db(uint8_t data) { store_eb(SAR_b(load_eb(), data)); }
void CPUExecutor::SAR_ew_db(uint8_t data) { store_ew(SAR_w(load_ew(), data)); }
void CPUExecutor::SAR_eb_1() { store_eb(SAR_b(load_eb(), 1)); }
void CPUExecutor::SAR_ew_1() { store_ew(SAR_w(load_ew(), 1)); }
void CPUExecutor::SAR_eb_CL(){ store_eb(SAR_b(load_eb(), REG_CL)); }
void CPUExecutor::SAR_ew_CL(){ store_ew(SAR_w(load_ew(), REG_CL)); }


/*******************************************************************************
 * SBB-Integer Subtraction With Borrow
 */

uint8_t CPUExecutor::SBB_b(uint8_t op1, uint8_t op2)
{
	uint8_t cf = FLAG_CF;
	uint8_t res = op1 - (op2 + cf);

	SET_FLAG(OF, ((op1 ^ op2) & (op1 ^ res)) & 0x80);
	SET_FLAG(SF, res & 0x80);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, ((op1 ^ op2) ^ res) & 0x10);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(CF, (op1 < res) || (cf && (op2==0xff)));

	return res;
}

uint16_t CPUExecutor::SBB_w(uint16_t op1, uint16_t op2)
{
	uint16_t cf = FLAG_CF;
	uint16_t res = op1 - (op2 + cf);

	SET_FLAG(OF, ((op1 ^ op2) & (op1 ^ res)) & 0x8000);
	SET_FLAG(SF, res & 0x8000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, ((op1 ^ op2) ^ res) & 0x10);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(CF, (op1 < res) || (cf && (op2==0xffff)));

	return res;
}

void CPUExecutor::SBB_eb_rb() { store_eb(SBB_b(load_eb(), load_rb())); }
void CPUExecutor::SBB_ew_rw() { store_ew(SBB_w(load_ew(), load_rw())); }
void CPUExecutor::SBB_rb_eb() { store_rb(SBB_b(load_rb(), load_eb())); }
void CPUExecutor::SBB_rw_ew() { store_rw(SBB_w(load_rw(), load_ew())); }
void CPUExecutor::SBB_AL_db(uint8_t imm) { REG_AL = SBB_b(REG_AL, imm); }
void CPUExecutor::SBB_AX_dw(uint16_t imm){ REG_AX = SBB_w(REG_AX, imm); }
void CPUExecutor::SBB_eb_db(uint8_t imm) { store_eb(SBB_b(load_eb(), imm)); }
void CPUExecutor::SBB_ew_dw(uint16_t imm){ store_ew(SBB_w(load_ew(), imm)); }
void CPUExecutor::SBB_ew_db(uint8_t imm) { store_ew(SBB_w(load_ew(), int8_t(imm))); }


/*******************************************************************************
 * SCASB/SCASW-Compare String Data
 */

void CPUExecutor::SCASB()
{
	uint8_t op1 = REG_AL;
	//no segment override is possible.
	uint8_t op2 = read_byte(REG_ES, REG_DI);

	CMP_b(op1, op2);

	if(FLAG_DF) {
		REG_DI -= 1;
	} else {
		REG_DI += 1;
	}
}

void CPUExecutor::SCASW()
{
	uint16_t op1 = REG_AX;
	//no segment override is possible.
	uint16_t op2 = read_word(REG_ES, REG_DI);

	CMP_w(op1, op2);

	if(FLAG_DF) {
		REG_DI -= 2;
	} else {
		REG_DI += 2;
	}
}


/*******************************************************************************
 * SGDT/SIDT/SLDT-Store Global/Interrupt/Local Descriptor Table Register
 */

void CPUExecutor::SGDT()
{
	uint16_t limit_16 = GET_LIMIT(GDTR);
	uint32_t base_32  = GET_BASE(GDTR);

	SegReg & sr = EA_get_segreg();
	uint16_t off = EA_get_offset();

	write_word(sr, off, limit_16);
	//here Bochs seems to store a dword, but it should store 3 bytes only,
	//the 4th is undefined (don't touch it, or the POST procedure of the
	//PS/1 won't succeed)
	// g_memory.write_dword(addr+2, base_32);
	write_byte(sr, off+2, base_32);
	write_byte(sr, off+3, base_32>>8);
	write_byte(sr, off+4, base_32>>16);
}

void CPUExecutor::SIDT()
{
	uint16_t limit_16 = GET_LIMIT(IDTR);
	uint32_t base_32  = GET_BASE(IDTR);

	SegReg & sr = EA_get_segreg();
	uint16_t off = EA_get_offset();

	write_word(sr, off, limit_16);
	//see above
	// g_memory.write_dword(addr, base_32);
	write_byte(sr, off+2, base_32);
	write_byte(sr, off+3, base_32>>8);
	write_byte(sr, off+4, base_32>>16);
}

void CPUExecutor::SLDT_ew()
{
	if(!IS_PMODE()) {
		PDEBUGF(LOG_V2, LOG_CPU, "SLDT: not recognized in real mode\n");
		throw CPUException(CPU_UD_EXC, 0);
	}

	uint16_t val16 = REG_LDTR.sel.value;
	store_ew(val16);
}


/*******************************************************************************
 * SMSW-Store Machine Status Word
 */

void CPUExecutor::SMSW_ew()
{
	uint16_t msw = GET_MSW(MSW_ALL);
	store_ew(msw);
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
 * STOSB/STOSW-Store String Data
 */

void CPUExecutor::STOSB()
{
	//no segment override is possible.
	write_byte(REG_ES, REG_DI, REG_AL);

	if(FLAG_DF) {
		REG_DI -= 1;
	} else {
		REG_DI += 1;
	}
}

void CPUExecutor::STOSW()
{
	//no segment override is possible.
	write_word(REG_ES, REG_DI, REG_AX);

	if(FLAG_DF) {
		REG_DI -= 2;
	} else {
		REG_DI += 2;
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
}


/*******************************************************************************
 * SUB-Integer Subtraction
 */

uint8_t CPUExecutor::SUB_b(uint8_t op1, uint8_t op2)
{
	uint8_t res = op1 - op2;

	SET_FLAG(OF, ((op1 ^ op2) & (op1 ^ res)) & 0x80);
	SET_FLAG(SF, res & 0x80);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, ((op1 ^ op2) ^ res) & 0x10);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(CF, op1 < op2);

	return res;
}

uint16_t CPUExecutor::SUB_w(uint16_t op1, uint16_t op2)
{
	uint16_t res = op1 - op2;

	SET_FLAG(OF, ((op1 ^ op2) & (op1 ^ res)) & 0x8000);
	SET_FLAG(SF, res & 0x8000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(AF, ((op1 ^ op2) ^ res) & 0x10);
	SET_FLAG(PF, PARITY(res));
	SET_FLAG(CF, op1 < op2);

	return res;
}

void CPUExecutor::SUB_eb_rb() { store_eb(SUB_b(load_eb(), load_rb())); }
void CPUExecutor::SUB_ew_rw() { store_ew(SUB_w(load_ew(), load_rw())); }
void CPUExecutor::SUB_rb_eb() { store_rb(SUB_b(load_rb(), load_eb())); }
void CPUExecutor::SUB_rw_ew() { store_rw(SUB_w(load_rw(), load_ew())); }
void CPUExecutor::SUB_AL_db(uint8_t imm) { REG_AL = SUB_b(REG_AL, imm); }
void CPUExecutor::SUB_AX_dw(uint16_t imm){ REG_AX = SUB_w(REG_AX, imm); }
void CPUExecutor::SUB_eb_db(uint8_t imm) { store_eb(SUB_b(load_eb(), imm)); }
void CPUExecutor::SUB_ew_dw(uint16_t imm){ store_ew(SUB_w(load_ew(), imm)); }
void CPUExecutor::SUB_ew_db(uint8_t imm) { store_ew(SUB_w(load_ew(), int8_t(imm))); }


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
}

void CPUExecutor::TEST_w(uint16_t _value1, uint16_t _value2)
{
	uint16_t res = _value1 & _value2;

	SET_FLAG(OF, false);
	SET_FLAG(CF, false);
	SET_FLAG(SF, res & 0x8000);
	SET_FLAG(ZF, res == 0);
	SET_FLAG(PF, PARITY(res));
}

void CPUExecutor::TEST_eb_rb() { TEST_b(load_eb(), load_rb()); }
void CPUExecutor::TEST_ew_rw() { TEST_w(load_ew(), load_rw()); }
void CPUExecutor::TEST_AL_db(uint8_t db) { TEST_b(REG_AL, db); }
void CPUExecutor::TEST_AX_dw(uint16_t dw){ TEST_w(REG_AX, dw); }
void CPUExecutor::TEST_eb_db(uint8_t db) { TEST_b(load_eb(), db); }
void CPUExecutor::TEST_ew_dw(uint16_t dw){ TEST_w(load_ew(), dw); }


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
		descriptor = g_cpucore.fetch_descriptor(selector,0);
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
		if(descriptor.is_code_segment_conforming() && descriptor.is_code_segment_readable())
	    {
			PDEBUGF(LOG_V2, LOG_CPU, "VERR: conforming code, OK\n");
			SET_FLAG(ZF, true);
			return;
	    }
	    if(!descriptor.is_code_segment_readable()) {
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
		descriptor = g_cpucore.fetch_descriptor(selector,0);
	} catch(CPUException &e) {
		PDEBUGF(LOG_V2, LOG_CPU, "VERW: not within descriptor table\n");
		SET_FLAG(ZF, false);
		return;
	}

	// rule out system segments & code segments
	if(!descriptor.segment || descriptor.is_code_segment()) {
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
	if(descriptor.is_data_segment_writeable()) {
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
	if(GET_MSW(MSW_TS) && GET_MSW(MSW_MP)) {
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

void CPUExecutor::XCHG_AX_rw()
{
	uint16_t ax = REG_AX;
	REG_AX = GEN_REG(m_instr->reg).word[0];
	GEN_REG(m_instr->reg).word[0] = ax;
}


/*******************************************************************************
 * XLATB-Table Look-up Translation
 */

void CPUExecutor::XLATB()
{
	REG_AL = read_byte(SEG_REG(m_base_ds), (REG_BX + uint16_t(REG_AL)));
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

	return res;
}

void CPUExecutor::XOR_rb_eb() { store_rb(XOR_b(load_rb(), load_eb())); }
void CPUExecutor::XOR_rw_ew() { store_rw(XOR_w(load_rw(), load_ew())); }
void CPUExecutor::XOR_eb_rb() { store_eb(XOR_b(load_eb(), load_rb())); }
void CPUExecutor::XOR_ew_rw() { store_ew(XOR_w(load_ew(), load_rw())); }
void CPUExecutor::XOR_AL_db(uint8_t db) { REG_AL = XOR_b(REG_AL, db); }
void CPUExecutor::XOR_AX_dw(uint16_t dw){ REG_AX = XOR_w(REG_AX, dw); }
void CPUExecutor::XOR_eb_db(uint8_t db) { store_eb(XOR_b(load_eb(), db)); }
void CPUExecutor::XOR_ew_dw(uint16_t dw){ store_ew(XOR_w(load_ew(), dw)); }
void CPUExecutor::XOR_ew_db(uint8_t db) { store_ew(XOR_w(load_ew(), int8_t(db))); }

