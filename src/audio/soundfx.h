/*
 * Copyright (C) 2015-2023  Marco Bortolin
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
#ifndef IBMULATOR_SOUNDFX_H
#define IBMULATOR_SOUNDFX_H

#include "mixer.h"
#include "machine.h"


class SoundFX
{
protected:
	uint64_t m_audio_cue_time;

public:
	struct sample_def {
		std::string name;
		std::string file;
	};
	typedef std::vector<sample_def> samples_t;

	static std::vector<AudioBuffer> load_samples(const AudioSpec &_spec,
		const samples_t &_samples);

	static bool play_motor(uint64_t _time_span_us, MixerChannel &_channel,
		bool _is_on, bool _is_changing_state,
		const AudioBuffer &_power_up, const AudioBuffer &_running,
		const AudioBuffer &_power_down);

	template<class Event, class EventQueue>
	bool play_timed_events(uint64_t _time_span_ns, bool _first_upd,
		MixerChannel &_channel, EventQueue &_events,
		std::function<void(Event&,uint64_t)> _play);

protected:
	SoundFX() : m_audio_cue_time(0) {}
	virtual ~SoundFX() {}

	static void load_audio_file(const char *_filename, AudioBuffer &_sample,
		const AudioSpec &_spec);
};


template<class Event, class EventQueue>
bool SoundFX::play_timed_events(uint64_t _time_span_ns, bool _first_upd,
		MixerChannel &_channel, EventQueue &_events,
		std::function<void(Event&,uint64_t)> _play)
{
	uint64_t mtime_us = g_machine.get_virt_time_us_mt();

	PDEBUGF(LOG_V2, LOG_AUDIO, "%s: mix span: %04llu ns (1st upd:%d), cue time:%lld us, events:%d\n",
			_channel.name(), _time_span_ns, _first_upd, m_audio_cue_time, _events.size());

	bool empty = true;
	do {
		Event event;
		empty = !_events.try_and_copy(event);
		if(empty || event.time > mtime_us) {
			break;
		} else {
			_events.try_and_pop();
			if(_first_upd) {
				m_audio_cue_time = event.time;
				_first_upd = false;
			}
			assert(event.time >= m_audio_cue_time);
			uint64_t time_span = event.time - m_audio_cue_time;
			_play(event, time_span);
		}
	} while(1);

	unsigned in_duration = round(_channel.in().duration_ns());
	if(in_duration < _time_span_ns) {
		unsigned fill_ns = _time_span_ns - in_duration;
		unsigned samples = _channel.in().fill_ns_silence(fill_ns);
		assert(samples == round(_channel.in().spec().ns_to_samples(fill_ns)));
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s: silence fill: %d frames (%d ns)\n",
				_channel.name(),
				_channel.in().spec().samples_to_frames(samples),
				fill_ns
				);
	}
	m_audio_cue_time = mtime_us;
	_channel.input_finish(_time_span_ns);
	if(empty) {
		return _channel.check_disable_time(mtime_us*1000);
	}
	_channel.set_disable_time(mtime_us*1000);
	return true;
}

#endif
