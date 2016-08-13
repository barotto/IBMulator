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
		write_word(new_stack, temp_SP-2, return_SS, CPU_SS_EXC, errcode);
		write_word(new_stack, temp_SP-4, return_SP, CPU_SS_EXC, errcode);
		temp_SP -= 4;

		for(unsigned n = param_count; n>0; n--) {
			temp_SP -= 2;
			uint32_t addr = GET_PHYADDR(SS, return_SP + (n-1)*2);
			uint16_t param = g_cpubus.mem_read<2>(addr);
			write_word(new_stack, temp_SP, param, CPU_SS_EXC, errcode);
		}
		// push return address onto new stack
		write_word(new_stack, temp_SP-2, return_CS, CPU_SS_EXC, errcode);
		write_word(new_stack, temp_SP-4, return_IP, CPU_SS_EXC, errcode);
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
		stack_push_word(REG_CS.sel.value);
		stack_push_word(REG_IP);

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
		uint16_t errcode = REG_SS.sel.rpl != CPL ? (REG_SS.sel.value & SELECTOR_RPL_MASK) : 0;

		write_word(REG_SS, temp_SP - 2, REG_CS.sel.value, CPU_SS_EXC, errcode);
		write_word(REG_SS, temp_SP - 4, REG_IP, CPU_SS_EXC, errcode);
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
			case DESC_TYPE_AVAIL_286_TSS:
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

			case DESC_TYPE_286_CALL_GATE:
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
