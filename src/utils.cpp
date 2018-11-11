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

#include <string>
#include <array>
#include "utils.h"

void str_replace_all(std::string &_str, const std::string &_search, const std::string &_replace)
{
	std::string::size_type i = _str.find(_search);
	while(i != std::string::npos) {
		_str.replace(i, _search.length(), _replace);
		i = _str.find(_search, i+_replace.length());
	}
}

std::string bitfield_to_string(uint8_t _bitfield,
		const std::array<std::string, 8> &_set_names)
{
	return bitfield_to_string(_bitfield, _set_names, {"","","","","","","",""});
}

std::string bitfield_to_string(uint8_t _bitfield,
		const std::array<std::string, 8> &_set_names,
		const std::array<std::string, 8> &_clear_names)
{
	std::string s;
	for(int i=7; i>=0; i--) {
		if(_bitfield & 1<<i) {
			if(!_set_names[i].empty()) {
				s += _set_names[i] + " ";
			}
		} else {
			if(!_clear_names[i].empty()) {
				s += _clear_names[i] + " ";
			}
		}
	}
	s.pop_back();
	return s;
}
