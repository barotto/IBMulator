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

#ifndef IBMULATOR_CPU_LOGGER_H
#define IBMULATOR_CPU_LOGGER_H

#include "core.h"
#include "decoder.h"

struct CPULogEntry
{
	uint64_t time;
	CPUCore core;
	CPUBus bus;
	Instruction instr;
	unsigned cycles;
};

class CPULogger
{
private:
	uint m_log_idx;
	uint m_log_size;
	CPULogEntry m_log[CPULOG_MAX_SIZE];
	uint32_t m_iret_address;
	FILE *m_log_file;
	std::string m_log_filename;
	std::map<int,uint64_t> m_global_counters;
	std::map<int,uint64_t> m_file_counters;

	static int get_opcode_index(const Instruction &_instr);
	static int write_entry(FILE *_dest, CPULogEntry &_entry);
	static const std::string & disasm(CPULogEntry &_log_entry);
	static void write_counters(const std::string _filename, std::map<int,uint64_t> &_cnt);

public:

	CPULogger();
	~CPULogger();

	void add_entry(const Instruction &_instr, uint64_t _time,
			const CPUCore &_core, const CPUBus &_bus, unsigned _cycles);
	void open_file(const std::string _filename);
	void close_file();
	void set_iret_address(uint32_t _address);
	void reset_iret_address();
	void reset_global_counters();
	void reset_file_counters();
	void dump(const std::string _filename);
};

#endif
