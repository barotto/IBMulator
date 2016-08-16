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
	PDEBUGF(LOG_V2, LOG_CPU, "call gate\n");

	cs_selector     = gate_descriptor.selector;
	uint16_t new_IP = gate_descriptor.offset;

	// selector must not be null else #GP(0)
	if((cs_selector.value & SELECTOR_RPL_MASK) == 0) {
		PDEBUGF(LOG_V2, LOG_CPU, "call_gate: selector in gate null\n");
		throw CPUException(CPU_GP_EXC, 0);
	}
	// selector must be within its descriptor table limits,
	//   else #GP(code segment selector)
	cs_descriptor = g_cpucore.fetch_descriptor(cs_selector, CPU_GP_EXC);

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
			PDEBUGF(LOG_V2, LOG_CPU, "call_gate: new SS null\n");
			throw CPUException(CPU_TS_EXC, 0);
		}

		// selector index must be within its descriptor table limits,
		//   else #TS(SS selector)
		ss_selector   = SS_for_cpl_x;
		ss_descriptor = g_cpucore.fetch_descriptor(ss_selector, CPU_TS_EXC);

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
			PDEBUGF(LOG_V2, LOG_CPU, "call_gate: IP not within CS limits\n");
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

void CPUExecutor::branch_far_pmode(uint16_t _cs, uint16_t _disp)
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
	descriptor = g_cpucore.fetch_descriptor(selector, CPU_GP_EXC);

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
				jump_call_gate(selector, descriptor);
				return;
			default:
				PDEBUGF(LOG_V2, LOG_CPU,"branch_far_pmode: gate type %u unsupported\n", descriptor.type);
				throw CPUException(CPU_GP_EXC, _cs & SELECTOR_RPL_MASK);
		}
	}
}

void CPUExecutor::call_pmode(uint16_t cs_raw, uint16_t disp)
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
		cs_descriptor = g_cpucore.fetch_descriptor(cs_selector, CPU_GP_EXC);
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
			PDEBUGF(LOG_V2, LOG_CPU,"call_pmode: descriptor.dpl < CPL\n");
			throw CPUException(CPU_GP_EXC, cs_raw & SELECTOR_RPL_MASK);
		}

		// descriptor DPL must be >= gate selector RPL else #GP(gate selector)
		if(gate_descriptor.dpl < gate_selector.rpl) {
			PDEBUGF(LOG_V2, LOG_CPU,"call_pmode: descriptor.dpl < selector.rpl\n");
			throw CPUException(CPU_GP_EXC, cs_raw & SELECTOR_RPL_MASK);
		}

		switch (gate_descriptor.type) {
			case DESC_TYPE_AVAIL_286_TSS:
				PDEBUGF(LOG_V2, LOG_CPU, "call_pmode: available TSS\n");
				if (!gate_descriptor.valid || gate_selector.ti) {
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
	gate_cs_descriptor = g_cpucore.fetch_descriptor(gate_cs_selector, CPU_GP_EXC);

	// check code-segment descriptor
	CPUCore::check_CS(gate_cs_selector, gate_cs_descriptor, 0, CPL);

	uint16_t newIP = gate_descriptor.offset;
	branch_far(gate_cs_selector, gate_cs_descriptor, newIP, CPL);
}

void CPUExecutor::iret_pmode()
{
	Selector cs_selector, ss_selector;
	Descriptor cs_descriptor, ss_descriptor;

	if(FLAG_NT)   /* NT = 1: RETURN FROM NESTED TASK */
	{
		/* what's the deal with NT ? */
		Selector   link_selector;
		Descriptor tss_descriptor;

		PDEBUGF(LOG_V2, LOG_CPU, "iret_pmode: nested task return\n");

		if(!REG_TR.desc.valid)
			PERRF_ABORT(LOG_CPU, "iret_pmode: TR not valid!\n");

		// examine back link selector in TSS addressed by current TR
		link_selector = g_cpubus.mem_read<2>(REG_TR.desc.base);

		// must specify global, else #TS(new TSS selector)
		if(link_selector.ti) {
			PDEBUGF(LOG_V2, LOG_CPU, "iret_pmode: link selector.ti=1\n");
			throw CPUException(CPU_TS_EXC, link_selector.value & SELECTOR_RPL_MASK);
		}

		// index must be within GDT limits, else #TS(new TSS selector)
		tss_descriptor = g_cpucore.fetch_descriptor(link_selector, CPU_TS_EXC);

		if(!tss_descriptor.valid || tss_descriptor.segment) {
			PDEBUGF(LOG_V2, LOG_CPU, "iret_pmode: TSS selector points to bad TSS\n");
			throw CPUException(CPU_TS_EXC, link_selector.value & SELECTOR_RPL_MASK);
		}
		// AR byte must specify TSS, else #TS(new TSS selector)
		// new TSS must be busy, else #TS(new TSS selector)
		if(tss_descriptor.type != DESC_TYPE_BUSY_286_TSS) {
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

	new_flags   = stack_read_word(REG_SP + 4);
    cs_selector = stack_read_word(REG_SP + 2);
    new_ip      = stack_read_word(REG_SP + 0);

	// return CS selector must be non-null, else #GP(0)
	if((cs_selector.value & SELECTOR_RPL_MASK) == 0) {
		PDEBUGF(LOG_V2, LOG_CPU, "iret_pmode: return CS selector null\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	// selector index must be within descriptor table limits,
	// else #GP(return selector)
	cs_descriptor = g_cpucore.fetch_descriptor(cs_selector, CPU_GP_EXC);

	// return CS selector RPL must be >= CPL, else #GP(return selector)
	if(cs_selector.rpl < CPL) {
		PDEBUGF(LOG_V2, LOG_CPU, "iret_pmode: return selector RPL < CPL\n");
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

		ss_selector = stack_read_word(REG_SP + 8);

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
		ss_descriptor = g_cpucore.fetch_descriptor(ss_selector, CPU_GP_EXC);

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

		new_ip    = stack_read_word(REG_SP + 0);
		new_flags = stack_read_word(REG_SP + 4);
		new_sp    = stack_read_word(REG_SP + 6);

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

void CPUExecutor::return_pmode(uint16_t pop_bytes)
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

	return_IP   = stack_read_word(temp_SP);
	cs_selector = stack_read_word(temp_SP + 2);

	// selector must be non-null else #GP(0)
	if((cs_selector.value & SELECTOR_RPL_MASK) == 0) {
		PDEBUGF(LOG_V2, LOG_CPU, "return_pmode: CS selector null\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	// selector index must be within its descriptor table limits,
	// else #GP(selector)
	cs_descriptor = g_cpucore.fetch_descriptor(cs_selector, CPU_GP_EXC);

	// return selector RPL must be >= CPL, else #GP(return selector)
	if(cs_selector.rpl < CPL) {
		PDEBUGF(LOG_V2, LOG_CPU, "return_pmode: CS.rpl < CPL\n");
		throw CPUException(CPU_GP_EXC, cs_selector.value & SELECTOR_RPL_MASK);
	}

	// descriptor AR byte must indicate code segment, else #GP(selector)
	// check code-segment descriptor
	CPUCore::check_CS(cs_selector.value, cs_descriptor, 0, cs_selector.rpl);

	// if return selector RPL == CPL then
	// RETURN TO SAME PRIVILEGE LEVEL
	if(cs_selector.rpl == CPL) {
		PDEBUGF(LOG_V2, LOG_CPU, "return_pmode: return to SAME PRIVILEGE LEVEL\n");
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

		PDEBUGF(LOG_V2, LOG_CPU, "return_pmode: return to OUTER PRIVILEGE LEVEL\n");
		return_SP   = stack_read_word(temp_SP + 4 + pop_bytes);
		ss_selector = stack_read_word(temp_SP + 6 + pop_bytes);

		if((ss_selector.value & SELECTOR_RPL_MASK) == 0) {
			PDEBUGF(LOG_V2, LOG_CPU, "return_pmode: SS selector null\n");
			throw CPUException(CPU_GP_EXC, 0);
		}

		// selector index must be within its descriptor table limits,
		// else #GP(selector)
		ss_descriptor = g_cpucore.fetch_descriptor(ss_selector, CPU_GP_EXC);

		// selector RPL must = RPL of the return CS selector,
		// else #GP(selector)
		if(ss_selector.rpl != cs_selector.rpl) {
			PDEBUGF(LOG_V2, LOG_CPU, "return_pmode: ss.rpl != cs.rpl\n");
			throw CPUException(CPU_GP_EXC, ss_selector.value & SELECTOR_RPL_MASK);
		}

		// descriptor AR byte must indicate a writable data segment,
		// else #GP(selector)
		if(!ss_descriptor.valid || !ss_descriptor.is_data_segment() || !ss_descriptor.is_writeable()) {
			PDEBUGF(LOG_V2, LOG_CPU, "return_pmode: SS.AR byte not writable data\n");
			throw CPUException(CPU_GP_EXC, ss_selector.value & SELECTOR_RPL_MASK);
		}

		// descriptor dpl must == RPL of the return CS selector,
		// else #GP(selector)
		if(ss_descriptor.dpl != cs_selector.rpl) {
			PDEBUGF(LOG_V2, LOG_CPU, "return_pmode: SS.dpl != cs.rpl\n");
			throw CPUException(CPU_GP_EXC, ss_selector.value & SELECTOR_RPL_MASK);
		}

		// segment must be present else #SS(selector)
		if(!ss_descriptor.present) {
			PDEBUGF(LOG_V2, LOG_CPU, "return_pmode: ss.present == 0\n");
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
