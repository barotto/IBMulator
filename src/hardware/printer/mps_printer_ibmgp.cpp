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
 * Changes the international charset for IBM emulation
 * 
 * @param _cs  New charset
 */
void MpsPrinter::cmd_set_ibm_charset(uint8_t _cs)
{
	m_cmd_queue.push([=] () {
		if (_cs == 0 || _cs > 6) {
			return;
		}

		// If charset changed and emulation is currently for IBM
		if (_cs != m_config.ibm_charset &&
				(m_interpreter == MPS_PRINTER_INTERPRETER_IBMPP || m_interpreter == MPS_PRINTER_INTERPRETER_IBMGP))
		{
			if (m_charset) {
				m_charset = _cs;
			}
			PDEBUGF(LOG_V1, LOG_LPT, "IBM Graphics: current charset set to %d\n", m_charset);
		}

		m_config.ibm_charset = _cs;
	});
}

/**
 * IBM Graphics Printer single data interpreter automata
 *  
 * @param _input  Data byte to interpret
 */
void MpsPrinter::interpret_ibmgp(uint8_t _input)
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
					while (i < MPS_PRINTER_MAX_HTABULATIONS && m_htab[i] < m_margin_right)
					{
						if (m_htab[i] > m_head_x)
						{
							m_head_x = m_htab[i];
							break;
						}

						i++;
					}
				}
				break;

				case 0x0A:   // LF: line feed (no CR)
				case 0x0B:   // VT: vertical tabulation
					line_feed(false);
					break;

				case 0x0C:   // FF: form feed
					form_feed();
					break;

				case 0x0D:   // CR: carriage return (CR only, no LF)
					m_head_x = m_margin_left;
					break;

				case 0x0E:   // SO: Double width printing ON
					m_double_width = true;
					break;

				case 0x0F:   // SI: 17.1 chars/inch on
					m_step = MPS_PRINTER_STEP_CONDENSED;
					break;

				case 0x12:   // DC2: 17.1 chars/inch off
					m_step = MPS_PRINTER_STEP_PICA;
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
				case 0x0E:   // ESC SO: Double width printing on
					m_double_width = true;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x21:  // ESC ! : Select graphics layout types
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

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
					m_interline = 36;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x33:  // ESC 3 : Spacing = n/216"
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x34:  // ESC 4 : Italic ON
					m_italic = true;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x35:  // ESC 5 : Italic OFF
					m_italic = false;
					m_state = MPS_PRINTER_STATE_INITIAL;
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
					// to be done
					break;

				case 0x41:  // ESC A : Spacing = n/72"
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

				case 0x4D:  // ESC M : Print pitch ELITE ON
					m_step = MPS_PRINTER_STEP_ELITE;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x4E:  // ESC N : Defines bottom of from (BOF) in lines
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x4F:  // ESC O : Clear bottom of from (BOF)
					set_bof(0);
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
					// to be done
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

				case 0x5B:  // ESC [ : Set horizontal spacing
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

				case 0x72:  // ESC r : Color ink selection
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x78:  // ESC x : DRAFT/NLQ print mode selection
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				case 0x79:  // ESC y : Yellow ink
					m_color = MPS_PRINTER_COLOR_YELLOW;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x7E:  // ESC ~ : MPS-1230 extension
					m_state = MPS_PRINTER_STATE_ESC_PARAM;
					break;

				default:
					PDEBUGF(LOG_V1, LOG_LPT, "IBM Graphics: undefined escape sequence 0x%02X\n", _input);
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
			}
			break;

		// =======  Escape sequence parameters
		case MPS_PRINTER_STATE_ESC_PARAM:
			m_param_count++;
			switch(m_esc_command)
			{
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

				case 0x2D:  // ESC - : Underline on/off
					m_underline = _input & 0x01 ? true : false;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x33:  // ESC 3 : Spacing = n/216"
					m_interline = _input;
					m_state = MPS_PRINTER_STATE_INITIAL;
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
					m_interline = _input * 3;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x43:  // ESC C : Set form length
					if (m_param_count == 1 && _input != 0)
					{
						// in lines
						set_form_length(_input * m_interline);
						m_state = MPS_PRINTER_STATE_INITIAL;
					}
					else if (m_param_count > 1)
					{
						if(_input > 0 && _input < 23) {
							// in inches
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
							PWARNF(LOG_V1, LOG_LPT, "IBM Graphics: Draft + DLL not implemented\n");
							m_nlq = false;
							break;
						case 0x06:  // NLQ + DLL enabled (not implemented)
						case 0x36:
							PWARNF(LOG_V1, LOG_LPT, "IBM Graphics: NLQ + DLL not implemented\n");
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
						if (m_param_count - 2u >= m_param_build)
							m_state = MPS_PRINTER_STATE_INITIAL;
					}
					break;

				case 0x4E:  // ESC N : Defines bottom of from (BOF)
					if(_input >= 1 && _input <= 127) {
						set_bof(_input * m_interline);
					}
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x53:  // ESC S : Superscript/subscript printing
					m_script = _input & 0x01 ? MPS_PRINTER_SCRIPT_SUB : MPS_PRINTER_SCRIPT_SUPER;
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x55:  // ESC U : Mono/Bidirectional printing
					m_state = MPS_PRINTER_STATE_INITIAL;
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
						if (m_param_count - 2u >= m_param_build)
							m_state = MPS_PRINTER_STATE_INITIAL;
					}
					break;

				case 0x5A:  // ESC Z : Four times density BIM selection
					// First parameter is data length LSB
					if (m_param_count == 1)
					{
						m_param_build = _input;
						m_bim_density  = m_bim_Z_density;
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
						if (m_param_count - 2u >= m_param_build)
							m_state = MPS_PRINTER_STATE_INITIAL;
					}
					break;

				case 0x5B:  // ESC [ : Set horizontal spacing
				{
					uint8_t new_step = _input & 0x0F;
					if (new_step < 7) {
						m_step = new_step;
					}
					m_state = MPS_PRINTER_STATE_INITIAL;
				}
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
							break;
					}
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;

				case 0x78:  // ESC x : DRAFT/NLQ print mode selection
					m_nlq = _input & 0x01 ? true : false;
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
							case 2:         // ESC ~ 2 n : reverse ON/OFF
							case '2':
								m_reverse = (_input & 1) ? true : false;
								break;

							case 3:         // ESC ~ 3 n : select pitch
							case '3':
							{
								uint8_t new_step = _input & 0x0F;
								if (new_step < 7)
									m_step = new_step;
							}
							break;

							case 4:         // ESC ~ 4 n : slashed zero ON/OFF
							case '4':
								// ignored
								break;

							case 5:         // ESC ~ 5 n : switch EPSON, Commodore, Proprinter, Graphics Printer
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
					PDEBUGF(LOG_V1, LOG_LPT, "IBM Graphics: undefined escape sequence 0x%02X parameter %d\n", m_esc_command, _input);
					m_state = MPS_PRINTER_STATE_INITIAL;
					break;
			}
			break;

		default:
			PDEBUGF(LOG_V1, LOG_LPT, "IBM Graphics: undefined state %d\n", m_state);
			m_state = MPS_PRINTER_STATE_INITIAL;
			break;
	}
}

