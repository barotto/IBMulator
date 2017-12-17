/*
 * Copyright (C) 2017  Marco Bortolin
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

//  VGA CRTC registers
#define CRTC_HTOTAL           0x00  // Index 00h (00) -- Horizontal Total Register
#define CRTC_END_HDISPLAY     0x01  // Index 01h (01) -- End Horizontal Display Register
#define CRTC_START_HDISPLAY   0x02  // Index 02h (02) -- Start Horizontal Blanking Register
#define CRTC_END_HBLANK       0x03  // Index 03h (03) -- End Horizontal Blanking Register
#define CRTC_START_HBLANK     0x04  // Index 04h (04) -- Start Horizontal Retrace Register
#define CRTC_END_HRETRACE     0x05  // Index 05h (05) -- End Horizontal Retrace Register
#define CRTC_VTOTAL           0x06  // Index 06h (06) -- Vertical Total Register
#define CRTC_OVERFLOW         0x07  // Index 07h (07) -- Overflow Register
#define CRTC_PRESET_ROW_SCAN  0x08  // Index 08h (08) -- Preset Row Scan Register
#define CRTC_MAX_SCANLINE     0x09  // Index 09h (09) -- Maximum Scan Line Register
#define CRTC_CURSOR_START     0x0A  // Index 0Ah (10) -- Cursor Start Register
#define CRTC_CURSOR_END       0x0B  // Index 0Bh (11) -- Cursor End Register
#define CRTC_STARTADDR_HI     0x0C  // Index 0Ch (12) -- Start Address High Register
#define CRTC_STARTADDR_LO     0x0D  // Index 0Dh (13) -- Start Address Low Register
#define CRTC_CURSOR_HI        0x0E  // Index 0Eh (14) -- Cursor Location High Register
#define CRTC_CURSOR_LO        0x0F  // Index 0Fh (15) -- Cursor Location Low Register
#define CRTC_VRETRACE_START   0x10  // Index 10h (16) -- Vertical Retrace Start Register
#define CRTC_VRETRACE_END     0x11  // Index 11h (17) -- Vertical Retrace End Register
#define CRTC_VDISPLAY_END     0x12  // Index 12h (18) -- Vertical Display End Register
#define CRTC_OFFSET           0x13  // Index 13h (19) -- Offset Register
#define CRTC_UNDERLINE        0x14  // Index 14h (20) -- Underline Location Register
#define CRTC_START_VBLANK     0x15  // Index 15h (21) -- Start Vertical Blanking Register
#define CRTC_END_VBLANK       0x16  // Index 16h (22) -- End Vertical Blanking
#define CRTC_MODE_CONTROL     0x17  // Index 17h (23) -- CRTC Mode Control Register
#define CRTC_LINE_COMPARE     0x18  // Index 18h (24) -- Line Compare Register

// End Horizontal Blanking Register (Index 03h)
#define CRTC_EVRA   0x80  // Enable Vertical Retrace Access
#define CRTC_DESK   0x60  // Display Enable Skew
#define CRTC_EHB40  0x1F  // End Horizontal Blanking (bits 4-0)

// End Horizontal Retrace Register (Index 05h)
#define CRTC_EHB5   0x80  // End Horizontal Blanking (bit 5)
#define CRTC_HRSK   0x60  // Horizontal Retrace Skew
#define CRTC_EHR    0x1F  // End Horizontal Retrace

// Overflow Register (Index 07h)
#define CRTC_VRS9   0x80  // Vertical Retrace Start (bit 9)
#define CRTC_VDE9   0x40  // Vertical Display End (bit9)
#define CRTC_VT9    0x20  // Vertical Total (bit 9)
#define CRTC_LC8    0x10  // Line Compare (bit 8)
#define CRTC_SVB8   0x08  // Start Vertical Blanking (bit 8)
#define CRTC_VRS8   0x04  // Vertical Retrace Start (bit 8)
#define CRTC_VDE8   0x02  // Vertical Display End (bit 8)
#define CRTC_VT8    0x01  // Vertical Total (bit 8)

// Preset Row Scan Register (Index 08h)
#define CRTC_BPAN   0x60  // Byte Panning
#define CRTC_PRS    0x1F  // Preset Row Scan

// Maximum Scan Line Register (Index 09h)
#define CRTC_SD     0x80  // Scan Doubling
#define CRTC_LC9    0x40  // Line Compare (bit 9)
#define CRTC_SVB9   0x20  // Start Vertical Blanking (bit 9)
#define CRTC_MSL    0x1F  // Maximum Scan Line

// Cursor Start Register (Index 0Ah)
#define CRTC_CD     0x20  // Cursor Disable
#define CRTC_CSLS   0x1F  // Cursor Scan Line Start

// Cursor End Register (Index 0Bh)
#define CRTC_CSK    0x60  // Cursor Skew
#define CRTC_CSLE   0x1F  // Cursor Scan Line End

// Vertical Retrace End Register (Index 11h)
#define CRTC_PROT   0x80  // CRTC Registers Protect Enable
#define CRTC_MRB    0x40  // Memory Refresh Bandwidth
#define CRTC_DINT   0x20  // Disable Vertical Interrupts
#define CRTC_NCINT  0x10  // Not Clear pending Vertical Interrupts
#define CRTC_VRE    0x0F  // Vertical Retrace End

// Underline Location Register (Index 14h)
#define CRTC_DW     0x40  // Double-Word Addressing
#define CRTC_DIV4   0x20  // Divide Memory Address Clock by 4
#define CRTC_UL     0x1F  // Underline Location

// End Vertical Blanking Register (Index 16h)
#define CRTC_EVB    0x7F  //End Vertical Blanking

// Mode Control Register (Index 17h)
#define CRTC_SE     0x80  // Sync Enable
#define CRTC_BWM    0x40  // Byte/Word Mode Select
#define CRTC_AW     0x20  // Address Wrap Select
#define CRTC_DIV2   0x08  // Divide Memory Address clock by 2
#define CRTC_SLDIV  0x04  // Divide Scan Line clock by 2
#define CRTC_MAP14  0x02  // Map Display Address 14
#define CRTC_MAP13  0x01  // Map Display Address 13


struct VGA_CRTC
{
	uint8_t address;
	uint8_t reg[0x19];
	bool interrupt;
	uint16_t start_address;

	void latch_start_address() {
		start_address = (reg[CRTC_STARTADDR_HI] << 8) | reg[CRTC_STARTADDR_LO];
	}
	bool write_protect() const {
		return ((reg[CRTC_VRETRACE_END] & CRTC_PROT) > 0);
	}
};
