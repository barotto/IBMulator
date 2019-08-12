/*
 * Copyright (C) 2017-2019  Marco Bortolin
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

// VGA CRT Controller
// 26 registers, 51 fields

#include "utils.h"
#include <array>

enum VGA_CRTCRegisters {
	CRTC_HTOTAL          =  0x00,  // Index 00h (00) -- Horizontal Total Register
	CRTC_HDISPLAY_END    =  0x01,  // Index 01h (01) -- Horizontal Display End Register
	CRTC_START_HBLANK    =  0x02,  // Index 02h (02) -- Start Horizontal Blanking Register
	CRTC_END_HBLANK      =  0x03,  // Index 03h (03) -- End Horizontal Blanking Register
	CRTC_START_HRETRACE  =  0x04,  // Index 04h (04) -- Start Horizontal Retrace Register
	CRTC_END_HRETRACE    =  0x05,  // Index 05h (05) -- End Horizontal Retrace Register
	CRTC_VTOTAL          =  0x06,  // Index 06h (06) -- Vertical Total Register
	CRTC_OVERFLOW        =  0x07,  // Index 07h (07) -- Overflow Register
	CRTC_PRESET_ROW_SCAN =  0x08,  // Index 08h (08) -- Preset Row Scan Register
	CRTC_MAX_SCANLINE    =  0x09,  // Index 09h (09) -- Maximum Scan Line Register
	CRTC_CURSOR_START    =  0x0A,  // Index 0Ah (10) -- Cursor Start Register
	CRTC_CURSOR_END      =  0x0B,  // Index 0Bh (11) -- Cursor End Register
	CRTC_STARTADDR_HI    =  0x0C,  // Index 0Ch (12) -- Start Address High Register
	CRTC_STARTADDR_LO    =  0x0D,  // Index 0Dh (13) -- Start Address Low Register
	CRTC_CURSOR_HI       =  0x0E,  // Index 0Eh (14) -- Cursor Location High Register
	CRTC_CURSOR_LO       =  0x0F,  // Index 0Fh (15) -- Cursor Location Low Register
	CRTC_VRETRACE_START  =  0x10,  // Index 10h (16) -- Vertical Retrace Start Register
	CRTC_VRETRACE_END    =  0x11,  // Index 11h (17) -- Vertical Retrace End Register
	CRTC_VDISPLAY_END    =  0x12,  // Index 12h (18) -- Vertical Display End Register
	CRTC_OFFSET          =  0x13,  // Index 13h (19) -- Offset Register
	CRTC_UNDERLINE       =  0x14,  // Index 14h (20) -- Underline Location Register
	CRTC_START_VBLANK    =  0x15,  // Index 15h (21) -- Start Vertical Blanking Register
	CRTC_END_VBLANK      =  0x16,  // Index 16h (22) -- End Vertical Blanking
	CRTC_MODE_CONTROL    =  0x17,  // Index 17h (23) -- CRT Mode Control Register
	CRTC_LINE_COMPARE    =  0x18,  // Index 18h (24) -- Line Compare Register
	CRTC_REGCOUNT
};

enum VGA_CRTCEndHorizontalBlanking { // (Index 03h)
	CRTC_EVRA = 0x80,  // Enable Vertical Retrace Access (7)
	CRTC_DES  = 0x60,  // Display Enable Skew (6-5)
	CRTC_EB   = 0x1F   // End Horizontal Blanking (4-0), bits 4-0 of 6
};

enum VGA_CRTCEndHorizontalRetrace { // (Index 05h)
	CRTC_EB5  = 0x80,  // End Horizontal Blanking, bit 5 (7)
	CRTC_HRD  = 0x60,  // Horizontal Retrace Delay (6-5)
	CRTC_EHR  = 0x1F   // End Horizontal Retrace (4-0)
};

enum VGA_CRTCOverflow { // (Index 07h)
	CRTC_VRS9 = 0x80,  // Vertical Retrace Start, bit 9 (7)
	CRTC_VDE9 = 0x40,  // Vertical Display End, bit 9 (6)
	CRTC_VT9  = 0x20,  // Vertical Total, bit 9 (5)
	CRTC_LC8  = 0x10,  // Line Compare, bit 8 (4)
	CRTC_VBS8 = 0x08,  // Vertical Blanking Start, bit 8 (3)
	CRTC_VRS8 = 0x04,  // Vertical Retrace Start, bit 8 (2)
	CRTC_VDE8 = 0x02,  // Vertical Display End, bit 8 (1)
	CRTC_VT8  = 0x01   // Vertical Total, bit 8 (0)
};

enum VGA_CRTCPresetRowScan { // (Index 08h)
	CRTC_BP   = 0x60,  // Byte Panning (6-5)
	CRTC_SRS  = 0x1F   // Starting Row Scan Count (4-0)
};

enum VGA_CRTCMaximumScanLine { // (Index 09h)
	CRTC_DSC  = 0x80,  // 200 to 400 Line Conversion (Double Scanning) (7)
	CRTC_LC9  = 0x40,  // Line Compare, Bit 9 (6)
	CRTC_VBS9 = 0x20,  // Vertical Blanking Start, Bit 9 (5)
	CRTC_MSL  = 0x1F   // Maximum Scan Line (4-0)
};

enum VGA_CRTCCursorStart { // (Index 0Ah)
	CRTC_CO   = 0x20,  // Cursor Off (5)
	CRTC_RSCB = 0x1F   // Row Scan Cursor Begins (4-0)
};

enum VGA_CRTCCursorEnd { // (Index 0Bh)
	CRTC_CSK  = 0x60,  // Cursor Skew Control (6-5)
	CRTC_RSCE = 0x1F   // Row Scan Cursor Ends (4-0)
};

enum VGA_CRTCVerticalRetraceEnd { // (Index 11h)
	CRTC_PR   = 0x80,  // Protect Registers 0-7 (7)
	CRTC_S5R  = 0x40,  // Select 5 Refresh Cycles (6)
	CRTC_EVI  = 0x20,  // Enable Vertical Interrupt (5)
	CRTC_CVI  = 0x10,  // Clear Vertical Interrupt (4)
	CRTC_VRE  = 0x0F   // Vertical Retrace End (3-0)
};

enum VGA_CRTCUnderlineLocation { // (Index 14h)
	CRTC_DW   = 0x40,  // Doubleword Mode (6)
	CRTC_CB4  = 0x20,  // Count By 4 (5)
	CRTC_SUL  = 0x1F   // Start Underline (4-0)
};

enum VGA_CRTCModeControl { // (Index 17h)
	CRTC_RST  = 0x80,  // Hardware Reset (7)
	CRTC_WB   = 0x40,  // Word/Byte Mode Select (6)
	CRTC_ADW  = 0x20,  // Address Wrap Select (5)
	CRTC_CB2  = 0x08,  // Count By Two (3)
	CRTC_HRS  = 0x04,  // Horizontal Retrace Select (2)
	CRTC_SRC  = 0x02,  // Select Row Scan Counter (Map Display Address 14) (1)
	CRTC_CMS  = 0x01   // Compatibility Mode Support (Map Display Address 13) (0)
};


struct VGA_CRTC
{
	uint8_t address;        // Register index

	uint8_t htotal;         // Index 00h (00) -- Horizontal Total
	uint8_t hdisplay_end;   // Index 01h (01) -- Horizontal Display End
	uint8_t start_hblank;   // Index 02h (02) -- Start Horizontal Blanking

	struct {
		bool    EVRA;         // Enable Vertical Retrace Access (7)
		uint8_t DES;          // Display Enable Skew (6-5)
		uint8_t EB;           // End Horizontal Blanking (4-0), bits 4-0 of 6
		operator uint8_t() const {
			return (EVRA << 7) | (DES << 5 & CRTC_DES) | (EB & CRTC_EB);
		}
		void operator=(uint8_t _v) {
			EVRA = (_v & CRTC_EVRA); DES = (_v & CRTC_DES) >> 5; EB = (_v & CRTC_EB);
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{5,"EB"},{2,"DES"},{1,"EVRA"}});
		}
	} end_hblank;           // Index 03h (03) -- End Horizontal Blanking

	uint8_t start_hretrace; // Index 04h (04) -- Start Horizontal Retrace

	struct {
		bool    EB5;          // End Horizontal Blanking, bit 5 (7)
		uint8_t HRD;          // Horizontal Retrace Delay (6-5)
		uint8_t EHR;          // End Horizontal Retrace (4-0)
		operator uint8_t() const {
			return (EB5 << 7) | (HRD << 5 & CRTC_HRD) | (EHR & CRTC_EHR);
		}
		void operator=(uint8_t _v) {
			EB5 = (_v & CRTC_EB5); HRD = (_v & CRTC_HRD) >> 5; EHR = (_v & CRTC_EHR);
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{5,"EHR"},{2,"HRD"},{1,"EB5"}});
		}
	} end_hretrace;         // Index 05h (05) -- End Horizontal Retrace

	uint8_t vtotal;         // Index 06h (06) -- Vertical Total, bits 7-0 of 10

	struct {
		bool VRS9;            // Vertical Retrace Start, bit 9 (7)
		bool VDE9;            // Vertical Display End, bit 9 (6)
		bool VT9;             // Vertical Total, bit 9 (5)
		bool LC8;             // Line Compare, bit 8 (4)
		bool VBS8;            // Vertical Blanking Start, bit 8 (3)
		bool VRS8;            // Vertical Retrace Start, bit 8 (2)
		bool VDE8;            // Vertical Display End, bit 8 (1)
		bool VT8;             // Vertical Total, bit 8 (0)
		operator uint8_t() const {
			return (VRS9 << 7) | (VDE9 << 6) | (VT9  << 5) | (LC8  << 4) |
			       (VBS8 << 3) | (VRS8 << 2) | (VDE8 << 1) | (VT8  << 0);
		}
		void operator=(uint8_t _v) {
			VRS9 = (_v & CRTC_VRS9); VDE9 = (_v & CRTC_VDE9); VT9  = (_v & CRTC_VT9); LC8  = (_v & CRTC_LC8);
			VBS8 = (_v & CRTC_VBS8); VRS8 = (_v & CRTC_VRS8); VDE8 = (_v & CRTC_VDE8); VT8  = (_v & CRTC_VT8);
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{1,"VT8"},{1,"VDE8"},{1,"VRS8"},{1,"VBS8"},{1,"LC8"},{1,"VT9"},{1,"VDE9"},{1,"VRS9"}});
		}
	} overflow;             // Index 07h (07) -- Overflow Register

	struct {
		uint8_t BP;           // Byte Panning (6-5)
		uint8_t SRS;          // Starting Row Scan Count (4-0)
		operator uint8_t() const {
			return (BP << 6 & CRTC_BP) | (SRS & CRTC_SRS);
		}
		void operator=(uint8_t _v) {
			BP  = (_v & CRTC_BP) >> 5; SRS = (_v & CRTC_SRS);
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{5,"SRS"},{2,"BP"}});
		}
	} preset_row_scan;      // Index 08h (08) -- Preset Row Scan

	struct {
		bool    DSC;          // 200 to 400 Line Conversion (Double Scanning) (7)
		bool    LC9;          // Line Compare, Bit 9 (6)
		bool    VBS9;         // Vertical Blanking Start, Bit 9 (5)
		uint8_t MSL;          // Maximum Scan Line (4-0)
		operator uint8_t() const {
			return (DSC << 7) | (LC9 << 6) | (VBS9 << 5) | (MSL & CRTC_MSL);
		}
		void operator=(uint8_t _v) {
			DSC = (_v & CRTC_DSC); LC9 = (_v & CRTC_LC9); VBS9 = (_v & CRTC_VBS9);
			MSL = (_v & CRTC_MSL);
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{5,"MSL"},{1,"VBS9"},{1,"LC9"},{1,"DSC"}});
		}
	} max_scanline;         // Index 09h (09) -- Maximum Scan Line

	struct {
		bool    CO;           // Cursor Off (5)
		uint8_t RSCB;         // Row Scan Cursor Begins (4-0)
		operator uint8_t() const {
			return (CO << 5) | (RSCB & CRTC_RSCB);
		}
		void operator=(uint8_t _v) {
			CO = (_v & CRTC_CO); RSCB = (_v & CRTC_RSCB);
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{5,"RSCB"},{1,"CO"}});
		}
	} cursor_start;         // Index 0Ah (10) -- Cursor Start

	struct {
		uint8_t CSK;          // Cursor Skew Control (6-5)
		uint8_t RSCE;         // Row Scan Cursor Ends (4-0)
		operator uint8_t() const {
			return (CSK << 5 & CRTC_CSK) | (RSCE & CRTC_RSCE);
		}
		void operator=(uint8_t _v) {
			CSK = (_v & CRTC_CSK) >> 5; RSCE = (_v & CRTC_RSCE);
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{5,"RSCE"},{2,"CSK"}});
		}
	} cursor_end;           // Index 0Bh (11) -- Cursor End

	uint8_t startaddr_hi;   // Index 0Ch (12) -- Start Address High, bits 15-8 of 16
	uint8_t startaddr_lo;   // Index 0Dh (13) -- Start Address Low, bits 7-0 of 16
	uint8_t cursor_hi;      // Index 0Eh (14) -- Cursor Location High, bits 15-8 of 16
	uint8_t cursor_lo;      // Index 0Fh (15) -- Cursor Location Low, bits 7-0 of 16
	uint8_t vretrace_start; // Index 10h (16) -- Vertical Retrace Start

	struct {
		bool    PR;           // Protect Registers 0-7 (7)
		bool    S5R;          // Select 5 Refresh Cycles (6)
		bool    EVI;          // Enable Vertical Interrupt (5)
		bool    CVI;          // Clear Vertical Interrupt (4)
		uint8_t VRE;          // Vertical Retrace End (3-0)
		operator uint8_t() const {
			return (PR  << 7) | (S5R << 6) | (EVI << 5) |
			       (CVI << 4) | (VRE & CRTC_VRE);
		}
		void operator=(uint8_t _v) {
			PR  = (_v & CRTC_PR);  S5R = (_v & CRTC_S5R); EVI = (_v & CRTC_EVI);
			CVI = (_v & CRTC_CVI); VRE = (_v & CRTC_VRE);
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{4,"VRE"},{1,"CVI"},{1,"EVI"},{1,"S5R"},{1,"PR"}});
		}
	} vretrace_end;         // Index 11h (17) -- Vertical Retrace End

	uint8_t vdisplay_end;   // Index 12h (18) -- Vertical Display End, bits 7-0 of 10
	uint8_t offset;         // Index 13h (19) -- Offset

	struct {
		bool    DW;           // Doubleword Mode (6)
		bool    CB4;          // Count By 4 (5)
		uint8_t SUL;          // Start Underline (4-0)
		operator uint8_t() const {
			return (DW << 6) | (CB4 << 5) | (SUL & CRTC_SUL);
		}
		void operator=(uint8_t _v) {
			DW = (_v & CRTC_DW); CB4 = (_v & CRTC_CB4); SUL = (_v & CRTC_SUL);
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{5,"SUL"},{1,"CB4"},{1,"DW"}});
		}
	} underline;            // Index 14h (20) -- Underline Location

	uint8_t start_vblank;   // Index 15h (21) -- Start Vertical Blanking, bits 7-0 of 10
	uint8_t end_vblank;     // Index 16h (22) -- End Vertical Blanking

	struct {
		bool RST;             // Hardware Reset (7)
		bool WB;              // Word/Byte Mode (6)
		bool ADW;             // Address Wrap (5)
		bool CB2;             // Count By Two (3)
		bool HRS;             // Horizontal Retrace Select (2)
		bool SRC;             // Select Row Scan Counter (1)
		bool CMS;             // Compatibility Mode Support (0)
		operator uint8_t() const {
			return (RST << 7) | (WB << 6) | (ADW << 5) | (CB2 << 3) |
			       (HRS << 2) | (SRC << 1) | (CMS << 0);
		}
		void operator=(uint8_t _v) {
			RST = (_v & CRTC_RST); WB  = (_v & CRTC_WB); ADW = (_v & CRTC_ADW); CB2  = (_v & CRTC_CB2);
			HRS = (_v & CRTC_HRS); SRC = (_v & CRTC_SRC); CMS = (_v & CRTC_CMS);
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{1,"CMS"},{1,"SRC"},{1,"HRS"},{1,"CB2"},{1,""},{1,"ADW"},{1,"WB"},{1,"RST"}});
		}
	} mode_control;         // Index 17h (23) -- CRT Mode Control

	uint8_t line_compare;   // Index 18h (24) -- Line Compare, bits 7-0 of 10

	struct {
		uint16_t line_offset;      // Screen's logical line width (10-bit)
		uint16_t line_compare;     // Line compare target (10-bit)
		uint16_t vretrace_start;   // Vertical Retrace Start (10-bit)
		uint16_t vdisplay_end;     // Vertical-display-enable end position (10-bit)
		uint16_t vtotal;           // Vertical Total (10-bit)
		uint16_t end_hblank;       // End Horizontal Blanking (6-bit)
		uint16_t start_vblank;     // Start Vertical Blanking (10-bit)
		uint16_t start_address;    // Starting address for the regenerative buffer (16-bit)
		uint16_t cursor_location;  // Cursor Location (16-bit)
	} latches;
	bool interrupt;                // 1=vretrace interrupt has been raised

	void latch_line_offset();
	void latch_line_compare();
	void latch_vretrace_start();
	void latch_vdisplay_end();
	void latch_vtotal();
	void latch_end_hblank();
	void latch_start_vblank();
	void latch_start_address();
	void latch_cursor_location();

	inline  bool is_write_protected() const {
		return vretrace_end.PR;
	}
	ALWAYS_INLINE unsigned scanlines_div() const {
		return ((max_scanline.MSL + 1) << max_scanline.DSC);
	}

	inline struct VGA_CRTC& operator[](uint8_t _address) {
		address = _address;
		return *this;
	}
	operator uint8_t() const   { return get_register(address); }
	void operator=(uint8_t _v) { set_register(address, _v);    }

	void    set_register(uint8_t _index, uint8_t _value);
	uint8_t get_register(uint8_t _index) const;

	void set_registers(const std::array<uint8_t,CRTC_REGCOUNT>);
	std::array<uint8_t,CRTC_REGCOUNT> get_registers();

	uint16_t mux_mem_address(uint16_t _row_addr_cnt, uint16_t _row_scan_cnt);
	
	// DEBUGGING
	operator const char*() const { return register_to_string(address); }
	const char * register_to_string(uint8_t _index) const;
	void registers_to_textfile(FILE *_txtfile);


	static constexpr const std::array<const char*, CRTC_REGCOUNT> regnames = {{
		"Horizontal Total",
		"End Horizontal Display",
		"Start Horizontal Blanking",
		"End Horizontal Blanking",
		"Start Horizontal Retrace",
		"End Horizontal Retrace",
		"Vertical Total",
		"Overflow",
		"Preset Row Scan",
		"Maximum Scan Line",
		"Cursor Start",
		"Cursor End",
		"Start Address High",
		"Start Address Low",
		"Cursor Location High",
		"Cursor Location Low",
		"Vertical Retrace Start",
		"Vertical Retrace End",
		"Vertical Display End",
		"Offset",
		"Underline Location",
		"Start Vertical Blanking",
		"End Vertical Blanking",
		"Mode Control",
		"Line Compare"
	}};
	static constexpr const std::array<std::array<uint8_t,CRTC_REGCOUNT>,0x14> modes = {{
		{0x2D,0x27,0x28,0x90,0x2B,0xA0,0xBF,0x1F,0x00,0x4F,0x0D,0x0E,0x00,0x00,0x00,0x00,0x9C,0x8E,0x8F,0x14,0x1F,0x96,0xB9,0xA3,0xFF}, // 0x00
		{0x2D,0x27,0x28,0x90,0x2B,0xA0,0xBF,0x1F,0x00,0x4F,0x0D,0x0E,0x00,0x00,0x00,0x00,0x9C,0x8E,0x8F,0x14,0x1F,0x96,0xB9,0xA3,0xFF}, // 0x01
		{0x5F,0x4F,0x50,0x82,0x55,0x81,0xBF,0x1F,0x00,0x4F,0x0D,0x0E,0x00,0x00,0x00,0x00,0x9C,0x8E,0x8F,0x28,0x1F,0x96,0xB9,0xA3,0xFF}, // 0x02
		{0x5F,0x4F,0x50,0x82,0x55,0x81,0xBF,0x1F,0x00,0x4F,0x0D,0x0E,0x00,0x00,0x00,0x00,0x9C,0x8E,0x8F,0x28,0x1F,0x96,0xB9,0xA3,0xFF}, // 0x03
		{0x2D,0x27,0x28,0x90,0x2B,0x80,0xBF,0x1F,0x00,0xC1,0x00,0x00,0x00,0x00,0x00,0x00,0x9C,0x8E,0x8F,0x14,0x00,0x96,0xB9,0xA2,0xFF}, // 0x04
		{0x2D,0x27,0x28,0x90,0x2B,0x80,0xBF,0x1F,0x00,0xC1,0x00,0x00,0x00,0x00,0x00,0x00,0x9C,0x8E,0x8F,0x14,0x00,0x96,0xB9,0xA2,0xFF}, // 0x05
		{0x5F,0x4F,0x50,0x82,0x54,0x80,0xBF,0x1F,0x00,0xC1,0x00,0x00,0x00,0x00,0x00,0x00,0x9C,0x8E,0x8F,0x28,0x00,0x96,0xB9,0xC2,0xFF}, // 0x06
		{}, // 0x07 TODO
		{},{},{},{},{}, // 0x08-0x0c undefined
		{0x2D,0x27,0x28,0x90,0x2B,0x80,0xBF,0x1F,0x00,0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x9C,0x8E,0x8F,0x14,0x00,0x96,0xB9,0xE3,0xFF}, // 0x0d
		{0x5F,0x4F,0x50,0x82,0x54,0x80,0xBF,0x1F,0x00,0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x9C,0x8E,0x8F,0x28,0x00,0x96,0xB9,0xE3,0xFF}, // 0x0e
		{}, // 0x0f TODO
		{0x5F,0x4F,0x50,0x82,0x54,0x80,0xBF,0x1F,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x83,0x85,0x5D,0x28,0x0F,0x63,0xBA,0xE3,0xFF}, // 0x10
		{}, // 0x11 TODO
		{0x5F,0x4F,0x50,0x82,0x54,0x80,0x0B,0x3E,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0xEA,0x8C,0xDF,0x28,0x00,0xE7,0x04,0xE3,0xFF}, // 0x12
		{0x5F,0x4F,0x50,0x82,0x54,0x80,0xBF,0x1F,0x00,0x41,0x00,0x00,0x00,0x00,0x00,0x00,0x9C,0x8E,0x8F,0x28,0x40,0x96,0xB9,0xA3,0xFF}  // 0x13
	}};
};
