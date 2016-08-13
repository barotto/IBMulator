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

#ifndef IBMULATOR_CPU_DECODER_H
#define IBMULATOR_CPU_DECODER_H

#include "../memory.h"
#include "bus.h"
#include <functional>

#define CPU_MAX_INSTR_SIZE 10

class CPUExecutor;
class CPUDecoder;
extern CPUDecoder g_cpudecoder;

/*
 The 8086/80286 instruction format
╔═══════════════╦══════════════╦══════════╦══════════╦════════════════╦═════════════╗
║  INSTRUCTION  ║   SEGMENT    ║  OPCODE  ║  MODR/M  ║  DISPLACEMENT  ║  IMMEDIATE  ║
║    PREFIX     ║   OVERRIDE   ║          ║          ║                ║             ║
╠═══════════════╩══════════════╩══════════╩══════════╩════════════════╩═════════════╣
║     0 OR 1         0 OR 1       1 OR 2     0 OR 1       0,1 OR 2       0,1 OR 2   ║
╟─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─╢
║                                 NUMBER OF BYTES                                   ║
╚═══════════════════════════════════════════════════════════════════════════════════╝


 The 80386 instruction format
╔═══════════════╦═══════════════╦═══════════════╦═══════════════╗
║  INSTRUCTION  ║   ADDRESS-    ║    OPERAND-   ║   SEGMENT     ║
║    PREFIX     ║  SIZE PREFIX  ║  SIZE PREFIX  ║   OVERRIDE    ║
╠═══════════════╩═══════════════╩═══════════════╩═══════════════╣
║     0 OR 1         0 OR 1           0 OR 1         0 OR 1     ║
╟─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─╢
║                        NUMBER OF BYTES                        ║
╚═══════════════════════════════════════════════════════════════╝

╔══════════╦═══════════╦═══════╦══════════════════╦═════════════╗
║  OPCODE  ║  MODR/M   ║  SIB  ║   DISPLACEMENT   ║  IMMEDIATE  ║
║          ║           ║       ║                  ║             ║
╠══════════╩═══════════╩═══════╩══════════════════╩═════════════╣
║  1 OR 2     0 OR 1    0 OR 1      0,1,2 OR 4       0,1,2 OR 4 ║
╟─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─╢
║                        NUMBER OF BYTES                        ║
╚═══════════════════════════════════════════════════════════════╝
*/

class ModRM
{
	/* 7    6    5    4    3    2    1    0
	  ╔════════╦═════════════╦════════════╗
	  ║  MOD   ║ REG/OPCODE  ║     R/M    ║ ModR/M byte
	  ╚════════╩═════════════╩════════════╝
	  ╔════════╦═════════════╦════════════╗
	  ║ SCALE  ║    INDEX    ║    BASE    ║ SIB byte
	  ╚════════╩═════════════╩════════════╝
	*/
public:
	uint8_t mod;
	union {
		uint8_t r;
		uint8_t n;
	};
	uint8_t rm;
	uint8_t scale;
	uint8_t index;
	uint8_t base;
	uint32_t disp;

	ModRM() = default;
	inline void load(bool _32bit=false);

	inline bool mod_is_reg() { return mod == 3; }

private:
	inline void load_SIB();
};

struct Cycles
{
	int base;     //!< CPU cycles for execution
	int memop;    //!< CPU cycles for execution if memory is accessed
	int extra;    //!< any run-time dependent extra amount (like shifts and rotates)
	int rep;      //!< execution cycles for the rep "warmup" (time spent before the loop)
	int base_rep; //!< CPU cycles for execution if inside a rep loop
	int pmode;    //!< CPU cycles penalty if protected mode (added to base)
	int noj;      //!< for jumps if jump not taken
	int bu;       //!< cycles added to or reduced from the bu cycles count
				  //   this is a hack, to account for proper bu operations ordering
};

typedef void (CPUExecutor::*CPUExecutor_fun)();

struct Instruction
{
	bool valid;     //!< true if valid
	CPUExecutor_fun fn; //!< executor function
	uint8_t db;       //!< byte function arg
	uint16_t dw1,dw2; //!< word function args
	uint32_t dd1,dd2; //!< dword function args
	uint32_t offset;  //!< memory offset
	uint8_t reg;    //!< register index for op+ instructions (like MOVs)
	uint8_t seg;    //!< index of the segment override
	bool op32;      //!< operand-size is 32 bit
	bool addr32;    //!< address-size is 32 bit
	ModRM modrm;    //!< the ModRM
	bool rep;       //!< true if REP/REPE/REPNE
	bool rep_zf;    //!< tells the executor that the exit condition is by checking the ZF
	bool rep_equal; //!< true if REPE, false if REPNE
	uint32_t eip;   //!< used in cpu logging only
	uint32_t cseip; //!< the instruction physical memory address
	uint size;      //!< total size of the instruction (prefixes included)
	Cycles cycles;
	uint8_t bytes[CPU_MAX_INSTR_SIZE]; //!< the instruction bytes (prefixes included)
	uint16_t opcode; //!< main opcode (used only when CPULOG is true)
};


class CPUDecoder
{

friend class ModRM;

private:
	uint32_t m_ilen;
	Instruction m_instr;
	bool m_rep;

	enum CyclesTableIndex {
		CTB_IDX_NONE,
		CTB_IDX_0F,

		// Group 1
		CTB_IDX_80,
		CTB_IDX_81,
		CTB_IDX_83,

		// Group 2
		CTB_IDX_C0,
		CTB_IDX_C1,
		CTB_IDX_D0,
		CTB_IDX_D1,
		CTB_IDX_D2,
		CTB_IDX_D3,

		// Group 3
		CTB_IDX_F6,
		CTB_IDX_F7,

		// Group 4
		CTB_IDX_FE,

		// Group 5
		CTB_IDX_FF,

		// Group 6
		CTB_IDX_0F00,

		// Group 7
		CTB_IDX_0F01,

		// Group 8
		CTB_IDX_0FBA,

		CTB_COUNT
	};

	static const Cycles * ms_cycles[CTB_COUNT];

public:
	Instruction * decode();
	inline uint32_t get_next_cseip() {
		//return the linear address of the next decoded instruction
		return g_cpubus.get_cseip();
	}

private:
	void prefix_none(uint8_t _opcode, unsigned &ctb_idx_, unsigned &ctb_op_);
	void prefix_0F(uint8_t _opcode, unsigned &ctb_idx_, unsigned &ctb_op_);
	void illegal_opcode();

	inline uint8_t fetchb() {
		uint8_t b = g_cpubus.fetchb();
		if(m_ilen < CPU_MAX_INSTR_SIZE) {
			m_instr.bytes[m_ilen] = b;
		}
		m_ilen += 1;
		return b;
	}

	inline uint16_t fetchw() {
		uint16_t w = g_cpubus.fetchw();
		if(m_ilen+1 < CPU_MAX_INSTR_SIZE) {
			*(uint16_t*)(&m_instr.bytes[m_ilen]) = w;
		}
		m_ilen += 2;
		return w;
	}

	inline uint32_t fetchdw() {
		uint32_t dw = g_cpubus.fetchdw();
		if(m_ilen+3 < CPU_MAX_INSTR_SIZE) {
			*(uint32_t*)(&m_instr.bytes[m_ilen]) = dw;
		}
		m_ilen += 4;
		return dw;
	}
};


inline void ModRM::load_SIB()
{
	uint8_t sib = g_cpudecoder.fetchb();
	scale = (sib >> 6) & 3;
	index = (sib >> 3) & 7;
	base = sib & 7;
}

inline void ModRM::load(bool _32bit)
{
	uint8_t modrm = g_cpudecoder.fetchb();
	mod = (modrm >> 6) & 3;
	r = (modrm >> 3) & 7;
	rm = modrm & 7;
	disp = 0;
	if(_32bit) {
		if(mod == 0) {
			if(rm == 4) {
				load_SIB();
				if(base == 5) {
					disp = g_cpudecoder.fetchdw();
				}
			} else if(rm == 5) {
				disp = g_cpudecoder.fetchdw();
			}
		} else if(mod == 1) {
			if(rm == 4) {
				load_SIB();
			}
			disp = int8_t(g_cpudecoder.fetchb());
		} else if(mod == 2) {
			if(rm == 4) {
				load_SIB();
			}
			disp = g_cpudecoder.fetchdw();
		}
	} else {
		if(mod==0 && rm==6) {
			disp = g_cpudecoder.fetchw();
		} else if(mod==0 || mod==3) {
			disp = 0;
		} else if(mod==1) {
			disp = int8_t(g_cpudecoder.fetchb());
		} else if(mod==2) {
			disp = g_cpudecoder.fetchw();
		}
	}
}


#endif
