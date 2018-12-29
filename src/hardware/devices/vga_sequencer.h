/*
 * Copyright (C) 2018  Marco Bortolin
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

// VGA Sequencer
// 6 registers, 19 fields

#include "utils.h"
#include <array>

enum VGA_SequencerRegisters {
	SEQ_RESET    = 0x00,  // Index 00h -- Reset Register
	SEQ_CLOCKING = 0x01,  // Index 01h -- Clocking Mode Register
	SEQ_MAP_MASK = 0x02,  // Index 02h -- Map Mask Register
	SEQ_CHARMAP  = 0x03,  // Index 03h -- Character Map Select register
	SEQ_MEM_MODE = 0x04,  // Index 04h -- Memory Mode Register
	SEQ_REGCOUNT
};

enum VGA_SequencerReset {
	SEQ_SR  = 0x02, // Synchronous reset (1)
	SEQ_ASR = 0x01  // Asynchronous reset (0)
};

enum VGA_SequencerClocking {
	SEQ_SO  = 0x20, // Screen Off (5)
	SEQ_SH4 = 0x10, // Shift 4 (4)
	SEQ_DC  = 0x08, // Dot Clock (3)
	SEQ_SL  = 0x04, // Shift Load (2)
	SEQ_D89 = 0x01  // 8/9 Dot Clocks (0)
};

enum VGA_SequencerMapMask {
	SEQ_M3E = 0x08, // Map 3 Enable (3)
	SEQ_M2E = 0x04, // Map 2 Enable (2)
	SEQ_M1E = 0x02, // Map 1 Enable (1)
	SEQ_M0E = 0x01  // Map 0 Enable (0)
};

enum VGA_SequencerCharMap {
	SEQ_MAH = 0x20, // Character Map A Select (MSB) (5)
	SEQ_MBH = 0x10, // Character Map B Select (MSB) (4)
	SEQ_MAL = 0x0c, // Character Map A Select (LS bits) (2-3)
	SEQ_MBL = 0x03  // Character Map B Select (LS bits) (0-1)
};

enum VGA_SequencerMemMode {
	SEQ_CH4 = 0x08, // Chain 4 (3)
	SEQ_OE  = 0x04, // Odd/Even (2)
	SEQ_EM  = 0x02  // Extended Memory (1)
};

struct VGA_Sequencer
{
	uint8_t address;       // Address register

	struct {
		bool SR;             // Synchronous reset (1)
		bool ASR;            // Asynchronous reset (0)
		operator uint8_t() const {
			return (SR << 1) | (ASR << 0);
		}
		void operator=(uint8_t _v) {
			SR = (_v & SEQ_SR); ASR = (_v & SEQ_ASR);
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{1,"ASR"}, {1,"SR"}});
		}
	} reset;               // Index 00h -- Reset Register

	struct {
		bool SO;             // Screen Off (5)
		bool SH4;            // Shift 4 (4)
		bool DC;             // Dot Clock (3)
		bool SL;             // Shift Load (2)
		bool D89;            // 8/9 Dot Clocks (0)
		operator uint8_t() const {
			return (SO << 5) | (SH4 << 4) | (DC << 3) |
			       (SL << 2) | (D89 << 0);
		}
		void operator=(uint8_t _v) {
			SO = (_v & SEQ_SO); SH4 = (_v & SEQ_SH4); DC = (_v & SEQ_DC);
			SL = (_v & SEQ_SL); D89 = (_v & SEQ_D89);
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{1,"D89"},{1,""},{1,"SL"},{1,"DC"},{1,"SH4"},{1,"SO"}});
		}
	} clocking;            // Index 01h -- Clocking Mode Register

	struct {
		bool M3E;            // Map 3 Enable (3)
		bool M2E;            // Map 2 Enable (2)
		bool M1E;            // Map 1 Enable (1)
		bool M0E;            // Map 0 Enable (0)
		operator uint8_t() const {
			return (M3E << 3) | (M2E << 2) | (M1E << 1) | (M0E << 0);
		}
		void operator=(uint8_t _v) {
			M3E = (_v & SEQ_M3E); M2E = (_v & SEQ_M2E);
			M1E = (_v & SEQ_M1E); M0E = (_v & SEQ_M0E);
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{1,"M0E"},{1,"M1E"},{1,"M2E"},{1,"M3E"}});
		}
	} map_mask;            // Index 02h -- Map Mask Register

	struct {
		uint8_t MAH;         // Character Map A Select (MSB) (5)
		uint8_t MBH;         // Character Map B Select (MSB) (4)
		uint8_t MAL;         // Character Map A Select (LS bits) (3-2)
		uint8_t MBL;         // Character Map B Select (LS bits) (1-0)
		operator uint8_t() const {
			return (MAH << 5) | (MBH << 4) | (MAL << 2) | (MBL << 0);
		}
		void operator=(uint8_t _v) {
			MAH = (_v & SEQ_MAH)>>5; MBH = (_v & SEQ_MBH)>>4;
			MAL = (_v & SEQ_MAL)>>2; MBL = (_v & SEQ_MBL)>>0;
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{2,"MBL"},{2,"MAL"},{1,"MBH"},{1,"MAH"}});
		}
	} char_map;            // Index 03h -- Character Map Select register

	struct {
		bool CH4;            // Chain 4 (3)
		bool OE;             // Odd/Even (2)
		bool EM;             // Extended Memory (1)
		operator uint8_t() const {
			return (CH4 << 3) | (OE << 2) | (EM << 1);
		}
		void operator=(uint8_t _v) {
			CH4 = (_v & SEQ_CH4); OE = (_v & SEQ_OE); EM = (_v & SEQ_EM);
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{1,""},{1,"EM"},{1,"OE"},{1,"CH4"}});
		}
	} mem_mode;            // Index 04h -- Memory Mode Register

	inline struct VGA_Sequencer& operator[](uint8_t _address) {
		address = _address;
		return *this;
	}
	operator uint8_t() const   { return get_register(address); }
	void operator=(uint8_t _v) { set_register(address, _v);    }

	void    set_register(uint8_t _index, uint8_t _value);
	uint8_t get_register(uint8_t _index) const;

	void set_registers(const std::array<uint8_t,SEQ_REGCOUNT> _regs);
	std::array<uint8_t,SEQ_REGCOUNT> get_registers();

	// DEBUGGING
	operator const char*() const { return register_to_string(address); }
	const char * register_to_string(uint8_t _index) const;
	void registers_to_textfile(FILE *_txtfile);

	static constexpr const std::array<const char*, SEQ_REGCOUNT> regnames = {{
		"Reset",
		"Clocking Mode",
		"Map Mask",
		"Character Map Select",
		"Memory Mode"
	}};
	static constexpr const std::array<std::array<uint8_t,SEQ_REGCOUNT>,0x14> modes = {{
		{0x03,0x08,0x03,0x00,0x03}, // 0x00
		{0x03,0x08,0x03,0x00,0x03}, // 0x01
		{0x03,0x00,0x03,0x00,0x03}, // 0x02
		{0x03,0x00,0x03,0x00,0x03}, // 0x03
		{0x03,0x09,0x03,0x00,0x02}, // 0x04
		{0x03,0x09,0x03,0x00,0x02}, // 0x05
		{0x03,0x01,0x01,0x00,0x06}, // 0x06
		{}, // 0x07 TODO
		{},{},{},{},{}, // 0x08-0x0c undefined
		{0x03,0x09,0x0F,0x00,0x06}, // 0x0d
		{0x03,0x01,0x0F,0x00,0x06}, // 0x0e
		{}, // 0x0f TODO
		{0x03,0x01,0x0F,0x00,0x06}, // 0x10
		{}, // 0x11 TODO
		{0x03,0x01,0x0F,0x00,0x06}, // 0x12
		{0x03,0x01,0x0F,0x00,0x0E}  // 0x13
	}};

};
