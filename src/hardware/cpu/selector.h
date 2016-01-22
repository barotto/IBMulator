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

#ifndef IBMULATOR_CPU_SELECTOR_H
#define IBMULATOR_CPU_SELECTOR_H

#define SELECTOR_RPL(selector) ((selector) & 0x03)
#define SELECTOR_RPL_MASK  0xfffc

/* (cfr. 6-2)
 */
struct Selector
{
	uint16_t value; // the raw selector value (real mode)

	union {
		uint8_t rpl; // Requested Privilege Level
		uint8_t cpl; // Current Privilege Level (for loaded CS and SS)
	};
	uint8_t ti;     // Table indicator
	uint16_t index; // Segment index

	inline void operator=(uint16_t _value) {
		value = _value;
		rpl = value & 3;
		ti = (value >> 2) & 1;
		index = value >> 3;
	}
};

#endif
