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

#ifndef IBMULATOR_HW_CPU_H
#define IBMULATOR_HW_CPU_H

#include "cpu/core.h"
#include "cpu/decoder.h"
#include "cpu/state.h"
#include "cpu/exception.h"
#include "cpu/logger.h"
#include <regex>

class CPU;
extern CPU g_cpu;

/* Supported CPU models:
 *
 * CPU model   family   address bus   data bus   pref. q.
 * 80286          286        24-bit     16-bit     6 byte
 * 80386 SX       386        24-bit     16-bit    16 byte
 * 80386 DX       386        32-bit¹    32-bit    12 byte²
 *
 * ¹ The PS/1 was equipped with the SX variant, so the system had a 24-bit
 * address bus (16MB max RAM), which is the only supported by IBMulator.
 * ² Due to a bug in the pipelining architecture, Intel had to abandon the
 * 16-byte queue, and only use a 12-byte queue.
 */

enum CPUFamily {
	CPU_286 = 2,
	CPU_386 = 3,

	CPU_COUNT = 2
};

#define CPU_FAMILY  g_cpu.family()

/* Signatures are reported in the EDX register upon a RESET.
Sig    Model       Step
-----------------------
0303   386 DX      B1
0305   386 DX      D0
0308   386 DX      D1/D2/E1
2304   386 SX      A0
2305   386 SX      D0
2308   386 SX      D1
43??   386 SL      ??
0400   486 DX      A1
0401   486 DX      Bx
0402   486 DX      C0
0404   486 DX      D0
0410   486 DX      cAx
0411   486 DX      cBx
0420   486 SX      A0
0433   486 DX2-66
*/
#define CPU_SIGNATURE  g_cpu.signature()
#define CPU_SIG_386SX  0x2300

#include "cpu/executor.h"

class CPU
{
protected:
	std::string m_model;
	unsigned m_family;
	unsigned m_signature;
	double   m_frequency;
	uint32_t m_cycle_time;
	Instruction *m_instr;
	std::function<void(void)> m_shutdown_trap;

	CPUState m_s;

	CPULogger m_logger;
	std::string m_log_prg_name;
	std::regex m_log_prg_regex;

public:
	CPU();
	~CPU();

	void init();
	void reset(uint _signal);
	void power_off();
	void config_changed();

	uint step();

	inline std::string model() const { return m_model; }
	inline unsigned family() const { return m_family; }
	inline unsigned signature() const { return m_signature; }
	inline double frequency() const { return m_frequency; }
	inline uint32_t cycle_time_ns() const { return m_cycle_time; }

	void interrupt(uint8_t _vector, unsigned _type, bool _push_error, uint16_t _error_code);
	void clear_INTR();
	void raise_INTR();
	void deliver_NMI();
	void inhibit_interrupts(unsigned mask);
	bool interrupts_inhibited(unsigned mask);
	void interrupt_mask_change();
	void unmask_event(uint32_t event);
	inline void set_async_event() { m_s.async_event = true; }
	inline void clear_inhibit_mask() { m_s.inhibit_mask = 0; }
	inline void set_debug_trap(uint32_t _value) { m_s.debug_trap = _value; }
	inline void clear_debug_trap() { m_s.debug_trap = 0; }
	inline void set_debug_trap_bit(uint32_t _bit) { m_s.debug_trap |= _bit; }
	inline void clear_debug_trap_bit(uint32_t _bit) { m_s.debug_trap &= ~_bit; }

	void set_HRQ(bool _val);
	bool get_HRQ() { return m_s.HRQ; }

	void set_shutdown_trap(std::function<void(void)> _fn);
	void enter_sleep_state(CPUActivityState _state);
	void exception(CPUException _exc);

	void write_log();
	void enable_prg_log(std::string _prg_name);
	void disable_prg_log();
	void DOS_program_launch(std::string _name);
	void DOS_program_start(std::string _name);
	void DOS_program_finish(std::string _name);

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

protected:
	void handle_async_event();
	void mask_event(uint32_t event);
	void signal_event(uint32_t _event);
	void clear_event(uint32_t _event);
	bool is_masked_event(uint32_t _event);
	bool is_pending(uint32_t _event);
	bool is_unmasked_event_pending(uint32_t _event);
	uint32_t unmasked_events_pending();
	void default_shutdown_trap() {}
	bool is_double_fault(uint8_t _first_vec, uint8_t _current_vec);
	bool v86_redirect_interrupt(uint8_t _vector);

	void wait_for_event();

	int get_execution_cycles(bool _memtx);
	int get_io_cycles(int _io_time);
};


#endif
