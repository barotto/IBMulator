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

#include <string>
#include <sstream>
#include <array>
#include <algorithm>
#include <regex>
#include <numeric>
#include "utils.h"

std::string str_implode(const std::vector<std::string> &_list, const std::string &_delim)
{
	// thanks, random internet stranger @stackoverflow!
	return std::accumulate(_list.begin(), _list.end(), std::string(), 
		[&](const std::string& a, const std::string& b) -> std::string { 
			return a + (a.length() > 0 ? _delim : "") + b; 
		} );
}

void str_replace_all(std::string &_str, const std::string &_search, const std::string &_replace)
{
	std::string::size_type i = _str.find(_search);
	while(i != std::string::npos) {
		_str.replace(i, _search.length(), _replace);
		i = _str.find(_search, i+_replace.length());
	}
}

std::string str_to_lower(std::string _str)
{
	std::transform(_str.begin(), _str.end(), _str.begin(), ::tolower);
	return _str;
}

std::string str_to_upper(std::string _str)
{
	std::transform(_str.begin(), _str.end(), _str.begin(), ::toupper);
	return _str;
}

std::string str_trim(std::string _str)
{
	const char* ws = " \t\n\r\f\v";
	
	_str.erase(0, _str.find_first_not_of(ws));
	_str.erase(_str.find_last_not_of(ws) + 1);
	
	return _str;
}

std::string str_compress_spaces(std::string _str)
{
	return std::regex_replace(_str, std::regex("[' ']{2,}"), " ");
}

std::vector<std::string> str_parse_tokens(std::string _str, std::string _regex_sep)
{
	std::regex re(_regex_sep);
	std::vector<std::string> tokens;
	std::sregex_token_iterator it{_str.begin(), _str.end(), re, -1}, end;
	for(; it != end; it++) {
		auto s = str_trim(*it);
		if(!s.empty()) {
			tokens.push_back(s);
		}
	}
	return tokens;
}

std::string str_format_time(time_t _time, const std::string &_fmt)
{
	std::stringstream s;
	s.imbue(std::locale(""));
	auto &tmput = std::use_facet<std::time_put<char>>(s.getloc());
	std::tm *my_time = std::localtime(&_time);
	std::string fmt = "%x %X";
	tmput.put({s}, s, ' ', my_time, &_fmt[0], &_fmt[0]+_fmt.size());
	return s.str();
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
	if(!s.empty()) {
		s.pop_back();
	}
	return s;
}

const char *register_to_string(uint8_t _register,
	const std::vector<std::pair<int, std::string> > &_fields)
{
	std::ostringstream sstr;
	thread_local static std::string s;

	sstr << std::hex;
	int pos = 0;
	for(auto const &p : _fields) {
		int bits = p.first;
		int mask = 0;
		while(bits--) {
			mask |= 1<<bits;
		}
		if(!p.second.empty()) {
			sstr << p.second << "=" << (_register >> pos & mask) << " ";
		}
		pos += p.first;
		if(pos > 7) {
			break;
		}
	}
	s = sstr.str();
	if(!s.empty()) {
		s.pop_back();
	}
	return s.c_str();
}
