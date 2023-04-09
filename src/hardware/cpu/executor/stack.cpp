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

#include "ibmulator.h"
#include "hardware/cpu/executor.h"

void CPUExecutor::stack_push_word(uint16_t _value)
{
	if(REG_SS.desc.big) {
		// StackAddrSize = 32
		write_word(REG_SS, REG_ESP-2, _value);
		REG_ESP -= 2;
	} else {
		// StackAddrSize = 16
		write_word(REG_SS, uint16_t(REG_SP-2), _value);
		REG_SP -= 2;
	}
}

void CPUExecutor::stack_push_dword(uint32_t _value)
{
	if(REG_SS.desc.big) {
		// StackAddrSize = 32
		write_dword(REG_SS, REG_ESP-4, _value);
		REG_ESP -= 4;
	} else {
		// StackAddrSize = 16
		write_dword(REG_SS, uint16_t(REG_SP-4), _value);
		REG_SP -= 4;
	}
}

void CPUExecutor::stack_push_sr_dword(uint16_t _value)
{
	// 80386, 80486 perform a 16-bit move, leaving the upper portion of the stack
	// location unmodified (tested on real hardware). Probably all 32-bit Intel
	// CPUs behave in this way, but this behaviour is not specified in the docs
	// for older CPUs and is cited in the most recent docs like this:
	// "If the source operand is a segment register (16 bits) and the operand
	// size is 32-bits, either a zero-extended value is pushed on the stack or
	// the segment selector is written on the stack using a 16-bit move. For the
	// last case, all recent Core and Atom processors perform a 16-bit move,
	// leaving the upper portion of the stack location unmodified."
	if(REG_SS.desc.big) {
		// StackAddrSize = 32
		write_word(REG_SS, REG_ESP-4, _value);
		REG_ESP -= 4;
	} else {
		// StackAddrSize = 16
		write_word(REG_SS, uint16_t(REG_SP-4), _value);
		REG_SP -= 4;
	}
}

uint16_t CPUExecutor::stack_pop_word()
{
	uint16_t value;

	if(REG_SS.desc.big) {
		// StackAddrSize = 32
		value = read_word(REG_SS, REG_ESP);
		REG_ESP += 2;
	} else {
		// StackAddrSize = 16
		value = read_word(REG_SS, REG_SP);
		REG_SP += 2;
	}

	return value;
}

uint32_t CPUExecutor::stack_pop_dword()
{
	uint32_t value;

	if(REG_SS.desc.big) {
		// StackAddrSize = 32
		value = read_dword(REG_SS, REG_ESP);
		REG_ESP += 4;
	} else {
		// StackAddrSize = 16
		value = read_dword(REG_SS, REG_SP);
		REG_SP += 4;
	}

	return value;
}

void CPUExecutor::stack_write_word(uint16_t _value, uint32_t _offset)
{
	if(!REG_SS.desc.big) {
		_offset &= 0xFFFF;
	}
	write_word(REG_SS, _offset, _value);
}

void CPUExecutor::stack_write_dword(uint32_t _value, uint32_t _offset)
{
	if(!REG_SS.desc.big) {
		_offset &= 0xFFFF;
	}
	write_dword(REG_SS, _offset, _value);
}

uint16_t CPUExecutor::stack_read_word(uint32_t _offset)
{
	if(!REG_SS.desc.big) {
		_offset &= 0xFFFF;
	}
	return read_word(REG_SS, _offset);
}

uint32_t CPUExecutor::stack_read_dword(uint32_t _offset)
{
	if(!REG_SS.desc.big) {
		_offset &= 0xFFFF;
	}
	return read_dword(REG_SS, _offset);
}
