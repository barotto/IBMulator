/*
 * Copyright (C) 2015-2023  Marco Bortolin
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
#include <iomanip>
#include "utils.h"
#ifdef _WIN32
#include "wincompat.h"
#endif

int str_parse_int_num(const std::string &_str)
{
	const char* value = _str.c_str();
	char* end;
	// This parses "1234" (decimal) and also "0x4D2" (hex)
	int n = strtol(value, &end, 0);
	if(end <= value) {
		throw std::runtime_error(str_format("not an integer number: '%s'", _str.c_str()));
	}
	return n;
}

double str_parse_real_num(const std::string &_str)
{
	const char *value = _str.c_str();
	char *end;
	double n = strtod(value, &end);
	if(end <= value) {
		throw std::runtime_error(str_format("not a real number: '%s'", _str.c_str()));
	}
	return n;
}

std::string str_implode(const std::vector<std::string> &_list, const std::string &_delim)
{
	// thanks, random internet stranger @stackoverflow!
	return std::accumulate(_list.begin(), _list.end(), std::string(), 
		[&](const std::string& a, const std::string& b) -> std::string { 
			return a + (a.length() > 0 ? _delim : "") + b; 
		} );
}

std::string str_implode(const std::vector<const char*> &_list, const std::string &_delim)
{
	std::vector<std::string> list;
	for(auto s : _list) {
		list.push_back(std::string(s));
	}
	return str_implode(list, _delim);
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
	static const std::regex spaces_re("[' ']{2,}");
	return std::regex_replace(_str, spaces_re, " ");
}

std::vector<std::string> str_parse_tokens_re(std::string _str, const std::regex &_regex)
{
	std::vector<std::string> tokens;
	std::sregex_token_iterator it{_str.begin(), _str.end(), _regex, -1}, end;
	for(; it != end; it++) {
		auto s = str_trim(*it);
		if(!s.empty()) {
			tokens.push_back(s);
		}
	}
	return tokens;
}

std::vector<std::string> str_parse_tokens(std::string _str, std::string _regex_sep)
{
	return str_parse_tokens_re(_str, std::regex(_regex_sep));
}

std::string::const_iterator str_find_ci(const std::string &_haystack, const std::string &_needle)
{
	return std::search(
		_haystack.begin(), _haystack.end(),
		_needle.begin(), _needle.end(),
		[](unsigned char ch1, unsigned char ch2) { return std::toupper(ch1) == std::toupper(ch2); }
	);
}

std::string str_format_time(time_t _time, const std::string &_fmt)
{
#if 1

	std::string old_locale(setlocale(LC_TIME, NULL));
	setlocale(LC_TIME, "");
	std::string s(100,0);
	size_t size = std::strftime(s.data(), 100, _fmt.c_str(), std::localtime(&_time));
	setlocale(LC_TIME, old_locale.c_str());
	if(size == 0) {
		return "";
	}
	s.resize(size);
	return s;

#else

	// In MinGW the only currently supported locale is "C".
	// Practically C++ locales work only on Linux.
	// Keeping this implementation here waiting for better times.

	std::stringstream s;
	try {
		s.imbue(std::locale(""));
	} catch(std::runtime_error &) {
		// It throws an exception when compiled with MinGW and run in a terminal.
	}
	auto &tmput = std::use_facet<std::time_put<char>>(s.getloc());
	std::tm *my_time = std::localtime(&_time);
	tmput.put({s}, s, ' ', my_time, &_fmt[0], &_fmt[0]+_fmt.size());
	return s.str();

#endif
}

std::string str_to_html(std::string _text, bool _nbsp)
{
	if(_nbsp) {
		str_replace_all(_text, " ", "&nbsp;");
	}
	str_replace_all(_text, "<", "&lt;");
	str_replace_all(_text, ">", "&gt;");
	str_replace_all(_text, "\n", "<br />");
	return _text;
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

std::string bytearray_to_string(const uint8_t *_data, unsigned _len)
{
	std::stringstream ss;
	ss << std::setfill('0');
	ss << "[";
	for(unsigned i=0; i<_len; i++) {
		ss << std::hex << std::setw(2) << int(_data[i]);
		if(i<_len-1) {
			ss << "|";
		}
	}
	ss << "]";
	return ss.str();
}

std::string get_error_string(int _error_id)
{
#ifdef _WIN32

	if(_error_id == 0) {
		_error_id = ::GetLastError();
		if(_error_id == NO_ERROR) {
			return std::string("No error");
		}
	}

	LPWSTR buff = nullptr;
	DWORD size = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, //lpSource
			_error_id, //dwMessageId
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), //dwLanguageId
			(LPWSTR)&buff, //lpBuffer
			0, //nSize
			NULL //Arguments
		);
	std::wstring message(buff, size);
	LocalFree(buff);

	return str_trim(utf8::narrow(buff));

#else

	if(_error_id == 0) {
		_error_id = errno;
		if(_error_id == 0) {
			return std::string("No error");
		}
	}
	return std::string(::strerror(errno));

#endif
}