/*
 * Copyright (C) Rene Garcia
 * Copyright (C) 2022-2023  Marco Bortolin
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

// Derivative work of MPS Emulator by Rene Garcia, released under GPLv3 and
// included in 1541 Ultimate software (https://github.com/GideonZ/1541ultimate)

#include "ibmulator.h"
#include "mps_printer.h"


/**
 * Changes international charset for Epson emulation
 * 
 * @ param _cs New charset
 */
void MpsPrinter::cmd_set_epson_charset(uint8_t _cs)
{
	m_cmd_queue.push([=] () {
		if (_cs > 11) {
			return;
		}

		// If charset changed and emulation is for Epson
		if (_cs != m_config.epson_charset && m_interpreter == MPS_PRINTER_INTERPRETER_EPSON) {
			m_charset = _cs;
			PDEBUGF(LOG_V1, LOG_PRN, "Epson: current charset set to %u\n", m_charset);
		}

		m_config.epson_charset = _cs;
	});
}

/**
 * Prints a single bitmap image record (Epson standard)
 * 
 * @param _head  record to print
 * @return printed width (in pixels)
 */
uint16_t MpsPrinter::print_epson_bim(uint8_t _head)
{
	// =======  Horizontal steps to simulate the 7 pitches
	static uint8_t density_tab[7][3] =
	{
		{ 4, 4, 4 },  // 60 dpi
		{ 2, 2, 2 },  // 120 dpi
		{ 2, 2, 2 },  // 120 dpi high speed
		{ 1, 1, 1 },  // 240 dpi
		{ 3, 3, 3 },  // 80 dpi
		{ 3, 4, 3 },  // 72 dpi
		{ 3, 2, 3 }   // 90 dpi
	};
	
	// -------  Each dot to print (LSB is up)
	for (int j=0; j<8; j++) {
		// Need to print a dot?
		if (_head & 0x80) {
			print_dot(m_head_x, m_head_y+ms_spacing_y[MPS_PRINTER_SCRIPT_NORMAL][j], true);
		}
		_head <<= 1;
	}

	// Return horizontal spacing
	return density_tab[m_bim_density][m_bim_position++ % 3];
}

/**
 * Prints a single bitmap image record (epson standard)
 * 
 * @param _head  record to print 8 up needles
 * @param _low   record to print last down needle
 * @return printed width (in pixels)
 */
uint16_t MpsPrinter::print_epson_bim9(uint8_t _head, uint8_t _low)
{
	// =======  Horizontal steps to simulate the 7 pitches
	static uint8_t density_tab[2] = { 4, 2 };    /* 60, 120 dpi */
	
	// -------  Each dot to print (LSB is up)
	for (int j=0; j<8; j++) {
		// Need to print a dot?
		if (_head & 0x80) {
			print_dot(m_head_x, m_head_y+ms_spacing_y[MPS_PRINTER_SCRIPT_NORMAL][j], true);
		}
		_head <<= 1;
	}

	if (_low) {
		print_dot(m_head_x, m_head_y+ms_spacing_y[MPS_PRINTER_SCRIPT_NORMAL][8], true);
	}

	// Return horizontal spacing
	return density_tab[m_bim_density];
}

/**
 * Epson FX-80 single data interpreter automata
 * 
 * @param _input  Data byte to interpret
 */
void MpsPrinter::interpret_epson(uint8_t _input)
{
	switch(m_state)
	{
		case MPS_PRINTER_STATE_INITIAL:
	
			// =======  Select action if command char received
			m_param_count = 0;
			switch(_input)
			{
				case 0x08:   // Backspace
					{
						PDEBUGF(LOG_V2, LOG_PRN, "Epson: Backspace\n");
						uint16_t cwidth = print_char(charset2chargen(' '));
						if (cwidth <= m_head_x) {
							m_head_x -= cwidth;
						} else {
							m_head_x = 0;
						}
					}
					break;
	
				case 0x09:   // TAB: horizontal tabulation
					{
						PDEBUGF(LOG_V2, LOG_PRN, "Epson: TAB: horizontal tabulation\n");
						// The printer ignores this command if no tab is set to the right of the current position or if
						// the next tab is to the right of the right margin.
						for(int i=0; i < MPS_PRINTER_MAX_HTABULATIONS; i++) {
							// The tab settings move to match any movement in the left margin.
							uint16_t tab_x = m_htab[i] + m_margin_left;
							if(tab_x > m_margin_right) {
								break;
							}
							if(tab_x > m_head_x) {
								m_head_x = tab_x;
								break;
							}
						}
					}
					break;
	
				case 0x0A:   // LF: line feed (LF+CR)
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: LF: line feed (LF+CR)\n");
					line_feed();
					break;
	
				case 0x0B:   // VT: vertical tabulation
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: VT: vertical tabulation\n");
					if (m_vtab[0] == 0)
					{
						// If vertical tab stops are not defined, VT does only LF
						line_feed();
					}
					else
					{
						for(int i = 0; i < MPS_PRINTER_MAX_VTABULATIONS; i++) {
							if(m_vtab[i] > m_head_y) {
								move_paper(m_vtab[i]);
								break;
							}
						}
						carriage_return();
					}
					break;
	
				case 0x0C:   // FF: form feed
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: FF: form feed\n");
					form_feed();
					break;
	
				case 0x0D:   // CR: carriage return (CR only, no LF)
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: CR: carriage return\n");
					carriage_return();
					break;
	
				case 0x0E:   // SO: Double width printing ON
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: SO\n");
					m_double_width = true;
					break;
	
				case 0x0F:   // SI: 17.1 chars/inch on
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: SI: 17.1 chars/inch ON\n");
					m_step = MPS_PRINTER_STEP_CONDENSED;
					break;
	
				case 0x11:   // DC1: Printer select
					// ignore
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: DC1: Printer select (ignored)\n");
					break;
	
				case 0x12:   // DC2: 17.1 chars/inch off
					m_step = MPS_PRINTER_STEP_PICA;
					break;
	
				case 0x13:   // DC3: Printer suspend
					// ignore
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: DC3: Printer suspend (ignored)\n");
					break;
	
				case 0x14:   // DC4: Double width printing off
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: DC4: Double width printing OFF\n");
					m_double_width = false;
					break;
	
				case 0x18:   // CAN: Clear print buffer
					// ignored
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: CAN: Clear print buffer (ignored)\n");
					break;
	
				case 0x1B:   // ESC: ASCII code for escape
					PDEBUGF(LOG_V3, LOG_PRN, "Epson: ESC\n");
					m_state = MPS_PRINTER_STATE_ESC;
					break;
	
				case 0x7F:   // DEL: Clear last printable character
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: DEL: Clear last printable character (ignored)\n");
					// ignore
					break;
	
				default:    // maybe a printable character
					if (is_printable(_input))
					{
						PDEBUGF(LOG_V2, LOG_PRN, "Epson: printable: %u, 0x%02x\n", _input, _input);
						m_head_x += print_char(charset2chargen(_input));
						if(m_head_x > m_margin_right) {
							line_feed();
						}
					}
					break;
			}
			break;
	
		// =======  Escape sequences
		case MPS_PRINTER_STATE_ESC:
			m_esc_command = _input;
			m_param_count = 0;
			switch (_input)
			{
				case 0x0E:   // ESC SO: Double width printing on
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC SO: Double width printing on\n");
					m_double_width = true;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x0F:   // ESC SI: 17.1 chars/inch on
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC SI: 17.1 chars/inch on\n");
					m_step = MPS_PRINTER_STEP_CONDENSED;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x19:  // ESC EM : Control paper loading/ejecting
					// used by the Windows 3.1 FX-80 driver, not supported by the FX-80
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC EM : Control paper loading/ejecting (ignored)\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x21:  // ESC ! : Select graphics layout types
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC ! : Select graphics layout types\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x23:  // ESC # : Clear bit 7 forcing (MSB)
					// ignored
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC # : Clear bit 7 forcing (MSB) (ignored)\n");
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x25:  // ESC % : Select RAM (special characters) and ROM (standard characters)
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC %% : Select RAM (special characters) and ROM (standard characters)\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x26:  // ESC & : Define spacial characters in RAM
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC & : Define spacial characters in RAM\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x2A:  // ESC * : Set graphics layout in diferent density
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC * : Set graphics layout in diferent density\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x2D:  // ESC - : Underline on/off
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC - : Underline on/off\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x2F:  // ESC / : Vertical TAB stops program
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC / : Vertical TAB stops program\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x30:  // ESC 0 : Spacing = 1/8"
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC 0 : Spacing = 1/8\"\n");
					m_interline = 27;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x31:  // ESC 1 : Spacing = 7/72"
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC 1 : Spacing = 7/72\"\n");
					m_interline = 21;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x32:  // ESC 2 : Spacing = 1/6"
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC 2 : Spacing = 1/6\"\n");
					m_interline = 36;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x33:  // ESC 3 : Spacing = n/216"
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC 3 : Spacing = n/216\"\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x34:  // ESC 4 : Italic ON
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC 4 : Italic ON\n");
					m_italic = true;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x35:  // ESC 5 : Italic OFF
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC 5 : Italic OFF\n");
					m_italic = false;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x36:  // ESC 6 : Extend printable character set
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC 6 : Extend printable character set (ignored)\n");
					m_state = MPS_PRINTER_STATE_INITIAL;
					// ignore
					break;
	
				case 0x37:  // ESC 7 : Select basic national characters table
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC 7 : Select basic national characters table\n");
					m_charset = 0;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x38:  // ESC 8 : Out of paper detection disabled
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC 8 : Out of paper detection disabled\n");
					m_state = MPS_PRINTER_STATE_INITIAL;
					// ignored
					break;
	
				case 0x39:  // ESC 9 : Out of paper detection enabled
					m_state = MPS_PRINTER_STATE_INITIAL;
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC 9 : Out of paper detection enabled (ignored)\n");
					// ignored
					break;
	
				case 0x3A:  // ESC : :  Copy standard character generator (ROM) into RAM
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC : :  Copy standard character generator (ROM) into RAM\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x3C:  // ESC < : Set left to right printing for one line
					m_state = MPS_PRINTER_STATE_INITIAL;
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC < : Set left to right printing for one line (ignored)\n");
					// ignore
					break;
	
				case 0x3D:  // ESC = : Force bit 7 (MSB) to "0"
					m_state = MPS_PRINTER_STATE_INITIAL;
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC = : Force bit 7 (MSB) to 0 (ignored)\n");
					// ignore
					break;
	
				case 0x3E:  // ESC > : Force bit 7 (MSB) to "1"
					m_state = MPS_PRINTER_STATE_INITIAL;
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC > : Force bit 7 (MSB) to 1 (ignored)\n");
					// ignore
					break;
	
				case 0x3F:  // ESC ? : Change BIM density selected by graphics commands
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC ? : Change BIM density selected by graphics commands\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x40:  // ESC @ : Initialise printer (main reset)
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC @ : Initialise printer (main reset)\n");
					init_interpreter();
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x41:  // ESC A : Spacing = n/72"
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC A : Spacing = n/72\"\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x42:  // ESC B : Vertical TAB stops program
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC B : Vertical TAB stops program\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x43:  // ESC C : Set form length
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC C : Set form length\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x44:  // ESC D : Horizontal TAB stops program
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC D : Horizontal TAB stops program\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x45:  // ESC E : Emphasized printing ON
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC E : Emphasized printing ON\n");
					m_bold = true;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x46:  // ESC F : Emphasized printing OFF
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC F : Emphasized printing OFF\n");
					m_bold = false;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x47:  // ESC G : NLQ Printing ON
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC G : NLQ Printing ON\n");
					m_double_strike = true;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x48:  // ESC H : NLQ Printing OFF
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC H : NLQ Printing OFF\n");
					m_double_strike = false;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x49:  // ESC I : Extend printable characters set
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC I : Extend printable characters set\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x4A:  // ESC J : Skip n/216" of paper
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC J : Skip n/216\" of paper\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x4B:  // ESC K : Set normal density graphics
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC K : Set normal density graphics\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x4C:  // ESC L : Set double density graphics
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC L : Set double density graphics\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x4D:  // ESC M : Print pitch ELITE ON
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC M : Print pitch ELITE ON\n");
					m_step = MPS_PRINTER_STEP_ELITE;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x4E:  // ESC N : Defines bottom of form (BOF) in lines
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC N : Defines bottom-of-form (BOF) in lines\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x4F:  // ESC O : Clear bottom of form (BOF)
					// Ignored in this version, usefull only for continuous paper feed
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC O : Clear bottom-of-form (BOF) (ignored)\n");
					break;
	
				case 0x50:  // ESC P : Print pitch ELITE OFF
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC P : Print pitch ELITE OFF\n");
					m_step = MPS_PRINTER_STEP_PICA;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x51:  // ESC Q : Define right margin
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC Q : Define right margin\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x52:  // ESC R : Select national character set
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC R : Select national character set\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x53:  // ESC S : Superscript/subscript printing
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC S : Superscript/subscript printing\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x54:  // ESC T : Clear superscript/subscript printing
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC T : Clear superscript/subscript printing\n");
					m_state = MPS_PRINTER_STATE_INITIAL;
					m_script = MPS_PRINTER_SCRIPT_NORMAL;
					break;
	
				case 0x55:  // ESC U : Mono/Bidirectional printing
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC U : Mono/Bidirectional printing\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x57:  // ESC W : Double width characters ON/OFF
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC W : Double width characters ON/OFF\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x59:  // ESC Y : Double dentity BIM selection, normal speed
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC Y : Double dentity BIM selection, normal speed\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x5A:  // ESC Z : Four times density BIM selection
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC Z : Four times density BIM selection\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x5E:  // ESC ^ : 9-dot high strips BIM printing
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC ^ : 9-dot high strips BIM printing\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x62:  // ESC b : Select up to 8 vertical tab stops programs
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC b : Select up to 8 vertical tab stops programs\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x69:  // ESC i : Immediate character printing ON/OFF
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC i : Immediate character printing ON/OFF\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x6A:  // ESC j : Reverse paper feed n/216"
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC j : Reverse paper feed n/216\"\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x6C:  // ESC l : Define left margin
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC l : Define left margin\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x70:  // ESC p : Proportional spacing ON/OFF
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC p : Proportional spacing ON/OFF\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x72:  // ESC r : Color ink selection
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC r : Color ink selection\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x73:  // ESC s : Half speed printing ON/OFF
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC s : Half speed printing ON/OFF\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x74: // ESC t : Select character table
					// used by the Windows 3.1 FX-80 driver, but not supported by the FX-80
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC t : Select character table (ignored)\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x78:  // ESC x : DRAFT/NLQ print mode selection
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC x : DRAFT/NLQ print mode selection\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				case 0x7E:  // ESC ~ : MPS-1230 extension
					PDEBUGF(LOG_V2, LOG_PRN, "Epson: ESC ~ : MPS-1230 extension\n");
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;
	
				default:
					PDEBUGF(LOG_V1, LOG_PRN, "Epson: undefined ESC sequence 0x%02x\n", _input);
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
			}
	
			break;
	
		// =======  Escape sequence parameters
		case MPS_PRINTER_STATE_ESC_PARAM:
			m_param_count++;
			switch(m_esc_command)
			{
				case 0x19:  // ESC EM : Control paper loading/ejecting
					// used by the Windows 3.1 FX-80 driver, not supported by the FX-80
					// ignore
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x21:  // ESC ! : Select graphics layout types
					m_step = MPS_PRINTER_STEP_PICA;
					if (_input & 4) {
						m_step = MPS_PRINTER_STEP_CONDENSED;
					}
					if (_input & 1) {
						m_step = MPS_PRINTER_STEP_ELITE;
					}
					m_underline = (_input & 0x80) ? true : false;
					m_italic = (_input & 0x40) ? true : false;
					m_double_width = (_input & 0x20) ? true : false;
					m_double_strike = (_input & 0x10) ? true : false;
					m_bold = (_input & 0x08) ? true : false;
					//porportional = (input & 0x02) ? true : false;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x25:  // ESC % : Select RAM (special characters) and ROM (standard characters)
					// ignore
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x26:  // ESC & : Define spacial characters in RAM
					// Fisrt parameter has to be '0'
					if (m_param_count == 1 && _input != '0') {
						m_state = MPS_PRINTER_STATE_INITIAL;
					}
					// Second parameter is ascii code of first redefined character
					if (m_param_count == 2) m_param_build = _input;
					// Thirs parameter is ascii code of last redefined character
					if (m_param_count == 3)
					{
						// If firest is greater than last, error
						if (m_param_build > _input) {
							m_state = MPS_PRINTER_STATE_INITIAL;
						} else {
							// Otherwise calculate amout of data to be uploaded
							m_param_build = (_input - m_param_build + 1) * 12 + 3;
						}
					}
					// ignore, skip uploaded data
					if (m_param_count >= m_param_build) {
						m_state = MPS_PRINTER_STATE_INITIAL;
					}
					break;
	
				case 0x2A:  // ESC * : Set graphics layout in different density
					// First parameter is density
					if (m_param_count == 1)
					{
						m_bim_density  = (_input<7) ? _input : 0;
						m_bim_position = 0;
					}
					// Second parameter is data length LSB
					if (m_param_count == 2) {
						m_param_build = _input;
					}
					// Third parameter is data length MSB
					if (m_param_count == 3) {
						m_param_build |= _input<<8;
					}
					// Follows the BIM data
					if (m_param_count>3)
					{
						m_head_x += print_epson_bim(_input);
						if (m_param_count - 3u >= m_param_build) {
							m_state = MPS_PRINTER_STATE_INITIAL;
						}
					}
					break;
	
				case 0x2D:  // ESC - : Underline on/off
					m_underline = _input & 0x01 ? true : false;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x2F:  // ESC / : Vertical TAB stops program
					if (_input < MPS_PRINTER_MAX_VTABSTORES) {
						m_vtab = m_vtab_store[_input];
					}
					break;
	
				case 0x33:  // ESC 3 : Spacing = n/216"
					m_interline = _input;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x3A:  // ESC : :  Copy standard character generator (ROM) into RAM
					if (m_param_count == 3) {
						m_state = MPS_PRINTER_STATE_INITIAL;
					}
					// ignored
					break;
	
				case 0x3F:  // ESC ? : Change BIM density selected by graphics commands
					// BIM mode to change
					if (m_param_count == 1) {
						m_param_build = _input;
					}
					// New density for selected mode
					if (m_param_count == 2)
					{
						_input &= 0x07;
	
						switch (m_param_build)
						{
							case 'K': m_bim_K_density = _input; break;
							case 'L': m_bim_L_density = _input; break;
							case 'Y': m_bim_Y_density = _input; break;
							case 'Z': m_bim_Z_density = _input; break;
							default: break;
						}
	
						m_state = MPS_PRINTER_STATE_INITIAL;
					}
					break;
	
				case 0x41:  // ESC A : Spacing = n/72"
					m_interline = _input * 3;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x42:  // ESC B : Vertical TAB stops program
					if (_input == 0)
					{
						m_state = MPS_PRINTER_STATE_INITIAL;
					}
					else
					{
						if ((m_param_count > 1 && _input < m_param_build) || m_param_count > MPS_PRINTER_MAX_VTABULATIONS) {
							m_state = MPS_PRINTER_STATE_INITIAL;
						} else {
							m_param_build = _input;
							m_vtab[m_param_count-1] = _input * m_interline;
						}
					}
					break;
	
				case 0x43:  // ESC C : Set form length
					if (m_param_count == 1 && _input != 0)
					{
						// form height in lines
						set_form_length(_input * m_interline);
						m_state = MPS_PRINTER_STATE_INITIAL;
					}
					else if (m_param_count > 1)
					{
						if (_input > 0 && _input < 23) {
							// form height in inches
							set_form_length(_input * MPS_PRINTER_DPI_Y);
						}
						m_state = MPS_PRINTER_STATE_INITIAL;
					}
					break;

				case 0x44:  // ESC D : Horizontal TAB stops program
					if (_input == 0)
					{
						m_state = MPS_PRINTER_STATE_INITIAL;
					}
					else
					{
						if ((m_param_count > 1 && _input < m_param_build) || m_param_count > MPS_PRINTER_MAX_HTABULATIONS) {
							m_state = MPS_PRINTER_STATE_INITIAL;
						} else {
							m_param_build = _input;
							m_htab[m_param_count-1] = _input * ms_spacing_x[m_step][12];
						}
					}
					break;
	
				case 0x49:  // ESC I : Extend printable characters set
					m_epson_charset_extended = (_input & 1) ? true : false;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x4A:  // ESC J : Skip n/216" of paper
					move_paper(_input);
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x4B:  // ESC K : Set normal density graphics
					// First parameter is data length LSB
					if (m_param_count == 1)
					{
						m_param_build = _input;
						m_bim_density  = m_bim_K_density;
						m_bim_position = 0;
					}
					// Second parameter is data length MSB
					if (m_param_count == 2) {
						m_param_build |= _input<<8;
					}
					// Follows the BIM data
					if (m_param_count > 2)
					{
						m_head_x += print_epson_bim(_input);
						if (m_param_count - 2u >= m_param_build) {
							m_state = MPS_PRINTER_STATE_INITIAL;
						}
					}
					break;
	
				case 0x4C:  // ESC L : Set double density graphics
					// First parameter is data length LSB
					if (m_param_count == 1)
					{
						m_param_build = _input;
						m_bim_density  = m_bim_L_density;
						m_bim_position = 0;
					}
					// Second parameter is data length MSB
					if (m_param_count == 2) {
						m_param_build |= _input<<8;
					}
					// Follows the BIM data
					if (m_param_count>2)
					{
						m_head_x += print_epson_bim(_input);
						if (m_param_count - 2u >= m_param_build) {
							m_state = MPS_PRINTER_STATE_INITIAL;
						}
					}
					break;
	
				case 0x4E:  // ESC N : Defines bottom of form (BOF)
					if(_input >= 1 && _input <= 127) {
						set_bof(_input * m_interline);
					}
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x51:  // ESC Q : Define right margin
					m_margin_right = _input * ms_spacing_x[m_step][12];
					if (m_margin_right <= m_margin_left || m_margin_right > MPS_PRINTER_MAX_WIDTH_PX) {
						m_margin_right = MPS_PRINTER_MAX_WIDTH_PX;
					}
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x52:  // ESC R : Select national character set
					if (_input == '0') {
						_input = 0;
					}
					if (_input < 11) {
						m_charset = _input + 1;
					}
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x53:  // ESC S : Superscript/subscript printing
					m_script = _input & 0x01 ? MPS_PRINTER_SCRIPT_SUB : MPS_PRINTER_SCRIPT_SUPER;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x55:  // ESC U : Mono/Bidirectional printing
					m_state = MPS_PRINTER_STATE_INITIAL;
					// ignore
					break;
	
				case 0x57:  // ESC W : Double width characters ON/OFF
					m_double_width = (_input & 1) ? true : false;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x59:  // ESC Y : Double dentity BIM selection, normal speed
					// First parameter is data length LSB
					if (m_param_count == 1)
					{
						m_param_build = _input;
						m_bim_density  = m_bim_Y_density;
						m_bim_position = 0;
					}
					// Second parameter is data length MSB
					if (m_param_count == 2) {
						m_param_build |= _input<<8;
					}
					// Follows the BIM data
					if (m_param_count > 2)
					{
						m_head_x += print_epson_bim(_input);
						if (m_param_count - 2u >= m_param_build) {
							m_state = MPS_PRINTER_STATE_INITIAL;
						}
					}
					break;
	
				case 0x5A:  // ESC Z : Four times density BIM selection
					// First parameter is data length LSB
					if (m_param_count == 1)
					{
						m_param_build = _input;
						m_bim_density = m_bim_Z_density;
						m_bim_position = 0;
					}
					// Second parameter is data length MSB
					if (m_param_count == 2) {
						m_param_build |= _input<<8;
					}
					// Follows the BIM data
					if (m_param_count>2)
					{
						m_head_x += print_epson_bim(_input);
						if (m_param_count - 2u >= m_param_build) {
							m_state = MPS_PRINTER_STATE_INITIAL;
						}
					}
					break;
	
				case 0x5E:  // ESC ^ : 9-dot high strips BIM printing
					// First parameter is density
					if (m_param_count == 1)
					{
						// Only density 0 & 1 are allowed
						m_bim_density  = _input & 0x01;
						m_bim_position = 0;
					}
					// Second parameter is data length LSB
					if (m_param_count == 2) {
						m_param_build = _input;
					}
					// Third parameter is data length MSB
					if (m_param_count == 3) {
						m_param_build |= _input<<8;
					}
					// Follows the BIM data
					if (m_param_count > 3)
					{
						static uint8_t keep;
	
						if (m_param_count & 0x01)
						{
							m_head_x += print_epson_bim9(keep, _input);
						}
						else
						{
							keep = _input;
						}
	
						if (m_param_count >= m_param_build + 3) {
							m_state = MPS_PRINTER_STATE_INITIAL;
						}
					}
					break;
	
				case 0x62:  // ESC b : Select up to 8 vertical tab stops programs
					if (m_param_count == 1)
					{
						m_param_build = _input;
					}
					else
					{
						if (_input == 0)
						{
							m_state = MPS_PRINTER_STATE_INITIAL;
						}
						else
						{
							if ((m_param_count > 2 && (_input * m_interline) < m_vtab_store[m_param_build][m_param_count-3])
							  || m_param_count-1 > MPS_PRINTER_MAX_VTABULATIONS)
							{
								m_state = MPS_PRINTER_STATE_INITIAL;
							}
							else
							{
								m_vtab_store[m_param_build][m_param_count-2] = _input * m_interline;
							}
						}
					}
					break;
	
				case 0x69:  // ESC i : Immediate character printing ON/OFF
					m_state = MPS_PRINTER_STATE_INITIAL;
					// ignored
					break;
	
				case 0x6A:  // ESC j : Reverse paper feed n/216"
					move_paper(-int(_input));
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x6C:  // ESC l : Define left margin
					m_margin_left = _input * ms_spacing_x[m_step][12];
					if (m_margin_left >= m_margin_right) {
						m_margin_left = 0;
					}
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x70:  // ESC p : Proportional spacing ON/OFF
					m_state = MPS_PRINTER_STATE_INITIAL;
					// ignore
					break;
	
				case 0x72:  // ESC r : Color ink selection
					switch (_input)
					{
						case 0x00:  // Black
						case 0x30:
							m_color = MPS_PRINTER_COLOR_BLACK;
							break;
	
						case 0x01:  // Magenta
						case 0x31:
							m_color = MPS_PRINTER_COLOR_MAGENTA;
							break;
	
						case 0x02:  // Cyan
						case 0x32:
							m_color = MPS_PRINTER_COLOR_CYAN;
							break;
	
						case 0x03:  // Violet
						case 0x33:
							m_color = MPS_PRINTER_COLOR_VIOLET;
							break;
	
						case 0x04:  // Yellow
						case 0x34:
							m_color = MPS_PRINTER_COLOR_YELLOW;
							break;
	
						case 0x05:  // Orange
						case 0x35:
							m_color = MPS_PRINTER_COLOR_ORANGE;
							break;
	
						case 0x06:  // Green
						case 0x36:
							m_color = MPS_PRINTER_COLOR_GREEN;
							break;

						default:
							PDEBUGF(LOG_V1, LOG_PRN, "Epson: ESC r : Color ink selection, invalid param _input 0x%02x\n", _input);
							break;
					}
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
	
				case 0x73:  // ESC s : Half speed printing ON/OFF
					m_state = MPS_PRINTER_STATE_INITIAL;
					// ignore
					break;

				case 0x74: // ESC t : Select character table
					// used by Windows 3.1 FX-80 driver
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x78:  // ESC x : DRAFT/NLQ print mode selection
					m_nlq = _input & 0x01 ? true : false;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x7E:  // ESC ~ : MPS-1230 extension
					if (m_param_count == 1)
					{
						m_param_build = _input;  // Command number
					}
					if (m_param_count == 2)
					{
						switch (m_param_build)
						{
							case 2:         // ESX ~ 2 n : reverse ON/OFF
							case '2':
								m_reverse = (_input & 1) ? true : false;
								break;

							case 3:         // ESX ~ 3 n : select pitch
							case '3':
								{
									uint8_t new_step = _input & 0x0F;
									if (new_step < 7) {
										m_step = new_step;
									}
								}
								break;
	
							case 4:         // ESX ~ 4 n : slashed zero ON/OFF
							case '4':
								// ignored
								break;
	
							case 5:         // ESX ~ 5 n : switch EPSON, Commodore, Proprinter, Graphics Printer
							case '5':
								switch (_input)
								{
									case 0:
									case '0':
										set_interpreter(MPS_PRINTER_INTERPRETER_EPSON);
										break;
	
									case 1:
									case '1':
										//set_interpreter(MPS_PRINTER_INTERPRETER_CBM);
										break;
	
									case 2:
									case '2':
										set_interpreter(MPS_PRINTER_INTERPRETER_IBMPP);
										break;
	
									case 3:
									case '3':
										set_interpreter(MPS_PRINTER_INTERPRETER_IBMGP);
										break;

									default:
										PDEBUGF(LOG_V1, LOG_PRN, "Epson: ESX ~ 5 n : switch EPSON, Commodore, Proprinter, Graphics Printer, invalid param _input 0x%02x\n", _input);
										break;
								}
								break;

							default:
								PDEBUGF(LOG_V1, LOG_PRN, "Epson: ESC ~ : MPS-1230 extension, invalid param _input 0x%02x\n", m_param_build);
								break;
						}
	
						m_state = MPS_PRINTER_STATE_INITIAL;
					}
					break;
	
				default:
					PDEBUGF(LOG_V1, LOG_PRN, "Epson: undefined ESC 0x%02x parameter 0x%02x\n", m_esc_command, _input);
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
			}
			break;
	
		default:
			PDEBUGF(LOG_V1, LOG_PRN, "Epson: undefined printer state %d\n", m_state);
			m_state = MPS_PRINTER_STATE_INITIAL;
			break;
	}
}
