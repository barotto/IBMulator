/*mee
 * Copyright (C) 2025  Marco Bortolin
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

#ifndef IBMULATOR_TTS_DEV_H
#define IBMULATOR_TTS_DEV_H

#include "tts_format.h"

class TTSDev
{
public:
	enum class Type {
		UNKNOWN,
		SYNTH,
		FILE
	};

protected:
	Type m_type;
	std::string m_name;
	std::string m_conf;
	std::unique_ptr<TTSFormat> m_format;
	int m_volume; // -10 .. +10
	int m_rate; // -10 .. +10
	int m_pitch; // -10 .. +10

public:
	TTSDev(Type _type, const char *_name) : m_type(_type), m_name(_name),
		m_volume(0), m_rate(0), m_pitch(0) {}
	virtual ~TTSDev() {}

	virtual void open(const std::vector<std::string> &_conf) = 0;
	virtual bool is_open() const { return false; }
	virtual void speak(const std::string &_text, bool _purge) = 0;
	virtual bool is_speaking() const { return false; }
	virtual void stop() {}
	virtual int volume() const { return m_volume; }
	virtual int rate() const { return m_rate; }
	virtual int pitch() const { return m_pitch; }
	virtual bool set_volume(int) { return false; }  // -10 .. 10
	virtual bool set_rate(int) { return false; } // -10 .. 10
	virtual bool set_pitch(int) { return false; } // -10 .. 10
	virtual void close() {}

	virtual const char * name() const { return m_name.c_str(); };
	virtual const std::string & conf() const { return m_conf; }
	virtual Type type() const { return m_type; }
	virtual const TTSFormat * format() const { return m_format.get(); }
};

#endif