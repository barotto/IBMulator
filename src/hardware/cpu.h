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

#ifndef IBMULATOR_HW_CPU_H
#define IBMULATOR_HW_CPU_H

#include "cpu/core.h"
#include "cpu/decoder.h"
#include "cpu/executor.h"
#include <regex>

class CPU;
extern CPU g_cpu;



struct CPULogEntry
{
	uint64_t time;
	CPUCore core;
	CPUBus bus;
	Instruction instr;
	unsigned cycles;
};

#define CPU_EVENT_NMI           (1 << 0)
#define CPU_EVENT_PENDING_INTR  (1 << 1)

#define CPU_INHIBIT_INTERRUPTS  0x01
#define CPU_INHIBIT_DEBUG       0x02

#define CPU_INHIBIT_INTERRUPTS_BY_MOVSS \
	(CPU_INHIBIT_INTERRUPTS | CPU_INHIBIT_DEBUG)

// exception types for interrupt method
enum CPUInterruptType {
	CPU_EXTERNAL_INTERRUPT = 0,
	CPU_NMI = 2,
	CPU_HARDWARE_EXCEPTION = 3,  // all exceptions except #BP and #OF
	CPU_SOFTWARE_INTERRUPT = 4,
	CPU_PRIVILEGED_SOFTWARE_INTERRUPT = 5,
	CPU_SOFTWARE_EXCEPTION = 6
};

/* The names of CPU interrupts reflect those reported in the 80286 programmers's
 * reference manual. Some codes have the same value but different names because
 * of this.
 *
 * Interrupts and exceptions are special cases of control transfer within a
 * program. An interrupt occurs as a result of an event that is independent of
 * the currently executing program, while exceptions are a direct result of the
 * program currently being executed, Interrupts may be external or internal.
 * External interrupts are generated by either the INTR or NMI input pins.
 * Internal interrupts are caused by the INT instruction. Exceptions occur when
 * an instruction cannot be completed normally. (cfr. 9-1)
 *
 */
enum CPUInterrupt {
	CPU_DIV_ER_EXC      = 0,  // Divide Error exception
	CPU_SINGLE_STEP_INT = 1,  // Single step interrupt
	CPU_NMI_INT         = 2,  // NMI interrupt
	CPU_BREAKPOINT_INT  = 3,  // Breakpoint interrupt
	CPU_INTO_EXC        = 4,  // INTO detected overflow exception
	CPU_BOUND_EXC       = 5,  // BOUND range exceeded exception
	CPU_UD_EXC          = 6,  // Undefined opcode exception (rmode/pmode)
	CPU_NM_EXC          = 7,  // NPX not available exception (rmode/pmode)
	CPU_IDT_LIMIT_EXC   = 8,  // Interrupt table limit too small exception (rmode)
	CPU_DF_EXC          = 8,  // Double Fault exception (pmode)
	CPU_NPX_SEG_OVR_INT = 9,  // NPX segment overrun interrupt (rmode)
	CPU_MP_EXC          = 9,  // NPX protection fault exception (pmode)
	CPU_TS_EXC          = 10, // Invalid Task State Segment exception (pmode)
	CPU_NP_EXC          = 11, // Segment Not Present exception (pmode)
	CPU_SS_EXC          = 12, // Stack Fault exception (pmode)
	CPU_SEG_OVR_EXC     = 13, // Segment overrun exception (rmode)
	CPU_GP_EXC          = 13, // General Protection exception (pmode)
	CPU_NPX_ERR_INT     = 16, // NPX error interrupt (rmode)
	CPU_MF_EXC          = 16  // Math Fault exception (pmode)
};

class CPUException
{
public:
	uint8_t vector;
	uint16_t error_code;

	CPUException(uint8_t _vector, uint16_t _error_code)
	: vector(_vector), error_code(_error_code) { }
};

enum CPUActivityState {
	CPU_STATE_ACTIVE = 0,
	CPU_STATE_HALT,
	CPU_STATE_SHUTDOWN,
	CPU_STATE_POWEROFF
};

class CPU
{
friend class Machine;
protected:

	uint32_t m_freq;
	uint32_t m_cycle_time;
	Instruction *m_instr;
	bool m_pq_valid;
	std::function<void(void)> m_shutdown_trap;

	struct {
		uint64_t icount;
		uint64_t ccount;
		uint32_t activity_state;
		uint32_t pending_event;
		uint32_t event_mask;
		bool     async_event;
		bool     debug_trap;

		// What events to inhibit at any given time.  Certain instructions
		// inhibit interrupts, some debug exceptions and single-step traps.
		unsigned inhibit_mask;
		uint64_t inhibit_icount;

		bool HRQ; //DMA Hold Request
		bool EXT; /* EXT is 1 if an external event (ie., a single step, an
					   external interrupt, an #MF exception, or an #MP exception)
					   caused the interrupt; 0 if not (ie., an INT instruction or
					   other exceptions) (cfr. B-50)
					 */
	} m_s;

	void handle_async_event();

	void interrupt(uint8_t _vector, unsigned _type,
			bool _push_error, uint16_t _error_code);

	void mask_event(uint32_t event);

	void signal_event(uint32_t _event);
	void clear_event(uint32_t _event);
	bool is_masked_event(uint32_t _event);
	bool is_pending(uint32_t _event);
	bool is_unmasked_event_pending(uint32_t _event);
	uint32_t unmasked_events_pending();
	void default_shutdown_trap() {}

	bool interrupts_inhibited(unsigned mask);

	void wait_for_event();

	uint m_log_idx;
	uint m_log_size;
	CPULogEntry m_log[CPULOG_MAX_SIZE];
	FILE *m_log_file;
	std::string m_log_prg_name;
	std::regex m_log_prg_regex;
	FILE *m_log_prg_file;
	uint32_t m_log_prg_iret;

	void add_to_log(const Instruction &_instr, uint64_t _time,
			const CPUCore &_core, const CPUBus &_bus, unsigned _cycles);
	void write_log_entry(FILE *_dest, CPULogEntry &_entry);
	const std::string & disasm(CPULogEntry &_log_entry);

	uint get_execution_cycles(bool _memtx);

public:

	CPU();
	~CPU();

	void init();
	void reset(uint _signal);
	void power_off();
	void config_changed();

	uint step();

	inline uint32_t get_freq() { return m_freq; }
	GCC_ATTRIBUTE(always_inline)
	inline uint32_t get_cycle_time_ns() { return m_cycle_time; }

	void clear_INTR();
	void raise_INTR();
	void deliver_NMI();
	void inhibit_interrupts(unsigned mask);
	void interrupt_mask_change();
	void unmask_event(uint32_t event);
	inline void set_async_event() { m_s.async_event = true; }
	inline void clear_inhibit_mask() { m_s.inhibit_mask = 0; }
	inline void clear_debug_trap() { m_s.debug_trap = false; }

	void set_HRQ(bool _val);
	bool get_HRQ() { return m_s.HRQ; }

	void set_shutdown_trap(std::function<void(void)> _fn);
	void enter_sleep_state(CPUActivityState _state);
	void exception(CPUException _exc);

	void write_log();
	void enable_prg_log(std::string _prg_name);
	void disable_prg_log();
	void INT(uint32_t _retaddr);
	void DOS_program_launch(std::string _name);
	void DOS_program_start(std::string _name);
	void DOS_program_finish(std::string _name);

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
};


#endif
