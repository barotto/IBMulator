/*
 * Copyright (C) 2020  Marco Bortolin
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

#ifndef IBMULATOR_MIDIDEV_H
#define IBMULATOR_MIDIDEV_H

class MIDI;

class MIDIDev
{
public:
	enum Type {
		UNKNOWN, LA, GS, GM, XG
	};
	
protected:
	std::string m_conf;
	std::string m_name;
	Type m_type;
	MIDI *m_instance;
	
public:	
	MIDIDev(MIDI *_instance);
	virtual ~MIDIDev() {}
	virtual void open(std::string _conf) {
		UNUSED(_conf);
		throw std::exception();
	}
	virtual bool is_open() {
		return false;
	}
	virtual void close() {}
	virtual void send_event(uint8_t _msg[3]) {
		UNUSED(_msg);
	}
	virtual void send_sysex(uint8_t *_sysex, int _len) {
		UNUSED(_sysex);
		UNUSED(_len);
	}
	virtual const char * name() {
		return m_name.c_str();
	};

	const std::string & conf() const {
		return m_conf;
	}
	Type type() const {
		return m_type;
	}
	
	void reset();
};

#endif