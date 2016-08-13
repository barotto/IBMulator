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

void CPUExecutor::write_flags(uint16_t _flags,
		bool _change_IOPL, bool _change_IF, bool _change_NT)
{
	// Build a mask of the following bits:
	// x,NT,IOPL,OF,DF,IF,TF,SF,ZF,x,AF,x,PF,x,CF
	uint16_t changeMask = 0x0dd5;

	if(_change_NT)
		changeMask |= FMASK_NT;   // NT is modified as requested.
	if(_change_IOPL)
		changeMask |= FMASK_IOPL; // IOPL is modified as requested.
	if(_change_IF)
		changeMask |= FMASK_IF;   // IF is modified as requested.

	// Screen out changing of any unsupported bits.
	changeMask &= FMASK_VALID;

	uint16_t new_flags = (GET_FLAGS() & ~changeMask) | (_flags & changeMask);
	SET_FLAGS(new_flags);
}

void CPUExecutor::write_flags(uint16_t _flags)
{
	if(IS_PMODE()) {
		write_flags(_flags,
			(CPL == 0),         // IOPL
			(CPL <= FLAG_IOPL), // IF
			true                // NT
		);
	} else if(IS_V8086()) {
		if(FLAG_IOPL < 3) {
			PDEBUGF(LOG_CPU, LOG_V2, "write_flags: general protection in v8086 mode\n");
			throw CPUException(CPU_GP_EXC, 0);
		}
		write_flags(_flags,
			false, // IOPL CPL is always 3 in V86 mode
			true,  // IF   CPL<=FLAG_IOPL is always true
			true   // NT
		);
	} else {
		write_flags(_flags,
			false, // IOPL
			true,  // IF
			false  // NT
		);
	}
}
