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

//  VGA Digital-to-Analog Converter
struct VGA_DAC
{
	uint8_t write_data_register; // Palette Address Register (write mode)
	uint8_t write_data_cycle;    // 0,1,2: current write data cycle
	uint8_t read_data_register;  // Palette Address Register (read mode)
	uint8_t read_data_cycle;     // 0,1,2: current read data cycle
	uint8_t state;               // DAC State Register
	struct {
		uint8_t red;             // Palette entry red value (6-bit)
		uint8_t green;           // Palette entry green value (6-bit)
		uint8_t blue;            // Palette entry blue value (6-bit)
	} palette[256];              // Palette Data registers
	uint8_t pel_mask;            // PEL Mask Register

	void registers_to_textfile(FILE *_txtfile);
};
