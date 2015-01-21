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

#ifndef IBMULATOR_GUI_FORMAT_H
#define IBMULATOR_GUI_FORMAT_H

const Rocket::Core::String & format_uint16(uint16_t _value);
const Rocket::Core::String & format_bit(uint _value);
const Rocket::Core::String & format_hex16(uint16_t _value);
const Rocket::Core::String & format_hex24(uint32_t _value);
const Rocket::Core::String & format_hex32(uint32_t _value);
const char *byte_to_binary(uint8_t _value, char _buf[9]);
const Rocket::Core::String & format_bin16(uint _value);
const Rocket::Core::String & format_words(uint8_t *_buf, uint _len);
const Rocket::Core::String & format_words_string(uint8_t *_buf, uint _len);

#endif
