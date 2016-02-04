/*
 * Copyright (C) 2015, 2016  Marco Bortolin
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
public:
	struct sample_def {
		const char* name;
		const char* file;
	};
	typedef std::vector<sample_def> samples_t;

	static std::vector<AudioBuffer> load_samples(const AudioSpec &_spec,
		const samples_t &_samples);

	static bool play_motor(uint64_t _time_span_us, MixerChannel &_channel,
		bool _is_on,
		const AudioBuffer &_on);

	static bool play_motor(uint64_t _time_span_us, MixerChannel &_channel,
		bool _is_on, bool _is_changing_state,
		const AudioBuffer &_power_up, const AudioBuffer &_on,
		const AudioBuffer &_power_down);

	template<class Event, class EventQueue>
	static bool play_timed_events(uint64_t _time_span_us, bool _first_upd,
		MixerChannel &_channel, EventQueue &_events,
		std::function<void(Event&,uint64_t)> _play);

private:
	SoundFX();

	static void load_audio_file(const char *_filename, AudioBuffer &_sample,
		const AudioSpec &_spec);

};


template<class Event, class EventQueue>
bool SoundFX::play_timed_events(uint64_t _time_span_us, bool _first_upd,
		MixerChannel &_channel, EventQueue &_events,
		std::function<void(Event&,uint64_t)> _play)
{
	static uint64_t audio_cue_time = 0;
	uint64_t mtime_us = g_machine.get_virt_time_us_mt();

	PDEBUGF(LOG_V2, LOG_AUDIO, "%s: mix span: %04d us (1st upd:%d), cue time:%lld us, events:%d\n",
			_channel.name(), _time_span_us, _first_upd, audio_cue_time, _events.size());

	unsigned evtcnt = 0;

	do {
		Event event;
		bool empty = !_events.try_and_copy(event);
		if(empty || event.time > mtime_us) {
			break;
		} else {
			evtcnt++;
			_events.try_and_pop();
			if(_first_upd) {
				audio_cue_time = event.time;
				_first_upd = false;
			}
			uint64_t time_span = event.time - audio_cue_time;
			_play(event,time_span);
		}
	} while(1);

	unsigned buf_span;
	if(audio_cue_time==0) {
		buf_span = _time_span_us;
	} else {
		buf_span = mtime_us - audio_cue_time;
	}
	unsigned in_duration = _channel.in().duration_us();
	if(in_duration < buf_span) {
		unsigned fill_us = buf_span - in_duration;
		unsigned samples = _channel.in().fill_us_silence(fill_us);
		assert(samples == _channel.in().spec().us_to_samples(fill_us));
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s: silence fill: %d frames (%d us)\n",
				_channel.name(),
				_channel.in().spec().samples_to_frames(samples),
				fill_us
				);
	}
	audio_cue_time = mtime_us;
	_channel.input_finish(buf_span);
	if(evtcnt == 0) {
		return _channel.check_disable_time(mtime_us);
	}
	_channel.set_disable_time(mtime_us);
	return true;
}

#endif
