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
#include "cpu/mmu.h"
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
	{ CPU_BENIGN_EXC,       CPU_TRAP_EXC,  false }, // #1  CPU_DEBUG_EXC
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
m_family(0),
m_signature(0),
m_frequency(.0),
m_cycle_time(0),
m_instr(nullptr)
{
	m_shutdown_trap = std::bind(&CPU::default_shutdown_trap,this);
}

CPU::~CPU()
{
}

void CPU::init()
{
	static Instruction nullinstr;
	nullinstr.valid = false;
	nullinstr.eip = 0;
	m_instr = &nullinstr;

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

	m_model = g_program.config().get_string(CPU_SECTION, CPU_MODEL,
		{ "286", "386SX", "386DX" }, g_machine.model().cpu_model);
	m_family = cpu_families[m_model];
	m_signature = cpu_signatures[m_model];

	double freq = g_program.config().get_real(CPU_SECTION, CPU_FREQUENCY, g_machine.model().cpu_freq);
	double cycle = 1000.0 / freq;
	int icycle = int(cycle);
	// prefer a faster CPU rounding down at 0.5
	if((cycle - icycle) <= 0.5) {
		m_cycle_time = icycle;
	} else {
		m_cycle_time = icycle + 1;
	}
	m_frequency = 1e3 / m_cycle_time; // in MHz

	PINFOF(LOG_V0, LOG_CPU, "Installed CPU: %s @ %.0fMHz\n", m_model.c_str(), freq);
	PINFOF(LOG_V1, LOG_CPU, "  Family: %d86, Signature: 0x%04x\n", m_family, m_signature);
	PINFOF(LOG_V1, LOG_CPU, "  Cycle time: %u nsec (%.3fMHz)\n", m_cycle_time, m_frequency);

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
	m_s.debug_trap = 0;
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

	g_cpucore.save_state(_state);
	g_cpubus.save_state(_state);
	//decoder and executor don't have a state to save and restore
}

void CPU::restore_state(StateBuf &_state)
{
	StateHeader h;
	h.name = CPU_STATE_NAME;
	h.data_size = sizeof(m_s);
	_state.read(&m_s,h);

	// restore the core before the bus.
	g_cpucore.restore_state(_state);
	g_cpubus.restore_state(_state);

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
	CPUCore core_log;
	CPUState state_log;
	CPUException log_exc;
	bool do_log = false;

	g_cpubus.reset_counters();
	CPUCycles cycles = { 0,0,0,0,0,0 };

	if(m_s.activity_state == CPU_STATE_ACTIVE) {

		try {
			if(m_s.async_event) {
				// check on events which occurred for previous instructions (traps)
				// and ones which are asynchronous to the CPU (hardware interrupts)
				handle_async_event();
				g_cpubus.update(0);

				if(m_s.activity_state != CPU_STATE_ACTIVE) {
					// something (eg. triple-fault) put the CPU in non active state
					// return a non zero number of elapsed cycles anyway
					return 1;
				}
			}

			if(m_instr->cseip != CS_EIP) {
				// When RF is set, it causes any debug fault to be ignored during the next instruction.
				if(DR7_ENABLED_ANY && !FLAG_RF && !interrupts_inhibited(CPU_INHIBIT_DEBUG)) {
					// Priority 6:
					//   Code breakpoint fault.
					//   Instruction breakpoints are the highest priority debug
					//   exceptions. They are serviced before any other exceptions
					//   detected during the decoding or execution of an instruction.
					uint32_t debug_trap = g_cpucore.match_x86_code_breakpoint(CS_EIP);
					if(debug_trap & CPU_DEBUG_TRAP_HIT) {
						m_s.debug_trap = debug_trap | CPU_DEBUG_TRAP_CODE;
						throw CPUException(CPU_DEBUG_EXC, 0);
					}
				}

				// instruction decoding
				if(!g_cpubus.pq_is_valid()) {
					g_cpubus.reset_pq();
					m_instr = g_cpudecoder.decode();
					cycles.decode = m_instr->size;
				} else {
					m_instr = g_cpudecoder.decode();
				}

				if(CPULOG) {
					do_log = true;
					core_log = g_cpucore;
					state_log = m_s;
				}
			}

			// instruction execution
			g_cpuexecutor.execute(m_instr);

			cycles.eu = get_execution_cycles(g_cpubus.memory_accessed());
			int io_time = g_devices.get_last_io_time();
			if(io_time) {
				cycles.io = get_io_cycles(io_time);
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
			if(CPULOG) {
				if(!do_log) {
					m_logger.set_prev_i_exc(e, m_instr->cseip);
				} else {
					log_exc = e;
				}
			}
			exception(e);
			cycles.eu = 5; //just a random number
		} catch(CPUShutdown &s) {
			PDEBUGF(LOG_V2, LOG_CPU, "Entering shutdown for %s\n", s.what());
			g_cpu.enter_sleep_state(CPU_STATE_SHUTDOWN);
			cycles.eu = 5; //just a random number
		}

	} else {
		// the CPU is idle and waiting for an external event
		wait_for_event();
		//we need to spend at least 1 cycle, otherwise the timers will never fire
		cycles.eu = 1;
	}

	if(g_cpubus.pq_is_valid()) {
		g_cpubus.update(cycles.decode + cycles.eu);
		// other possible strategies:
		// g_cpubus.update(cycles.eu);
		// g_cpubus.update((!g_cpubus.memory_written()) + cycles.eu);
		// g_cpubus.update((!g_cpubus.memory_written()) + cycles.decode + cycles.eu);
	} else {
		g_cpubus.update(0);
	}

	// determine the total amount of cycles spent
	cycles.bu = g_cpubus.pipelined_mem_cycles() + m_instr->cycles.bu;
	if(cycles.bu < 0) {
		cycles.bu = 0;
	}
	cycles.bu += g_cpubus.pipelined_fetch_cycles();
	cycles.bus = g_cpubus.fetch_cycles() + g_cpubus.mem_r_cycles();

	int tot_cycles = cycles.sum();
	if(cycles.bus && (g_machine.get_virt_time_ns()%15085)<((tot_cycles*m_cycle_time))) {
		// DRAM refresh
		// TODO count only for DRAM not other bus uses
		cycles.refresh = g_memory.dram_cycles();
	}
	tot_cycles += cycles.refresh;

	if(CPULOG && do_log) {
		m_logger.add_entry(
			g_machine.get_virt_time_ns(), // time
			*m_instr,                     // instruction
			state_log,                    // state
			log_exc,                      // cpu exception?
			core_log,                     // core
			g_cpubus,                     // bus
			cycles                        // cycles used
		);
	}

	m_s.icount++;
	m_s.ccount += tot_cycles;

	return tot_cycles;
}

int CPU::get_execution_cycles(bool _memtx)
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
		if(g_cpubus.pq_is_valid()) {
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

int CPU::get_io_cycles(int _io_time)
{
	int io_cycles = (_io_time + m_cycle_time - 1) / m_cycle_time; // round up
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
	// Priority 1: Hardware Reset and Machine Checks
	//   RESET
	//   Machine Check
	// not supported

	// Priority 2: Trap on Task Switch
	//   T flag in TSS is set
	if(m_s.debug_trap & CPU_DEBUG_TRAP_TASK_SWITCH_BIT) {
		throw CPUException(CPU_DEBUG_EXC, 0);
	}

	// Priority 3: External Hardware Interventions
	//   FLUSH
	//   STOPCLK
	//   SMI
	//   INIT
	// TODO 486+

	// Priority 4: Traps on Previous Instruction
	//   Breakpoints
	//   Debug Trap Exceptions (TF flag set or data/I-O breakpoint)
	// A trap may be inhibited on this boundary due to an instruction which loaded SS
	if(!interrupts_inhibited(CPU_INHIBIT_DEBUG)) {
		if(m_s.debug_trap & CPU_DEBUG_ANY) {
			if(m_s.debug_trap & CPU_DEBUG_TRAP_DATA) {
				// data breakpoint hit, must update any inactive code breakpoint
				// on previous instruction.
				m_s.debug_trap |= g_cpucore.match_x86_code_breakpoint(m_instr->cseip);
			}
			throw CPUException(CPU_DEBUG_EXC, 0);
		} else {
			m_s.debug_trap = 0;
		}
	}

	// Priority 5: External Interrupts
	//   Nonmaskable Interrupts (NMI)
	//   Maskable Hardware Interrupts
	if(interrupts_inhibited(CPU_INHIBIT_INTERRUPTS)) {
		// Processing external interrupts is inhibited on this
		// boundary because of certain instructions like STI.
	} else if(is_unmasked_event_pending(CPU_EVENT_NMI)) {
		clear_event(CPU_EVENT_NMI);
		mask_event(CPU_EVENT_NMI);
		m_s.EXT = true;
		interrupt(2, CPU_NMI, false, 0);
	} else if(is_unmasked_event_pending(CPU_EVENT_PENDING_INTR)) {
		uint8_t vector = g_devices.pic()->IAC(); // may set INTR with next interrupt
		m_s.EXT = true;
		interrupt(vector, CPU_EXTERNAL_INTERRUPT, 0, 0);
	} else if(m_s.HRQ) {
		// assert Hold Acknowledge (HLDA) and go into a bus hold state
		g_devices.dma()->raise_HLDA();
	}

	if(FLAG_TF) {
		// TF is set before execution of next instruction.
		// Schedule a debug exception (#DB) after execution.
		m_s.debug_trap |= CPU_DEBUG_SINGLE_STEP_BIT;

		/* As I keep forgetting how the T flag really works inside the emulator,
		 * here's the sequence:
		 * 1. an instruction access EFLAGS to update TF, CPUCore::set_EFLAGS() is called
		 * 2. CPUCore::set_EFLAGS() sets m_s.async_event
		 * 3. at the next cpu loop iteration, CPU::handle_async_event() is called
		 * 4. single step bit is set in debug_trap while async_event is kept true
		 * 5. the next instruction is executed
		 * 6. at the next cpu loop iteration, CPU::handle_async_event() is called again
		 * 7. this time CPU::handle_async_event() calls CPU::exception() with #DB (Priority 4)
		 * 8. interrupt is called, TF is pushed onto the stack, m_s.async_event is cleared
		 */
	}

	// Priority 6: Code Breakpoint Fault
	// (handled in the cpu loop, before deconding)

	// Priority 7: Faults from fetching next instruction
	//   Code page fault (handled during decoding by the bus and mmu units)
	//   Code segment limit violation (handled during execution by the execution unit

	// Priority 8: Faults from decoding next instruction
	//   Instruction length > 10/15 bytes
	//   Illegal opcode
	//   Coprocessor not available
	// (handled during execution by the execution unit)

	// Priority 9: Faults on executing an instruction
	//   Floating point execution (TODO)
	//   Overflow
	//   Bound error
	//   Invalid TSS
	//   Segment not present
	//   Stack fault
	//   General protection
	//   Data page fault
	//   Alignment check (TODO 486+)
	// (handled during execution by the execution unit)

	if( !(unmasked_events_pending() || m_s.debug_trap || m_s.HRQ) ) {
		m_s.async_event = false;
	}
}

bool CPU::v86_redirect_interrupt(uint8_t _vector)
{
	// TODO see Bochs code for CPU 586+
	if(FLAG_IOPL < 3) {
		PDEBUGF(LOG_V2, LOG_CPU, "Redirecting soft INT in V8086 mode: %d\n", _vector);
		throw CPUException(CPU_GP_EXC, 0);
	}
	return false;
}

void CPU::interrupt(uint8_t _vector, unsigned _type, bool _push_error, uint16_t _error_code)
{
	bool soft_int = false;
	const char *typestr = nullptr;
	switch(_type) {
		case CPU_SOFTWARE_INTERRUPT:
		case CPU_SOFTWARE_EXCEPTION:
			soft_int = true;
			break;
		case CPU_PRIVILEGED_SOFTWARE_INTERRUPT:
			typestr = "PRIVILEGED SOFTWARE";
			// INT1
			m_s.EXT = true;
			break;
		case CPU_EXTERNAL_INTERRUPT:
			typestr = "EXTERNAL";
			break;
		case CPU_NMI:
			typestr = "NMI";
			break;
		case CPU_HARDWARE_EXCEPTION:
			typestr = "HARDWARE EXCEPTION";
			break;
		default:
			PERRF_ABORT(LOG_CPU, "interrupt(): unknown exception type %u\n", _type);
			break;
	}

	if(typestr) {
		PDEBUGF(LOG_V2, LOG_CPU, "interrupt(): vector = %02x, TYPE = %s(%u), EXT = %u\n",
			_vector, typestr, _type, m_s.EXT);
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
		if(IS_RMODE()) {
			g_cpuexecutor.interrupt(_vector);
		} else {
			g_cpuexecutor.interrupt_pmode(_vector, soft_int, _push_error, _error_code);
		}
	}

	m_s.EXT = false;
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
			if(m_family >= CPU_386) {
				// default is trap, so determine only fault conditions
				if(m_s.debug_trap & CPU_DEBUG_DR_ACCESS_BIT) {
					// General detect
					exc_class = CPU_FAULT_EXC;
				}
				// Instruction address breakpoint is also a fault, but is thrown
				// before deconding (Priority 6) so EIP is already at the
				// faulting instruction.

				// Commit debug events to DR6: preserve BS and BD values, only
				// software can clear them.
				REG_DR(6) = (REG_DR(6) & 0xffff6ff0) | (m_s.debug_trap & DR6_MASK);

				// clear GD flag in the DR7 prior entering debug exception handler
				REG_DR(7) &= ~(1<<DR7BIT_GD);
			}
			error_code = (_exc.error_code & 0xfffe) + m_s.EXT;
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
		if(m_family >= CPU_386) {
			// The processor automatically sets RF in the EFLAGS image on the
			// stack before entry into any FAULT handler except a debug
			// exception generated in response to an instruction breakpoint,
			if(_exc.vector != CPU_DEBUG_EXC || (m_s.debug_trap & CPU_DEBUG_DR_ACCESS_BIT)) {
				SET_FLAG(RF, true);
			}
		}
	}

	// set EXT in case another exception happens in interrupt()
	m_s.EXT = true;

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
	std::string filename = g_program.config().get_cfg_home() + FS_SEP CPULOG_FILE;
	m_logger.dump(filename);
}
