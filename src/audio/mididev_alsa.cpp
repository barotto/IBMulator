/*
 * Copyright (C) 2002-2020  The DOSBox Team
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

#include "ibmulator.h"

#if HAVE_ALSA

#include "appconfig.h"
#include "mididev.h"
#include "mididev_alsa.h"
#include <sstream>
#include <regex>

MIDIDev_ALSA::MIDIDev_ALSA(MIDI *_instance)
:
MIDIDev(_instance),
m_seq_client(-1),
m_seq_port(-1),
m_this_port(-1),
m_seq_handle(nullptr)
{
	m_name = "ALSA " + m_name;
}

MIDIDev_ALSA::~MIDIDev_ALSA()
{
}

void MIDIDev_ALSA::parse_addr(std::string _arg)
{
	if(_arg.empty()) {
		throw std::exception();
	}

	if(_arg[0] == 's' || _arg[0] == 'S') {
		m_seq_client = SND_SEQ_ADDRESS_SUBSCRIBERS;
		m_seq_port = 0;
		return;
	}

	std::regex re("^([0-9]*):([0-9]*)$", std::regex::ECMAScript|std::regex::icase);
	std::smatch match;
	if(!std::regex_match(_arg, match, re) || match.size() != 3) {
		throw std::exception();
	}
	
	m_seq_client = std::stoi(match[1].str());
	m_seq_port = std::stoi(match[2].str());
	
	PDEBUGF(LOG_V2, LOG_MIDI, "%s: port %d:%d\n", name(), m_seq_client, m_seq_port);
}

void MIDIDev_ALSA::show_port_list()
{
	snd_seq_client_info_t *cinfo;
	snd_seq_client_info_alloca(&cinfo);
	snd_seq_client_info_set_client(cinfo, -1);
	PINFOF(LOG_V0, LOG_MIDI, " Port     %-30.30s    %s\n", "Client name", "Port name");

	while(snd_seq_query_next_client(m_seq_handle, cinfo) >= 0) {
		int client = snd_seq_client_info_get_client(cinfo);
		snd_seq_port_info_t *pinfo;
		snd_seq_port_info_alloca(&pinfo);
		snd_seq_port_info_set_client(pinfo, client);

		snd_seq_port_info_set_port(pinfo, -1);
		while(snd_seq_query_next_port(m_seq_handle, pinfo) >= 0) {
			unsigned cap = (SND_SEQ_PORT_CAP_SUBS_WRITE|SND_SEQ_PORT_CAP_WRITE);
			if((snd_seq_port_info_get_capability(pinfo) & cap) == cap) {
				PINFOF(LOG_V0, LOG_MIDI, "%3d:%-3d   %-30.30s    %s\n",
					snd_seq_port_info_get_client(pinfo),
					snd_seq_port_info_get_port(pinfo),
					snd_seq_client_info_get_name(cinfo),
					snd_seq_port_info_get_name(pinfo)
					);
			}
		}
	}
}

void MIDIDev_ALSA::open(std::string _conf)
{
	if(snd_seq_open(&m_seq_handle, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) {
		PERRF(LOG_MIDI, "%s: Cannot open the ALSA interface: %s\n", name(), snd_strerror(errno));
		throw std::exception();
	}
	
	if(!_conf.empty()) { 
		try {
			parse_addr(_conf);
		} catch(std::exception &) {
			PERRF(LOG_MIDI, "%s: Invalid port '%s'\n", name(), _conf.c_str());
			close();
			throw;
		}
	} else {
		PWARNF(LOG_V0, LOG_MIDI, "%s: Device configuration is missing in [%s]:%s.\n", name(), MIDI_SECTION, MIDI_DEVICE);
		PINFOF(LOG_V0, LOG_MIDI, "%s: Please use one of the following available ports:\n", name());
		show_port_list();
		close();
		throw std::exception();
	}
	
	m_conf = _conf;
	
	int this_client = snd_seq_client_id(m_seq_handle);
	snd_seq_set_client_name(m_seq_handle, PACKAGE_NAME);
	
	unsigned caps = SND_SEQ_PORT_CAP_READ;
	if(m_seq_client == SND_SEQ_ADDRESS_SUBSCRIBERS) {
		caps |= SND_SEQ_PORT_CAP_SUBS_READ;
	}
	m_this_port = snd_seq_create_simple_port(m_seq_handle, PACKAGE_NAME, caps,
			SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
	if(m_this_port < 0) {
		close();
		PERRF(LOG_MIDI, "%s: Cannot create port\n", name());
		throw std::exception();
	}

	if(m_seq_client == SND_SEQ_ADDRESS_SUBSCRIBERS) {
		PINFOF(LOG_V0, LOG_MIDI, "%s: Client initialized (all subscribed ports)\n", name());
		return;
	}

	if(snd_seq_connect_to(m_seq_handle, m_this_port, m_seq_client, m_seq_port) < 0) {
		PERRF(LOG_MIDI, "%s: Cannot subscribe to MIDI port %d:%d\n", name(), m_seq_client, m_seq_port);
		PINFOF(LOG_V0, LOG_MIDI, "%s: Please use one of the following available ports:\n", name());
		show_port_list();
		close();
		throw std::exception();
	}

	PINFOF(LOG_V0, LOG_MIDI, "%s: Client initialized, port: %d:%d\n",
		name(), m_seq_client, m_seq_port);
}

void MIDIDev_ALSA::close()
{
	if(m_seq_handle) {
		PDEBUGF(LOG_V1, LOG_MIDI, "%s: closing\n", name());
		snd_seq_close(m_seq_handle);
		m_seq_handle = nullptr;
	}
}

void MIDIDev_ALSA::send_event(uint8_t _msg[3])
{
	snd_seq_event_t ev;
	
	ev.type = SND_SEQ_EVENT_OSS;

	ev.data.raw32.d[0] = _msg[0];
	ev.data.raw32.d[1] = _msg[1];
	ev.data.raw32.d[2] = _msg[2];

	if(_msg[0] >= 0xf8) {
		PDEBUGF(LOG_V2, LOG_MIDI, "%s: RT message: %02X\n", name(), _msg[0]);
		send_event(ev, true);
		return;
	}
		
	unsigned char chan = _msg[0] & 0x0F;
	switch(_msg[0] & 0xF0) {
	case 0x80:
		snd_seq_ev_set_noteoff(&ev, chan, _msg[1], _msg[2]);
		PDEBUGF(LOG_V2, LOG_MIDI, "%s: event: note off, ch:%d\n", name(), chan);
		send_event(ev, true);
		break;
	case 0x90:
		snd_seq_ev_set_noteon(&ev, chan, _msg[1], _msg[2]);
		PDEBUGF(LOG_V2, LOG_MIDI, "%s: event: note on, ch:%d\n", name(), chan);
		send_event(ev, true);
		break;
	case 0xA0:
		snd_seq_ev_set_keypress(&ev, chan, _msg[1], _msg[2]);
		PDEBUGF(LOG_V2, LOG_MIDI, "%s: event: keypress, ch:%d\n", name(), chan);
		send_event(ev, true);
		break;
	case 0xB0:
		snd_seq_ev_set_controller(&ev, chan, _msg[1], _msg[2]);
		PDEBUGF(LOG_V2, LOG_MIDI, "%s: event: controller, ch:%d\n", name(), chan);
		send_event(ev, true);
		break;
	case 0xC0:
		snd_seq_ev_set_pgmchange(&ev, chan, _msg[1]);
		PDEBUGF(LOG_V2, LOG_MIDI, "%s: event: program change, ch:%d\n", name(), chan);
		send_event(ev, false);
		break;
	case 0xD0:
		snd_seq_ev_set_chanpress(&ev, chan, _msg[1]);
		PDEBUGF(LOG_V2, LOG_MIDI, "%s: event: channel pressure, ch:%d\n", name(), chan);
		send_event(ev, false);
		break;
	case 0xE0: {
			int bend_val = (int(_msg[1]) + (int(_msg[2]) << 7)) - 0x2000;
			snd_seq_ev_set_pitchbend(&ev, chan, bend_val);
			PDEBUGF(LOG_V2, LOG_MIDI, "%s: event: pitchwheel, ch:%d\n", name(), chan);
			send_event(ev, true);
		}
		break;
	default:
		// original DOSBox comment:
		// Maybe filter out FC as it leads for at least one user to crash,
		// but the entire midi stream has not yet been checked.
		PDEBUGF(LOG_V2, LOG_MIDI, "%s: Unknown event: %02X\n", name(), _msg[0]);
		send_event(ev, true);
		break;
	}
}

void MIDIDev_ALSA::send_sysex(uint8_t *_sysex, int _len)
{
	snd_seq_event_t ev;
	
	snd_seq_ev_set_sysex(&ev, _len, _sysex);
	PDEBUGF(LOG_V2, LOG_MIDI, "%s: SysEx, len: %d bytes\n", name(), _len);
	send_event(ev, true);
}

void MIDIDev_ALSA::send_event(snd_seq_event_t &_ev, bool _flush)
{
	snd_seq_ev_set_direct(&_ev);
	snd_seq_ev_set_source(&_ev, m_this_port);
	snd_seq_ev_set_dest(&_ev, m_seq_client, m_seq_port);

	snd_seq_event_output(m_seq_handle, &_ev);
	if(_flush) {
		snd_seq_drain_output(m_seq_handle);
	}
}

#endif