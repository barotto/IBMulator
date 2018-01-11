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

#ifndef IBMULATOR_CPU_DISASM_H
#define IBMULATOR_CPU_DISASM_H

class CPUCore;
class Memory;

class Disasm
{
private:
	CPUCore *m_cpu;
	Memory *m_memory;

	bool must_do_size; // used with size of operand
	bool wordop;       // dealing with word or byte operand

	uint32_t instruction_offset;
	uint32_t instruction_segment;

	char* ubufs;  // start of buffer
	char* ubufp;  // last position of buffer
	int ubuflen;  // lenght of buffer
	bool invalid_opcode = false;
	bool first_space = true;

	int prefix;   // segment override prefix byte
	int modrmv;   // flag for getting modrm byte
	int sibv;     // flag for getting sib byte
	int opsize;   // operand size
	int addrsize; // address size

	uint32_t getbyte_mac;
	uint32_t startPtr;
	const uint8_t * instr_buffer;
	uint instr_buffer_size;

	char addr_to_hex_buffer[11];

	char *addr_to_hex(uint32_t addr, bool splitup = false);
	uint32_t getbyte();
	int modrm();
	int sib();
	void uprintf(char const *s, ...);
	void uputchar(char c);
	int bytes(char c);
	void outhex(char subtype, int extend, int optional, int defsize, int sign);
	void reg_name(int regnum, char size);
	void ua_str(char const *str);
	void do_sib(int m);
	void do_modrm(char subtype);
	void floating_point(int e1);
	void percent(char type, char subtype);

public:
	uint32_t disasm(char *_buffer, uint _buffer_len, uint32_t _cs, uint32_t _eip,
			CPUCore *_core, Memory *_memory, const uint8_t *_instr_buf=nullptr, uint _instr_buf_len=0,
			bool _32bit=false);

	int last_operand_size() {
		return opsize;
	};
};

#endif
