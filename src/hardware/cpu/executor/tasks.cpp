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

void CPUExecutor::get_SS_SP_from_TSS(unsigned pl, uint16_t &ss_, uint16_t &sp_)
{
	if(!REG_TR.desc.valid)
		PERRF_ABORT(LOG_CPU, "get_SS_ESP_from_TSS: TR invalid\n");

	if(!(REG_TR.desc.type!=DESC_TYPE_AVAIL_286_TSS || REG_TR.desc.type!=DESC_TYPE_BUSY_286_TSS)) {
		PERRF_ABORT(LOG_CPU, "get_SS_ESP_from_TSS: TR is bogus type (%u)", REG_TR.desc.type);
	}

	uint32_t TSSstackaddr = 4 * pl + 2;
	if((TSSstackaddr+3) > REG_TR.desc.limit) {
		PDEBUGF(LOG_V2, LOG_CPU, "get_SS_SP_from_TSS: TSSstackaddr > TSS.LIMIT\n");
		throw CPUException(CPU_TS_EXC, REG_TR.sel.value & SELECTOR_RPL_MASK);
	}
	ss_ = g_cpubus.mem_read<2>(REG_TR.desc.base + TSSstackaddr + 2);
	sp_ = g_cpubus.mem_read<2>(REG_TR.desc.base + TSSstackaddr);
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
		raw_es_selector, raw_fs_selector, raw_gs_selector, raw_ldt_selector;
	uint16_t trap_word;
	Descriptor cs_descriptor, ss_descriptor, ldt_descriptor;
	uint32_t new_TSS_max, old_TSS_max, old_TSS_limit, new_TSS_limit;
	uint32_t newEAX, newECX, newEDX, newEBX;
	uint32_t newESP, newEBP, newESI, newEDI;
	uint32_t newEFLAGS, newEIP, newCR3;

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
	//         to verify that the TSS limit is greater than or equal to 67h for
	//         32-bit TSS or 2Bh for 16-bit TSS.

	if(descriptor.is_286_system()) {
		new_TSS_max = 0x2B;
	} else {
		new_TSS_max = 0x67;
	}

	nbase32 = descriptor.base;
	new_TSS_limit = descriptor.limit;

	if(new_TSS_limit < new_TSS_max) {
		PERRF(LOG_CPU,"switch_tasks(): new TSS limit < %d\n", new_TSS_max);
		throw CPUException(CPU_TS_EXC, selector.value & SELECTOR_RPL_MASK);
	}

	if(REG_TR.desc.type <= 3) {
		old_TSS_max = 0x29;
	} else {
		old_TSS_max = 0x5F;
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

	// Check that old TSS, new TSS, and all segment descriptors
	// used in the task switch are paged in.
	if(IS_PAGING()) {
		//TODO a comment in Bochs suggests to check for WRITE.
		TLB_check(nbase32, false, false);
		TLB_check(nbase32 + new_TSS_max, false, false);
		if(source==CPU_TASK_FROM_CALL || source==CPU_TASK_FROM_INT) {
			TLB_check(nbase32, false, true);
			TLB_check(nbase32 + 1, false, true);
		}
	}

	// Privilege and busy checks done in CALL, JUMP, INT, IRET

	// Step 3: If JMP or IRET, clear busy bit in old task TSS descriptor,
	//         otherwise leave set.

	// effect on Busy bit of old task
	if(source == CPU_TASK_FROM_JUMP || source == CPU_TASK_FROM_IRET) {
		// Bit is cleared
		// Read only the AR byte
		const unsigned AR_byte_offset = 5;
		uint32_t laddr = GET_BASE(GDTR) + REG_TR.sel.index*8 + AR_byte_offset;
		uint8_t ar = read_byte(laddr);
		ar &= ~0x2;
		write_byte(laddr, ar);
	}

	// STEP 4: If the task switch was initiated with an IRET instruction,
	//         clears the NT flag in a temporarily saved EFLAGS image;
	//         if initiated with a CALL or JMP instruction, an exception, or
	//         an interrupt, the NT flag is left unchanged.

	uint32_t oldEFLAGS = GET_EFLAGS();

	/* if moving to busy task, clear NT bit */
	if(descriptor.type == DESC_TYPE_BUSY_286_TSS || descriptor.type == DESC_TYPE_BUSY_386_TSS) {
		oldEFLAGS &= ~FMASK_NT;
	}

	// STEP 5: Save the current task state in the TSS. Up to this point,
	//         any exception that occurs aborts the task switch without
	//         changing the processor state.

	/* save current machine state in old task's TSS */
	if(REG_TR.desc.is_286_system()) {
		//286
		if(IS_PAGING()) {
			// check that we won't page fault while writing
			uint32_t start = obase32 + 14, end = obase32 + 41;
			TLB_check(start, false, true);
			TLB_check(end,   false, true);
		}
		write_word(obase32 + 14, REG_IP);
		write_word(obase32 + 16, oldEFLAGS);
		write_word(obase32 + 18, REG_AX);
		write_word(obase32 + 20, REG_CX);
		write_word(obase32 + 22, REG_DX);
		write_word(obase32 + 24, REG_BX);
		write_word(obase32 + 26, REG_SP);
		write_word(obase32 + 28, REG_BP);
		write_word(obase32 + 30, REG_SI);
		write_word(obase32 + 32, REG_DI);
		write_word(obase32 + 34, REG_ES.sel.value);
		write_word(obase32 + 36, REG_CS.sel.value);
		write_word(obase32 + 38, REG_SS.sel.value);
		write_word(obase32 + 40, REG_DS.sel.value);
	} else {
		//386
		if(IS_PAGING()) {
			// check that we won't page fault while writing
			uint32_t start = obase32 + 0x20, end = obase32 + 0x5d;
			TLB_check(start, false, true);
			TLB_check(end,   false, true);
		}
		write_dword(obase32 + 0x20, REG_EIP);
		write_dword(obase32 + 0x24, oldEFLAGS);
		write_dword(obase32 + 0x28, REG_EAX);
		write_dword(obase32 + 0x2c, REG_ECX);
		write_dword(obase32 + 0x30, REG_EDX);
		write_dword(obase32 + 0x34, REG_EBX);
		write_dword(obase32 + 0x38, REG_ESP);
		write_dword(obase32 + 0x3c, REG_EBP);
		write_dword(obase32 + 0x40, REG_ESI);
		write_dword(obase32 + 0x44, REG_EDI);
		write_word(obase32 + 0x48, REG_ES.sel.value);
		write_word(obase32 + 0x4c, REG_CS.sel.value);
		write_word(obase32 + 0x50, REG_SS.sel.value);
		write_word(obase32 + 0x54, REG_DS.sel.value);
		write_word(obase32 + 0x58, REG_FS.sel.value);
		write_word(obase32 + 0x5c, REG_GS.sel.value);
	}
	// effect on link field of new task
	if(source == CPU_TASK_FROM_CALL || source == CPU_TASK_FROM_INT) {
		// set to selector of old task's TSS
		write_word(nbase32, REG_TR.sel.value);
	}

	// STEP 6: The new-task state is loaded from the TSS
	if(REG_TR.desc.is_286_system()) {
		//286
		/* incoming TSS is 16bit:
		 * upper word of EIP is zero'd
		 * upper word of EFLAGS is zero'd
		 * upper word of general registers is set to 0xFFFF
		 * FS, GS are zero'd
		 */
		newEIP    = read_word(nbase32 + 14);
		newEFLAGS = read_word(nbase32 + 16);
		newEAX = 0xFFFF0000 | read_word(nbase32 + 18);
		newECX = 0xFFFF0000 | read_word(nbase32 + 20);
		newEDX = 0xFFFF0000 | read_word(nbase32 + 22);
		newEBX = 0xFFFF0000 | read_word(nbase32 + 24);
		newESP = 0xFFFF0000 | read_word(nbase32 + 26);
		newEBP = 0xFFFF0000 | read_word(nbase32 + 28);
		newESI = 0xFFFF0000 | read_word(nbase32 + 30);
		newEDI = 0xFFFF0000 | read_word(nbase32 + 32);
		raw_es_selector  = read_word(nbase32 + 34);
		raw_cs_selector  = read_word(nbase32 + 36);
		raw_ss_selector  = read_word(nbase32 + 38);
		raw_ds_selector  = read_word(nbase32 + 40);
		raw_ldt_selector = read_word(nbase32 + 42);
		raw_fs_selector = 0;
		raw_gs_selector = 0;
		newCR3 = 0;
		trap_word = 0;
	} else {
		//386
		if(IS_PAGING()) {
			newCR3 = read_dword(nbase32 + 0x1C);
		} else {
			newCR3 = 0;
		}
		newEIP    = read_dword(nbase32 + 0x20);
		newEFLAGS = read_dword(nbase32 + 0x24);
		newEAX    = read_dword(nbase32 + 0x28);
		newECX    = read_dword(nbase32 + 0x2C);
		newEDX    = read_dword(nbase32 + 0x30);
		newEBX    = read_dword(nbase32 + 0x34);
		newESP    = read_dword(nbase32 + 0x38);
		newEBP    = read_dword(nbase32 + 0x3C);
		newESI    = read_dword(nbase32 + 0x40);
		newEDI    = read_dword(nbase32 + 0x44);
		raw_es_selector  = read_word(nbase32 + 0x48);
		raw_cs_selector  = read_word(nbase32 + 0x4C);
		raw_ss_selector  = read_word(nbase32 + 0x50);
		raw_ds_selector  = read_word(nbase32 + 0x54);
		raw_fs_selector  = read_word(nbase32 + 0x58);
		raw_gs_selector  = read_word(nbase32 + 0x5C);
		raw_ldt_selector = read_word(nbase32 + 0x60);
		trap_word        = read_word(nbase32 + 0x64);
	}
	// Step 7: If CALL, interrupt, or JMP, set busy flag in new task's
	//         TSS descriptor.  If IRET, leave set.

	if(source != CPU_TASK_FROM_IRET) {
		// set the new task's busy bit
		const unsigned AR_byte_offset = 5;
		uint32_t laddr = GET_BASE(GDTR) + (selector.index*8) + AR_byte_offset;
		uint8_t ar = read_byte(laddr);
		ar |= 0x2;
		write_byte(laddr, ar);
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
	REG_TR.desc.type |= TSS_BUSY_BIT; // mark TSS in TR as busy

	// Step 9: Set TS flag

	SET_CR0(TS, true);

	// Task switch clears LE/L3/L2/L1/L0 in DR7
	// TODO
	//REG_DR7 &= ~0x00000155;

	// Step 10: If call or interrupt, set the NT flag in the eflags
	//          image stored in new task's TSS.  If IRET or JMP,
	//          NT is restored from new TSS eflags image. (no change)

	// effect on NT flag of new task
	if(source == CPU_TASK_FROM_CALL || source == CPU_TASK_FROM_INT) {
		newEFLAGS |= FMASK_NT; // NT flag is set
	}

	// Step 11: Load the new task (dynamic) state from new TSS.
	//          Any errors associated with loading and qualification of
	//          segment descriptors in this step occur in the new task's
	//          context.  State loaded here includes LDTR,
	//          EFLAGS, EIP, general purpose registers, and segment
	//          descriptor parts of the segment registers.

	SET_EIP(newEIP);

	REG_EAX = newEAX;
	REG_ECX = newECX;
	REG_EDX = newEDX;
	REG_EBX = newEBX;
	REG_ESP = newESP;
	REG_EBP = newEBP;
	REG_ESI = newESI;
	REG_EDI = newEDI;

	SET_EFLAGS(newEFLAGS);

	// Fill in selectors for all segment registers.  If errors
	// occur later, the selectors will at least be loaded.
	REG_CS.sel   = raw_cs_selector;
	REG_SS.sel   = raw_ss_selector;
	REG_DS.sel   = raw_ds_selector;
	REG_ES.sel   = raw_es_selector;
	REG_FS.sel   = raw_fs_selector;
	REG_GS.sel   = raw_gs_selector;
	REG_LDTR.sel = raw_ldt_selector;

	// Start out with invalid descriptor, fill in with
	// values only as they are validated
	REG_LDTR.desc.valid = false;
	REG_CS.desc.valid   = false;
	REG_SS.desc.valid   = false;
	REG_DS.desc.valid   = false;
	REG_ES.desc.valid   = false;
	REG_FS.desc.valid   = false;
	REG_GS.desc.valid   = false;

	if(descriptor.is_386_system() && IS_PAGING()) {
		// change CR3 only if it actually modified
		if(newCR3 != REG_CR3) {
			PDEBUGF(LOG_V2, LOG_CPU, "switch_tasks: changing CR3 to 0x%08X", newCR3);
			set_CR3(newCR3);
		}
	}

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

	if(IS_V8086()) {
	    // load seg regs as 8086 registers
		SET_SS(raw_ss_selector);
	    SET_DS(raw_ds_selector);
	    SET_ES(raw_es_selector);
	    SET_FS(raw_fs_selector);
	    SET_GS(raw_gs_selector);
	    SET_CS(raw_cs_selector);
	    // CPL is set from CS selector
	} else {
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
			if(!ss_descriptor.valid || !ss_descriptor.is_data_segment() ||
			   !ss_descriptor.is_writeable())
			{
				PERRF(LOG_CPU,"switch_tasks(exception after commit point): SS not valid or writeable segment\n");
				throw CPUException(CPU_TS_EXC, raw_ss_selector & SELECTOR_RPL_MASK);
			}

			// Stack segment is present in memory, else #SS(new stack segment)
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
		switch_tasks_load_selector(REG_FS, REG_CS.sel.rpl);
		switch_tasks_load_selector(REG_GS, REG_CS.sel.rpl);

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
			if(!cs_descriptor.valid || !cs_descriptor.is_code_segment()) {
				PERRF(LOG_CPU,"switch_tasks(exception after commit point): CS not valid executable seg\n");
				throw CPUException(CPU_TS_EXC, raw_cs_selector & SELECTOR_RPL_MASK);
			}

			// if non-conforming then DPL must equal selector RPL else #TS(CS)
			if(!cs_descriptor.is_conforming() && cs_descriptor.dpl != REG_CS.sel.rpl) {
				PERRF(LOG_CPU,"switch_tasks(exception after commit point): non-conforming: CS.dpl!=CS.RPL\n");
				throw CPUException(CPU_TS_EXC, raw_cs_selector & SELECTOR_RPL_MASK);
			}

			// if conforming then DPL must be <= selector RPL else #TS(CS)
			if(cs_descriptor.is_conforming() && cs_descriptor.dpl > REG_CS.sel.rpl) {
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
	}

	/* TODO
	if(descriptor.is_386_system() && (trap_word & 0x1)) {
		debug_trap |= DEBUG_TRAP_TASK_SWITCH_BIT; // BT flag
		g_cpu.async_event = true; // so processor knows to check
		PDEBUGF(LOG_V2, LOG_CPU, "switch_tasks: T bit set in new TSS\n");
	}
	*/

	//
	// Step 12: Begin execution of new task.
	//
	PDEBUGF(LOG_V2, LOG_CPU, "TASKING: LEAVE\n");

	// push error code onto stack
	if(push_error) {
		if(descriptor.is_386_system()) {
			stack_push_dword(error_code);
		} else {
			stack_push_word(error_code);
		}
	}

	// instruction pointer must be in CS limit, else #GP(0)
	if(REG_EIP > REG_CS.desc.limit) {
		PERRF(LOG_CPU,"switch_tasks: EIP > CS.limit\n");
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

	if(!tss_descriptor.valid || tss_descriptor.segment) {
		PERRF(LOG_CPU,"task_gate: TSS selector points to bad TSS\n");
		throw CPUException(CPU_GP_EXC, tss_selector.value & SELECTOR_RPL_MASK);
	}
	// descriptor AR byte must specify available TSS
	//   else #GP(TSS selector)
	if(tss_descriptor.type != DESC_TYPE_AVAIL_286_TSS &&
	   tss_descriptor.type != DESC_TYPE_AVAIL_386_TSS)
	{
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
