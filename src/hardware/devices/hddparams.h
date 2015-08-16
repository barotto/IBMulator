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

#ifndef IBMULATOR_HDD_PARAMS_H
#define IBMULATOR_HDD_PARAMS_H

struct HDDParams
{
	uint16_t cylinders;  // number of cylinders
	uint8_t  heads;      // number of heads
	uint16_t rwcyl;      // starting reduced write current cylinder
	uint16_t wpcyl;      // starting write precompensation cylinder number
	uint8_t  ECClen;     // maximum ECC burst length
	uint8_t  options;    // drive step options:
	                     //   bit 0  unused
	                     //   bit 1  reserved (0)  (disable IRQ)
	                     //   bit 2  reserved (0)  (no reset)
	                     //   bit 3  set if more than 8 heads
	                     //   bit 4  always 0
	                     //   bit 5  set if manufacturer's defect map on max cylinder+1
	                     //   bit 6  disable ECC retries
	                     //   bit 7  disable access retries
	uint8_t  timeoutstd; // standard timeout value
	uint8_t  timeoutfmt; // timeout value for format drive
	uint8_t  timeoutchk; // timeout value for check drive
	uint16_t lzone;      // cylinder number of landing zone
	uint8_t  sectors;    // number of sectors per track
	uint8_t  reserved;   // always 0
} GCC_ATTRIBUTE(packed);

#endif
