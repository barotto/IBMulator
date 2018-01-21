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

#ifndef IBMULATOR_CPU_LOGGER_H
#define IBMULATOR_CPU_LOGGER_H

#define CPULOG               false        // activate CPU logging?
#define CPULOG_FILE          "cpulog.log" // log file name
#if CPULOG
#define CPULOG_MAX_SIZE      400000u      // number of instructions to log
#else
#define CPULOG_MAX_SIZE      1u
#endif
#define CPULOG_WRITE_TIME    true       // write instruction machine time?
#define CPULOG_WRITE_CSEIP   true       // write instruction address as CS:EIP?
#define CPULOG_WRITE_HEX     true       // write instruction as hex codes?
#define CPULOG_WRITE_DISASM  true       // write the disassembled instruction?
#define CPULOG_WRITE_STATE   true       // write the CPU global state?
#define CPULOG_WRITE_CORE    true       // write the CPU registers?
#define CPULOG_DECODE_FLAGS  true       // decode flags register into a string
#define CPULOG_WRITE_SEGREGS true       // write extended seg regs status? (only if CPULOG_WRITE_CORE is true)
#define CPULOG_WRITE_PQ      false      // write the prefetch queue?
#define CPULOG_WRITE_TIMINGS false      // write various timing values?
#define CPULOG_START_ADDR    0x0        // lower bound, instr. before this address are not logged
#define CPULOG_END_ADDR      0xFFFFFFFF // upper bound, instr. after this address are not logged
#define CPULOG_LOG_INTS      true       // log INTs' instructions?
#define CPULOG_INT21_EXIT_IP -1         // the OS dependent IP of the last instr. of INT 21/4B
                                        // For PC-DOS 4.01 under ROMSHELL is 0x7782,
                                        //                 under plain DOS is 0x7852
                                        // use -1 to disable (logging starts at INT call)
#define CPULOG_COUNTERS      false      // count every instruction executed

#include "core.h"
#include "decoder.h"
#include "state.h"
#include "exception.h"

struct CPULogIRQ
{
	uint8_t irq;
	uint8_t vector;
};

struct CPULogEntry
{
	uint64_t time;
	CPUState state;
	CPUCore core;
	CPUException exc;
	CPUBus bus;
	Instruction instr;
	CPUCycles cycles;
	CPULogIRQ irq;
};

class CPULogger
{
private:
	uint m_log_idx;
	uint m_log_size;
	CPULogEntry m_log[CPULOG_MAX_SIZE];
	uint32_t m_iret_address;
	CPULogIRQ m_irq;
	FILE *m_log_file;
	std::string m_log_filename;
	std::map<int,uint64_t> m_global_counters;
	std::map<int,uint64_t> m_file_counters;

	static int get_opcode_index(const Instruction &_instr);
	static int write_entry(FILE *_dest, CPULogEntry &_entry);
	static const std::string & disasm(CPULogEntry &_log_entry);
	static void write_counters(const std::string _filename, std::map<int,uint64_t> &_cnt);
	static int write_segreg(FILE *_dest, const CPUCore &_core, const SegReg &_segreg, const char *_name);
	static const char* decode_eflags(uint32_t _eflags, bool _32bit);

public:

	CPULogger();
	~CPULogger();

	void add_entry(
		uint64_t _time,
		const Instruction &_instr,
		const CPUState &_state,
		const CPUException &_exc,
		const CPUCore &_core,
		const CPUBus &_bus,
		const CPUCycles &_cycles
	);
	void set_prev_i_exc(const CPUException &_exc, uint32_t _cseip);
	void set_next_i_irq(uint8_t _irq, uint8_t _vector);
	void open_file(const std::string _filename);
	void close_file();
	void set_iret_address(uint32_t _address);
	void reset_iret_address();
	void reset_global_counters();
	void reset_file_counters();
	void dump(const std::string _filename);
};

#endif
