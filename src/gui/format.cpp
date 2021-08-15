/*
 * Copyright (C) 2015-2021  Marco Bortolin
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

#include "utils.h"
#include "format.h"
#include <cstring>


const std::string & format_uint16(uint16_t _value)
{
	static std::string str("65535");
	str_format(str, "%u", _value);
	return str;
}

const std::string & format_bit(unsigned _value)
{
	static std::string str("1");
	str_format(str, "%u", _value);
	return str;
}

const std::string & format_hex8(uint8_t _value)
{
	static std::string str("FF");
	str_format_sized(str, "%02X", _value);
	return str;
}

const std::string & format_hex16(uint16_t _value)
{
	static std::string str("FFFF");
	str_format_sized(str, "%04X", _value);
	return str;
}

const std::string & format_hex24(uint32_t _value)
{
	static std::string str("FFFFFF");
	str_format_sized(str, "%06X", _value);
	return str;
}

const std::string & format_hex32(uint32_t _value)
{
	static std::string str("FFFFFFFF");
	str_format_sized(str, "%08X", _value);
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

const std::string & format_bin4(unsigned _value)
{
	static std::string str("1111");
	nibble_to_binary(_value&0xF, str.data());
	return str;
}

const std::string & format_bin8(unsigned _value)
{
	static std::string str("11111111");
	byte_to_binary(uint8_t(_value), str.data());
	return str;
}

const std::string & format_bin16(unsigned _value)
{
	static std::string str("0000000011111111");
	byte_to_binary(uint8_t(_value), &str[0]);
	byte_to_binary(uint8_t(_value>>8), &str[8]);
	return str;
}

const std::string & format_words(uint8_t *_buf, unsigned _len)
{
	static std::string str, word("FFFF");
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
		str_format_sized(word, "%02X%02X", byte0, byte1);
		str += word;
		if(_len) {
			str += " ";
		}
	}

	return str;
}

const std::string & format_words_string(uint8_t *_buf, unsigned _len)
{
	static std::string str;
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
