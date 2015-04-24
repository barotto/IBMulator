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

#ifndef IBMULATOR_CPU_DESCRIPTOR_H
#define IBMULATOR_CPU_DESCRIPTOR_H


/*
 * Segment descriptor AR masks
 */
#define SEG_ACCESSED	0x1
#define SEG_READWRITE	0x2
#define SEG_CONFORMING  0x4
#define SEG_EXP_DOWN    0x4
#define SEG_EXECUTABLE  0x8
#define SEG_CODE        0x8
#define SEG_SEGMENT     0x10
#define SEG_PRESENT     0x80

/* Segment descriptor type masks
 */
#define SEG_TYPE_READWRITE  0x1
#define SEG_TYPE_READABLE   0x1
#define SEG_TYPE_WRITABLE   0x1
#define SEG_TYPE_CONFORMING 0x2
#define SEG_TYPE_EXP_DOWN   0x2
#define SEG_TYPE_EXECUTABLE 0x4
#define SEG_TYPE_CODE       0x4


enum DescriptorType {
	DESC_TYPE_INVALID    = 0,
	DESC_TYPE_AVAIL_TSS  = 1,
	DESC_TYPE_LDT_DESC   = 2,
	DESC_TYPE_BUSY_TSS   = 3,
	DESC_TYPE_CALL_GATE  = 4,
	DESC_TYPE_TASK_GATE  = 5,
	DESC_TYPE_INTR_GATE  = 6,
	DESC_TYPE_TRAP_GATE  = 7,
	DESC_TYPE_RINVALID   = 8
};

#define DESC_TSS_BUSY_BIT64 0x02000000

struct Descriptor
{
	union {
		uint16_t limit;  // Segment descriptor
		uint16_t offset; // Gate descr. / Trap/Int Gate desc.
	};
	union {
		uint16_t base_15_0; // Segment descriptor
		uint16_t selector; // Gate desc. / Task Gate desc. / Trap/Int Gate desc.
	};
	union {
		uint8_t base_23_16; // Segment descriptor
		uint8_t word_count; // Gate descriptor
	};
	uint32_t base;
	uint8_t ar;
	// Access Rights (AR) bits:
	bool accessed; // | 0 (if segment, 1=desc. has been accessed)
	uint8_t type;  // | 0,1,2,3 (b0 if gate)
	bool segment;  // | 4 (1=segment desc., 0=control desc.)
	uint8_t dpl;   // | 5,6 (Descriptor Privilege Level)
	bool present;  // | 7 (1=present in real memory)

	bool valid;

	inline uint8_t get_AR() {
		if(segment) {
			ar = type<<1 | accessed;
		} else {
			ar = type;
		}
		ar |= segment << 4 | dpl << 5 | present << 7;
		return ar;
	}

	inline void set_AR(uint8_t _AR) {
		ar = _AR;
		valid = true;
		segment = ar & 0x10;
		if(segment) {
			//Code or Data Segment Descriptor
			accessed = ar & 1;
			type = (ar>>1) & 7;
		} else {
			//System Segment Descriptor or Gate Descriptor
			type = (ar & 0xF);
			if(type==DESC_TYPE_INVALID || type>=DESC_TYPE_RINVALID) {
				valid = false;
			}
		}
		dpl = (ar>>5) & 3;
		present = ar & 0x80;
	}

	inline void set_BASE(uint16_t _BASE16, uint8_t _BASE8) {
		base_15_0 = _BASE16;
		base_23_16 = _BASE8;
		base = base_23_16;
		base = (base<<16) | base_15_0;
	}

	inline void set_LIMIT(uint16_t _LIMIT) {
		limit = _LIMIT;
	}

	void set(uint64_t _data) {
		set_LIMIT(uint16_t(_data));
		set_BASE(uint16_t(_data>>16), uint8_t(_data>>32));
		set_AR(uint8_t(_data>>40));
	}

	void set_from_cache(uint16_t _data[3]) {
		set_LIMIT(_data[2]);
		set_BASE(_data[0], _data[1]&0xFF);
		/*
		 * Access rights byte is in the format of the access byte in a descriptor.
		 * The only difference is that the present bit becomes a valid bit.
		 * If zero, the descriptor is considered invalid and any memory reference
		 * using the descriptor will cause exception 13 with an error code of zero.
		 */
		set_AR(_data[1] >> 8);
		valid = (_data[1]>>8) & 0x80;
	}

	inline bool is_code_segment() { return (ar & SEG_CODE); }
	inline bool is_code_segment_conforming() { return (ar & SEG_CONFORMING); }
	inline bool is_data_segment_expand_down() { return (ar & SEG_EXP_DOWN); }
	inline bool is_code_segment_readable() { return (ar & SEG_READWRITE); }
	inline bool is_data_segment_writeable() { return (ar & SEG_READWRITE); }
	inline bool is_data_segment() { return (!is_code_segment()); }
	inline bool is_code_segment_non_conforming() { return (!is_code_segment_conforming()); }
	inline bool is_system_segment() { return !(ar & SEG_SEGMENT); }
};

/*
 * Segment Descriptors (S=1) (cf. 6-5)

   7                             0 7                              0
  ╔═══════════════════════════════╤════════════════════════════════╗
+7║                         INTEL RESERVED                         ║+6
  ╟───┬───────┬───┬───────────┬───┬────────────────────────────────╢
+5║ P │  DPL  │S=1│   TYPE    │ A │          BASE 23-16            ║+4
  ╟───┴───┴───┴───┴───┴───┴───┴───┴────────────────────────────────╢
+3║                           BASE 15-0                            ║+2
  ╟───────────────────────────────┴────────────────────────────────╢
+1║                           LIMIT 15-0                           ║ 0
  ╚═══════════════════════════════╧════════════════════════════════╝
   15                            8 7                              0

 * System Segment Descriptors or Gate Descriptor (S=0) (cf. 6-6)

   7                             0 7                              0
  ╔═══════════════════════════════╤════════════════════════════════╗
+7║                         INTEL RESERVED                         ║+6
  ╟───┬───────┬───┬───────────────┬────────────────────────────────╢
+5║ P │  DPL  │S=0│     TYPE      │           BASE 23-16           ║+4
  ╟───┴───┴───┴───┴───┴───┴───┴───┴────────────────────────────────╢
+3║                           BASE 15-0                            ║+2
  ║───────────────────────────────┴────────────────────────────────╢
+1║                           LIMIT 15-0                           ║ 0
  ╚═══════════════════════════════╧════════════════════════════════╝
   15                            8 7                              0

 * Gate Descriptor (cf. 8-4)

	7							  0 7                             0
  ╔═══════════════════════════════════════════════════════════════╗
+7║                        INTEL RESERVED                         ║+6
  ╟───┬───────┬───┬───────────────┬───────────┬───────────────────╢
+5║ P │  DPL  │ 0 │      TYPE     │ x   x   x │  WORD COUNT 4-0   ║+4
  ╟───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───────────┬───────╢
+3║                 DESTINATION SELECTOR 15-2             │ x   x ║+2
  ╟───────────────────────────────┴───────────────────────┴───┴───╢
+1║                  DESTINATION OFFSET 15-0                      ║ 0
  ╚═══════════════════════════════╧═══════════════════════════════╝
   15                                                           0

 * TSS Descriptor (cf. 8-4)

   7                             0 7                             0
  ╔═══════════════════════════════╤═══════════════════════════════╗
+7║                        INTEL RESERVED                         ║+6
  ╟───┬───────┬───┬───────────┬───┬───────────────────────────────╢
+5║ P │  DPL  │ 0 │ 0   0   B │ 1 │         TSS BASE 23-16        ║+4
  ╟───┴───┴───┴───┴───┴───┴───┴───┴───────────────────────────────╢
+3║                          TSS BASE 15-0                        ║+2
  ╟───────────────────────────────┴───────────────────────────────╢
+1║                            TSS LIMIT                          ║ 0
  ╚═══════════════════════════════╧═══════════════════════════════╝
   15                                                            0

 * Task Gate Descriptor (cf. 8-8)

   7                             0 7                             0
  ╔═══════════════════════════════╤═══════════════════════════════╗
+7║                        INTEL RESERVED                         ║+6
  ╟───┬───────┬───┬───────────┬───┬───────────────────────────────╢
+5║ P │  DPL  │ 0 │ 0   1   0 │ 1 │            UNUSED             ║+4
  ╟───┴───┴───┴───┴───┴───┴───┴───┴───────────────────────────────╢
+3║                          TSS SELECTOR                         ║+2
  ╟───────────────────────────────┴───────────────────────────────╢
+1║                             UNUSED                            ║ 0
  ╚═══════════════════════════════╧═══════════════════════════════╝
   15                                                            0

 * Trap/Interrupt Gate Descriptors (cf. 9-4)

   7                             0 7                             0
  ╔═══════════════════════════════╤═══════════════════════════════╗
+7║                        INTEL RESERVED                         ║+6
  ╟───┬───────┬───┬───────────┬───┬───────────────────────────────╢
+5║ P │  DPL  │ 0 │ 0   1   1 │ T │            UNUSED             ║+4
  ╟───┴───┴───┴───┴───┴───┴───┴───┴───────────────────────────────╢
+3║                          TSS SELECTOR                         ║+2
  ╟───────────────────────────────┴───────────────────────────────╢
+1║                             UNUSED                            ║ 0
  ╚═══════════════════════════════╧═══════════════════════════════╝
   15                                                            0

 */

#endif
