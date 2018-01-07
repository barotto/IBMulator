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

void CPUExecutor::seg_check_read(SegReg & _seg, uint32_t _offset, unsigned _len, uint8_t _vector, uint16_t _errcode)
{
	assert(_len!=0);

	if(_seg.desc.is_expand_down()) {
		uint32_t upper_limit = 0xFFFF;
		if(_seg.desc.big) {
			upper_limit = 0xFFFFFFFF;
		}
		if(_offset <= _seg.desc.limit || _offset > upper_limit || (upper_limit - _offset) < _len) {
			PDEBUGF(LOG_V2, LOG_CPU, "seg_check_read(): segment limit violation exp.down\n");
			throw CPUException(_vector, _errcode);
		}
	} else if(_offset+_len-1 > _seg.desc.limit) {
		PDEBUGF(LOG_V2, LOG_CPU, "seg_check_read(): segment limit violation\n");
		throw CPUException(_vector, _errcode);
	}
	if(_seg.desc.is_code_segment() && !_seg.desc.is_readable()) {
		PDEBUGF(LOG_V2, LOG_CPU, "seg_check_read(): execute only\n");
		throw CPUException(_vector, _errcode);
	}
}

void CPUExecutor::seg_check_write(SegReg & _seg, uint32_t _offset, unsigned _len, uint8_t _vector, uint16_t _errcode)
{
	assert(_len!=0);

	if(!_seg.desc.is_writeable()) {
		PDEBUGF(LOG_V2, LOG_CPU, "seg_check_write(): segment not writeable\n");
		throw CPUException(_vector, _errcode);
	}
	if(_seg.desc.is_expand_down()) {
		uint32_t upper_limit = 0xFFFF;
		if(_seg.desc.big) {
			upper_limit = 0xFFFFFFFF;
		}
		if(_offset <= _seg.desc.limit || _offset > upper_limit || (upper_limit - _offset) < _len) {
			PDEBUGF(LOG_V2, LOG_CPU, "seg_check_write(): segment limit violation exp.down\n");
			throw CPUException(_vector, _errcode);
		}
	} else if(_offset+_len-1 > _seg.desc.limit) {
		PDEBUGF(LOG_V2, LOG_CPU, "seg_check_write(): segment limit violation\n");
		throw CPUException(_vector, _errcode);
	}
}

void CPUExecutor::seg_check(SegReg & _seg, uint32_t _offset, unsigned _len,
		bool _write, uint8_t _vector, uint16_t _errcode)
{
	if(_vector == CPU_INVALID_INT) {
		_vector = _seg.is(REG_SS)?CPU_SS_EXC:CPU_GP_EXC;
	}
	if(!_seg.desc.valid) {
		PDEBUGF(LOG_V2, LOG_CPU, "seg_check(): segment not valid\n");
		throw CPUException(_vector, _errcode);
	}
	if(_write) {
		seg_check_write(_seg, _offset, _len, _vector, _errcode);
	} else {
		seg_check_read(_seg, _offset, _len, _vector, _errcode);
	}
}

void CPUExecutor::io_check(uint16_t _port, unsigned _len)
{
	if((IS_PMODE() && (CPL > FLAG_IOPL)) || IS_V8086()) {
		if(CPU_FAMILY <= CPU_286) {
			/* #GP(O) if the current privilege level is bigger (has less privilege)
			 * than IOPL; which is the privilege level found in the flags register.
			 */
			PDEBUGF(LOG_V2, LOG_CPU, "I/O access not allowed\n");
			throw CPUException(CPU_GP_EXC, 0);
		}
		if(!REG_TR.desc.valid || !REG_TR.desc.is_system_segment() || (
			REG_TR.desc.type != DESC_TYPE_AVAIL_386_TSS &&
			REG_TR.desc.type != DESC_TYPE_BUSY_386_TSS
		))
		{
			PDEBUGF(LOG_V2, LOG_CPU, "TR doesn't point to a valid 32bit TSS\n");
			throw CPUException(CPU_GP_EXC, 0);
		}
		uint32_t io_base = read_word(REG_TR, 102, CPU_GP_EXC, 0);
		uint16_t permission = read_word(REG_TR, io_base + _port/8, CPU_GP_EXC, 0);
		unsigned bit_index = _port & 0x7;
		unsigned mask = (1 << _len) - 1;
		if((permission >> bit_index) & mask) {
			throw CPUException(CPU_GP_EXC, 0);
		}
	}
}
