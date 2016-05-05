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

class SynthChip
{
public:
	virtual ~SynthChip() {}
	virtual void reset() {}
	virtual void remove() {}
	virtual void config_changed(int) {}
	virtual void generate(int16_t *_buffer, int _frames, int _stride) = 0;
	virtual bool is_silent() { return true; }
	virtual void save_state(StateBuf &) {}
	virtual void restore_state(StateBuf &) {}
	virtual const char *name() { return "SynthChip"; }
};

class Synth
{
public:
	struct Event {
		uint64_t time;
		uint16_t reg;
		uint16_t value;
	};

	typedef std::function<void(Event&)> synthfunc_t;
	typedef std::function<void(AudioBuffer&,int)> genfunc_t;
	typedef std::function<void(bool, VGMFile&)> captfunc_t;

private:
	std::string m_name;
	SynthChip * m_chips[2];
	std::shared_ptr<MixerChannel> m_channel;
	int         m_rate;
	double      m_frames_per_ns;
	uint64_t    m_last_time;
	VGMFile     m_vgm;
	std::mutex  m_evt_lock;
	shared_deque<Event> m_events;
	AudioBuffer m_buffer;
	double      m_fr_rem;
	synthfunc_t m_synthcmd_fn;
	genfunc_t   m_generate_fn;
	captfunc_t  m_capture_fn;

public:
	Synth();
	virtual ~Synth() {}

	void install(std::string _name, int _chtimeout,
			synthfunc_t _synthcmd,
			genfunc_t   _generate,
			captfunc_t  _capture);
	void remove();
	void reset();
	void power_off();
	void config_changed(const AudioSpec &_spec, float _volume=1.f);
	void set_chip(int id, SynthChip *_chip);
	void enable_channel();
	inline bool is_channel_enabled() {
		return m_channel->is_enabled();
	}
	inline void add_event(const Event &_evt) {
		m_events.push(_evt);
	}
	inline bool has_events() {
		return !m_events.empty();
	}
	inline bool is_capturing() {
		return m_vgm.is_open();
	}
	inline void capture_command(int _cmd, const Event &_e) {
		m_vgm.command(NSEC_TO_USEC(_e.time), _cmd, _e.reg, _e.value);
	}
	bool create_samples(uint64_t _time_span_us, bool _prebuf, bool);
	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

private:
	unsigned generate(uint64_t _delta_ns);
	bool is_silent();
	void on_capture(bool _start);
};

#endif
