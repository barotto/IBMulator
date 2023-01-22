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

#ifndef IBMULATOR_SHADER_EXCEPTION
#define IBMULATOR_SHADER_EXCEPTION


class ShaderExc : public std::runtime_error {
public:
	ShaderExc(const std::string &_message) : std::runtime_error(_message) {}
	
	virtual void log_print(unsigned _facility) = 0;
};

class ShaderLinkExc : public ShaderExc {
	int m_program;
public:
	ShaderLinkExc(const std::string &_message, int _program)
	: ShaderExc(_message), m_program(_program) {}
	
	int program() const { return m_program; }
	void log_print(unsigned _facility);
};

class ShaderCompileExc : public ShaderExc
{
	std::string m_progname;
	std::list<std::string> m_progsrc;
	int m_line;

public:
	ShaderCompileExc(const std::string &_message, const std::string &_progname, const std::list<std::string> &_progsrc, int _line)
		: ShaderExc(_message), m_progname(_progname), m_line(_line) {
		m_progsrc = std::move(_progsrc);
	}

	const char * progname() const { return m_progname.c_str(); }
	const std::list<std::string> & progsrc() const { return m_progsrc; }
	int line() const { return m_line; }

	virtual void log_print(unsigned _facility);
};

class ShaderPresetExc : public ShaderExc
{
	std::string m_name;
	std::list<std::string> m_data;
	int m_line;

public:
	ShaderPresetExc(const std::string &_message, const std::string &_name, const std::list<std::string> &_data, int _line)
	 : ShaderExc(_message), m_name(_name), m_line(_line) {
		m_data = std::move(_data);
	}

	virtual void log_print(unsigned _facility);
};

#endif