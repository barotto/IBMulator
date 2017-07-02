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
#include "hardware/cpu/executor.h"

void CPUExecutor::call_gate(Descriptor &gate_descriptor)
{
	Selector   cs_selector;
	Descriptor cs_descriptor;

	// examine code segment selector in call gate descriptor
	PDEBUGF(LOG_V2, LOG_CPU, "call gate\n");

	cs_selector      = gate_descriptor.selector;
	uint32_t new_EIP = gate_descriptor.offset;

	// selector must not be null else #GP(0)
	if((cs_selector.value & SELECTOR_RPL_MASK) == 0) {
		PDEBUGF(LOG_V2, LOG_CPU, "call_gate: selector in gate null\n");
		throw CPUException(CPU_GP_EXC, 0);
	}
	// selector must be within its descriptor table limits,
	//   else #GP(code segment selector)
	cs_descriptor = fetch_descriptor(cs_selector, CPU_GP_EXC);

	// AR byte of selected descriptor must indicate code segment,
	//   else #GP(code segment selector)
	// DPL of selected descriptor must be <= CPL,
	// else #GP(code segment selector)
	if(!cs_descriptor.valid || !cs_descriptor.is_code_segment() || cs_descriptor.dpl > CPL)
	{
		PDEBUGF(LOG_V2, LOG_CPU, "call_gate: selected descriptor is not code\n");
		throw CPUException(CPU_GP_EXC, cs_selector.value & SELECTOR_RPL_MASK);
	}

	// code segment must be present else #NP(selector)
	if(!cs_descriptor.present) {
		PDEBUGF(LOG_V2, LOG_CPU, "call_gate: code segment not present!\n");
		throw CPUException(CPU_NP_EXC, cs_selector.value & SELECTOR_RPL_MASK);
	}

	// CALL GATE TO MORE PRIVILEGE
	// if non-conforming code segment and DPL < CPL then
	if(!cs_descriptor.is_conforming() && (cs_descriptor.dpl < CPL)) {
		uint16_t SS_for_cpl_x;
		uint32_t ESP_for_cpl_x;
		Selector   ss_selector;
		Descriptor ss_descriptor;
		uint16_t   return_SS, return_CS;
		uint32_t   return_ESP, return_EIP;

		PDEBUGF(LOG_V2, LOG_CPU, "CALL GATE TO MORE PRIVILEGE LEVEL\n");

		// get new SS selector for new privilege level from TSS
		get_SS_ESP_from_TSS(cs_descriptor.dpl, SS_for_cpl_x, ESP_for_cpl_x);

		// check selector & descriptor for new SS:
		// selector must not be null, else #TS(0)
		if((SS_for_cpl_x & SELECTOR_RPL_MASK) == 0) {
			PDEBUGF(LOG_V2, LOG_CPU, "call_gate: new SS null\n");
			throw CPUException(CPU_TS_EXC, 0);
		}

		// selector index must be within its descriptor table limits,
		//   else #TS(SS selector)
		ss_selector   = SS_for_cpl_x;
		ss_descriptor = fetch_descriptor(ss_selector, CPU_TS_EXC);

		// selector's RPL must equal DPL of code segment,
		//   else #TS(SS selector)
		if(ss_selector.rpl != cs_descriptor.dpl) {
			PDEBUGF(LOG_V2, LOG_CPU, "call_gate: SS selector.rpl != CS descr.dpl\n");
			throw CPUException(CPU_TS_EXC, SS_for_cpl_x & SELECTOR_RPL_MASK);
		}

		// stack segment DPL must equal DPL of code segment,
		//   else #TS(SS selector)
		if(ss_descriptor.dpl != cs_descriptor.dpl) {
			PDEBUGF(LOG_V2, LOG_CPU, "call_gate: SS descr.rpl != CS descr.dpl\n");
			throw CPUException(CPU_TS_EXC, SS_for_cpl_x & SELECTOR_RPL_MASK);
		}

		// descriptor must indicate writable data segment,
		//   else #TS(SS selector)
		if(!ss_descriptor.valid || !ss_descriptor.is_data_segment() || !ss_descriptor.is_writeable())
		{
			PDEBUGF(LOG_V2, LOG_CPU, "call_gate: ss descriptor is not writable data seg\n");
			throw CPUException(CPU_TS_EXC, SS_for_cpl_x & SELECTOR_RPL_MASK);
		}

		// segment must be present, else #SS(SS selector)
		if(!ss_descriptor.present) {
			PDEBUGF(LOG_V2, LOG_CPU, "call_gate: ss descriptor not present\n");
			throw CPUException(CPU_SS_EXC, SS_for_cpl_x & SELECTOR_RPL_MASK);
		}

		// get word count from call gate, mask to 5 bits
		unsigned param_count = gate_descriptor.word_count & 0x1f;

		// save return SS:eSP to be pushed on new stack
		return_SS = REG_SS.sel.value;
		return_ESP = (REG_SS.desc.big)?(REG_ESP):(REG_SP);

		// save return CS:EIP to be pushed on new stack
		return_CS = REG_CS.sel.value;
		return_EIP = (REG_CS.desc.big)?(REG_EIP):(REG_IP);

		// Prepare new stack segment
		SegReg new_stack;
		new_stack.sel     = ss_selector;
		new_stack.desc    = ss_descriptor;
		new_stack.sel.rpl = cs_descriptor.dpl;
		// add cpl to the selector value
		new_stack.sel.value = (new_stack.sel.value & SELECTOR_RPL_MASK) | new_stack.sel.rpl;

		const uint16_t errcode = new_stack.sel.rpl != CPL ? (new_stack.sel.value & SELECTOR_RPL_MASK) : 0;
		const unsigned pl = cs_descriptor.dpl;

		/* load new SS:ESP value from TSS */
		if(ss_descriptor.big) {
			uint32_t temp_ESP = ESP_for_cpl_x;

			// push pointer of old stack onto new stack
			if(gate_descriptor.type == DESC_TYPE_386_CALL_GATE) {
				PDEBUGF(LOG_V2, LOG_CPU, "386 CALL GATE (32bit SS) ");
				write_dword(new_stack, temp_ESP-4, return_SS, pl, CPU_SS_EXC, errcode);
				write_dword(new_stack, temp_ESP-8, return_ESP, pl, CPU_SS_EXC, errcode);
				temp_ESP -= 8;

				for(unsigned n = param_count; n>0; n--) {
					temp_ESP -= 4;
					uint32_t param = stack_read_dword(return_ESP + (n-1)*4);
					write_dword(new_stack, temp_ESP, param, pl, CPU_SS_EXC, errcode);
				}
				// push return address onto new stack
				write_dword(new_stack, temp_ESP-4, return_CS, pl, CPU_SS_EXC, errcode);
				write_dword(new_stack, temp_ESP-8, return_EIP, pl, CPU_SS_EXC, errcode);
				temp_ESP -= 8;
			} else {
				PDEBUGF(LOG_V2, LOG_CPU, "286 CALL GATE (32bit SS) ");
				write_word(new_stack, temp_ESP-2, return_SS, pl, CPU_SS_EXC, errcode);
				write_word(new_stack, temp_ESP-4, return_ESP, pl, CPU_SS_EXC, errcode);
				temp_ESP -= 4;

				for(unsigned n = param_count; n>0; n--) {
					temp_ESP -= 2;
					uint16_t param = stack_read_word(return_ESP + (n-1)*2);
					write_word(new_stack, temp_ESP, param, pl, CPU_SS_EXC, errcode);
				}
				// push return address onto new stack
				write_word(new_stack, temp_ESP-2, return_CS, pl, CPU_SS_EXC, errcode);
				write_word(new_stack, temp_ESP-4, return_EIP, pl, CPU_SS_EXC, errcode);
				temp_ESP -= 4;
			}

			REG_ESP = temp_ESP;
		} else {
			uint16_t temp_SP = ESP_for_cpl_x;

			// push pointer of old stack onto new stack
			if(gate_descriptor.type == DESC_TYPE_386_CALL_GATE) {
				PDEBUGF(LOG_V2, LOG_CPU, "386 CALL GATE (16bit SS) ");
				write_dword(new_stack, uint16_t(temp_SP-4), return_SS, pl, CPU_SS_EXC, errcode);
				write_dword(new_stack, uint16_t(temp_SP-8), return_ESP, pl, CPU_SS_EXC, errcode);
				temp_SP -= 8;

				for(unsigned n = param_count; n>0; n--) {
					temp_SP -= 4;
					uint32_t param = stack_read_dword(return_ESP + (n-1)*4);
					write_dword(new_stack, temp_SP, param, pl, CPU_SS_EXC, errcode);
				}
				// push return address onto new stack
				write_dword(new_stack, uint16_t(temp_SP-4), return_CS, pl, CPU_SS_EXC, errcode);
				write_dword(new_stack, uint16_t(temp_SP-8), return_EIP, pl, CPU_SS_EXC, errcode);
				temp_SP -= 8;
			} else {
				PDEBUGF(LOG_V2, LOG_CPU, "286 CALL GATE (16bit SS) ");
				write_word(new_stack, uint16_t(temp_SP-2), return_SS, pl, CPU_SS_EXC, errcode);
				write_word(new_stack, uint16_t(temp_SP-4), return_ESP, pl, CPU_SS_EXC, errcode);
				temp_SP -= 4;

				for(unsigned n = param_count; n>0; n--) {
					temp_SP -= 2;
					uint16_t param = stack_read_word(return_ESP + (n-1)*2);
					write_word(new_stack, temp_SP, param, pl, CPU_SS_EXC, errcode);
				}
				// push return address onto new stack
				write_word(new_stack, uint16_t(temp_SP-2), return_CS, pl, CPU_SS_EXC, errcode);
				write_word(new_stack, uint16_t(temp_SP-4), return_EIP, pl, CPU_SS_EXC, errcode);
				temp_SP -= 4;
			}
			REG_SP = temp_SP;
		}
		PDEBUGF(LOG_V2, LOG_CPU, "to %04X:%08X\n", cs_selector.value, new_EIP);

		// new EIP must be in code segment limit else #GP(0)
		if(new_EIP > cs_descriptor.limit) {
			PDEBUGF(LOG_V2, LOG_CPU, "new EIP not within CS limits\n");
			throw CPUException(CPU_GP_EXC, 0);
		}

		/* load SS descriptor */
		SET_SS(ss_selector, ss_descriptor, cs_descriptor.dpl);

		/* load new CS:IP value from gate */
		/* load CS descriptor */
		/* set CPL to stack segment DPL */
		/* set RPL of CS to CPL */
		SET_CS(cs_selector, cs_descriptor, cs_descriptor.dpl);
		SET_EIP(new_EIP);

		g_cpubus.invalidate_pq();
	}
	else   // CALL GATE TO SAME PRIVILEGE
	{
		PDEBUGF(LOG_V2, LOG_CPU, "CALL GATE TO SAME PRIVILEGE\n");

		if(gate_descriptor.type == DESC_TYPE_386_CALL_GATE) {
			// call gate 32bit, push return address onto stack
			PDEBUGF(LOG_V2, LOG_CPU, "386 CALL GATE ");
			stack_push_dword(REG_CS.sel.value);
			stack_push_dword(REG_EIP);
		} else {
			// call gate 16bit, push return address onto stack
			PDEBUGF(LOG_V2, LOG_CPU, "286 CALL GATE ");
			stack_push_word(REG_CS.sel.value);
			stack_push_word(REG_IP);
		}
		// load CS:EIP from gate
		// load code segment descriptor into CS register
		// set RPL of CS to CPL
		PDEBUGF(LOG_V2, LOG_CPU, "to %04X:%08X\n", cs_selector.value, new_EIP);
		branch_far(cs_selector, cs_descriptor, new_EIP, CPL);
	}
}

void CPUExecutor::branch_relative(int32_t _offset)
{
	uint32_t new_EIP;

	if(m_instr->op32) {
		new_EIP = REG_EIP + _offset;
	} else {
		new_EIP = (REG_IP + _offset) & 0xFFFF;
	}

	branch_near(new_EIP);
}

void CPUExecutor::branch_near(uint32_t new_EIP)
{
	// check always, not only in protected mode
	if(new_EIP > GET_LIMIT(CS)) {
		PDEBUGF(LOG_V2,LOG_CPU,"branch_near: offset outside of CS limits\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	SET_EIP(new_EIP);
	g_cpubus.invalidate_pq();
}

void CPUExecutor::branch_far(Selector &selector, Descriptor &descriptor, uint32_t eip, uint8_t cpl)
{
	/* instruction pointer must be in code segment limit else #GP(0) */
	if(eip > descriptor.limit) {
		PERRF(LOG_CPU, "branch_far: EIP > descriptor limit\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	/* Load CS:EIP from destination pointer */
	SET_CS(selector, descriptor, cpl);
	SET_EIP(eip);
	g_cpubus.invalidate_pq();
}

void CPUExecutor::branch_far(uint16_t _sel, uint32_t _disp)
{
	// CS LIMIT can't change when in real mode
	if(_disp > GET_LIMIT(CS)) {
		PDEBUGF(LOG_V2,LOG_CPU, "branch_far: offset outside of CS limits\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	SET_CS(_sel);
	SET_EIP(_disp);
	g_cpubus.invalidate_pq();
}

void CPUExecutor::branch_far_pmode(uint16_t _cs, uint32_t _disp)
{
	//see jmp_far.cc/jump_protected

	Descriptor  descriptor;
	Selector    selector;

	/* destination selector is not null else #GP(0) */
	if((_cs & SELECTOR_RPL_MASK) == 0) {
		PDEBUGF(LOG_V2, LOG_CPU,"branch_far_pmode: cs == 0\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	selector = _cs;

	/* destination selector index is within its descriptor table
	 * limits else #GP(selector)
	 */
	descriptor = fetch_descriptor(selector, CPU_GP_EXC);

	/* examine AR byte of destination selector for legal values: */
	if(descriptor.segment) {
		CPUCore::check_CS(selector, descriptor, selector.rpl, CPL);
		branch_far(selector, descriptor, _disp, CPL);
		return;
	} else {
		// call gate DPL must be >= CPL else #GP(gate selector)
		if(descriptor.dpl < CPL) {
			PDEBUGF(LOG_V2, LOG_CPU,"branch_far_pmode: call gate.dpl < CPL\n");
			throw CPUException(CPU_GP_EXC, _cs & SELECTOR_RPL_MASK);
		}

		// call gate DPL must be >= gate selector RPL else #GP(gate selector)
		if(descriptor.dpl < selector.rpl) {
			PDEBUGF(LOG_V2, LOG_CPU,"branch_far_pmode: call gate.dpl < selector.rpl\n");
			throw CPUException(CPU_GP_EXC, _cs & SELECTOR_RPL_MASK);
		}

		switch(descriptor.type) {
			case DESC_TYPE_AVAIL_286_TSS:
			case DESC_TYPE_AVAIL_386_TSS:
				PDEBUGF(LOG_V2,LOG_CPU,"branch_far_pmode: jump to TSS\n");

				if(!descriptor.valid || selector.ti) {
					PDEBUGF(LOG_V2, LOG_CPU,"branch_far_pmode: jump to bad TSS selector\n");
					throw CPUException(CPU_GP_EXC, _cs & SELECTOR_RPL_MASK);
				}

				// TSS must be present, else #NP(TSS selector)
				if(!descriptor.present) {
					PDEBUGF(LOG_V2, LOG_CPU,"branch_far_pmode: jump to not present TSS\n");
					throw CPUException(CPU_NP_EXC, _cs & SELECTOR_RPL_MASK);
				}

				// SWITCH_TASKS _without_ nesting to TSS
				switch_tasks(selector, descriptor, CPU_TASK_FROM_JUMP);
				return;

			case DESC_TYPE_TASK_GATE:
				task_gate(selector, descriptor, CPU_TASK_FROM_JUMP);
				return;

			case DESC_TYPE_286_CALL_GATE:
			case DESC_TYPE_386_CALL_GATE:
				jump_call_gate(selector, descriptor);
				return;

			default:
				PDEBUGF(LOG_V2, LOG_CPU,"branch_far_pmode: gate type %u unsupported\n", descriptor.type);
				throw CPUException(CPU_GP_EXC, _cs & SELECTOR_RPL_MASK);
		}
	}
}

void CPUExecutor::call_relative(int32_t _offset)
{
	uint32_t new_EIP;

	if(m_instr->op32) {
		stack_push_dword(REG_EIP);
		new_EIP = REG_EIP + _offset;
	} else {
		stack_push_word(REG_IP);
		new_EIP = (REG_IP + _offset) & 0xFFFF;
	}

	branch_near(new_EIP);
}

void CPUExecutor::call_16(uint16_t _cs, uint16_t _ip)
{
	if(IS_PMODE()) {
		call_pmode(_cs, _ip);
		return;
	}
	//REAL mode
	// CS LIMIT can't change when in real mode
	if(_ip > GET_LIMIT(CS)) {
		PDEBUGF(LOG_V2, LOG_CPU, "CALL_cd: instruction pointer not within code segment limits\n");
		throw CPUException(CPU_GP_EXC, 0);
	}
	stack_push_word(REG_CS.sel.value);
	stack_push_word(REG_IP);
	SET_CS(_cs);
	SET_IP(_ip);
	g_cpubus.invalidate_pq();
}

void CPUExecutor::call_32(uint16_t _cs, uint32_t _eip)
{
	if(IS_PMODE()) {
		call_pmode(_cs, _eip);
		return;
	}
	//REAL mode
	// CS LIMIT can't change when in real mode
	if(_eip > GET_LIMIT(CS)) {
		PDEBUGF(LOG_V2, LOG_CPU, "CALL_cd: instruction pointer not within code segment limits\n");
		throw CPUException(CPU_GP_EXC, 0);
	}
	stack_push_dword(REG_CS.sel.value);
	stack_push_dword(REG_EIP);
	SET_CS(_cs);
	SET_EIP(_eip);
	g_cpubus.invalidate_pq();
}

void CPUExecutor::call_pmode(uint16_t cs_raw, uint32_t disp)
{
	Selector   cs_selector;
	Descriptor cs_descriptor;

	/* new cs selector must not be null, else #GP(0) */
	if((cs_raw & SELECTOR_RPL_MASK) == 0) {
		PDEBUGF(LOG_V2, LOG_CPU,"call_pmode: CS selector null\n");
		throw CPUException(CPU_GP_EXC, 0);
	}
	cs_selector = cs_raw;

	// check new CS selector index within its descriptor limits,
	// else #GP(new CS selector)
	try {
		cs_descriptor = fetch_descriptor(cs_selector, CPU_GP_EXC);
	} catch(CPUException &e) {
		PDEBUGF(LOG_V2, LOG_CPU, "call_pmode: descriptor fetch error\n");
		throw;
	}

	// examine AR byte of selected descriptor for various legal values
	if(!cs_descriptor.valid) {
		PDEBUGF(LOG_V2, LOG_CPU,"call_pmode: invalid CS descriptor\n");
		throw CPUException(CPU_GP_EXC, cs_raw & SELECTOR_RPL_MASK);
	}

	if(cs_descriptor.segment) {  // normal segment

		CPUCore::check_CS(cs_raw, cs_descriptor, SELECTOR_RPL(cs_raw), CPL);

		uint32_t temp_ESP = (REG_SS.desc.big)?(REG_ESP):(REG_SP);
		uint16_t errcode = REG_SS.sel.rpl != CPL ? (REG_SS.sel.value & SELECTOR_RPL_MASK) : 0;

		if(m_instr->op32) {
			write_dword(REG_SS, temp_ESP-4, REG_CS.sel.value, cs_descriptor.dpl, CPU_SS_EXC, errcode);
			write_dword(REG_SS, temp_ESP-8, REG_EIP, cs_descriptor.dpl, CPU_SS_EXC, errcode);
			temp_ESP -= 8;
		} else {
			write_word(REG_SS, temp_ESP-2, REG_CS.sel.value, cs_descriptor.dpl, CPU_SS_EXC, errcode);
			write_word(REG_SS, temp_ESP-4, REG_IP, cs_descriptor.dpl, CPU_SS_EXC, errcode);
			temp_ESP -= 4;
		}

		// load code segment descriptor into CS cache
		// load CS with new code segment selector
		// set RPL of CS to CPL
		branch_far(cs_selector, cs_descriptor, disp, CPL);

		if(REG_SS.desc.big) {
			REG_ESP = temp_ESP;
		} else {
			REG_SP = uint16_t(temp_ESP);
		}

		return;

	} else { // gate & special segment

		Descriptor  gate_descriptor = cs_descriptor;
		Selector    gate_selector = cs_selector;

		// descriptor DPL must be >= CPL else #GP(gate selector)
		if(gate_descriptor.dpl < CPL) {
			PDEBUGF(LOG_V2, LOG_CPU,"call_pmode: descriptor.dpl < CPL\n");
			throw CPUException(CPU_GP_EXC, cs_raw & SELECTOR_RPL_MASK);
		}

		// descriptor DPL must be >= gate selector RPL else #GP(gate selector)
		if(gate_descriptor.dpl < gate_selector.rpl) {
			PDEBUGF(LOG_V2, LOG_CPU,"call_pmode: descriptor.dpl < selector.rpl\n");
			throw CPUException(CPU_GP_EXC, cs_raw & SELECTOR_RPL_MASK);
		}

		switch(gate_descriptor.type) {
			case DESC_TYPE_AVAIL_286_TSS:
			case DESC_TYPE_AVAIL_386_TSS:
				PDEBUGF(LOG_V2, LOG_CPU, "call_pmode: available TSS\n");
				if(!gate_descriptor.valid || gate_selector.ti) {
					PDEBUGF(LOG_V2, LOG_CPU,"call_pmode: call bad TSS selector!\n");
					throw CPUException(CPU_GP_EXC, cs_raw & SELECTOR_RPL_MASK);
				}

				// TSS must be present, else #NP(TSS selector)
				if(!gate_descriptor.present) {
					PDEBUGF(LOG_V2, LOG_CPU,"call_pmode: call not present TSS !\n");
					throw CPUException(CPU_NP_EXC, cs_raw & SELECTOR_RPL_MASK);
				}

				// SWITCH_TASKS _without_ nesting to TSS
				switch_tasks(gate_selector, gate_descriptor, CPU_TASK_FROM_CALL);
				return;

			case DESC_TYPE_TASK_GATE:
				task_gate(gate_selector, gate_descriptor, CPU_TASK_FROM_CALL);
				return;

			case DESC_TYPE_286_CALL_GATE:
			case DESC_TYPE_386_CALL_GATE:
				// gate descriptor must be present else #NP(gate selector)
				if(!gate_descriptor.present) {
					PDEBUGF(LOG_V2, LOG_CPU,"call_pmode: gate not present\n");
					throw CPUException(CPU_NP_EXC, cs_raw & SELECTOR_RPL_MASK);
				}
				call_gate(gate_descriptor);
				return;

			default: // can't get here
				PDEBUGF(LOG_V2, LOG_CPU,"call_pmode: gate.type(%u) unsupported\n",
						(unsigned) gate_descriptor.type);
				throw CPUException(CPU_GP_EXC, cs_raw & SELECTOR_RPL_MASK);
		}
	}
}

void CPUExecutor::jump_call_gate(Selector &selector, Descriptor &gate_descriptor)
{
	Selector   gate_cs_selector;
	Descriptor gate_cs_descriptor;

	// task gate must be present else #NP(gate selector)
	if(!gate_descriptor.present) {
		PERRF(LOG_CPU,"jump_call_gate: call gate not present!\n");
		throw CPUException(CPU_NP_EXC, selector.value & SELECTOR_RPL_MASK);
	}

	gate_cs_selector = gate_descriptor.selector;

	// examine selector to code segment given in call gate descriptor
	// selector must not be null, else #GP(0)
	if((gate_cs_selector.value & SELECTOR_RPL_MASK) == 0) {
		PERRF(LOG_CPU,"jump_call_gate: CS selector null\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	// selector must be within its descriptor table limits else #GP(CS selector)
	gate_cs_descriptor = fetch_descriptor(gate_cs_selector, CPU_GP_EXC);

	// check code-segment descriptor
	CPUCore::check_CS(gate_cs_selector, gate_cs_descriptor, 0, CPL);

	uint32_t newEIP = gate_descriptor.offset;
	branch_far(gate_cs_selector, gate_cs_descriptor, newEIP, CPL);
}

void CPUExecutor::iret_pmode(bool _32bit)
{
	Selector cs_selector, ss_selector;
	Descriptor cs_descriptor, ss_descriptor;

	if(FLAG_NT)   /* NT = 1: RETURN FROM NESTED TASK */
	{
		/* what's the deal with NT & VM ? */
		Selector   link_selector;
		Descriptor tss_descriptor;

		assert(!FLAG_VM);

		PDEBUGF(LOG_V2, LOG_CPU, "iret_pmode: nested task return\n");

		if(!REG_TR.desc.valid)
			PERRF_ABORT(LOG_CPU, "iret_pmode: TR not valid!\n");

		// examine back link selector in TSS addressed by current TR
		link_selector = read_word(REG_TR.desc.base);

		// must specify global, else #TS(new TSS selector)
		if(link_selector.ti) {
			PDEBUGF(LOG_V2, LOG_CPU, "iret_pmode: link selector.ti=1\n");
			throw CPUException(CPU_TS_EXC, link_selector.value & SELECTOR_RPL_MASK);
		}

		// index must be within GDT limits, else #TS(new TSS selector)
		tss_descriptor = fetch_descriptor(link_selector, CPU_TS_EXC);

		if(!tss_descriptor.valid || tss_descriptor.segment) {
			PDEBUGF(LOG_V2, LOG_CPU, "iret_pmode: TSS selector points to bad TSS\n");
			throw CPUException(CPU_TS_EXC, link_selector.value & SELECTOR_RPL_MASK);
		}
		// AR byte must specify TSS, else #TS(new TSS selector)
		// new TSS must be busy, else #TS(new TSS selector)
		if(tss_descriptor.type != DESC_TYPE_BUSY_286_TSS &&
		   tss_descriptor.type != DESC_TYPE_BUSY_386_TSS)
		{
			PDEBUGF(LOG_V2, LOG_CPU, "iret_pmode: TSS selector points to bad TSS\n");
			throw CPUException(CPU_TS_EXC, link_selector.value & SELECTOR_RPL_MASK);
		}

		// TSS must be present, else #NP(new TSS selector)
		if(!tss_descriptor.present) {
			PDEBUGF(LOG_V2, LOG_CPU, "iret_pmode: task descriptor.p == 0\n");
			throw CPUException(CPU_NP_EXC, link_selector.value & SELECTOR_RPL_MASK);
		}

		// switch tasks (without nesting) to TSS specified by back link selector
		switch_tasks(link_selector, tss_descriptor, CPU_TASK_FROM_IRET);
		return;
	}

	/* NT = 0: INTERRUPT RETURN ON STACK */
	uint32_t new_esp, new_eip = 0, new_eflags = 0, temp_ESP;

	/* 16bit opsize  |   32bit opsize
	 * ==============================
	 * SS     eSP+8  |   SS     eSP+16
	 * SP     eSP+6  |   ESP    eSP+12
	 * -------------------------------
	 * FLAGS  eSP+4  |   EFLAGS eSP+8
	 * CS     eSP+2  |   CS     eSP+4
	 * IP     eSP+0  |   EIP    eSP+0
	 */

	if(REG_SS.desc.big) {
		temp_ESP = REG_ESP;
	} else {
		temp_ESP = REG_SP;
	}
	unsigned top_nbytes_same;
	if(_32bit) {
		top_nbytes_same = 12;
		new_eflags  =          stack_read_dword(temp_ESP + 8);
		cs_selector = uint16_t(stack_read_dword(temp_ESP + 4));
		new_eip     =          stack_read_dword(temp_ESP + 0);
		if(new_eflags & FMASK_VM) {
			if(CPL == 0) {
				stack_return_to_v86(cs_selector, new_eip, new_eflags);
				return;
			}
		}

	} else {
		top_nbytes_same = 6;
		new_eflags  = stack_read_word(temp_ESP + 4);
	    cs_selector = stack_read_word(temp_ESP + 2);
	    new_eip     = stack_read_word(temp_ESP + 0);
	}

	// return CS selector must be non-null, else #GP(0)
	if((cs_selector.value & SELECTOR_RPL_MASK) == 0) {
		PDEBUGF(LOG_V2, LOG_CPU, "iret_pmode: return CS selector null\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	// selector index must be within descriptor table limits,
	// else #GP(return selector)
	cs_descriptor = fetch_descriptor(cs_selector, CPU_GP_EXC);

	// return CS selector RPL must be >= CPL, else #GP(return selector)
	if(cs_selector.rpl < CPL) {
		PDEBUGF(LOG_V2, LOG_CPU, "iret_pmode: return selector RPL < CPL\n");
		throw CPUException(CPU_GP_EXC, cs_selector.value & SELECTOR_RPL_MASK);
	}

	// check code-segment descriptor
	CPUCore::check_CS(cs_selector.value,cs_descriptor,0,cs_selector.rpl);

	if(cs_selector.rpl == CPL) { /* INTERRUPT RETURN TO SAME LEVEL */

		PDEBUGF(LOG_V2, LOG_CPU, "iret_pmode: return to same level\n");

		/* top 6/12 bytes on stack must be within limits, else #SS(0) */
		/* satisfied above */

		/* load CS-cache with new code segment descriptor */
		branch_far(cs_selector, cs_descriptor, new_eip, cs_selector.rpl);
		if(_32bit) {
			// IF only changed if (CPL <= EFLAGS.IOPL)
			// IOPL only changed if CPL == 0
			// VM unaffected
			write_eflags(new_eflags,
					(CPL == 0), // IOPL
					(CPL <= FLAG_IOPL), //IF
					true, //NT
					false //VM
				);
		} else {
			/* load flags with third word on stack */
			write_flags(new_eflags,
					(CPL == 0), //IOPL
					(CPL <= FLAG_IOPL), // IF
					true //NT
					);
		}

		/* increment stack by 6/12 */
		if(REG_SS.desc.big) {
			REG_ESP += top_nbytes_same;
		} else {
			REG_SP += top_nbytes_same;
		}
		return;

	} else { /* INTERRUPT RETURN TO OUTER PRIVILEGE LEVEL */

		PDEBUGF(LOG_V2, LOG_CPU, "iret_pmode: return to outer privilege level\n");

		/* 16bit opsize  |   32bit opsize
		 * ==============================
		 * SS     eSP+8  |   SS     eSP+16
		 * SP     eSP+6  |   ESP    eSP+12
		 * FLAGS  eSP+4  |   EFLAGS eSP+8
		 * CS     eSP+2  |   CS     eSP+4
		 * IP     eSP+0  |   EIP    eSP+0
		 */

		/* examine return SS selector and associated descriptor */
		if(_32bit) {
			ss_selector = stack_read_word(temp_ESP + 16);
		} else {
			ss_selector = stack_read_word(temp_ESP + 8);
		}

		/* selector must be non-null, else #GP(0) */
		if((ss_selector.value & SELECTOR_RPL_MASK) == 0) {
			PDEBUGF(LOG_V2, LOG_CPU, "iret_pmode: SS selector null\n");
			throw CPUException(CPU_GP_EXC, 0);
		}

		/* selector RPL must = RPL of return CS selector,
		 * else #GP(SS selector) */
		if(ss_selector.rpl != cs_selector.rpl) {
			PDEBUGF(LOG_V2, LOG_CPU, "iret_pmode: SS.rpl != CS.rpl\n");
			throw CPUException(CPU_GP_EXC, ss_selector.value & SELECTOR_RPL_MASK);
		}

		/* selector index must be within its descriptor table limits,
		 * else #GP(SS selector) */
		ss_descriptor = fetch_descriptor(ss_selector, CPU_GP_EXC);

		/* AR byte must indicate a writable data segment,
		 * else #GP(SS selector) */
		if(!ss_descriptor.valid || !ss_descriptor.is_data_segment() || !ss_descriptor.is_writeable()) {
			PDEBUGF(LOG_V2, LOG_CPU, "iret_pmode: SS AR byte not writable or code segment\n");
			throw CPUException(CPU_GP_EXC, ss_selector.value & SELECTOR_RPL_MASK);
		}

		/* stack segment DPL must equal the RPL of the return CS selector,
		 * else #GP(SS selector) */
		if(ss_descriptor.dpl != cs_selector.rpl) {
			PDEBUGF(LOG_V2, LOG_CPU, "iret_pmode: SS.dpl != CS selector RPL\n");
			throw CPUException(CPU_GP_EXC, ss_selector.value & SELECTOR_RPL_MASK);
		}

		/* SS must be present, else #NP(SS selector) */
		if(!ss_descriptor.present) {
			PDEBUGF(LOG_V2, LOG_CPU, "iret_pmode: SS not present!\n");
			throw CPUException(CPU_NP_EXC, ss_selector.value & SELECTOR_RPL_MASK);
		}

		if(_32bit) {
			new_esp    = stack_read_dword(temp_ESP + 12);
			new_eflags = stack_read_dword(temp_ESP + 8);
			new_eip    = stack_read_dword(temp_ESP + 0);
		} else {
			new_esp    = stack_read_word(temp_ESP + 6);
			new_eflags = stack_read_word(temp_ESP + 4);
			new_eip    = stack_read_word(temp_ESP + 0);
		}

		bool change_IF = (CPL <= FLAG_IOPL);
		bool change_IOPL = (CPL == 0);

		/* load CS:EIP from stack */
		/* load the CS-cache with CS descriptor */
		/* set CPL to the RPL of the return CS selector */
		branch_far(cs_selector, cs_descriptor, new_eip, cs_selector.rpl);

		// IF only changed if (prev_CPL <= FLAGS.IOPL)
		// IOPL only changed if prev_CPL == 0
		if(_32bit) {
			write_eflags(new_eflags, change_IOPL, change_IF, true, false);
		} else {
			write_flags(new_eflags, change_IOPL, change_IF, true);
		}

		// load SS:SP from stack
		// load the SS-cache with SS descriptor
		SET_SS(ss_selector, ss_descriptor, cs_selector.rpl);
		if(ss_descriptor.big) {
			REG_ESP = new_esp;
		} else {
			REG_SP = new_esp;
		}

		REG_ES.validate();
		REG_DS.validate();
		REG_FS.validate();
		REG_GS.validate();
	}
}

void CPUExecutor::return_near(uint32_t _newEIP, uint16_t _pop_bytes)
{
	if(_newEIP > REG_CS.desc.limit) {
		PDEBUGF(LOG_V2, LOG_CPU, "return_near: offset outside of CS limits\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	SET_EIP(_newEIP);

	if(REG_SS.desc.big) {
		REG_ESP += _pop_bytes; // pop bytes
	} else {
		REG_SP += _pop_bytes; // pop bytes
	}

	g_cpubus.invalidate_pq();
}

void CPUExecutor::return_far_rmode(uint16_t _newCS, uint32_t _newEIP, uint16_t _pop_bytes)
{
	// CS.LIMIT can't change when in real mode
	if(_newEIP > REG_CS.desc.limit) {
		PDEBUGF(LOG_V2, LOG_CPU,
				"return_far_real: instruction pointer not within code segment limits\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	SET_CS(_newCS);
	SET_EIP(_newEIP);

	if(REG_SS.desc.big) {
		REG_ESP += _pop_bytes;
	} else {
		REG_SP += _pop_bytes;
	}

	g_cpubus.invalidate_pq();
}

void CPUExecutor::return_far_pmode(uint16_t _pop_bytes, bool _32bit)
{
	Selector cs_selector, ss_selector;
	Descriptor cs_descriptor, ss_descriptor;
	uint32_t stack_param_offset;
	uint32_t return_EIP, return_ESP, temp_ESP;

	/* + 6+N*2: SS      | +12+N*4:     SS */
	/* + 4+N*2: SP      | + 8+N*4:    ESP */
	/*          parm N  | +        parm N */
	/*          parm 3  | +        parm 3 */
	/*          parm 2  | +        parm 2 */
	/* + 4:     parm 1  | + 8:     parm 1 */
	/* + 2:     CS      | + 4:         CS */
	/* + 0:     IP      | + 0:        EIP */

	if(REG_SS.desc.big) {
		temp_ESP = REG_ESP;
	} else {
		temp_ESP = REG_SP;
	}

	if(_32bit) {
		cs_selector = stack_read_dword(temp_ESP + 4);
		return_EIP  = stack_read_dword(temp_ESP);
		stack_param_offset = 8;
	} else {
		cs_selector = stack_read_word(temp_ESP + 2);
		return_EIP  = stack_read_word(temp_ESP);
		stack_param_offset = 4;
	}

	// selector must be non-null else #GP(0)
	if((cs_selector.value & SELECTOR_RPL_MASK) == 0) {
		PDEBUGF(LOG_V2, LOG_CPU, "return_far_pmode: CS selector null\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	// selector index must be within its descriptor table limits,
	// else #GP(selector)
	cs_descriptor = fetch_descriptor(cs_selector, CPU_GP_EXC);

	// return selector RPL must be >= CPL, else #GP(return selector)
	if(cs_selector.rpl < CPL) {
		PDEBUGF(LOG_V2, LOG_CPU, "return_far_pmode: CS.rpl < CPL\n");
		throw CPUException(CPU_GP_EXC, cs_selector.value & SELECTOR_RPL_MASK);
	}

	// descriptor AR byte must indicate code segment, else #GP(selector)
	// check code-segment descriptor
	CPUCore::check_CS(cs_selector.value, cs_descriptor, 0, cs_selector.rpl);

	// if return selector RPL == CPL then
	// RETURN TO SAME PRIVILEGE LEVEL
	if(cs_selector.rpl == CPL) {
		PDEBUGF(LOG_V2, LOG_CPU, "return_far_pmode: return to SAME PRIVILEGE LEVEL\n");
		branch_far(cs_selector, cs_descriptor, return_EIP, CPL);
		if(REG_SS.desc.big) {
			REG_ESP += stack_param_offset + _pop_bytes;
		} else {
			REG_SP += stack_param_offset + _pop_bytes;
		}
	}
	/* RETURN TO OUTER PRIVILEGE LEVEL */
	else {
		/* + 6+N*2: SS      | +12+N*4:     SS */
		/* + 4+N*2: SP      | + 8+N*4:    ESP */
		/*          parm N  | +        parm N */
		/*          parm 3  | +        parm 3 */
		/*          parm 2  | +        parm 2 */
		/* + 4:     parm 1  | + 8:     parm 1 */
		/* + 2:     CS      | + 4:         CS */
		/* + 0:     IP      | + 0:        EIP */

		PDEBUGF(LOG_V2, LOG_CPU, "return_far_pmode: return to OUTER PRIVILEGE LEVEL\n");
		if(_32bit) {
			ss_selector = stack_read_word(temp_ESP + 12 + _pop_bytes);
			return_ESP  = stack_read_dword(temp_ESP + 8 + _pop_bytes);
		} else {
			ss_selector = stack_read_word(temp_ESP + 6 + _pop_bytes);
			return_ESP  = stack_read_word(temp_ESP + 4 + _pop_bytes);
		}

		if((ss_selector.value & SELECTOR_RPL_MASK) == 0) {
			PDEBUGF(LOG_V2, LOG_CPU, "return_far_pmode: SS selector null\n");
			throw CPUException(CPU_GP_EXC, 0);
		}

		// selector index must be within its descriptor table limits,
		// else #GP(selector)
		ss_descriptor = fetch_descriptor(ss_selector, CPU_GP_EXC);

		// selector RPL must = RPL of the return CS selector,
		// else #GP(selector)
		if(ss_selector.rpl != cs_selector.rpl) {
			PDEBUGF(LOG_V2, LOG_CPU, "return_far_pmode: ss.rpl != cs.rpl\n");
			throw CPUException(CPU_GP_EXC, ss_selector.value & SELECTOR_RPL_MASK);
		}

		// descriptor AR byte must indicate a writable data segment,
		// else #GP(selector)
		if(!ss_descriptor.valid || !ss_descriptor.is_data_segment() || !ss_descriptor.is_writeable()) {
			PDEBUGF(LOG_V2, LOG_CPU, "return_far_pmode: SS.AR byte not writable data\n");
			throw CPUException(CPU_GP_EXC, ss_selector.value & SELECTOR_RPL_MASK);
		}

		// descriptor dpl must == RPL of the return CS selector,
		// else #GP(selector)
		if(ss_descriptor.dpl != cs_selector.rpl) {
			PDEBUGF(LOG_V2, LOG_CPU, "return_far_pmode: SS.dpl != cs.rpl\n");
			throw CPUException(CPU_GP_EXC, ss_selector.value & SELECTOR_RPL_MASK);
		}

		// segment must be present else #SS(selector)
		if(!ss_descriptor.present) {
			PDEBUGF(LOG_V2, LOG_CPU, "return_pmode: ss.present == 0\n");
			throw CPUException(CPU_SS_EXC, ss_selector.value & SELECTOR_RPL_MASK);
		}

		branch_far(cs_selector, cs_descriptor, return_EIP, cs_selector.rpl);

		if((ss_selector.value & SELECTOR_RPL_MASK) != 0) {
			// load SS:RSP from stack
			// load the SS-cache with SS descriptor
			SET_SS(ss_selector, ss_descriptor, cs_selector.rpl);
		}

		if(ss_descriptor.big) {
			REG_ESP = return_ESP + _pop_bytes;
		} else {
			REG_SP  = uint16_t(return_ESP + _pop_bytes);
		}

		/* check ES, DS, FS, GS for validity */
		REG_ES.validate();
		REG_DS.validate();
		REG_FS.validate();
		REG_GS.validate();
	}
}

//
// Notes:
//
// The high bits of the 32bit eip image are ignored by
// the IRET to VM.  The high bits of the 32bit esp image
// are loaded into ESP.  A subsequent push uses
// only the low 16bits since it's in VM.  In neither case
// did a protection fault occur during actual tests.  This
// is contrary to the Intel docs which claim a #GP for
// eIP out of code limits.
//
// IRET to VM does affect IOPL, IF, VM, and RF
//
void CPUExecutor::stack_return_to_v86(Selector &cs_selector, uint32_t new_eip, uint32_t flags32)
{
	uint32_t temp_ESP, new_esp;
	uint16_t es_selector, ds_selector, fs_selector, gs_selector, ss_selector;

	// Must be 32bit effective opsize, VM is set in upper 16bits of eFLAGS
	// and CPL = 0 to get here

	// ----------------
	// |     | OLD GS | eSP+32
	// |     | OLD FS | eSP+28
	// |     | OLD DS | eSP+24
	// |     | OLD ES | eSP+20
	// |     | OLD SS | eSP+16
	// |  OLD ESP     | eSP+12
	// |  OLD EFLAGS  | eSP+8
	// |     | OLD CS | eSP+4
	// |  OLD EIP     | eSP+0
	// ----------------

	if(REG_SS.desc.big) {
		temp_ESP = REG_ESP;
	} else {
		temp_ESP = REG_SP;
	}

	// load SS:ESP from stack
	new_esp     =          stack_read_dword(temp_ESP + 12);
	ss_selector = uint16_t(stack_read_dword(temp_ESP + 16));

	// load ES,DS,FS,GS from stack
	es_selector = uint16_t(stack_read_dword(temp_ESP + 20));
	ds_selector = uint16_t(stack_read_dword(temp_ESP + 24));
	fs_selector = uint16_t(stack_read_dword(temp_ESP + 28));
	gs_selector = uint16_t(stack_read_dword(temp_ESP + 32));

	// load CS:IP from stack; already read and passed as args
	REG_CS.sel = cs_selector;
	SET_IP(new_eip);

	REG_ES.sel = es_selector;
	REG_DS.sel = ds_selector;
	REG_FS.sel = fs_selector;
	REG_GS.sel = gs_selector;
	REG_SS.sel = ss_selector;
	REG_ESP = new_esp; // full 32 bit are loaded

	for(unsigned sreg = REGI_ES; sreg <= REGI_GS; sreg++) {
		SEG_REG(sreg).desc.set_AR(SEG_SEGMENT|SEG_PRESENT|SEG_READWRITE|SEG_ACCESSED);
		SEG_REG(sreg).desc.dpl = 3;
		SEG_REG(sreg).desc.base = SEG_REG(sreg).sel.value << 4;
		SEG_REG(sreg).desc.limit = 0xFFFF;
		SEG_REG(sreg).desc.page_granular  = false;
		SEG_REG(sreg).desc.big  = false;
		SEG_REG(sreg).sel.rpl = 3;
	}

	//trigger the mode change
	write_eflags(flags32, true, true, true, true);
}
