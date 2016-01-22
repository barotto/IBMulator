/*
 * Copyright (C) 2015, 2016  Marco Bortolin
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

#include <Rocket/Core/Types.h>
#include "format.h"


const Rocket::Core::String & format_uint16(uint16_t _value)
{
	static Rocket::Core::String str;
	str.FormatString(6,"%u",_value);
	return str;
}

const Rocket::Core::String & format_bit(uint _value)
{
	static Rocket::Core::String str;
	str.FormatString(2,"%d",_value);
	return str;
}

const Rocket::Core::String & format_hex16(uint16_t _value)
{
	static Rocket::Core::String str;
	str.FormatString(5,"%04X",_value);
	return str;
}

const Rocket::Core::String & format_hex24(uint32_t _value)
{
	static Rocket::Core::String str;
	str.FormatString(7,"%06X",_value);
	return str;
}

const Rocket::Core::String & format_hex32(uint32_t _value)
{
	static Rocket::Core::String str;
	str.FormatString(9,"%08X",_value);
	return str;
}

const char *byte_to_binary(uint8_t _value, char _buf[9])
{
    _buf[0] = '\0';

    for(int i = 128; i > 0; i >>= 1) {
        strcat(_buf, ((_value & i) == i) ? "1" : "0");
    }

    return _buf;
}

const char *nibble_to_binary(uint8_t _value, char _buf[5])
{
    _buf[0] = '\0';

    for(int i = 8; i > 0; i >>= 1) {
        strcat(_buf, ((_value & i) == i) ? "1" : "0");
    }

    return _buf;
}

const Rocket::Core::String & format_bin4(uint _value)
{
	static Rocket::Core::String str;
	char nibble[5];

	nibble_to_binary(_value&0xF, nibble);

	str.FormatString(5,"%s",nibble);

	return str;
}

const Rocket::Core::String & format_bin8(uint _value)
{
	static Rocket::Core::String str;
	char byte[9];

	byte_to_binary(uint8_t(_value), byte);

	str.FormatString(9,"%s",byte);

	return str;
}

const Rocket::Core::String & format_bin16(uint _value)
{
	static Rocket::Core::String str;
	char byte0[9], byte1[9];

	byte_to_binary(uint8_t(_value), byte0);
	byte_to_binary(uint8_t(_value>>8), byte1);

	str.FormatString(17,"%s%s",byte1,byte0);

	return str;
}

const Rocket::Core::String & format_words(uint8_t *_buf, uint _len)
{
	static Rocket::Core::String str, word;
	str = "";
	int byte0, byte1;
	while(_len) {
		byte0 = *(_buf++);
		_len--;
		if(_len) {
			byte1 = *(_buf++);
			_len--;
		} else {
			byte1 = 0;
		}
		word.FormatString(5,"%02X%02X",byte0,byte1);
		str += word;
		if(_len) {
			str += " ";
		}
	}

	return str;
}


const Rocket::Core::String & format_words_string(uint8_t *_buf, uint _len)
{
	static Rocket::Core::String str;
	str = "";
	char byte0;
	while(_len--) {
		byte0 = char(*_buf++);
		if(byte0>=32 && byte0<=126) {
			if(byte0=='<') {
				str += "&lt;";
			} else if(byte0=='>') {
				str += "&gt;";
			} else {
				str += byte0;
			}
		} else {
			str += ".";
		}
	}

	return str;
}
