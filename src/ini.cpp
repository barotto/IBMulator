/*
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

#include "ibmulator.h"
#include "utils.h"
#include "filesys.h"
#include "ini.h"

#include <algorithm>
#include <regex>
#include <cctype>
#include <cstdlib>
#include "ini/ini.h"


void INIFile::parse(const std::string &_filename, bool _quoted_values)
{
	m_quoted_values = _quoted_values;
	m_error = ini_parse(FileSys::to_native(_filename).c_str(), value_handler, this);
	if(m_error != 0) {
		throw std::exception();
	}
	m_parsed_file = _filename;
}

void INIFile::parse(const std::list<std::string> &_content, const std::string &_filename, bool _quoted_values)
{
	auto file = FileSys::make_tmpfile();

	for(auto & line : _content) {
		if(fwrite(line.c_str(), line.size(), 1, file.get()) != 1) {
			throw std::runtime_error("cannot write to temporary file");
		}
	}
	fseek(file.get(), 0, SEEK_SET);

	m_quoted_values = _quoted_values;
	m_error = ini_parse_file(file.get(), value_handler, this);
	if(m_error != 0) {
		throw std::runtime_error(str_format("parse error at line %d", m_error));
	}

	m_parsed_file = _filename;
}

int INIFile::get_error()
{
	return m_error;
}

void INIFile::apply_defaults(const INIFile &_ini)
{
	for(auto & [secname, section] : _ini.m_values) {
		if(m_values.find(secname) == m_values.end()) {
			m_values[secname] = section;
		} else {
			for(auto & [key, value] : section) {
				if(m_values[secname].find(key) == m_values[secname].end()) {
					m_values[secname][key] = value;
				}
			}
		}
	}
}

int INIFile::parse_int(const std::string &_str)
{
	try {
		return str_parse_int_num(_str);
	} catch(std::runtime_error &) {
		PDEBUGF(LOG_V1, LOG_PROGRAM, "'%s' is not an integer number\n", _str.c_str());
		throw;
	}
}

double INIFile::parse_real(const std::string &_str)
{
	try {
		return str_parse_real_num(_str);
	} catch(std::runtime_error &) {
		PDEBUGF(LOG_V1, LOG_PROGRAM, "'%s' is not a valid number\n", _str.c_str());
		throw;
	}
}

bool INIFile::parse_bool(std::string _str)
{
	// Convert to lower case to make string comparisons case-insensitive
	_str = str_to_lower(_str);
	if (_str == "true" || _str == "yes" || _str == "on" || _str == "1") {
		return true;
	} else if (_str == "false" || _str == "no" || _str == "off" || _str == "0") {
		return false;
	} else {
		PDEBUGF(LOG_V1, LOG_PROGRAM, "'%s' is not a boolean\n", _str.c_str());
		throw std::exception();
	}
}

std::vector<std::string> INIFile::parse_tokens(std::string _str, std::string _regex_sep)
{
	return str_parse_tokens(_str, _regex_sep);
}

std::string INIFile::get_value(ini_file_t &_values, const std::string &section, const std::string &name)
{
	std::string s = make_key(section);
	std::string n = make_key(name);
	std::string value;
	if(_values.count(s)) {
		ini_section_t sec = _values[s];
		if(sec.count(n)) {
			value = sec[n];
		} else {
			PDEBUGF(LOG_V2, LOG_PROGRAM, "ini key '%s' in section [%s] is not present\n", name.c_str(), section.c_str());
			throw std::exception();
		}
	} else {
		PDEBUGF(LOG_V2, LOG_PROGRAM, "ini section [%s] is not present\n", section.c_str());
		throw std::exception();
	}
	return value;
}

std::string INIFile::get_value(const std::string &_section, const std::string &_name)
{
	return get_value(m_values, _section, _name);
}

bool INIFile::is_key_present(const std::string &_section, const std::string &_name)
{
	if(m_values.find(_section) != m_values.end()) {
		return m_values[_section].find(_name) != m_values[_section].end();
	}
	return false;
}

int INIFile::try_int(const std::string &_section, const std::string &_name)
{
	std::string valstr = get_value(_section, _name);
	return parse_int(valstr);
}

int INIFile::get_int(const std::string &_section, const std::string &_name)
{
	int value;
	try {
		value = try_int(_section, _name);
	} catch(std::exception &e) {
		PERRF(LOG_PROGRAM, "unable to get integer value for [%s]:%s\n", _section.c_str(), _name.c_str());
		throw;
	}
	return value;
}

int INIFile::get_int(const std::string &_section, const std::string &_name, int _default)
{
	try {
		return try_int(_section, _name);
	} catch(std::exception &e) {
		return _default;
	}
}

void INIFile::set_int(const std::string &_section, const std::string &_name, int _value)
{
	m_values[make_key(_section)][make_key(_name)] = std::to_string(_value);
}

double INIFile::try_real(const std::string &_section, const std::string &_name)
{
	std::string valstr = get_value(_section, _name);
	double value = parse_real(valstr);
	return value;
}

double INIFile::get_real(const std::string &_section, const std::string &_name)
{
	double value = 0.0;
	try {
		value = try_real(_section, _name);
	} catch(std::exception &e) {
		PERRF(LOG_PROGRAM, "unable to get real value for [%s]:%s\n", _section.c_str(), _name.c_str());
		throw;
	}
	return value;
}

double INIFile::get_real(const std::string &_section, const std::string &_name, double _default)
{
	try {
		return try_real(_section, _name);
	} catch(std::exception &e) {
		return _default;
	}
}

void INIFile::set_real(const std::string &_section, const std::string &_name, double _value)
{
	m_values[make_key(_section)][make_key(_name)] = std::to_string(_value);
}

bool INIFile::try_bool(const std::string &_section, const std::string &_name)
{
	std::string valstr = get_value(_section, _name);
	return parse_bool(valstr);
}

bool INIFile::get_bool(const std::string &_section, const std::string &_name)
{
	bool value;
	try {
		value = try_bool(_section, _name);
	} catch(std::exception &e) {
		PERRF(LOG_PROGRAM, "unable to get bool value for [%s]:%s\n", _section.c_str(), _name.c_str());
		throw;
	}
	return value;
}

bool INIFile::get_bool(const std::string &_section, const std::string &_name, bool _default)
{
	try {
		return try_bool(_section, _name);
	} catch(std::exception &e) {
		return _default;
	}
}

void INIFile::set_bool(const std::string &_section, const std::string &_name, bool _value)
{
	m_values[make_key(_section)][make_key(_name)] = _value?"yes":"no";
}

std::string INIFile::get_string(const std::string &_section, const std::string &_name)
{
	std::string val;
	try {
		val = get_value(_section, _name);
	} catch(std::exception &e) {
		PERRF(LOG_PROGRAM, "unable to get string for [%s]:%s\n", _section.c_str(), _name.c_str());
		throw;
	}
	return val;
}

std::string INIFile::get_string(const std::string &_section, const std::string &_name, const std::string &_default)
{
	try {
		return get_value(_section, _name);
	} catch(std::exception &e) {
		return _default;
	}
}

std::string INIFile::get_string(const std::string &_section, const std::string &_name,
		const std::set<std::string> _allowed,
		const std::string &_default)
{
	std::string value;
	try {
		value = get_value(_section, _name);
	} catch(std::exception &e) {
		return _default;
	}
	if(_allowed.find(value) == _allowed.end()) {
		return _default;
	}
	return value;
}

void INIFile::set_string(const std::string &_section, const std::string &_name, std::string _value)
{
	m_values[make_key(_section)][make_key(_name)] = _value;
}

unsigned INIFile::get_enum(const std::string &_section, const std::string &_name, const ini_enum_map_t &_enum_map)
{
	std::string enumstr;
	try {
		enumstr = get_value(_section, _name);
	} catch(std::exception &e) {
		PERRF(LOG_PROGRAM, "Unable to get string for [%s]:%s\n", _section.c_str(), _name.c_str());
		throw;
	}

	auto enumvalue = _enum_map.find(str_to_lower(enumstr));
	if(enumvalue == _enum_map.end()) {
		PERRF(LOG_PROGRAM, "Invalid value '%s' for [%s]:%s\n",
				enumstr.c_str(), _section.c_str(), _name.c_str());
		throw std::exception();
	}
	return enumvalue->second;
}

unsigned INIFile::get_enum(const std::string &_section, const std::string &_name,
		const ini_enum_map_t &_enum_map, unsigned _default)
{
	std::string enumstr;
	try {
		enumstr = get_value(_section, _name);
	} catch(std::exception &e) {
		return _default;
	}
	auto enumvalue = _enum_map.find(str_to_lower(enumstr));
	if(enumvalue == _enum_map.end()) {
		return _default;
	}
	return enumvalue->second;
}

unsigned INIFile::get_enum_quiet(const std::string &_section, const std::string &_name,
		const ini_enum_map_t &_enum_map)
{
	std::string enumstr = get_value(_section, _name);
	auto enumvalue = _enum_map.find(str_to_lower(enumstr));
	if(enumvalue == _enum_map.end()) {
		throw std::exception();
	}
	return enumvalue->second;
}

std::string INIFile::make_key(std::string name)
{
	// Convert to lower case to make section/name lookups case-insensitive
	// no, don't do this, keep it case sensitive
	//std::transform(name.begin(), name.end(), name.begin(), ::tolower);
	return name;
}

int INIFile::value_handler(void* _user, const char* _section, const char* _name, const char* _value)
{
	INIFile* reader = static_cast<INIFile*>(_user);

	std::string s = make_key(_section);
	std::string n = make_key(_name);
	std::string v = _value;
	if(reader->m_quoted_values && v.front() == '"' && v.back() == '"') {
		v = v.substr(1, v.length()-2);
	}
	ini_section_t & sec = reader->m_values[s];
	sec[n] = v;

	PDEBUGF(LOG_V2, LOG_PROGRAM, "config [%s]:%s=%s\n", s.c_str(), n.c_str(), v.c_str());

	return 1;
}
