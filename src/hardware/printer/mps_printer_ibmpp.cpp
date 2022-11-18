/*
 * Copyright (C) Rene Garcia
 * Copyright (C) 2022  Marco Bortolin
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
 * IBM Proprinter single data interpreter automata
 * 
 * @param _input  Data byte to interpret
 */
void MpsPrinter::interpret_ibmpp(uint8_t _input)
{
	switch(m_state)
	{
		case MPS_PRINTER_STATE_INITIAL:

			// =======  Select action if command char received
			m_param_count = 0;

			switch(_input)
			{
				case 0x07:   // BELL
					break;

				case 0x08:   // Backspace
					{
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
						int i = 0;
						while (i < MPS_PRINTER_MAX_HTABULATIONS && m_htab[i] < m_margin_right) {
							if (m_htab[i] > m_head_x) {
								m_head_x = m_htab[i];
								break;
							}
							i++;
						}
					}
					break;

				case 0x0D:   // CR: carriage return (CR only, no LF)
					m_head_x = m_margin_left;
					if(!m_auto_lf) {
						break;
					}
					[[fallthrough]];

				case 0x0A:   // LF: line feed (no CR)
					line_feed(false);
					break;

				case 0x0B:   // VT: vertical tabulation
					if (m_vtab[0] == 0)
					{
						// If vertical tab stops are not defined, VT does only LF
						line_feed(false);
					}
					else
					{
						for(int i = 0; i < MPS_PRINTER_MAX_VTABULATIONS; i++) {
							if(m_vtab[i] > m_head_y) {
								move_paper(m_vtab[i]);
								break;
							}
						}
					}
					break;

				case 0x0C:   // FF: form feed
					form_feed();
					break;

				case 0x0E:   // SO: Double width printing ON
					m_double_width = true;
					break;

				case 0x0F:   // SI: 17.1 chars/inch on
					m_step = MPS_PRINTER_STEP_CONDENSED;
					break;

				case 0x11:   // DC1: Printer select
					// ignore
					break;

				case 0x12:   // DC2: 17.1 chars/inch off
					m_step = MPS_PRINTER_STEP_PICA;
					break;

				case 0x13:   // DC3: Printer suspend
					// ignore
					break;

				case 0x14:   // DC4: Double width printing off
					m_double_width = false;
					break;

				case 0x18:   // CAN: Clear print buffer
					// ignored
					break;

				case 0x1B:   // ESC: ASCII code for escape
					m_state = MPS_PRINTER_STATE_ESC;
					break;

				default:    // maybe a printable character
					if (is_printable(_input))
					{
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
				case 0x2D:  // ESC - : Underline on/off
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x30:  // ESC 0 : Spacing = 1/8"
					m_interline = 27;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x31:  // ESC 1 : Spacing = 7/72"
					m_interline = 21;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x32:  // ESC 2 : Spacing = 1/6"
					m_interline = m_next_interline;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x33:  // ESC 3 : Spacing = n/216"
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x34:  // ESC 4 : Set Top Of Form (TOF)
					m_top_form = m_head_y;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x35:  // ESC 5 : Automatic LF ON/OFF
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x36:  // ESC 6 : IBM Table 2 selection
					m_charset = m_config.ibm_charset;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x37:  // ESC 7 : IBM Table 1 selection
					m_charset = 0;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x38:  // ESC 8 : Out of paper detection disabled
					m_state = MPS_PRINTER_STATE_INITIAL;
					// ignored
					break;

				case 0x39:  // ESC 9 : Out of paper detection enabled
					m_state = MPS_PRINTER_STATE_INITIAL;
					// ignored
					break;

				case 0x3A:  // ESC : :  Print pitch = 1/12"
					m_step = MPS_PRINTER_STEP_ELITE;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x3C:  // ESC < : Set left to right printing for one line
					m_state = MPS_PRINTER_STATE_INITIAL;
					// ignored
					break;

				case 0x3D:  // ESC = : Down Line Loading of user characters
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x40:  // ESC @ : Initialise printer (main reset)
					init_interpreter();
					m_state = MPS_PRINTER_STATE_INITIAL;

					break;

				case 0x41:  // ESC A : Prepare spacing = n/72"
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x42:  // ESC B : Vertical TAB stops program
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x43:  // ESC C : Set form length
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x44:  // ESC D : Horizontal TAB stops program
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x45:  // ESC E : Emphasized printing ON
					m_bold = true;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x46:  // ESC F : Emphasized printing OFF
					m_bold = false;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x47:  // ESC G : Double Strike Printing ON
					m_double_strike = true;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x48:  // ESC H : Double Strike Printing OFF
					m_double_strike = false;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x49:  // ESC I : Select print definition
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x4A:  // ESC J : Skip n/216" of paper
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x4B:  // ESC K : Set normal density graphics
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x4C:  // ESC L : Set double density graphics
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x4E:  // ESC N : Defines bottom of form (BOF) in lines
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x4F:  // ESC O : Clear bottom of form (BOF)
					set_bof(0);
					break;

				case 0x51:  // ESC Q : Deselect printer
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x52:  // ESC R : Clear tab stops
					for (int i=0; i<MPS_PRINTER_MAX_HTABULATIONS; i++) {
						m_htab[i] = 168+i*24*8;
					}
					for (int i=0; i<MPS_PRINTER_MAX_VTABULATIONS; i++) {
						m_vtab[i] = 0;
					}
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x53:  // ESC S : Superscript/subscript printing
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x54:  // ESC T : Clear superscript/subscript printing
					m_state = MPS_PRINTER_STATE_INITIAL;
					m_script = MPS_PRINTER_SCRIPT_NORMAL;
					break;

				case 0x55:  // ESC U : Mono/Bidirectional printing
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x57:  // ESC W : Double width characters ON/OFF
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x59:  // ESC Y : Double dentity BIM selection, normal speed
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x5A:  // ESC Z : Four times density BIM selection
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x5C:  // ESC \ : Print n characters from extended table
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x5E:  // ESC ^ : Print one character from extended table
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x5F:  // ESC _ : Overline on/off
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x62:  // ESC b : Black ink
					m_color = MPS_PRINTER_COLOR_BLACK;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x63:  // ESC c : Cyan ink
					m_color = MPS_PRINTER_COLOR_CYAN;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x6d:  // ESC m : Magenta ink
					m_color = MPS_PRINTER_COLOR_MAGENTA;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x79:  // ESC y : Yellow ink
					m_color = MPS_PRINTER_COLOR_YELLOW;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x7E:  // ESC ~ : MPS-1230 extension
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				default:
					PDEBUGF(LOG_V1, LOG_LPT, "IBM Proprinter: undefined escape sequence 0x%02x\n", _input);
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
			}
			break;

		// =======  Escape sequence parameters
		case MPS_PRINTER_STATE_ESC_PARAM:

			m_param_count++;

			switch(m_esc_command)
			{
				case 0x2D:  // ESC - : Underline on/off
					m_underline = _input & 0x01 ? true : false;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x33:  // ESC 3 : Spacing = n/216"
					m_interline = _input;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x35:  // ESC 5 : Automatic LF ON/OFF
					m_auto_lf = _input & 0x01 ? true : false;
					break;

				case 0x3D:  // ESC = : Down Line Loading of user characters (parse but ignore)
					if (m_param_count == 1) {
						m_param_build = _input;
					}
					if (m_param_count == 2) {
						m_param_build |= _input<<8;
					}
					if ((m_param_count > 2) && (m_param_count == m_param_build+2)) {
						m_state = MPS_PRINTER_STATE_INITIAL;
					}
					break;

				case 0x41:  // ESC A : Spacing = n/72"
					m_next_interline = _input * 3;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x42:  // ESC B : Vertical TAB stops program
					if (_input == 0)
					{
						m_state = MPS_PRINTER_STATE_INITIAL;
					}
					else
					{
						if ((m_param_count > 1 && _input < m_param_build) || m_param_count > MPS_PRINTER_MAX_VTABULATIONS)
						{
							m_state = MPS_PRINTER_STATE_INITIAL;
						}
						else
						{
							m_param_build = _input;
							m_vtab[m_param_count-1] = _input * m_interline;
						}
					}
					break;

				case 0x43:  // ESC C : Set form length
					if (m_param_count == 1 && _input != 0)
					{
						set_form_length(_input * m_interline);
						m_state = MPS_PRINTER_STATE_INITIAL;
					}
					else if (m_param_count > 1)
					{
						if (_input > 0 && _input < 23) {
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
						if ((m_param_count > 1 && _input < m_param_build) || m_param_count > MPS_PRINTER_MAX_HTABULATIONS)
						{
							m_state = MPS_PRINTER_STATE_INITIAL;
						}
						else
						{
							m_param_build = _input;
							m_htab[m_param_count-1] = _input * ms_spacing_x[m_step][12];
						}
					}
					break;

				case 0x49:  // ESC I : Select print definition
					switch (_input)
					{
						case 0x00:  // Draft
						case 0x30:
							m_nlq = false;
							break;

						case 0x02:  // NLQ
						case 0x32:
							m_nlq = true;
							break;

						case 0x04:  // Draft + DLL enabled (not implemented)
						case 0x34:
							m_nlq = false;
							break;

						case 0x06:  // NLQ + DLL enabled (not implemented)
						case 0x36:
							m_nlq = true;
							break;

						default:
							break;
					}
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
					if (m_param_count > 2)
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

				case 0x51:  // ESC Q : Deselect printer
					// ignore
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
						m_param_build  = _input;
						m_bim_density  = m_bim_Z_density;
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
						if (m_param_count - 2u >= m_param_build)
							m_state = MPS_PRINTER_STATE_INITIAL;
					}
					break;

				case 0x5C:  // ESC \ : Print n characters from extended table
					// First parameter is data length LSB
					if (m_param_count == 1) {
						m_param_build = _input;
					}
					// Second parameter is data length MSB
					if (m_param_count == 2) {
						m_param_build |= _input<<8;
					}
					// Follows the BIM data
					if (m_param_count > 2)
					{
						if (is_printable(_input)) {
							m_head_x += print_char(charset2chargen(_input));
						} else {
							m_head_x += print_char(charset2chargen(' '));
						}
						if (m_head_x > m_margin_right) {
							line_feed();
						}
						if (m_param_count - 2u >= m_param_build) {
							m_state = MPS_PRINTER_STATE_INITIAL;
						}
					}
					break;

				case 0x5E:  // ESC ^ : Print one character from extended table
					if (is_printable(_input)) {
						m_head_x += print_char(charset2chargen(_input));
					} else {
						m_head_x += print_char(charset2chargen(' '));
					}
					if (m_head_x > m_margin_right) {
						line_feed();
					}
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x5F:  // ESC _ : Overline on/off
					m_overline = _input & 0x01 ? true : false;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x7E:  // ESC ~ : MPS-1230 extension
					if (m_param_count == 1) {
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
										break;
								}
								break;

							default:
								break;
						}

						m_state = MPS_PRINTER_STATE_INITIAL;
					}
					break;

				default:
					PDEBUGF(LOG_V1, LOG_LPT, "IBM Proprinter: undefined escape sequence 0x%02X parameter %d\n", m_esc_command, _input);
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
			}
			break;

		default:
			PDEBUGF(LOG_V1, LOG_LPT, "IBM Proprinter: undefined state %d\n", m_state);
			m_state = MPS_PRINTER_STATE_INITIAL;
			break;
	}
}

