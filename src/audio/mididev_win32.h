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

#ifndef IBMULATOR_MIDIDEV_WIN32_H
#define IBMULATOR_MIDIDEV_WIN32_H

#if HAVE_WINMM

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

class MIDIDev_Win32 : public MIDIDev
{
	int m_devid;
	std::string m_devname;
	HANDLE m_event;
	HMIDIOUT m_out;
	
public:
	MIDIDev_Win32(MIDI *_instance);
	~MIDIDev_Win32();
	void open(std::string _conf);
	bool is_open() {
		return (m_devid >= 0);
	}
	void close();
	void send_event(uint8_t _msg[3]);
	void send_sysex(uint8_t * _sysex, int _len);
	
private:
	void find_device(std::string _arg);
	void list_available_devices();

};

#else

class MIDIDev_Win32 : public MIDIDev
{
public:
	MIDIDev_Win32(MIDI *_instance) : MIDIDev(_instance) {}
};

#endif

#endif