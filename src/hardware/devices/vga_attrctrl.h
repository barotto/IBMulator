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

// VGA Attribute Controller
// 22 registers, 33 fields

#include "utils.h"
#include <array>

enum VGA_AttrCtrlRegisters {
	ATTC_ATTMODE  = 0x10, // Index 10h -- Attribute Mode Control Register
	ATTC_OVERSCAN = 0x11, // Index 11h -- Overscan Color Register
	ATTC_COLPLANE = 0x12, // Index 12h -- Color Plane Enable Register
	ATTC_HPELPAN  = 0x13, // Index 13h -- Horizontal PEL Panning Register
	ATTC_COLSEL   = 0x14, // Index 14h -- Color Select Register
	ATTC_REGCOUNT
};

enum VGA_AttrCtrlAddress {
	ATTC_IPAS  = 0x20, // Internal Palette Address Source (5)
	ATTC_INDEX = 0x1f  // Index to data registers (4-0)
};

enum VGA_AttrCtrlAttributeMode {
	ATTC_PS  = 0x80,   // P5, P4 Select [palette size] (7)
	ATTC_PW  = 0x40,   // PEL Width (6)
	ATTC_PP  = 0x20,   // PEL Panning Compatibility (5)
	ATTC_EB  = 0x08,   // Enable Blink / Select Background Intensity (3)
	ATTC_ELG = 0x04,   // Enable Line Graphics Character Code (2)
	ATTC_ME  = 0x02,   // Mono Emulation (1)
	ATTC_GFX = 0x01    // Graphics/Alphanumeric Mode (0)
};

enum VGA_AttrCtrlColorPlaneEnable {
	ATTC_VSMUX = 0x30, // Video Status MUX (5-4)
	ATTC_ECP   = 0x0f  // Enable Color Plane (3-0)
};

enum VGA_AttrCtrlHPELPanning {
	ATTC_HPP   = 0x0f  // Horizontal PEL Panning (3-0)
};

enum VGA_AttrCtrlColorSelect {
	ATTC_SC7 = 0x08,   // S_color 7 (3)
	ATTC_SC6 = 0x04,   // S_color 6 (2)
	ATTC_SC5 = 0x02,   // S_color 5 (1)
	ATTC_SC4 = 0x01    // S_color 4 (0)
};


struct VGA_AttrCtrl
{
	struct {
		bool    IPAS;  // Internal Palette Address Source (5)
		uint8_t index; // Index to data registers (4-0)
		operator uint8_t() const {
			return (IPAS << 5) | (index & ATTC_INDEX);
		}
		void operator=(uint8_t _v) {
			IPAS = (_v & ATTC_IPAS); index = (_v & ATTC_INDEX);
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{5,"index"}, {1,"IPAS"}});
		}
	} address;                  // Address Register

	uint8_t palette[16];        // Index 00-0Fh -- Internal Palette Registers

	struct {
		bool PS;                   // P5, P4 Select [palette size] (7)
		bool PW;                   // PEL Width (6)
		bool PP;                   // PEL Panning Compatibility (5)
		bool EB;                   // Enable Blink / Select Background Intensity (3)
		bool ELG;                  // Enable Line Graphics Character Code (2)
		bool ME;                   // Mono Emulation (1)
		bool GFX;                  // Graphics/Alphanumeric Mode (0)
		operator uint8_t() const {
			return (PS << 7) | (PW  << 6) | (PP << 5) |
			       (EB << 3) | (ELG << 2) | (ME << 1) | (GFX << 0);
		}
		void operator=(uint8_t _v) {
			PS  = _v & ATTC_PS; PW  = _v & ATTC_PW; PP  = _v & ATTC_PP;
			EB  = _v & ATTC_EB; ELG = _v & ATTC_ELG; ME  = _v & ATTC_ME;
			GFX = _v & ATTC_GFX;
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{1,"GFX"},{1,"ME"},{1,"ELG"},{1,"EB"},{1,""},{1,"PP"},{1,"PW"},{1,"PS"}});
		}
	} attr_mode;                // Index 10h -- Attribute Mode Control Register

	uint8_t overscan_color;     // Index 11h -- Overscan Color Register

	struct {
		uint8_t VSMUX;             // Video Status MUX (5-4)
		                           //  Diagnostics use only.
		                           //  Two attribute bits appear on bits 4 and 5 of the Input Status
		                           //  Register 1 (3dAh). 0: Bit 2/0, 1: Bit 5/4, 2: bit 3/1, 3: bit 7/6
		uint8_t ECP;               // Enable Color Plane (3-0)
		operator uint8_t() const {
			return ((VSMUX << 4) & ATTC_VSMUX) | (ECP & ATTC_ECP);
		}
		void operator=(uint8_t _v) {
			VSMUX = ((_v & ATTC_VSMUX) >> 4); ECP = (_v & ATTC_ECP);
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{4,"ECP"},{2,"VSMUX"}});
		}
	} color_plane_enable;       // Index 12h -- Color Plane Enable Register

	struct {
		uint8_t HPP;               // Horizontal PEL Panning (3-0)
		operator uint8_t() const {
			return (HPP & ATTC_HPP);
		}
		void operator=(uint8_t _v) {
			HPP = (_v & ATTC_HPP);
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this), {{4,"HPP"}});
		}
	} horiz_pel_panning;        // Index 13h -- Horizontal PEL Panning Register

	struct {
		bool SC7;                  // S_color 7 (3)
		bool SC6;                  // S_color 6 (2)
		bool SC5;                  // S_color 5 (1)
		bool SC4;                  // S_color 4 (0)
		operator uint8_t() const {
			return (SC7 << 3) | (SC6 << 2) | (SC5 << 1) | (SC4 << 0);
		}
		void operator=(uint8_t _v) {
			SC7 = (_v & ATTC_SC7); SC6 = (_v & ATTC_SC6);
			SC5 = (_v & ATTC_SC5); SC4 = (_v & ATTC_SC4);
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{1,"SC4"},{1,"SC5"},{1,"SC6"},{1,"SC7"}});
		}
	} color_select;              // Index 14h -- Color Select Register

	bool flip_flop;              // 0=address mode, 1=data-write mode


	inline struct VGA_AttrCtrl& operator[](uint8_t _index) {
		address.index = _index;
		return *this;
	}
	operator uint8_t() const   { return get_register(address.index); }
	void operator=(uint8_t _v) { set_register(address.index, _v);    }

	void    set_register(uint8_t _index, uint8_t _value);
	uint8_t get_register(uint8_t _index) const;

	void set_registers(const std::array<uint8_t,ATTC_REGCOUNT> _regs);
	std::array<uint8_t,ATTC_REGCOUNT> get_registers();

	// DEBUGGING
	operator const char*() const { return register_to_string(address.index); }
	const char * register_to_string(uint8_t _index) const;
	void registers_to_textfile(FILE *_txtfile);

	static constexpr const std::array<const char*, ATTC_REGCOUNT> regnames = {{
		"Palette entry 00",
		"Palette entry 01",
		"Palette entry 02",
		"Palette entry 03",
		"Palette entry 04",
		"Palette entry 05",
		"Palette entry 06",
		"Palette entry 07",
		"Palette entry 08",
		"Palette entry 09",
		"Palette entry 0a",
		"Palette entry 0b",
		"Palette entry 0c",
		"Palette entry 0d",
		"Palette entry 0e",
		"Palette entry 0f",
		"Attribute Mode Control",
		"Overscan Color",
		"Color Plane Enable",
		"Horizontal Pixel Panning",
		"Color Select"
	}};
	static constexpr const std::array<std::array<uint8_t,ATTC_REGCOUNT>,0x14> modes = {{
		{0x00,0x01,0x02,0x03,0x04,0x05,0x14,0x07,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,0x0C,0x00,0x0F,0x08,0x00}, // 0x00
		{0x00,0x01,0x02,0x03,0x04,0x05,0x14,0x07,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,0x0C,0x00,0x0F,0x08,0x00}, // 0x01
		{0x00,0x01,0x02,0x03,0x04,0x05,0x14,0x07,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,0x0C,0x00,0x0F,0x08,0x00}, // 0x02
		{0x00,0x01,0x02,0x03,0x04,0x05,0x14,0x07,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,0x0C,0x00,0x0F,0x08,0x00}, // 0x03
		{0x00,0x13,0x15,0x17,0x02,0x04,0x06,0x07,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x01,0x00,0x03,0x00,0x00}, // 0x04
		{0x00,0x13,0x15,0x17,0x02,0x04,0x06,0x07,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x01,0x00,0x03,0x00,0x00}, // 0x05
		{0x00,0x17,0x17,0x17,0x17,0x17,0x17,0x17,0x17,0x17,0x17,0x17,0x17,0x17,0x17,0x17,0x01,0x00,0x01,0x00,0x00}, // 0x06
		{}, // 0x07 TODO
		{},{},{},{},{}, // 0x08-0x0c undefined
		{0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x01,0x00,0x0F,0x00,0x00}, // 0x0d
		{0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x01,0x00,0x0F,0x00,0x00}, // 0x0e
		{}, // 0x0f TODO
		{0x00,0x01,0x02,0x03,0x04,0x05,0x14,0x07,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,0x01,0x00,0x0F,0x00,0x00}, // 0x10
		{}, // 0x11 TODO
		{0x00,0x01,0x02,0x03,0x04,0x05,0x14,0x07,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,0x01,0x00,0x0F,0x00,0x00}, // 0x12
		{0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x41,0x00,0x0F,0x00,0x00}  // 0x13
	}};
};
