/*
 * Copyright (C) 2018-2019  Marco Bortolin
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

// VGA Graphics Controller
// 10 registers, 29 fields

#include "utils.h"
#include <array>

enum VGA_GfxCtrlRegisters {
	GFXC_SET_RESET     = 0x00, // Index 00h -- Set/Reset
	GFXC_EN_SET_RESET  = 0x01, // Index 01h -- Enable Set/Reset
	GFXC_COL_COMPARE   = 0x02, // Index 02h -- Color Compare
	GFXC_DATA_ROTATE   = 0x03, // Index 03h -- Data Rotate
	GFXC_READ_MAP_SEL  = 0x04, // Index 04h -- Read Map Select
	GFXC_GFX_MODE      = 0x05, // Index 05h -- Graphics Mode
	GFXC_MISC          = 0x06, // Index 06h -- Miscellaneous
	GFXC_COL_DONT_CARE = 0x07, // Index 07h -- Color Don't Care
	GFXC_BIT_MASK      = 0x08, // Index 08h -- Bit Mask
	GFXC_REGCOUNT
};

enum VGA_GfxCtrlSetReset {
	GFXC_SR3 = 0x08, // Set/Reset Map 3 (3)
	GFXC_SR2 = 0x04, // Set/Reset Map 2 (2)
	GFXC_SR1 = 0x02, // Set/Reset Map 1 (1)
	GFXC_SR0 = 0x01  // Set/Reset Map 0 (0)
};
enum VGA_GfxCtrlEnableSetReset {
	GFXC_ESR3 = 0x08, // Enable Set/Reset Map 3 (3)
	GFXC_ESR2 = 0x04, // Enable Set/Reset Map 2 (2)
	GFXC_ESR1 = 0x02, // Enable Set/Reset Map 1 (1)
	GFXC_ESR0 = 0x01  // Enable Set/Reset Map 0 (0)
};
enum VGA_GfxCtrlColorCompare {
	GFXC_CC3 = 0x08, // Color Compare Map 3
	GFXC_CC2 = 0x04, // Color Compare Map 2
	GFXC_CC1 = 0x02, // Color Compare Map 1
	GFXC_CC0 = 0x01  // Color Compare Map 0
};
enum VGA_GfxCtrlDataRotate {
	GFXC_FS   = 0x18, // Function Select (4-3)
	GFXC_ROTC = 0x07  // Rotate Count (2-0)
};
enum VGA_GfxCtrlReadMapSelect {
	GFXC_MS = 0x03 // Map Select (1-0)
};
enum VGA_GfxCtrlGraphicsMode {
	GFXC_C256 = 0x40, // 256 - Color Mode (6)
	GFXC_SR   = 0x20, // Shift Register Mode (5)
	GFXC_OE   = 0x10, // Odd/Even (4)
	GFXC_RM   = 0x08, // Read Mode (3)
	GFXC_WM   = 0x03  // Write Mode (1-0)
};
enum VGA_GfxCtrlMiscellaneous {
	GFXC_MM  = 0x0c, // Memory Map (3-2)
	GFXC_COE = 0x02, // Chain Odd/Even (1)
	GFXC_GM  = 0x01  // Graphics Mode (0)
};
enum VGA_GfxCtrlColorDontCare {
	GFXC_M3X = 0x08, // Map 3 is Don't Care (3)
	GFXC_M2X = 0x04, // Map 2 is Don't Care (2)
	GFXC_M1X = 0x02, // Map 1 is Don't Care (1)
	GFXC_M0X = 0x01  // Map 0 is Don't Care (0)
};

enum VGA_GfxCtrlMM {
	MM_A0000_128K = 0x00,
	MM_A0000_64K  = 0x01,
	MM_B0000_32K  = 0x02,
	MM_B8000_32K  = 0x03
};

struct VGA_GfxCtrl
{
	uint8_t address;           // Address register

	struct {
		bool SR3;                // Set/Reset Map 3 (3)
		bool SR2;                // Set/Reset Map 2 (2)
		bool SR1;                // Set/Reset Map 1 (1)
		bool SR0;                // Set/Reset Map 0 (0)
		operator uint8_t() const {
			return (SR3 << 3) | (SR2 << 2) | (SR1 << 1) | (SR0 << 0);
		}
		void operator=(uint8_t _v) {
			SR3 = _v & GFXC_SR3; SR2 = _v & GFXC_SR2; SR1 = _v & GFXC_SR1;
			SR0 = _v & GFXC_SR0;
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{1,"SR0"},{1,"SR1"},{1,"SR2"},{1,"SR3"}});
		}
	} set_reset;               // Index 00h -- Set/Reset

	struct {
		bool ESR3;               // Enable Set/Reset Map 3 (3)
		bool ESR2;               // Enable Set/Reset Map 2 (2)
		bool ESR1;               // Enable Set/Reset Map 1 (1)
		bool ESR0;               // Enable Set/Reset Map 0 (0)
		operator uint8_t() const {
			return (ESR3 << 3) | (ESR2 << 2) | (ESR1 << 1) | (ESR0 << 0);
		}
		void operator=(uint8_t _v) {
			ESR3 = _v & GFXC_ESR3; ESR2 = _v & GFXC_ESR2; ESR1 = _v & GFXC_ESR1;
			ESR0 = _v & GFXC_ESR0;
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{1,"ESR0"},{1,"ESR1"},{1,"ESR2"},{1,"ESR3"}});
		}
	} enable_set_reset;        // Index 01h -- Enable Set/Reset

	struct {
		bool CC3;                // Color Compare Map 3
		bool CC2;                // Color Compare Map 2
		bool CC1;                // Color Compare Map 1
		bool CC0;                // Color Compare Map 0
		operator uint8_t() const {
			return (CC3 << 3) | (CC2 << 2) | (CC1 << 1) | (CC0 << 0);
		}
		void operator=(uint8_t _v) {
			CC3 = _v & GFXC_CC3; CC2 = _v & GFXC_CC2; CC1 = _v & GFXC_CC1;
			CC0 = _v & GFXC_CC0;
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{1,"CC0"},{1,"CC1"},{1,"CC"},{1,"CC3"}});
		}
	} color_compare;           // Index 02h -- Color Compare

	struct {
		uint8_t FS;              // Function Select (4-3)
		uint8_t ROTC;            // Rotate Count (2-0)
		operator uint8_t() const {
			return ((FS << 3) & GFXC_FS) | (ROTC & GFXC_ROTC);
		}
		void operator=(uint8_t _v) {
			FS = (_v & GFXC_FS) >> 3;
			ROTC = (_v & GFXC_ROTC) >> 0;
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{3,"ROTC"},{2,"FS"}});
		}
	} data_rotate;             // Index 03h -- Data Rotate

	struct {
		uint8_t MS;              // Map Select (1-0)
		operator uint8_t() const {
			return (MS & GFXC_MS);
		}
		void operator=(uint8_t _v) {
			MS = (_v & GFXC_MS) >> 0;
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{2,"MS"}});
		}
	} read_map_select;         // Index 04h -- Read Map Select

	struct {
		bool    C256;            // 256 - Color Mode (6)
		bool    SR;              // Shift Register Mode (5)
		bool    OE;              // Odd/Even (4)
		bool    RM;              // Read Mode (3)
		uint8_t WM;              // Write Mode (1-0)
		operator uint8_t() const {
			return (C256 << 6) | (SR << 5) | (OE << 4) | (RM << 3) | (WM & GFXC_WM);
		}
		void operator=(uint8_t _v) {
			C256 = (_v & GFXC_C256); SR = (_v & GFXC_SR); OE = (_v & GFXC_OE);
			RM = (_v & GFXC_RM); WM = (_v & GFXC_WM) >> 0;
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{2,"WM"},{1,""},{1,"RM"},{1,"OE"},{1,"SR"},{1,"C256"}});
		}
	} gfx_mode;                // Index 05h -- Graphics Mode

	struct {
		uint8_t MM;              // Memory Map (3-2)
		bool    COE;             // Chain Odd/Even (1)
		bool    GM;              // Graphics Mode (0)
		operator uint8_t() const {
			return ((MM << 2) & GFXC_MM) | (COE << 1) | GM;
		}
		void operator=(uint8_t _v) {
			MM = (_v & GFXC_MM) >> 2; COE = (_v & GFXC_COE); GM = (_v & GFXC_GM);
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{1,"GM"},{1,"COE"},{2,"MM"}});
		}
	} misc;                    // Index 06h -- Miscellaneous

	struct {
		bool M3X;                // Map 3 is Don't Care (3)
		bool M2X;                // Map 2 is Don't Care (2)
		bool M1X;                // Map 1 is Don't Care (1)
		bool M0X;                // Map 0 is Don't Care (0)
		operator uint8_t() const {
			return (M3X << 3) | (M2X << 2) | (M1X << 1) | (M0X << 0);
		}
		void operator=(uint8_t _v) {
			M3X = _v & GFXC_M3X; M2X = _v & GFXC_M2X; M1X = _v & GFXC_M1X;
			M0X = _v & GFXC_M0X;
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{1,"M0X"},{1,"M1X"},{1,"M2X"},{1,"M3X"}});
		}
	} color_dont_care;         // Index 07h -- Color Don't Care

	uint8_t bitmask;           // Index 08h -- Bit Mask

	uint32_t memory_offset;   // current phy start address of video memory
	uint32_t memory_aperture; // current video memory accessible size
	uint8_t latch[4];         // data latches


	inline struct VGA_GfxCtrl& operator[](uint8_t _address) {
		address = _address;
		return *this;
	}
	operator uint8_t() const   { return get_register(address); }
	void operator=(uint8_t _v) { set_register(address, _v);    }

	void    set_register(uint8_t _index, uint8_t _value);
	uint8_t get_register(uint8_t _index) const;

	void set_registers(const std::array<uint8_t,GFXC_REGCOUNT> _regs);
	std::array<uint8_t,GFXC_REGCOUNT> get_registers();

	// DEBUGGING
	operator const char*() const { return register_to_string(address); }
	const char * register_to_string(uint8_t _index) const;
	void registers_to_textfile(FILE *_txtfile);

	static constexpr const std::array<const char*, GFXC_REGCOUNT> regnames = {{
		"Set/Reset",
		"Enable Set/Reset",
		"Color Compare",
		"Data Rotate",
		"Read Map Select",
		"Graphics Mode",
		"Miscellaneous",
		"Color Don't Care",
		"Bit Mask"
	}};
	static constexpr const std::array<std::array<uint8_t,GFXC_REGCOUNT>,0x14> modes = {{
		{0x00,0x00,0x00,0x00,0x00,0x10,0x0E,0x00,0xFF}, // 0x00
		{0x00,0x00,0x00,0x00,0x00,0x10,0x0E,0x00,0xFF}, // 0x01
		{0x00,0x00,0x00,0x00,0x00,0x10,0x0E,0x00,0xFF}, // 0x02
		{0x00,0x00,0x00,0x00,0x00,0x10,0x0E,0x00,0xFF}, // 0x03
		{0x00,0x00,0x00,0x00,0x00,0x30,0x0F,0x00,0xFF}, // 0x04
		{0x00,0x00,0x00,0x00,0x00,0x30,0x0F,0x00,0xFF}, // 0x05
		{0x00,0x00,0x00,0x00,0x00,0x00,0x0D,0x00,0xFF}, // 0x06
		{}, // 0x07 TODO
		{},{},{},{},{}, // 0x08-0x0c undefined
		{0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x0F,0xFF}, // 0x0d
		{0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x0F,0xFF}, // 0x0e
		{}, // 0x0f TODO
		{0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x0F,0xFF}, // 0x10
		{}, // 0x11 TODO
		{0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x0F,0xFF}, // 0x12
		{0x00,0x00,0x00,0x00,0x00,0x40,0x05,0x0F,0xFF}, // 0x13
	}};

};
