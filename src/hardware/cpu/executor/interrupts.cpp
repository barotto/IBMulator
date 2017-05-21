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

	if((_vector*4 + 2u + 1u) > GET_LIMIT(IDTR)) {
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
	stack_push_word(GET_FLAGS());
	stack_push_word(REG_CS.sel.value);
	stack_push_word(REG_IP);

	uint32_t addr = GET_BASE(IDTR) + _vector * 4;
	uint16_t new_ip = read_word(addr);
	uint16_t cs_selector = read_word(addr+2);

	SET_CS(cs_selector);
	SET_IP(new_ip);

	SET_FLAG(IF, false);
	SET_FLAG(TF, false);
	SET_FLAG(RF, false);

	g_cpubus.invalidate_pq();
}

template<typename T>
T CPUExecutor::interrupt_prepare_stack(SegReg &new_stack, T temp_ESP,
		unsigned _pl, unsigned _gate_type, bool _push_error, uint16_t _error_code)
{
	const uint8_t exc = CPU_SS_EXC;
	const uint16_t errcode = new_stack.sel.rpl!=CPL ? (new_stack.sel.value & SELECTOR_RPL_MASK) : 0;

	if(IS_V8086()) {
		if(_gate_type >= DESC_TYPE_386_INTR_GATE) { // 386 int/trap gate
			write_dword(new_stack, T(temp_ESP-4),  REG_GS.sel.value, _pl, exc, errcode);
			write_dword(new_stack, T(temp_ESP-8),  REG_FS.sel.value, _pl, exc, errcode);
			write_dword(new_stack, T(temp_ESP-12), REG_DS.sel.value, _pl, exc, errcode);
			write_dword(new_stack, T(temp_ESP-16), REG_ES.sel.value, _pl, exc, errcode);
			temp_ESP -= 16;
		} else {
			write_word(new_stack, T(temp_ESP-2), REG_GS.sel.value, _pl, exc, errcode);
			write_word(new_stack, T(temp_ESP-4), REG_FS.sel.value, _pl, exc, errcode);
			write_word(new_stack, T(temp_ESP-6), REG_DS.sel.value, _pl, exc, errcode);
			write_word(new_stack, T(temp_ESP-8), REG_ES.sel.value, _pl, exc, errcode);
			temp_ESP -= 8;
		}
	}

	if(_gate_type >= DESC_TYPE_386_INTR_GATE) { // 386 int/trap gate
		// push long pointer to old stack onto new stack
		write_dword(new_stack, T(temp_ESP-4),  REG_SS.sel.value, _pl, exc, errcode);
		write_dword(new_stack, T(temp_ESP-8),  REG_ESP,          _pl, exc, errcode);
		write_dword(new_stack, T(temp_ESP-12), GET_EFLAGS(),     _pl, exc, errcode);
		write_dword(new_stack, T(temp_ESP-16), REG_CS.sel.value, _pl, exc, errcode);
		write_dword(new_stack, T(temp_ESP-20), REG_EIP,          _pl, exc, errcode);
		temp_ESP -= 20;
		if (_push_error) {
			temp_ESP -= 4;
			write_dword(new_stack, temp_ESP, _error_code, _pl, exc, errcode);
		}
	} else { // 286 int/trap gate
		// push long pointer to old stack onto new stack
		write_word(new_stack, T(temp_ESP-2),  REG_SS.sel.value, _pl, exc, errcode);
		write_word(new_stack, T(temp_ESP-4),  REG_SP,           _pl, exc, errcode);
		write_word(new_stack, T(temp_ESP-6),  GET_FLAGS(),      _pl, exc, errcode);
		write_word(new_stack, T(temp_ESP-8),  REG_CS.sel.value, _pl, exc, errcode);
		write_word(new_stack, T(temp_ESP-10), REG_IP,           _pl, exc, errcode);
		temp_ESP -= 10;
		if (_push_error) {
			temp_ESP -= 2;
			write_word(new_stack, temp_ESP, _error_code, _pl, exc, errcode);
		}
	}

	return temp_ESP;
}

void CPUExecutor::interrupt_inner_privilege(Descriptor &gate_descriptor,
		Selector &cs_selector, Descriptor &cs_descriptor,
		bool _push_error, uint16_t _error_code)
{
	uint16_t SS_for_cpl_x;
	uint32_t ESP_for_cpl_x;
	Descriptor ss_descriptor;
	Selector   ss_selector;

	// check selector and descriptor for new stack in current TSS
	get_SS_ESP_from_TSS(cs_descriptor.dpl, SS_for_cpl_x, ESP_for_cpl_x);

	if(IS_V8086() && cs_descriptor.dpl != 0) {
		// if code segment DPL != 0 then #GP(new code segment selector)
		PDEBUGF(LOG_V2, LOG_CPU, "interrupt(): code segment DPL(%d) != 0 in v8086 mode\n", cs_descriptor.dpl);
		throw CPUException(CPU_GP_EXC, cs_selector.value & SELECTOR_RPL_MASK);
	}

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
		ss_descriptor = fetch_descriptor(ss_selector, CPU_TS_EXC);
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
	if(!ss_descriptor.valid || !ss_descriptor.is_data_segment() || !ss_descriptor.is_writeable())
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
	if(gate_descriptor.offset > cs_descriptor.limit) {
		PDEBUGF(LOG_V1,LOG_CPU,"interrupt(): gate EIP > CS.limit\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	// Prepare new stack segment
	SegReg new_stack;
	new_stack.sel = ss_selector;
	new_stack.desc = ss_descriptor;
	new_stack.sel.rpl = cs_descriptor.dpl;
	// add cpl to the selector value
	new_stack.sel.value =
		(new_stack.sel.value & SELECTOR_RPL_MASK) | new_stack.sel.rpl;

	if(new_stack.desc.big) {

		REG_ESP = interrupt_prepare_stack<uint32_t>(
				new_stack, ESP_for_cpl_x,
				cs_descriptor.dpl, gate_descriptor.type,
				_push_error, _error_code);

	} else {

		REG_SP = interrupt_prepare_stack<uint16_t>(
				new_stack, (uint16_t)ESP_for_cpl_x,
				cs_descriptor.dpl, gate_descriptor.type,
				_push_error, _error_code);

	}
	// load new CS:IP values from gate
	// set CPL to new code segment DPL
	// set RPL of CS to CPL
	SET_CS(cs_selector, cs_descriptor, cs_descriptor.dpl);
	//IP is set by the caller

	// load new SS:ESP values from TSS
	SET_SS(ss_selector, ss_descriptor, cs_descriptor.dpl);

    if(IS_V8086()) {
		REG_GS.desc.valid = false;
		REG_GS.sel.value = 0;
		REG_FS.desc.valid = false;
		REG_FS.sel.value = 0;
		REG_DS.desc.valid = false;
		REG_DS.sel.value = 0;
		REG_ES.desc.valid = false;
		REG_ES.sel.value = 0;
    }
}

void CPUExecutor::interrupt_same_privilege(Descriptor &gate_descriptor,
		Selector &cs_selector, Descriptor &cs_descriptor,
		bool _push_error, uint16_t _error_code)
{
	if(IS_V8086() && (cs_descriptor.is_conforming() || cs_descriptor.dpl != 0)) {
		// if code segment DPL != 0 then #GP(new code segment selector)
		PDEBUGF(LOG_V2, LOG_CPU, "interrupt(): code segment conforming or DPL(%d) != 0 in v8086 mode\n", cs_descriptor.dpl);
		throw CPUException(CPU_GP_EXC, cs_selector.value & SELECTOR_RPL_MASK);
	}

	// EIP must be in CS limit else #GP(0)
	if(gate_descriptor.offset > cs_descriptor.limit) {
		PDEBUGF(LOG_V1,LOG_CPU,"interrupt(): IP > CS descriptor limit\n");
		throw CPUException(CPU_GP_EXC, 0);
	}

	// push flags onto stack
	// push current CS selector onto stack
	// push return IP onto stack
	if(gate_descriptor.type >= DESC_TYPE_386_INTR_GATE) {
		stack_push_dword(GET_EFLAGS());
		stack_push_dword(REG_CS.sel.value);
		stack_push_dword(REG_EIP);
		if(_push_error) {
			stack_push_dword(_error_code);
		}
	} else {
		stack_push_word(GET_FLAGS());
		stack_push_word(REG_CS.sel.value);
		stack_push_word(REG_IP);
		if(_push_error) {
			stack_push_word(_error_code);
		}
	}

	// load CS:EIP from gate
	// load CS descriptor
	// set the RPL field of CS to CPL
	SET_CS(cs_selector, cs_descriptor, CPL);
}

void CPUExecutor::interrupt_pmode(uint8_t vector, bool soft_int,
		bool push_error, uint16_t error_code)
{
	Descriptor gate_descriptor;

	// interrupt vector must be within IDT table limits,
	// else #GP(vector*8 + 2 + EXT)
	if((vector*8 + 7) > GET_LIMIT(IDTR)) {
		PDEBUGF(LOG_V2,LOG_CPU,
			"interrupt(): vector must be within IDT table limits, IDT.limit = 0x%x\n",
			GET_LIMIT(IDTR));
		throw CPUException(CPU_GP_EXC, vector*8 + 2);
	}

	gate_descriptor = read_qword(GET_BASE(IDTR) + vector*8);

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
		case DESC_TYPE_286_INTR_GATE:
		case DESC_TYPE_286_TRAP_GATE:
		case DESC_TYPE_386_INTR_GATE:
		case DESC_TYPE_386_TRAP_GATE:
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
		case DESC_TYPE_TASK_GATE: {
			Selector tss_selector;
			Descriptor tss_descriptor;

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
				tss_descriptor = fetch_descriptor(tss_selector, CPU_GP_EXC);
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

			if(tss_descriptor.type != DESC_TYPE_AVAIL_286_TSS &&
			   tss_descriptor.type != DESC_TYPE_AVAIL_386_TSS)
			{
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
		}
		case DESC_TYPE_286_INTR_GATE:
		case DESC_TYPE_286_TRAP_GATE:
		case DESC_TYPE_386_INTR_GATE:
		case DESC_TYPE_386_TRAP_GATE: {
			Selector   cs_selector;
			Descriptor cs_descriptor;

			// examine CS selector and descriptor given in gate descriptor
			// selector must be non-null else #GP(EXT)
			if((gate_descriptor.selector & SELECTOR_RPL_MASK) == 0) {
				PDEBUGF(LOG_V1,LOG_CPU,"int_trap_gate(): selector null\n");
				throw CPUException(CPU_GP_EXC, 0);
			}
			cs_selector = gate_descriptor.selector;

			// selector must be within its descriptor table limits
			// else #GP(selector+EXT)
			try {
				cs_descriptor = fetch_descriptor(cs_selector, CPU_GP_EXC);
			} catch(CPUException &e) {
				PDEBUGF(LOG_V1,LOG_CPU, "interrupt_pmode: bad cs_selector fetch\n");
				throw;
			}

			// descriptor AR byte must indicate code seg
			// and code segment descriptor DPL<=CPL, else #GP(selector+EXT)
			if(!cs_descriptor.valid || !cs_descriptor.is_code_segment() || cs_descriptor.dpl > CPL) {
				PDEBUGF(LOG_V1,LOG_CPU, "interrupt(): not accessible or not code segment cs=0x%04x\n",
						cs_selector.value);
				throw CPUException(CPU_GP_EXC, cs_selector.value & SELECTOR_RPL_MASK);
			}

			// segment must be present, else #NP(selector + EXT)
			if(!cs_descriptor.present) {
				PDEBUGF(LOG_V1,LOG_CPU,"interrupt(): segment not present\n");
				throw CPUException(CPU_NP_EXC, cs_selector.value & SELECTOR_RPL_MASK);
			}

			// if code segment is non-conforming and DPL < CPL then int to inner priv
			if(!cs_descriptor.is_conforming() && cs_descriptor.dpl < CPL)
			{
				PDEBUGF(LOG_V2, LOG_CPU, "interrupt(): INTERRUPT TO INNER PRIVILEGE\n");
				interrupt_inner_privilege(gate_descriptor,
						cs_selector, cs_descriptor,
						push_error, error_code);
			}
			else
			{
				PDEBUGF(LOG_V2,LOG_CPU, "interrupt(): INTERRUPT TO SAME PRIVILEGE\n");
				interrupt_same_privilege(gate_descriptor,
						cs_selector, cs_descriptor,
						push_error, error_code);
			}

			SET_EIP(gate_descriptor.offset);

			/* The difference between a trap and an interrupt gate is whether
			 * the interrupt enable flag is to be cleared or not. An interrupt
			 * gate specifies a procedure that enters with interrupts disabled
			 * (i.e., with the interrupt enable flag cleared); entry via a trap
			 * gate leaves the interrupt enable status unchanged.
			 */
			if(gate_descriptor.type == DESC_TYPE_286_INTR_GATE ||
			   gate_descriptor.type == DESC_TYPE_386_INTR_GATE)
			{
				SET_FLAG(IF, false);
			}

			/* The NT flag is always cleared (after the old NT state is saved on
			 * the stack) when an interrupt uses these gates.
			 */
			SET_FLAG(NT, false);
			SET_FLAG(TF, false);
			SET_FLAG(VM, false);
			SET_FLAG(RF, false);

			g_cpubus.invalidate_pq();

			break;
		}
		default:
			PERRF_ABORT(LOG_CPU,"bad descriptor type in interrupt()!\n");
			break;
	}
}
