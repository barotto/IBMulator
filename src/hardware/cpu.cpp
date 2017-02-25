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
#include "cpu.h"
#include "cpu/core.h"
#include "cpu/decoder.h"
#include "cpu/executor.h"
#include "cpu/debugger.h"
#include "machine.h"
#include "devices.h"
#include "devices/pic.h"
#include "devices/dma.h"
#include "program.h"
#include "utils.h"
#include <cmath>

CPU g_cpu;

const CPUExceptionInfo g_cpu_exceptions[CPU_MAX_INT] = {
	{ CPU_CONTRIBUTORY_EXC, CPU_FAULT_EXC, false }, // #0  CPU_DIV_ER_EXC
	{ CPU_BENIGN_EXC,       CPU_FAULT_EXC, false }, // #1  CPU_SINGLE_STEP_INT
	{ CPU_BENIGN_EXC,       CPU_FAULT_EXC, false }, // #2  CPU_NMI_INT
	{ CPU_BENIGN_EXC,       CPU_TRAP_EXC,  false }, // #3  CPU_BREAKPOINT_INT
	{ CPU_BENIGN_EXC,       CPU_TRAP_EXC,  false }, // #4  CPU_INTO_EXC
	{ CPU_BENIGN_EXC,       CPU_FAULT_EXC, false }, // #5  CPU_BOUND_EXC
	{ CPU_BENIGN_EXC,       CPU_FAULT_EXC, false }, // #6  CPU_UD_EXC
	{ CPU_BENIGN_EXC,       CPU_FAULT_EXC, false }, // #7  CPU_NM_EXC
	{ CPU_DOUBLE_FAULT,     CPU_ABORT_EXC, true  }, // #8  CPU_DF_EXC
	{ CPU_CONTRIBUTORY_EXC, CPU_FAULT_EXC, false }, // #9  CPU_MP_EXC (Bochs has benign)
	{ CPU_CONTRIBUTORY_EXC, CPU_FAULT_EXC, true  }, // #10 CPU_TS_EXC
	{ CPU_CONTRIBUTORY_EXC, CPU_FAULT_EXC, true  }, // #11 CPU_NP_EXC
	{ CPU_CONTRIBUTORY_EXC, CPU_FAULT_EXC, true  }, // #12 CPU_SS_EXC
	{ CPU_CONTRIBUTORY_EXC, CPU_FAULT_EXC, true  }, // #13 CPU_GP_EXC
	{ CPU_PAGE_FAULTS,      CPU_FAULT_EXC, true  }, // #14 CPU_PF_EXC
	{ CPU_BENIGN_EXC,       CPU_FAULT_EXC, false }, // #15 reserved
	{ CPU_BENIGN_EXC,       CPU_FAULT_EXC, false }  // #16 CPU_MF_EXC
};


CPU::CPU()
:
m_instr(nullptr)
{
	m_shutdown_trap = std::bind(&CPU::default_shutdown_trap,this);
}

CPU::~CPU()
{
}

void CPU::init()
{
	g_cpubus.init();
}

void CPU::config_changed()
{
	static ini_enum_map_t cpu_families = {
		{ "286",   CPU_286 },
		{ "386SX", CPU_386 },
		{ "386DX", CPU_386 }
	};
	static ini_enum_map_t cpu_signatures = {
		{ "286",   0x0000 },
		{ "386SX", 0x2308 },
		{ "386DX", 0x0308 }
	};

	std::string cfgstr = g_program.config().get_string(CPU_SECTION, CPU_MODEL);
	if(cfgstr == "auto") {
		m_model = g_machine.model().cpu;
		m_family = cpu_families[m_model];
		m_signature = cpu_signatures[m_model];
	} else {
		m_model = cfgstr;
		m_family = g_program.config().get_enum(CPU_SECTION, CPU_MODEL, cpu_families);
		m_signature = g_program.config().get_enum(CPU_SECTION, CPU_MODEL, cpu_signatures);
	}

	double freq;
	cfgstr = g_program.config().get_string(CPU_SECTION, CPU_FREQUENCY);
	if(cfgstr == "auto") {
		freq = g_machine.model().cpu_freq;
	} else {
		freq = g_program.config().get_real(CPU_SECTION, CPU_FREQUENCY);
	}
	m_cycle_time = round(1000.0 / freq);
	m_freq = round(1e9 / m_cycle_time);

	PINFOF(LOG_V0, LOG_CPU, "Installed CPU: %s @ %.0fMHz\n", m_model.c_str(), freq);
	PINFOF(LOG_V1, LOG_CPU, "  Cycle time: %u nsec (%.3fMHz)\n", m_cycle_time, m_freq / 1.0e6);

	g_cpubus.config_changed();
	g_cpuexecutor.config_changed();
}

void CPU::reset(uint _signal)
{
	bool irqwaiting = is_pending(CPU_EVENT_PENDING_INTR);

	m_s.activity_state = CPU_STATE_ACTIVE;
	m_s.event_mask = 0;
	m_s.pending_event = 0;
	m_s.async_event = false;
	m_s.debug_trap = false;
	m_s.EXT = false;
	if(_signal==MACHINE_POWER_ON || _signal==MACHINE_HARD_RESET) {
		m_s.icount = 0;
		m_s.ccount = 0;
		m_s.HRQ = false;
		m_s.inhibit_mask = 0;
		m_s.inhibit_icount = 0;
		m_logger.reset_iret_address();
		disable_prg_log();
	} else {
		if(irqwaiting) {
			raise_INTR();
			mask_event(CPU_EVENT_PENDING_INTR); //the CPU starts with IF=0
		}
	}

	g_cpucore.reset();
	g_cpuexecutor.reset(_signal);
	g_cpubus.reset();
}

#define CPU_STATE_NAME "CPU"

void CPU::save_state(StateBuf &_state)
{
	//CPU state
	StateHeader h;
	h.name = CPU_STATE_NAME;
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);

	g_cpubus.save_state(_state);
	g_cpucore.save_state(_state);
	//decoder and executor don't have a state to save and restore
}

void CPU::restore_state(StateBuf &_state)
{
	StateHeader h;
	h.name = CPU_STATE_NAME;
	h.data_size = sizeof(m_s);
	_state.read(&m_s,h);

	g_cpubus.restore_state(_state);
	g_cpucore.restore_state(_state);

	m_logger.reset_iret_address();
	disable_prg_log();
}

void CPU::power_off()
{
	enter_sleep_state(CPU_STATE_POWEROFF);
	disable_prg_log();
}

uint CPU::step()
{
	int cycles, bu_cycles, eu_cycles, decode_cycles, io_cycles, dramtx, vramtx, bu_rops;
	CPUCore core_log;
	bool do_log = false;

	g_cpubus.reset_counters();
	cycles = 0;
	eu_cycles = 0;
	bu_cycles = 0;
	decode_cycles = 0;
	io_cycles = 0;
	bu_rops = 0;
	dramtx = 0;
	vramtx = 0;

	if(m_s.activity_state == CPU_STATE_ACTIVE) {

		if(m_s.async_event) {
			// check on events which occurred for previous instructions (traps)
			// and ones which are asynchronous to the CPU (hardware interrupts)
			try {
				handle_async_event();
			} catch(CPUException &e) {
				exception(e);
			}
			if(m_s.activity_state != CPU_STATE_ACTIVE) {
				// something (eg. triple-fault) put the CPU in non active state
				// return a non zero number of elapsed cycles anyway
				return 1;
			}
			// serialize any pending write
			g_cpubus.update(0);
		}

		// decode and execute the next instruction
		try {
			//if the prev instr is the same as the next don't decode
			if(m_instr==nullptr || m_instr->eip!=REG_EIP || !g_cpubus.is_pq_valid()) {
				if(!g_cpubus.is_pq_valid()) {

					// page faults can be generated at this point
					g_cpubus.reset_pq();

					/*
					According to various sources, the decoding time should be
					proportional to the size of the next instruction (1 cycle per
					decoded byte). But after some empirical tests, 2 cycles is the
					best value given the current setup.
					*/
					decode_cycles = 2;
				}

				// TODO actual decoding can be put in a separate thread
				m_instr = g_cpudecoder.decode();

				if(CPULOG) {
					do_log = true;
					core_log = g_cpucore;
				}
			}

			bu_rops = g_cpubus.get_dram_r();

			g_cpuexecutor.execute(m_instr);

			dramtx = g_cpubus.get_dram_tx() - bu_rops; // instruction execution transfers
			vramtx = g_cpubus.get_vram_tx();
			eu_cycles = get_execution_cycles(dramtx||vramtx);
			unsigned io_time = g_devices.get_last_io_time();
			if(io_time) {
				io_cycles = get_io_cycles(io_time);
			}
			m_instr->cycles.rep = 0;
		} catch(CPUException &e) {
			PDEBUGF(LOG_V2, LOG_CPU, "CPU exception %u\n", e.vector);
			if(STOP_AT_EXC && (STOP_AT_EXC_VEC==0xFF || e.vector==STOP_AT_EXC_VEC)) {
				g_machine.set_single_step(true);
				if(e.vector == CPU_UD_EXC && UD6_AUTO_DUMP) {
					PERRF(LOG_CPU,"illegal opcode at 0x%07X, dumping code segment\n", m_instr->cseip);
					g_machine.memdump(REG_CS.desc.base, GET_LIMIT(CS));
				}
			}
			exception(e);
			eu_cycles = 15; //just a random number
		} catch(CPUShutdown &s) {
			PDEBUGF(LOG_V2, LOG_CPU, "Entering shutdown for %s\n", s.what());
			g_cpu.enter_sleep_state(CPU_STATE_SHUTDOWN);
			eu_cycles = 15; //just a random number
		}

	} else {
		// the CPU is idle and waiting for an external event
		wait_for_event();
		//we need to spend at least 1 cycle, otherwise the timers will never fire
		eu_cycles = 1;
	}

	// serialize any pending write
	if(g_cpubus.is_pq_valid()) {
		g_cpubus.update(eu_cycles + decode_cycles);
	} else {
		g_cpubus.update(0);
	}

	// determine the total amount of cycles spent
	bu_cycles = g_cpubus.get_mem_cycles() + m_instr->cycles.bu;
	if(bu_cycles < 0) {
		bu_cycles = 0;
	}
	bu_cycles += g_cpubus.get_fetch_cycles();
	dramtx = g_cpubus.get_dram_r() - bu_rops;
	vramtx = g_cpubus.get_vram_r();
	cycles += eu_cycles + bu_cycles + decode_cycles + io_cycles +
			(bu_rops+dramtx)*DRAM_TX_CYCLES +
			vramtx*VRAM_TX_CYCLES;
	if((dramtx||bu_rops) && (g_machine.get_virt_time_ns()%15085)<((cycles*m_cycle_time))) {
		cycles += DRAM_REFRESH_CYCLES;
	}

	if(CPULOG && do_log) {
		m_logger.add_entry(
			g_machine.get_virt_time_us(), // time
			*m_instr,                     // instruction
			m_s,                          // state
			core_log,                     // core
			g_cpubus,                     // bus
			cycles                        // cycles used
		);
	}

	m_s.icount++;
	m_s.ccount += cycles;

	return cycles;
}

unsigned CPU::get_execution_cycles(bool _memtx)
{
	unsigned cycles_spent = 0;
	unsigned base = 0;

	if(m_instr->rep) {
		cycles_spent = m_instr->cycles.rep;
		base = m_instr->cycles.base_rep;
	} else {
		if(_memtx) {
			base = m_instr->cycles.memop;
		} else {
			base = m_instr->cycles.base;
		}
	}
	base += m_instr->cycles.extra;
	if(IS_PMODE()) {
		// protected mode penalty
		base += m_instr->cycles.pmode;
	}
	if(m_instr->cycles.noj>0) {
		//TODO consider the BOUND case
		if(g_cpubus.is_pq_valid()) {
			//jmp not taken
			cycles_spent += m_instr->cycles.noj;
		} else {
			cycles_spent += base;
		}
	} else {
		cycles_spent += base;
	}

	return cycles_spent;
}

unsigned CPU::get_io_cycles(unsigned _io_time)
{
	unsigned io_cycles = (_io_time + m_cycle_time - 1) / m_cycle_time; // round up
	if(io_cycles < m_instr->cycles.base) {
		io_cycles = 0;
	} else {
		io_cycles -= m_instr->cycles.base;
	}
	g_devices.reset_io_time();
	return io_cycles;
}

void CPU::set_shutdown_trap(std::function<void(void)> _fn)
{
	m_shutdown_trap = _fn;
}

void CPU::enter_sleep_state(CPUActivityState _state)
{
	// artificial trap bit, why use another variable.
	m_s.activity_state = _state;
	m_s.async_event = true; // so processor knows to check

	switch(_state) {
		case CPU_STATE_ACTIVE:
			assert(false); // should not be used for entering active CPU state
			break;

		case CPU_STATE_HALT:
		case CPU_STATE_POWEROFF:
			break;

		case CPU_STATE_SHUTDOWN:
			SET_FLAG(IF,0); // masking interrupts
			PDEBUGF(LOG_V2,LOG_CPU, "Shutdown\n");
			m_shutdown_trap();
			break;

		default:
			PERRF_ABORT(LOG_CPU,"enter_sleep_state: unknown state %d\n", _state);
			break;
	}
	// Execution completes.  The processor will remain in a sleep
	// state until one of the wakeup conditions is met.
}

void CPU::wait_for_event()
{
	// pass the time until an interrupt wakes up the CPU.

	if((is_pending(CPU_EVENT_PENDING_INTR) && FLAG_IF)
		|| is_unmasked_event_pending(CPU_EVENT_NMI) )
	{
		// interrupt ends the HALT condition
		m_s.activity_state = CPU_STATE_ACTIVE;
		m_s.inhibit_mask = 0; // clear inhibits for after resume
		return;
	}

	if(m_s.activity_state == CPU_STATE_ACTIVE) {
		return;
	}

	if(m_s.HRQ) {
		// handle DMA also when CPU is halted
		g_devices.dma()->raise_HLDA();
	}
}

void CPU::handle_async_event()
{
	/* Processing order:
	 * 1. instruction exception
	 * 2. single step
	 * 3. NMI
	 * 4. NPX segment overrun
	 * 5. INTR
	 *
	 * When simultaneous interrupt requests occur, they are processed in a fixed
	 * order as shown. Interrupt processing involves saving the flags, the
	 * return address, and setting CS:IP to point at the first instruction of
	 * the interrupt handler. If other interrupts remain enabled, they are
	 * processed before the first instruction of the current interrupt handler
	 * is executed. The last interrupt processed is therefore the first one
	 * serviced.
	 * (80286 programmer's reference manual 5-4)
	 */

	if(!interrupts_inhibited(CPU_INHIBIT_DEBUG)) {
	    // A trap may be inhibited on this boundary due to an instruction which loaded SS
		if(m_s.debug_trap) {
			exception(CPUException(CPU_DEBUG_EXC, 0)); // no error, not interrupt
			return;
		}
	}

	// External Interrupts
	//   NMI Interrupts
	//   Maskable Hardware Interrupts
	if(interrupts_inhibited(CPU_INHIBIT_INTERRUPTS)) {
		// Processing external interrupts is inhibited on this
		// boundary because of certain instructions like STI.
	} else if(is_unmasked_event_pending(CPU_EVENT_NMI)) {
		clear_event(CPU_EVENT_NMI);
		mask_event(CPU_EVENT_NMI);
		m_s.EXT = 1; /* external event */
		interrupt(2, CPU_NMI, false, 0);
	} else if(is_unmasked_event_pending(CPU_EVENT_PENDING_INTR)) {
		uint8_t vector = g_devices.pic()->IAC(); // may set INTR with next interrupt
		m_s.EXT = 1; /* external event */
		interrupt(vector, CPU_EXTERNAL_INTERRUPT, 0, 0);
	} else if(m_s.HRQ) {
		// assert Hold Acknowledge (HLDA) and go into a bus hold state
		g_devices.dma()->raise_HLDA();
	}

	if(FLAG_TF) {
		m_s.debug_trap = true;
	}

	// Faults from decoding next instruction
	//   Instruction length > 15 bytes
	//   Illegal opcode
	//   Coprocessor not available
	// (handled in main decode loop etc)

	// Faults on executing an instruction
	//   Floating point execution
	//   Overflow
	//   Bound error
	//   Invalid TSS
	//   Segment not present
	//   Stack fault
	//   General protection
	// (handled by rest of the code)

	if(!(unmasked_events_pending() || m_s.debug_trap || m_s.HRQ)) {
		m_s.async_event = false;
	}
}

bool CPU::v86_redirect_interrupt(uint8_t _vector)
{
	// see Bochs code for CPU 586+
	if(FLAG_IOPL < 3) {
		PDEBUGF(LOG_V2, LOG_CPU, "Redirecting soft INT in V8086 mode: %d\n", _vector);
		throw CPUException(CPU_GP_EXC, 0);
	}
	return false;
}

void CPU::interrupt(uint8_t _vector, unsigned _type, bool _push_error, uint16_t _error_code)
{
	bool soft_int = false;
	switch(_type) {
		case CPU_SOFTWARE_INTERRUPT:
		case CPU_SOFTWARE_EXCEPTION:
			soft_int = true;
			break;
		case CPU_PRIVILEGED_SOFTWARE_INTERRUPT:
		case CPU_EXTERNAL_INTERRUPT:
		case CPU_NMI:
		case CPU_HARDWARE_EXCEPTION:
			PDEBUGF(LOG_V2, LOG_CPU, "interrupt(): vector = %02x, TYPE = %u, EXT = %u\n",
					_vector, _type, m_s.EXT);
			break;
		default:
			PERRF_ABORT(LOG_CPU, "interrupt(): unknown exception type %d\n", _type);
			break;
	}

	// Discard any traps and inhibits for new context; traps will
	// resume upon return.
	clear_inhibit_mask();
	clear_debug_trap();

	if(CPULOG) {
		m_logger.set_iret_address(GET_LINADDR(CS, REG_EIP));
	}

	// software interrupts can be redirected in v8086 mode
	if((_type!=CPU_SOFTWARE_INTERRUPT) || !IS_V8086() || !v86_redirect_interrupt(_vector)) {
		if(IS_PMODE()) {
			g_cpuexecutor.interrupt_pmode(_vector, soft_int, _push_error, _error_code);
		} else {
			g_cpuexecutor.interrupt(_vector);
		}
	}

	m_s.EXT = 0;
}

bool CPU::is_double_fault(uint8_t _first_vec, uint8_t _current_vec)
{
	static const bool df_definition[3][3] = {
		//          second exc
		// BENIGN  CONTRIBUTORY  PAGE_FAULTS
		 { false,  false,        false      },  // BENIGN
		 { false,  true,         false      },  // CONTRIBUTORY  first exc
		 { false,  true,         true       }   // PAGE_FAULTS
	};

	switch(m_family) {
		case CPU_286: {
			/* If two separate faults occur during a single instruction, and if the
			 * first fault is any of #0, #10, #11, #12, and #13, exception 8
			 * (Double Fault) occurs (e.g., a general protection fault in level 3 is
			 * followed by a not-present fault due to a segment not-present). If
			 * another protection violation occurs during the processing of
			 * exception 8, the 80286 enters shutdown, during which time no further
			 * instructions or exceptions are processed.
			 */
			return (_first_vec==CPU_DIV_ER_EXC //#0
				 || _first_vec==CPU_TS_EXC     //#10
				 || _first_vec==CPU_NP_EXC     //#11
				 || _first_vec==CPU_SS_EXC     //#12
				 || _first_vec==CPU_GP_EXC     //#13
			);
		}
		case CPU_386: {
			/* To determine when two faults are to be signalled as a double
			 * fault, the 80386 divides the exceptions into three classes:
			 * benign exceptions, contributory exceptions, and page faults.
			 */
			assert(_first_vec != CPU_DF_EXC && _first_vec < CPU_MAX_INT);
			assert(_current_vec != CPU_DF_EXC && _current_vec < CPU_MAX_INT);
			unsigned first = g_cpu_exceptions[_first_vec].exc_type;
			unsigned second = g_cpu_exceptions[_current_vec].exc_type;
			return df_definition[first][second];
		}
		default:
			PERRF_ABORT(LOG_CPU, "is_double_fault(): unsupported CPU type\n");
			return false;
	}
}




void CPU::exception(CPUException _exc)
{
	assert(_exc.vector < CPU_MAX_INT);

	PDEBUGF(LOG_V2, LOG_CPU, "exception(0x%02x): error_code=%04x\n",
			_exc.vector, _exc.error_code);

	uint16_t error_code;
	unsigned exc_class = g_cpu_exceptions[_exc.vector].exc_class;
	bool push_error = g_cpu_exceptions[_exc.vector].push_error;

	switch(_exc.vector) {
		case CPU_DEBUG_EXC:
			/*TODO discriminate between:
			Instruction address breakpoint: fault.
			Data address breakpoint: trap.
			General detect: fault.
			Single-step: trap.
			Task-switch breakpoint: trap.
			*/
			PERRF_ABORT(LOG_CPU, "not implemented\n");
			break;
		case CPU_DF_EXC:
			error_code = 0;
			break;
		case CPU_PF_EXC:
			error_code = _exc.error_code;
			break;
		default:
			error_code = (_exc.error_code & 0xfffe) + m_s.EXT;
			break;
	}
	if(exc_class == CPU_FAULT_EXC) {
		/* The CS and EIP values saved when a fault is reported point to the instruction
		 * causing the fault.
		 */
		RESTORE_IP();
	}

	// set EXT in case another exception happens in interrupt()
	m_s.EXT = 1;

	try {
		interrupt(_exc.vector, CPU_HARDWARE_EXCEPTION, push_error, error_code);
	} catch(CPUException &e) {
		/* If another protection violation occurs during the processing of
		 * exception 8, the CPU enters shutdown, during which time no further
		 * instructions or exceptions are processed.
		 */
		if(_exc.vector == CPU_DF_EXC) {
			PDEBUGF(LOG_V2,LOG_CPU,"exception(): 3rd (#%d) exception with no resolution\n", e.vector);
			enter_sleep_state(CPU_STATE_SHUTDOWN);
			return;
		}

		if(is_double_fault(_exc.vector, e.vector)) {
			PDEBUGF(LOG_V2,LOG_CPU,"exception(): exc #%d while resolving exc #%d, generating #DF\n",
					e.vector, _exc.vector);
			exception(CPUException(CPU_DF_EXC,0));
		} else {
			PDEBUGF(LOG_V2,LOG_CPU,"exception(): exc #%d while resolving exc #%d\n", e.vector, _exc.vector);
			exception(e);
		}
	} catch(CPUShutdown &s) {
		PDEBUGF(LOG_V2, LOG_CPU, "Entering shutdown for %s\n", s.what());
		enter_sleep_state(CPU_STATE_SHUTDOWN);
		return;
	}
}

void CPU::signal_event(uint32_t _event)
{
	m_s.pending_event |= _event;
	if(!is_masked_event(_event)) {
		m_s.async_event = true;
	}
}

void CPU::clear_event(uint32_t _event)
{
	m_s.pending_event &= ~_event;
}

void CPU::mask_event(uint32_t event)
{
	m_s.event_mask |= event;
}

void CPU::unmask_event(uint32_t event)
{
	m_s.event_mask &= ~event;
	if(is_pending(event)) {
		m_s.async_event = true;
	}
}

bool CPU::is_masked_event(uint32_t _event)
{
	return (m_s.event_mask & _event) != 0;
}

bool CPU::is_pending(uint32_t _event)
{
	return (m_s.pending_event & _event) != 0;
}

bool CPU::is_unmasked_event_pending(uint32_t _event)
{
	return (m_s.pending_event & ~m_s.event_mask & _event) != 0;
}

uint32_t CPU::unmasked_events_pending()
{
	return (m_s.pending_event & ~m_s.event_mask);
}

void CPU::interrupt_mask_change()
{
	if(FLAG_IF) {
		// IF was set, unmask events
		unmask_event(CPU_EVENT_PENDING_INTR);
		return;
	}
	// IF was cleared, INTR would be masked
	mask_event(CPU_EVENT_PENDING_INTR);
}

void CPU::raise_INTR()
{
	signal_event(CPU_EVENT_PENDING_INTR);
}

void CPU::clear_INTR()
{
	clear_event(CPU_EVENT_PENDING_INTR);
}

void CPU::deliver_NMI()
{
	signal_event(CPU_EVENT_NMI);
}

void CPU::set_HRQ(bool _val)
{
	m_s.HRQ = _val;
	if(_val) {
		m_s.async_event = true;
	}
}

void CPU::inhibit_interrupts(unsigned mask)
{
	// Loading of SS disables interrupts until the next instruction completes
	// but only under assumption that previous instruction didn't load SS also.
	if(mask != CPU_INHIBIT_INTERRUPTS_BY_MOVSS
		|| !interrupts_inhibited(CPU_INHIBIT_INTERRUPTS_BY_MOVSS))
	{
		m_s.inhibit_mask = mask;
		m_s.inhibit_icount = m_s.icount + 1; // inhibit for next instruction
	}
}

bool CPU::interrupts_inhibited(unsigned mask)
{
	return (m_s.icount <= m_s.inhibit_icount) && (m_s.inhibit_mask & mask) == mask;
}

void CPU::enable_prg_log(std::string _prg_name)
{
	m_logger.close_file();
	m_log_prg_name = _prg_name;
	if(!_prg_name.empty()) {
		str_replace_all(_prg_name,".","\\.");
		m_log_prg_regex.assign(_prg_name+"$", std::regex::ECMAScript|std::regex::icase);
	}
}

void CPU::disable_prg_log()
{
	m_logger.close_file();
	m_log_prg_name.clear();
}

void CPU::DOS_program_launch(std::string /*_name*/)
{
}

void CPU::DOS_program_start(std::string _name)
{
	if(!m_log_prg_name.empty() && std::regex_search(_name, m_log_prg_regex)) {
		std::string filename = g_program.config().get_cfg_home() + FS_SEP + m_log_prg_name + ".log";
		try {
			PINFOF(LOG_V0,LOG_CPU, "logging instructions to '%s'\n", filename.c_str());
			m_logger.open_file(filename);
		} catch(std::exception &e) {
			PERRF(LOG_CPU, "unable to open file\n");
		}
	}
}

void CPU::DOS_program_finish(std::string _name)
{
	if((std::regex_search(_name, m_log_prg_regex) || _name.empty())) {
		m_logger.close_file();
		m_logger.reset_iret_address();
	}
}

void CPU::write_log()
{
	std::string filename = g_program.config().get_cfg_home() + FS_SEP "cpulog.log";
	m_logger.dump(filename);
}
