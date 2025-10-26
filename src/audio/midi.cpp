/*
 * Copyright (C) 2020-2025  Marco Bortolin
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
/*
 * Portions of code copyright (C) 2002-2020 The DOSBox Team
 */

#include "ibmulator.h"
#include "midi.h"
#include "program.h"
#include "mididev_alsa.h"
#include "mididev_win32.h"
#include "utils.h"
#include "filesys.h"

#define MIN_MT32_SYSEX_DELAY 20

MIDI::MIDI()
:
m_quit(false),
m_min_sysex_delay(0),
m_last_evt_time(0)
{
	
}

MIDI::~MIDI()
{
	close_device();
}

void MIDI::thread_start()
{
	PDEBUGF(LOG_V0, LOG_MIDI, "MIDI Thread started\n");
	
	while(!m_quit) {
		MIDI_fun_t fn;
		m_cmd_queue.wait_and_pop(fn);
		fn();
	}
	
	PDEBUGF(LOG_V0, LOG_MIDI, "MIDI thread stopped\n");
}

void MIDI::sig_config_changed(std::mutex &_mutex, std::condition_variable &_cv)
{
	m_cmd_queue.push([&] () {
		std::unique_lock<std::mutex> lock(_mutex);
		
		std::string conf = g_program.config().get_string(MIDI_SECTION, MIDI_DEVICE);
		
		if(!g_program.config().get_bool(MIDI_SECTION, MIDI_ENABLED, false)) {
			PINFOF(LOG_V0, LOG_MIDI, "MIDI output disabled\n");
			close_device();
		} else {
			if(!is_device_open() || conf != m_device->conf()) {
				close_device();
				open_device(conf);
			}
		}
		
		m_min_sysex_delay = 0;
		if(is_device_open()) {
			try {
				m_min_sysex_delay = g_program.config().try_int(MIDI_SECTION, MIDI_DELAY);
				if(m_min_sysex_delay < 0) {
					m_min_sysex_delay = 0;
				}
			} catch(std::exception &) {
				std::string delay_string = g_program.config().get_string(MIDI_SECTION, MIDI_DELAY, "auto");
				if(delay_string == "auto") {
					m_min_sysex_delay = -1;
				}
			}
			if(m_min_sysex_delay >= 0) {
				PINFOF(LOG_V0, LOG_MIDI, "Minimum delay for SysEx messages: %d ms.\n", m_min_sysex_delay);
			}
		}
		
		_cv.notify_one();
	});
}

void MIDI::cmd_save_state(StateBuf &_state, std::mutex &_mutex, std::condition_variable &_cv)
{
	m_cmd_queue.push([&] () {
		std::unique_lock<std::mutex> lock(_mutex);
		save_state(_state);
		_cv.notify_one();
	});
}

void MIDI::cmd_restore_state(StateBuf &_state, std::mutex &_mutex, std::condition_variable &_cv)
{
	m_cmd_queue.push([&] () {
		std::unique_lock<std::mutex> lock(_mutex);
		try {
			restore_state(_state);
		} catch(std::exception &e) {
			PERRF(LOG_MIDI, "error restoring the state\n");
		}
		_cv.notify_one();
	});
}

void MIDI::open_device(std::string _conf)
{
	if(HAVE_ALSA) {
		m_device = std::make_unique<MIDIDev_ALSA>(this);
	} else if(HAVE_WINMM) {
		m_device = std::make_unique<MIDIDev_Win32>(this);
	} else {
		PWARNF(LOG_V1, LOG_MIDI, "MIDI output is NOT available with this build!\n");
		return;
	}

	try {
		m_device->open(_conf);
	} catch(std::exception &) {
		PWARNF(LOG_V0, LOG_MIDI, "MIDI output NOT available!\n");
		m_device = nullptr;
		return;
	}
	
	PINFOF(LOG_V0, LOG_MIDI, "MIDI output enabled with the '%s' driver.\n", m_device->name());

	memset( &m_s, 0, sizeof(m_s) );
	memset( &m_s.ch, 0xff, sizeof(m_s.ch) );
	m_s.sysex.buf_used = 0;
	m_s.sysex.delay_ms = 0;
	m_s.sysex.start_ms = 0;
	m_sysex_data.clear();
	
	restore_state(m_s, m_sysex_data);
}

void MIDI::close_device()
{
	if(is_device_open()) {
		PDEBUGF(LOG_V0, LOG_MIDI, "Closing device\n");
		m_device->reset();
		m_device->close();
	}
	m_device = nullptr;
}

void MIDI::cmd_put_byte(uint8_t _byte, uint64_t _time_ns)
{
	m_cmd_queue.push([=] () {
		put_byte(_byte, true, _time_ns);
	});
}

void MIDI::cmd_put_bytes(const std::vector<uint8_t> &_bytes, uint64_t _time_ns)
{
	m_cmd_queue.push([=] () {
		put_bytes(_bytes, true, _time_ns);
	});
}

void MIDI::cmd_quit()
{
	m_cmd_queue.push([this] () {
		close_device();
		m_quit = true;
	});
}

void MIDI::cmd_stop_device()
{
	m_cmd_queue.push([=] () {
		stop_and_silence_device();
	});
}

void MIDI::cmd_start_capture()
{
	m_cmd_queue.push([=] () {
		m_midifile.close();
		std::string path = g_program.config().get_file(CAPTURE_SECTION, CAPTURE_DIR, FILE_TYPE_USER);
		path = FileSys::get_next_filename(path, "midi_", ".mid");
		if(!path.empty()) {
			try {
				m_midifile.open_write(path.c_str(),  0, 500);
				m_midifile.write_new_track();
				m_midifile.write_text("Dumped with " PACKAGE_STRING);
				PINFOF(LOG_V0, LOG_MIDI, "Raw MIDI commands recording started to %s\n", path.c_str());
			} catch(std::exception &e) {
				m_midifile.close_file();
				PERRF(LOG_MIDI, "Failed to open capture file: %s\n", e.what());
			}
		}
		m_last_evt_time = 0;
	});
}

void MIDI::cmd_stop_capture()
{
	m_cmd_queue.push([=] () {
		
		bool remove = ((m_midifile.mex_count() == 0) && (m_midifile.sys_count() == 0));
		PINFOF(LOG_V0, LOG_MIDI, "MIDI messages written to file: %u\n",
				m_midifile.mex_count() + m_midifile.sys_count());
		try {
			m_midifile.close();
			PINFOF(LOG_V0, LOG_MIDI, "Raw MIDI commands recording stopped\n");
		} catch(std::exception &e) {
			m_midifile.close_file();
			PERRF(LOG_MIDI, "Failed to finish capture: %s\n", e.what());
			remove = true;
		}
		if(remove) {
			PINFOF(LOG_V0, LOG_MIDI, "Deleting empty MIDI file\n");
			if(FileSys::remove(m_midifile.path()) < 0) {
				PWARNF(LOG_V0, LOG_MIDI, "Cannot remove '%s'!\n", m_midifile.path());
			}
		}
	});
}

void MIDI::stop_and_silence_device()
{
	if(!is_device_open()) {
		return;
	}

	PDEBUGF(LOG_V0, LOG_MIDI, "Silencing the device...\n");
	
	// flush data buffer - throw invalid midi message
	put_byte(0xf7);

	// shutdown sound
	for(uint8_t lcv=0; lcv<=0xf; lcv++) {
		put_bytes({ uint8_t(0xb0 + lcv), 0x78, 0x00, 0x79, 0x00, 0x7b, 0x00 });
	}
	
	m_last_evt_time = 0;
}

void MIDI::save_state(StateBuf &_state)
{
	PDEBUGF(LOG_V0, LOG_MIDI, "Saving state...\n");
	
	StateHeader h;

	h.name = "MIDIState";
	h.data_size = sizeof(m_s);
	_state.write(&m_s, h);
	
	h.name = "MIDISysExData";
	h.data_size = m_sysex_data.size();
	_state.write(h.data_size ? &m_sysex_data[0] : nullptr, h);
}

void MIDI::restore_state(StateBuf &_state)
{
	PDEBUGF(LOG_V0, LOG_MIDI, "Restoring state...\n");
	
	StateHeader h;
	MIDI::State midis;
	
	h.name = "MIDIState";
	h.data_size = sizeof(midis);
	_state.read(&midis, h);
	
	_state.get_next_lump_header(h);
	if(h.name != "MIDISysExData") {
		PDEBUGF(LOG_V0, LOG_MIDI, "MIDISysExData header not present\n");
		throw std::exception();
	}
	std::vector<uint8_t> sysex_data;
	if(h.data_size) {
		sysex_data.resize(h.data_size);
		_state.read(&sysex_data[0], h);
	}
	
	if(is_device_open()) {
		PINFOF(LOG_V1, LOG_MIDI, "Restoring MIDI device state...\n");
		restore_state(midis, sysex_data);
	}
	
	memcpy(&m_s, &midis, sizeof(midis));
	m_sysex_data = sysex_data;
}

void MIDI::restore_state(const MIDI::State &_state, const std::vector<uint8_t> &_sysex_data)
{
	if(!is_device_open()) {
		return;
	}
	
	stop_and_silence_device();
	
	if(m_sysex_data.size() != _sysex_data.size() || 
	  (!m_sysex_data.empty() && memcmp(&m_sysex_data[0], &_sysex_data[0], m_sysex_data.size()) != 0)
	)
	{
		m_device->reset();
		if(_sysex_data.size()) {
			PINFOF(LOG_V2, LOG_MIDI, "Uploading %u bytes of SysEx data to device...\n",
					static_cast<unsigned>(_sysex_data.size()));
			put_bytes(_sysex_data);
		}
	}
	
	for(int lcv=0; lcv<16; lcv++) {
		
		// control states (bx - 00-5F)
		put_byte( 0xb0 + lcv );

		for(int lcv2=0; lcv2<0x80; lcv2++) {
			if(lcv2 >= 0x60) {
				break;
			}
			if(_state.ch[lcv].code_b0[lcv2] == 0xffff ) {
				continue;
			}
			// RPN data - coarse / fine
			if( lcv2 == 0x06 ) {
				continue;
			}
			if( lcv2 == 0x26 ) {
				continue;
			}
			put_byte( lcv2 );
			put_byte( _state.ch[lcv].code_b0[lcv2] & 0xff );
		}

		// control states - RPN data (GM 1.0)
		for(uint8_t lcv2=0; lcv2<3; lcv2++) {
			if(_state.ch[lcv].code_rpn_coarse[lcv2] == 0xffff &&
			   _state.ch[lcv].code_rpn_fine[lcv2] == 0xffff )
			{
				continue;
			}
			put_bytes({ 0x64, lcv2, 0x65, 0x00 });
			if(_state.ch[lcv].code_rpn_coarse[lcv2] != 0xffff ) {
				put_byte( 0x06 );
				put_byte( _state.ch[lcv].code_rpn_coarse[lcv2] & 0xff );
			}
			if(_state.ch[lcv].code_rpn_fine[lcv2] != 0xffff) {
				put_byte( 0x26 );
				put_byte( _state.ch[lcv].code_rpn_fine[lcv2] & 0xff );
			}
			put_bytes({ 0x64, 0x7f, 0x65, 0x7f });
		}

		// program change
		if(_state.ch[lcv].code_c0[0] != 0xffff) {
			put_byte( 0xc0 + lcv );
			put_byte( _state.ch[lcv].code_c0[0] & 0xff );
		}

		// pitch wheel change
		if(_state.ch[lcv].code_e0[0] != 0xffff) {
			put_byte( 0xe0 + lcv );
			put_byte( _state.ch[lcv].code_e0[0] & 0xff );
			put_byte( _state.ch[lcv].code_e0[1] & 0xff );
		}

		// note on
		put_byte( 0x90+lcv );
		for(int lcv2=0; lcv2<0x80; lcv2++) {
			if(_state.ch[lcv].code_90[lcv2] == 0xffff) {
				continue;
			}
			put_byte( lcv2 );
			put_byte( _state.ch[lcv].code_90[lcv2] & 0xff );
		}

		// polyphonic aftertouch
		put_byte( 0xa0 + lcv );
		for(int lcv2=0; lcv2<0x80; lcv2++) {
			if(_state.ch[lcv].code_a0[lcv2] == 0xffff) {
				continue;
			}
			put_byte( lcv2 );
			put_byte( _state.ch[lcv].code_a0[lcv2] & 0xff );
		}

		// channel aftertouch
		if(_state.ch[lcv].code_d0[0] != 0xffff) {
			put_byte( 0xd0 + lcv );
			put_byte( _state.ch[lcv].code_d0[0] & 0xff );
		}
	}
}

void MIDI::save_message(uint8_t *_cmd_buf)
{
	uint8_t channel = _cmd_buf[0] & 0xf;
	uint8_t command = _cmd_buf[0] >> 4;
	uint8_t arg1 = _cmd_buf[1];
	uint8_t arg2 = _cmd_buf[2];

	switch( command ) {
		case 0x8: // Note off
			// - arg1 = note, arg2 = velocity off
			m_s.ch[channel].code_80[arg1] = arg2;

			memset( &m_s.ch[channel].code_90[arg1], 0xff, sizeof(m_s.ch[channel].code_90[arg1]) );
			memset( &m_s.ch[channel].code_a0[arg1], 0xff, sizeof(m_s.ch[channel].code_a0[arg1]) );
			memset( m_s.ch[channel].code_d0, 0xff, sizeof(m_s.ch[channel].code_d0) );
			break;

		case 0x9: // Note on
			if( arg2 > 0 ) {
				// - arg1 = note, arg2 = velocity on
				m_s.ch[channel].code_90[arg1] = arg2;

				memset( &m_s.ch[channel].code_80[arg1], 0xff, sizeof(m_s.ch[channel].code_80[arg1]) );
			} else {
				// velocity = 0 (note off)
				m_s.ch[channel].code_80[arg1] = arg2;

				memset( &m_s.ch[channel].code_90[arg1], 0xff, sizeof(m_s.ch[channel].code_90[arg1]) );
				memset( &m_s.ch[channel].code_a0[arg1], 0xff, sizeof(m_s.ch[channel].code_a0[arg1]) );
				memset( m_s.ch[channel].code_d0, 0xff, sizeof(m_s.ch[channel].code_d0) );
			}
			break;

		case 0xA: // Aftertouch (polyphonic pressure)
			m_s.ch[channel].code_a0[arg1] = arg2;
			break;

		case 0xB: // Controller #s
			m_s.ch[channel].code_b0[arg1] = arg2;

			switch( arg1 ) {
				// General Midi 1.0
				case 0x01: break; // Modulation
				case 0x07: break; // Volume
				case 0x0A: break; // Pan
				case 0x0B: break; // Expression
				case 0x40: break; // Sustain pedal
				case 0x64: break; // Registered Parameter Number (RPN) LSB
				case 0x65: break; // Registered Parameter Number (RPN) MSB
				case 0x79: // All controllers off
					// (likely GM1+GM2)
					// Set Expression (#11) to 127
					// Set Modulation (#1) to 0
					// Set Pedals (#64, #65, #66, #67) to 0
					// Set Registered and Non-registered parameter number LSB and MSB (#98-#101) to null value (127)
					// Set pitch bender to center (64/0)
					// Reset channel pressure to 0 
					// Reset polyphonic pressure for all notes to 0.
					memset( m_s.ch[channel].code_a0, 0xff, sizeof(m_s.ch[channel].code_a0) );
					memset( m_s.ch[channel].code_c0, 0xff, sizeof(m_s.ch[channel].code_c0) );
					memset( m_s.ch[channel].code_d0, 0xff, sizeof(m_s.ch[channel].code_d0) );
					memset( m_s.ch[channel].code_e0, 0xff, sizeof(m_s.ch[channel].code_e0) );
					memset( m_s.ch[channel].code_b0+0x01, 0xff, sizeof(m_s.ch[channel].code_b0[0x01]) );
					memset( m_s.ch[channel].code_b0+0x0b, 0xff, sizeof(m_s.ch[channel].code_b0[0x0b]) );
					memset( m_s.ch[channel].code_b0+0x40, 0xff, sizeof(m_s.ch[channel].code_b0[0x40]) );
					memset( m_s.ch[channel].code_rpn_coarse, 0xff, sizeof(m_s.ch[channel].code_rpn_coarse) );
					memset( m_s.ch[channel].code_rpn_fine, 0xff, sizeof(m_s.ch[channel].code_rpn_fine) );
					// (likely GM1+GM2)
					// Do NOT reset Bank Select (#0/#32)
					// Do NOT reset Volume (#7)
					// Do NOT reset Pan (#10)
					// Do NOT reset Program Change
					// Do NOT reset Effect Controllers (#91-#95) (5B-5F)
					// Do NOT reset Sound Controllers (#70-#79) (46-4F)
					// Do NOT reset other channel mode messages (#120-#127) (78-7F)
					// Do NOT reset registered or non-registered parameters.
					[[gnu::fallthrough]];
				case 0x7b: // All notes off
					memset( m_s.ch[channel].code_80, 0xff, sizeof(m_s.ch[channel].code_80) );
					memset( m_s.ch[channel].code_90, 0xff, sizeof(m_s.ch[channel].code_90) );
					break;

				// Roland GS
				case 0x00: break; // Bank select MSB
				case 0x05: break; // Portamento time
				case 0x20: break; // Bank select LSB
				case 0x41: break; // Portamento
				case 0x42: break; // Sostenuto
				case 0x43: break; // Soft pedal
				case 0x54: break; // Portamento control
				case 0x5B: break; // Effect 1 (Reverb) send level
				case 0x5D: break; // Effect 3 (Chorus) send level
				case 0x5E: break; // Effect 4 (Variation) send level
				// TODO: Add more as needed
				case 0x62: break; // NRPN LSB
				case 0x63: break; // NRPN MSB
				case 0x78: // All sounds off
					memset( m_s.ch[channel].code_80, 0xff, sizeof(m_s.ch[channel].code_80) );
					memset( m_s.ch[channel].code_90, 0xff, sizeof(m_s.ch[channel].code_90) );
					break;

				/*
				// Roland MT-32
				case 0x64: // RPNLSB
				case 0x65: // RPNMSB
				case 0x7B: // All notes off
				
				case 0x7C:
				case 0x7D:
				case 0x7E:
				case 0x7F:
					// Hold Pedal off
					// All notes off
				*/

				/*
				(LSB,MSB) RPN
				GM 1.0
				00,00 = Pitch bend range (fine + coarse)
				01,00 = Channel Fine tuning
				02,00 = Channel Coarse tuning
				7F,7F = None
				*/

				case 0x06: // Data entry (coarse)
				{
					int rpn;
					rpn = m_s.ch[channel].code_b0[0x64];
					rpn |= (m_s.ch[channel].code_b0[0x65]) << 8;
					// GM 1.0 = 0-2
					if( rpn >= 3 ) {
						break;
					}
					m_s.ch[channel].code_rpn_coarse[rpn] = arg2;
					break;
				}

				case 0x26: // Data entry (fine)
				{
					int rpn;
					rpn = m_s.ch[channel].code_b0[0x64];
					rpn |= (m_s.ch[channel].code_b0[0x65]) << 8;
					// GM 1.0 = 0-2
					if( rpn >= 3 ) {
						break;
					}
					m_s.ch[channel].code_rpn_fine[rpn] = arg2;
					break;
				}

				default: // unsupported
					break;
			}
			break;

		case 0xC: // Patch change
			m_s.ch[channel].code_c0[0] = arg1;
			break;

		case 0xD: // Channel pressure (aftertouch)
			m_s.ch[channel].code_d0[0] = arg1;
			break;

		case 0xE: // Pitch bend
			m_s.ch[channel].code_e0[0] = arg1;
			m_s.ch[channel].code_e0[1] = arg2;
			break;

		case 0xF: // System messages
			// TODO: General Midi 1.0 says 'Master Volume' SysEx
			break;

		// unhandled case
		default:
			break;
	}
}

void MIDI::save_sysex(const State::SysEx &_sysex)
{
	// only MT-32 sysex messages are saved
	if(!_sysex.is_mt_32()) {
		return;
	}
	if(_sysex.buf[5] == 0x7F) {
		// All Parameters reset
		m_sysex_data.clear();
	}
	m_sysex_data.insert(m_sysex_data.end(), _sysex.buf, _sysex.buf+_sysex.buf_used);
}

uint32_t MIDI::get_delta(uint64_t _time_ns)
{
	uint32_t delta;
	if(m_last_evt_time) {
		if(_time_ns < m_last_evt_time) {
			delta = 0;
		} else {
			delta = (_time_ns - m_last_evt_time) / 1000000u;
		}
	} else {
		delta = 0;
	}
	m_last_evt_time = _time_ns;
	return delta;
}

static uint8_t MIDI_evt_len[256] = {
	0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x00
	0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x10
	0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x20
	0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x30
	0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x40
	0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x50
	0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x60
	0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x70
	
	3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,  // 0x80
	3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,  // 0x90
	3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,  // 0xa0
	3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,  // 0xb0
	
	2,2,2,2, 2,2,2,2, 2,2,2,2, 2,2,2,2,  // 0xc0
	2,2,2,2, 2,2,2,2, 2,2,2,2, 2,2,2,2,  // 0xd0
	
	3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,  // 0xe0
	
	0,2,3,2, 0,0,1,0, 1,0,1,1, 1,0,1,0   // 0xf0
};

void MIDI::put_bytes(const std::vector<uint8_t> &_data, bool _save, uint64_t _time)
{
	for(uint8_t b : _data) {
		put_byte(b, _save, _time);
	}
}

void MIDI::put_byte(uint8_t _data, bool _save, uint64_t _time_ns)
{
	if(is_device_open() && m_s.sysex.delay_ms > 0) {
		uint64_t now_ms = get_curtime_ms();
		uint64_t elapsed_ms = now_ms - m_s.sysex.start_ms;
		if(elapsed_ms < unsigned(m_s.sysex.delay_ms)) {
			uint64_t ms = m_s.sysex.delay_ms - elapsed_ms;
			PDEBUGF(LOG_V2, LOG_MIDI, "Sleeping for %llu ms for SysEx delay...\n", ms);
			std::this_thread::sleep_for(std::chrono::milliseconds(ms));
			PDEBUGF(LOG_V2, LOG_MIDI, "  slept for %llu ms\n", get_curtime_ms() - now_ms);
		}
	}

	// Test for a realtime MIDI message
	if(_data >= 0xf8) {
		PDEBUGF(LOG_V2, LOG_MIDI, "RT message: %02X\n", _data);
		if(is_device_open()) {
			uint8_t rt_buf[3] = { _data, 0, 0 };
			m_device->send_event(rt_buf);
		}
		return;
	}
	
	// Test for an active sysex tranfer
	if(m_s.status == 0xf0) {
		if(!(_data & 0x80)) { 
			if(m_s.sysex.buf_used < (SYSEX_SIZE-1)) {
				m_s.sysex.buf[m_s.sysex.buf_used++] = _data;
			}
			return;
		} else {
			assert(m_s.sysex.buf_used < (SYSEX_SIZE-1));
			m_s.sysex.buf[m_s.sysex.buf_used++] = 0xf7;

			bool logmex = true;
			m_s.sysex.delay_ms = 0;
			if(m_s.sysex.is_mt_32()) {
				if(m_s.sysex.buf[5] == 0x7F) {
					m_s.sysex.delay_ms = 290; // All Parameters reset (DOSBox value)
					logmex = false;
					PDEBUGF(LOG_V2, LOG_MIDI, "SysEx: MT-32 All Parameters reset, delay: %u ms\n", m_s.sysex.delay_ms);
				} else if(m_s.sysex.buf[5] == 0x10 && m_s.sysex.buf[6] == 0x00 && m_s.sysex.buf[7] == 0x04) {
					m_s.sysex.delay_ms = 145; // Viking Child (DOSBox value)
				} else if(m_s.sysex.buf[5] == 0x10 && m_s.sysex.buf[6] == 0x00 && m_s.sysex.buf[7] == 0x01) {
					m_s.sysex.delay_ms = 30; // Dark Sun 1 (DOSBox value)
				} else {
					m_s.sysex.delay_ms = int(((float)(m_s.sysex.buf_used) * 1.25f) * 1000.0f / 3125.0f) + 2;
				}
				if(m_min_sysex_delay < 0 && m_s.sysex.delay_ms < MIN_MT32_SYSEX_DELAY) {
					// this is a MT-32 sysex message and delays are set to auto, so assume the worst case
					// (old MT-32 board) and set the minimum amount needed
					m_s.sysex.delay_ms = MIN_MT32_SYSEX_DELAY;
				}
			}
			if(m_s.sysex.delay_ms < m_min_sysex_delay || m_min_sysex_delay == 0) {
				m_s.sysex.delay_ms = m_min_sysex_delay;
			}
			
			if(logmex) {
				PDEBUGF(LOG_V2, LOG_MIDI, "SysEx: address:[%02X %02X %02X], length: %d bytes, delay: %d ms\n",
					m_s.sysex.buf[5], m_s.sysex.buf[6], m_s.sysex.buf[7], m_s.sysex.buf_used, m_s.sysex.delay_ms);
			}
			if(is_device_open()) {
				PDEBUGF(LOG_V2, LOG_MIDI, "SysEx: elapsed time: %llu ms\n", get_curtime_ms() - m_s.sysex.start_ms);
				m_device->send_sysex(m_s.sysex.buf, m_s.sysex.buf_used);
			}

			m_s.sysex.start_ms = get_curtime_ms();

			if(m_midifile.is_open()) {
				try {
					m_midifile.write_sysex(m_s.sysex.buf, m_s.sysex.buf_used, get_delta(_time_ns));
				} catch(...) {
					m_midifile.close_file();
				}
			}
			
			if(_save) {
				save_sysex(m_s.sysex);
			}
		}
	}
	
	if(_data & 0x80) {
		m_s.status = _data;
		m_s.cmd_pos = 0;
		m_s.cmd_len = MIDI_evt_len[_data];
		if(m_s.status == 0xf0) {
			memset(m_s.sysex.buf, 0, SYSEX_SIZE);
			m_s.sysex.buf[0] = 0xf0;
			m_s.sysex.buf_used = 1;
		}
	}
	
	if(m_s.cmd_len) {
		m_s.cmd_buf[m_s.cmd_pos++] = _data;
		if(m_s.cmd_pos >= m_s.cmd_len) {
			
			switch(m_s.cmd_len) {
				case 1: PDEBUGF(LOG_V2, LOG_MIDI, "command: %02X\n", m_s.cmd_buf[0]); break;
				case 2: PDEBUGF(LOG_V2, LOG_MIDI, "command: %02X %02X\n", m_s.cmd_buf[0], m_s.cmd_buf[1]); break;
				case 3: PDEBUGF(LOG_V2, LOG_MIDI, "command: %02X %02X %02X\n", m_s.cmd_buf[0], m_s.cmd_buf[1], m_s.cmd_buf[2]); break;
				default: PDEBUGF(LOG_V2, LOG_MIDI, "unexpected command len: %d\n", m_s.cmd_len); break;
			}
			if(is_device_open()) {
				m_device->send_event(m_s.cmd_buf);
			}
			
			if(m_midifile.is_open()) {
				try {
					m_midifile.write_message(m_s.cmd_buf, m_s.cmd_len, get_delta(_time_ns));
				} catch(...) {
					m_midifile.close_file();
				}
			}
			
			m_s.cmd_pos = 1; // Use Running status

			if(_save) {
				save_message(m_s.cmd_buf);
			}
		}
	}
}