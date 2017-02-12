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

#ifndef IBMULATOR_MODEL_H
#define IBMULATOR_MODEL_H

#include <string>
#include <map>

/* The BIOS is the 64K code segment at address 0xF0000
 * The same BIOS is used in different system ROMs for different regional markets.
 */
struct BIOSType
{
	std::string version;
	std::string type;
	// TODO the same BIOS can be used for different models. Implement a 1-n relation.
	unsigned machine;
	std::string machine_str;
	uint16_t hdd_ptable_off;
};

typedef std::map<const std::string, BIOSType> bios_db_t;
extern bios_db_t g_bios_db;

struct MachineModel
{
	std::string cpu;
	unsigned cpu_freq;
	unsigned ram;
	std::string hdd_interface;
	/* TODO the HDD param table does not have all the HDD models */
	unsigned hdd_type;
};

// TODO the same machine type is used for different models. Implement a 1-n relation.
typedef std::map<unsigned, MachineModel> machine_db_t;
extern machine_db_t g_machine_db;

#endif
