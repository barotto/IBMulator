/*
 * Copyright (C) 2023  Marco Bortolin
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

#ifndef IBMULATOR_INI_H
#define IBMULATOR_INI_H

#include <map>
#include <string>
#include <vector>
#include <set>

typedef std::map<std::string, unsigned> ini_enum_map_t;
typedef std::map<std::string, std::string> ini_section_t;
typedef std::map<std::string, ini_section_t> ini_file_t;

class INIFile
{
protected:
	std::string m_parsed_file;
	int m_error = 0;
	bool m_quoted_values = false;

	ini_file_t m_values;

	static std::string make_key(std::string _name);
	static int value_handler(void* user, const char* section, const char* name, const char* value);

	std::string get_value(ini_file_t &_values, const std::string &_section, const std::string &_name);

public:
	virtual ~INIFile() {}

	std::string get_path() const { return m_parsed_file; }

	void parse(const std::string &_filename, bool _quoted_values=false);
	void parse(const std::list<std::string> &_content, const std::string &_filename, bool _quoted_values=false);
	
	static int parse_int(const std::string &_str);
	static double parse_real(const std::string &_str);
	static bool parse_bool(std::string _str);
	static std::vector<std::string> parse_tokens(std::string _str, std::string _regex_sep);

	ini_file_t & get_values() { return m_values; }
	virtual std::string get_value(const std::string &_section, const std::string &_name);

	// Return the result of ini_parse(), i.e., 0 on success, line number of
	// first error on parse error, or -1 on file open error.
	int get_error();

	bool is_key_present(const std::string &section, const std::string &name);

	int try_int(const std::string &section, const std::string &name);
	int get_int(const std::string &section, const std::string &name);
	int get_int(const std::string &section, const std::string &name, int _default);
	double try_real(const std::string &section, const std::string &name);
	double get_real(const std::string &section, const std::string &name);
	double get_real(const std::string &section, const std::string &name, double _default);
	bool try_bool(const std::string &section, const std::string &name);
	bool get_bool(const std::string &section, const std::string &name);
	bool get_bool(const std::string &section, const std::string &name, bool _default);
	std::string get_string(const std::string &_section, const std::string &_name);
	std::string get_string(const std::string &_section, const std::string &_name, const std::string &_default);
	std::string get_string(const std::string &_section, const std::string &_name, const std::set<std::string> _allowed, const std::string &_default);
	unsigned get_enum(const std::string &_section, const std::string &_name, const ini_enum_map_t &_enum_map);
	unsigned get_enum(const std::string &_section, const std::string &_name, const ini_enum_map_t &_enum_map, unsigned _default);
	unsigned get_enum_quiet(const std::string &_section, const std::string &_name, const ini_enum_map_t &_enum_map);

	void set_int(const std::string &_section, const std::string &_name, int _value);
	void set_real(const std::string &_section, const std::string &_name, double _value);
	void set_bool(const std::string &section, const std::string &name, bool _value);
	void set_string(const std::string &_section, const std::string &_name, std::string _value);

	// Copies values from _ini only if they are not defined.
	void apply_defaults(const INIFile &_ini);
};

#endif

