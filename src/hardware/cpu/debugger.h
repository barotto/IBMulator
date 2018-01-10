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

#ifndef IBMULATOR_DEBUG_H
#define IBMULATOR_DEBUG_H

#include "disasm.h"
#include "core.h"
#include "../memory.h"
#include <map>

typedef std::function<void(
		bool call,
		uint16_t ax,
		CPUCore *core,
		Memory *mem,
		char* buf,
		uint buflen
	)> int_decoder_fun_t;

struct int_info_t {
	bool decode;
	int_decoder_fun_t decoder;
	const char * name;
};

typedef std::map<uint32_t, int_info_t > int_map_t;
typedef std::map<uint8_t, const char*> doscodes_map_t;

class CPUDebugger
{
protected:

	Disasm m_dasm;

	static std::map<uint32_t, const char*> ms_addrnames;
	static int_map_t ms_interrupts;
	static doscodes_map_t ms_dos_errors;
	static doscodes_map_t ms_disk_status;
	static doscodes_map_t ms_ioctl_code;

	static uint32_t get_hex_value(char *_str, char *&_hex, CPUCore *_core);
	static unsigned get_seg_idx(char *_str);
	static bool get_drive_CHS(const CPUCore &_core, int &_drive, int &_C, int &_H, int &_S);

	static void INT_def_ret(CPUCore *core, char* buf, uint buflen);
	static void INT_def_ret_errcode(CPUCore *core, char* buf, uint buflen);
	static void INT_10_00(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_10_0E(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_10_12(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_13(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_13_02_3_4_C(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_15_86(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_15_87(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_1A_00(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_21_09(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_21_25(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_21_0E(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_21_2C(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_21_30(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_21_32(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_21_36(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_21_39_A_B_4E(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_21_3D(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_21_3E(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_21_3F(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_21_40(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_21_42(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_21_43(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_21_440D(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_21_48(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_21_4A(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_21_4B(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_21_5F03(bool call, uint16_t ax, CPUCore *core, Memory *mem,char* buf, uint buflen);
	static void INT_2B_01(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_2F_1116(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);
	static void INT_2F_1123(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen);

public:

	CPUDebugger() {}

	unsigned disasm(char * _buf, uint _buflen, uint32_t _cs, uint32_t _eip, CPUCore *_core, Memory *_mem,
			const uint8_t *_instr_buf, uint _instr_buf_len, bool _32bit);
	unsigned last_disasm_opsize();
	char * analyze_instruction(char * _inst, CPUCore *_core, Memory *_mem, uint _opsize);

	static const char * INT_decode(bool call, uint8_t vector, uint16_t ax,
			CPUCore *core, Memory *mem);
	static std::string descriptor_table_to_CSV(Memory &_mem, uint32_t _base, uint16_t _limit);
};




#endif
