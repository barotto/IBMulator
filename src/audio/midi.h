/*
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

#ifndef IBMULATOR_MIDI_H
#define IBMULATOR_MIDI_H

#include "shared_queue.h"
#include "mididev.h"
#include "midifile.h"
#include "statebuf.h"

#define SYSEX_SIZE 8192
typedef std::function<void()> MIDI_fun_t;

class MIDI
{
private:
	friend class MIDIDev;
	
	bool m_quit;
	shared_queue<MIDI_fun_t> m_cmd_queue;
	std::unique_ptr<MIDIDev> m_device;
	
	struct State {
		
		uint8_t status;
		int     cmd_len;
		int     cmd_pos;
		uint8_t cmd_buf[8];
		
		// TODO should this stuff go into MIDIDev?
		struct Channels {
			// NOTE: 16-bit ($ffff = not used, $00-ff = data value)
			uint16_t code_80[0x80]; // note off (w/ velocity)
			uint16_t code_90[0x80]; // note on (w/ velocity)
			uint16_t code_a0[0x80]; // aftertouch (polyphonic key pressure)
			uint16_t code_b0[0x80]; // Continuous controller (GM 1.0 + GS)
			uint16_t code_c0[1];    // patch change
			uint16_t code_d0[1];    // channel pressure (after-touch)
			uint16_t code_e0[2];    // pitch bend
			//uint16_t code_f0-ff   // system messages TODO?
			uint16_t code_rpn_coarse[3]; // registered parameter numbers (GM 1.0)
			uint16_t code_rpn_fine[3];   // registered parameter numbers (GM 1.0)
		} ch[16];
		
		struct SysEx {
			uint8_t  buf[SYSEX_SIZE];
			unsigned buf_used;
			int delay_ms;
			uint64_t start_ms;

			bool is_mt_32() const {
				return (buf[1] == 0x41 && buf[3] == 0x16);
			}
		} sysex;
		
	} m_s;
	
	std::vector<uint8_t> m_sysex_data;
	int m_min_sysex_delay;
	
	MIDIFile m_midifile;
	uint64_t m_last_evt_time;
	
	void open_device(std::string _conf);
	void close_device();
	void stop_and_silence_device();
	bool is_device_open() { return (m_device != nullptr && m_device->is_open()); }
	void put_byte(uint8_t _data, bool _save = false, uint64_t _time = 0);
	void put_bytes(const std::vector<uint8_t> &_data, bool _save = false, uint64_t _time = 0);
	uint32_t get_delta(uint64_t _time_ns);
	
	void save_message(uint8_t *_cmd_buf);
	void save_sysex(const State::SysEx &_sysex);
	void restore_state(const MIDI::State &_state, const std::vector<uint8_t> &_sysex_data);
	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
	
public:
	MIDI();
	~MIDI();
	
	void thread_start();
	
	void sig_config_changed(std::mutex &_mutex, std::condition_variable &_cv);
	void cmd_save_state(StateBuf &_state, std::mutex &_mutex, std::condition_variable &_cv);
	void cmd_restore_state(StateBuf &_state, std::mutex &_mutex, std::condition_variable &_cv);
	void cmd_put_byte(uint8_t _data, uint64_t _time);
	void cmd_put_bytes(const std::vector<uint8_t> &_data, uint64_t _time);
	void cmd_stop_device();
	void cmd_start_capture();
	void cmd_stop_capture();
	void cmd_quit();
};

#endif