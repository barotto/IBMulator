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

void CPUExecutor::stack_push_word(uint16_t _value)
{
	if(REG_SS.desc.big) {
		// StackAddrSize = 32
		if(REG_ESP == 1) {
			if(IS_PMODE()) {
				throw CPUException(CPU_SS_EXC, 0);
			} else {
				throw CPUShutdown("insufficient stack space on push");
			}
		}
		REG_ESP -= 2;
		write_word(REG_SS, REG_ESP, _value);
	} else {
		// StackAddrSize = 16
		if(REG_SP == 1) {
			if(IS_PMODE()) {
				throw CPUException(CPU_SS_EXC, 0);
			} else {
				throw CPUShutdown("insufficient stack space on push");
			}
		}
		REG_SP -= 2;
		write_word(REG_SS, REG_SP, _value);
	}
}

void CPUExecutor::stack_push_dword(uint32_t _value)
{
	if(REG_SS.desc.big) {
		// StackAddrSize = 32
		if(REG_ESP < 4) {
			if(IS_PMODE()) {
				throw CPUException(CPU_SS_EXC, 0);
			} else {
				throw CPUShutdown("insufficient stack space on push");
			}
		}
		REG_ESP -= 4;
		write_word(REG_SS, REG_ESP, _value);
	} else {
		// StackAddrSize = 16
		if(REG_SP < 4) {
			if(IS_PMODE()) {
				throw CPUException(CPU_SS_EXC, 0);
			} else {
				throw CPUShutdown("insufficient stack space on push");
			}
		}
		REG_SP -= 4;
		write_word(REG_SS, REG_SP, _value);
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
