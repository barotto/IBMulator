/*
 * Copyright (C) 2016  Marco Bortolin
 *
 * This file is part of IBMulator
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
#ifndef IBMULATOR_SYNTH_H
#define IBMULATOR_SYNTH_H

#include "mixer.h"
#include "machine.h"
#include "vgm.h"

class Synth
{
public:
	struct Event {
		uint64_t time;
		uint16_t reg;
		uint16_t value;
	};

private:
	int m_rate;
	double m_frames_per_ns;
	uint64_t m_last_time;
	VGMFile m_vgm;
	std::mutex m_evt_lock;
	shared_deque<Event> m_events;
	AudioBuffer m_buffer;
	double m_frem;

public:
	Synth() : m_rate(0), m_frames_per_ns(0), m_last_time(0), m_frem(0.0) {}
	virtual ~Synth() {}

	void reset();
	void set_audio_spec(const AudioSpec &_spec);
	int generate(double _frames, std::function<void(AudioBuffer&,int)> _generate);
	inline void add_event(const Event &_evt) {
		m_events.push(_evt);
	}
	inline bool has_events() {
		return !m_events.empty();
	}
	inline void enable_channel(MixerChannel *_ch) {
		_ch->enable(true);
		m_last_time = 0;
	}
	inline bool is_capturing() {
		return m_vgm.is_open();
	}
	inline void capture_command(int _cmd, const Event &_e) {
		m_vgm.command(NSEC_TO_USEC(_e.time), _cmd, _e.reg, _e.value);
	}
	inline VGMFile& vgm() {
		return m_vgm;
	}
	std::pair<bool,int> play_events(
		uint64_t _mtime_ns, uint64_t _time_span_us, bool _prebuf,
		std::function<void(Event&)> _synthcmd,
		std::function<void(AudioBuffer&,int)> _generate);
	void start_capture(const std::string &_name);
	void stop_capture();
	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
};

#endif
