/*
 * Copyright (C) 2002-2020  The DOSBox Team
 * Copyright (C) 2020-2024  Marco Bortolin
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

#if HAVE_WINMM

#include "appconfig.h"
#include "mididev.h"
#include "mididev_win32.h"
#include "utils.h"
#include <regex>


MIDIDev_Win32::MIDIDev_Win32(MIDI *_instance)
:
MIDIDev(_instance),
m_devid(-1),
m_event(NULL),
m_out(NULL)
{
	m_name = "Win32" + m_name;
}

MIDIDev_Win32::~MIDIDev_Win32()
{
}

void MIDIDev_Win32::find_device(std::string _arg)
{
	unsigned availdevs = midiOutGetNumDevs();
	if(!availdevs) {
		PERRF(LOG_MIDI, "%s: no MIDI devices available!\n", name());
		throw std::exception();
	}

	MIDIOUTCAPS mididev;
	std::regex re("^#?([0-9]*)$", std::regex::ECMAScript|std::regex::icase);
	std::smatch match;
	if(std::regex_match(_arg, match, re)) {
		int midiid = std::stoi(match[1].str());
		if(midiid >= availdevs) {
			throw std::exception();
		}
		midiOutGetDevCaps(midiid, &mididev, sizeof(MIDIOUTCAPS));
		m_devid = midiid;
		m_devname = mididev.szPname;
	} else {
		_arg = str_to_lower(_arg);
		for(unsigned i = 0; i < availdevs; i++) {
			midiOutGetDevCaps(i, &mididev, sizeof(MIDIOUTCAPS));
			std::string devname(mididev.szPname);
			devname = str_to_lower(devname);
			if(devname.find(_arg) != std::string::npos) {
				m_devid = i;
				m_devname = mididev.szPname;
				break;
			}
		}
		if(m_devid < 0) {
			throw std::exception();
		}
	}
}

void MIDIDev_Win32::list_available_devices()
{
	for(unsigned i = 0; i < midiOutGetNumDevs(); i++) {
		MIDIOUTCAPS mididev;
		midiOutGetDevCaps(i, &mididev, sizeof(MIDIOUTCAPS));
		PINFOF(LOG_V0, LOG_MIDI, "  #%d: %s\n", i, mididev.szPname);
	}
}

void MIDIDev_Win32::open(std::string _conf)
{
	assert(!is_open());

	bool list_devs = true;
	if(_conf.empty() || _conf == "auto") {
		if(_conf.empty()) {
			PINFOF(LOG_V0, LOG_MIDI,
				"%s: Device configuration is missing in `[%s]:%s`.\n",
				name(), MIDI_SECTION, MIDI_DEVICE);
		}
		PINFOF(LOG_V0, LOG_MIDI, "%s: Available devices:\n", name());
		list_available_devices();
		list_devs = false;

		PINFOF(LOG_V0, LOG_MIDI, "%s: Trying with default device #0 ...\n", name());
		_conf = "#0";
	}

	try {
		find_device(_conf);
	} catch(std::exception &) {
		PERRF(LOG_MIDI, "%s: Invalid device '%s'\n", name(), _conf.c_str());
		if(list_devs) {
			PINFOF(LOG_V0, LOG_MIDI, "%s: Please use one of the following available devices:\n", name());
			list_available_devices();
		}
		throw;
	}

	m_conf = _conf;
	
	PINFOF(LOG_V0, LOG_MIDI, "%s: Using device #%d: \"%s\"\n", name(), m_devid, m_devname.c_str());
	
	m_event = CreateEvent(NULL, true, true, NULL);
	
	// SysEx messages can be asynchronous and we need to wait for a transmission to finish
	// before starting a new one by using the Windows Synch API.
	// We could use a function callback with std::mutex + cv for consistency with the rest
	// of the code base but this is easier and already tested by the DOSBox team, so why
	// reinvent the wheel?
	MMRESULT res = midiOutOpen(&m_out, m_devid, (DWORD_PTR)m_event, 0, CALLBACK_EVENT);
	if(res != MMSYSERR_NOERROR) {
		PERRF(LOG_MIDI, "%s: Cannot open MIDI out for device #%d \"%s\".\n", name(), m_devid, m_devname.c_str());
		close();
		throw std::exception();
	}
}

void MIDIDev_Win32::close()
{
	if(m_out != NULL) {
		PDEBUGF(LOG_V1, LOG_MIDI, "%s: closing...\n", name());
		midiOutReset(m_out);
		midiOutClose(m_out);
		m_out = NULL;
	}
	if(m_event != NULL) {
		CloseHandle(m_event);
	}
	m_event = NULL;
	m_devid = -1;
	m_devname = "";
}

void MIDIDev_Win32::send_event(uint8_t _msg[3])
{
	unsigned char chan = _msg[0] & 0x0F;
	switch(_msg[0] & 0xF0) {
	case 0x80: PDEBUGF(LOG_V2, LOG_MIDI, "%s: event: note off, ch:%d\n", name(), chan); break;
	case 0x90: PDEBUGF(LOG_V2, LOG_MIDI, "%s: event: note on, ch:%d\n", name(), chan); break;
	case 0xA0: PDEBUGF(LOG_V2, LOG_MIDI, "%s: event: keypress, ch:%d\n", name(), chan); break;
	case 0xB0: PDEBUGF(LOG_V2, LOG_MIDI, "%s: event: controller, ch:%d\n", name(), chan); break;
	case 0xC0: PDEBUGF(LOG_V2, LOG_MIDI, "%s: event: program change, ch:%d\n", name(), chan); break;
	case 0xD0: PDEBUGF(LOG_V2, LOG_MIDI, "%s: event: channel pressure, ch:%d\n", name(), chan); break;
	case 0xE0: PDEBUGF(LOG_V2, LOG_MIDI, "%s: event: pitchwheel, ch:%d\n", name(), chan); break;
	default:
		if(_msg[0] >= 0xf8) {
			PDEBUGF(LOG_V2, LOG_MIDI, "%s: RT message: %02X\n", name(), _msg[0]);
		} else {
			PDEBUGF(LOG_V2, LOG_MIDI, "%s: Unknown event: %02X\n", name(), _msg[0]);
		}
		break;
	}
	
	DWORD msg = _msg[0] | DWORD(_msg[1]) << 8 | DWORD(_msg[2]) << 16;
	midiOutShortMsg(m_out, msg);
}

void MIDIDev_Win32::send_sysex(uint8_t *_sysex, int _len)
{
	if(WaitForSingleObject(m_event, 2000) == WAIT_TIMEOUT) {
		PERRF(LOG_MIDI, "%s: Timeout while trying to send SysEx message to device #%d!\n", name(), m_devid);
		return;
	}

	PDEBUGF(LOG_V2, LOG_MIDI, "%s: SysEx, len: %d bytes\n", name(), _len);
	
	static MIDIHDR hdr = {};
	
	midiOutUnprepareHeader(m_out, &hdr, sizeof(MIDIHDR));
	
	hdr.lpData = LPSTR(_sysex);
	hdr.dwBufferLength = _len;
	hdr.dwBytesRecorded = _len;
	hdr.dwUser = 0;

	MMRESULT result = midiOutPrepareHeader(m_out, &hdr, sizeof(MIDIHDR));
	if(result != MMSYSERR_NOERROR) {
		PDEBUGF(LOG_V0, LOG_MIDI, "%s: midiOutPrepareHeader error=%d\n", name(), result);
		return;
	}
	
	ResetEvent(m_event);

	result = midiOutLongMsg(m_out, &hdr, sizeof(MIDIHDR));
	if(result != MMSYSERR_NOERROR) {
		SetEvent(m_event);
		PDEBUGF(LOG_V0, LOG_MIDI, "%s: midiOutLongMsg error=%d\n", name(), result);
		return;
	}
}


#endif