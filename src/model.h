/*
 * Copyright (C) 2016-2025  Marco Bortolin
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

#include "appconfig.h"
#include <string>
#include <map>

/* The BIOS is the 64K code segment at address 0xF0000
 * The same BIOS is used in different system ROMs for different regional markets.
 */
struct BIOSType
{
	std::string version;
	std::string type;
	unsigned machine_model;
	uint16_t hdd_ptable_off;
};

typedef std::map<const std::string, BIOSType> bios_db_t;
extern const bios_db_t g_bios_db;

struct ModelConfig
{
	std::string ini;
	std::string name;
	unsigned type;
	std::string machine_name;
	std::string cpu_model;
	unsigned cpu_freq;
	unsigned board_ram;
	unsigned exp_ram;
	unsigned ram_speed;
	unsigned rom_speed;
	unsigned rom_bit;
	unsigned floppy_a;
	unsigned floppy_b;
	std::string hdd_interface;
	unsigned hdd_type;
	unsigned cdrom;
	unsigned serial;
	unsigned parallel;

	std::string print() const;
};

typedef std::map<int, ModelConfig> machine_db_t;
extern const machine_db_t g_machine_db;
extern const ini_enum_map_t g_ini_model_names;
extern const std::map<unsigned, std::string> g_machine_type_str;


/* There's some confusion about the proper terminology.
 * "Type" is the 4 digit number with which IBM identified the various PS/1's,
 * like 2011 and 2121.
 * "Model" was the combination of machine "Type" with a variation, e.g. 2121-A82,
 * which identified a particular hardware configuration.
 * Unfortunately IBM later started to use "Model" to identify the machine "Type"
 * as well, like it used to do with the PS/2 line.
 * I use Type in the sense IBM originally intended.
 */
enum MachineType {
	MCH_UNK,
	PS1_2011,
	PS1_2121
};

enum MachineModel {
	MDL_UNKNOWN,
	PS1_2011_C34,
	PS1_2121_B82,
	PS1_2121_A82
};

#endif
