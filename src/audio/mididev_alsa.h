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

#ifndef IBMULATOR_MIDIDEV_ALSA_H
#define IBMULATOR_MIDIDEV_ALSA_H

#if HAVE_ALSA

#define USE_DRAIN 1
#include <alsa/asoundlib.h>

class MIDIDev_ALSA : public MIDIDev
{
private:
	int m_seq_client;
	int m_seq_port;
	int m_this_port;
	snd_seq_t *m_seq_handle;
	
public:
	MIDIDev_ALSA();
	~MIDIDev_ALSA();
	void open(std::string _conf);
	bool is_open() {
		return (m_seq_handle != nullptr);
	}
	void close();
	void send_event(uint8_t _msg[3]);
	void send_sysex(uint8_t * _sysex, int _len);

private:
	void parse_addr(std::string _arg);
	void show_port_list();
	void send_event(snd_seq_event_t &_ev, bool _flush);
};

#else

class MIDIDev_ALSA : public MIDIDev
{
};

#endif

#endif