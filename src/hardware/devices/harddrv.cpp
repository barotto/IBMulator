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

#include "ibmulator.h"
#include "harddrv.h"
#include "program.h"
#include "machine.h"
#include "hardware/devices.h"
#include "hardware/devices/systemboard.h"
#include <cstring>

HardDrive::HardDrive()
{

}

HardDrive::~HardDrive()
{

}

void HardDrive::init()
{

}

void HardDrive::reset(unsigned type)
{

}

uint16_t HardDrive::read(uint16_t address, unsigned io_len)
{
	g_sysboard.set_feedback();
}

void HardDrive::write(uint16_t address, uint16_t value, unsigned io_len)
{
	g_sysboard.set_feedback();
}
