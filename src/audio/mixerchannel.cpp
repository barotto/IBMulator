/*
 * Copyright (C) 2015, 2016  Marco Bortolin
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
#include "mixer.h"
#include "machine.h"


MixerChannel::MixerChannel(Mixer *_mixer, MixerChannel_handler _callback,
		const std::string &_name)
:
m_mixer(_mixer),
m_enabled(false),
m_name(_name),
m_update_clbk(_callback),
m_disable_time(0),
m_disable_timeout(0),
m_first_update(true),
m_in_time(0),
m_SRC_state(nullptr),
m_capture_clbk([](bool){}),
m_volume(1.f),
m_category(MixerChannelCategory::AUDIO)
{
}

MixerChannel::~MixerChannel()
{
#if HAVE_LIBSAMPLERATE
	if(m_SRC_state != nullptr) {
		src_delete(m_SRC_state);
	}
#endif
}

void MixerChannel::enable(bool _enabled)
{
	if(m_enabled != _enabled) {
		m_enabled = _enabled;
		m_disable_time = 0;
		if(_enabled) {
			reset_SRC();
			PDEBUGF(LOG_V1, LOG_MIXER, "%s: channel enabled\n", m_name.c_str());
		} else {
			m_first_update = true;
			PDEBUGF(LOG_V1, LOG_MIXER, "%s: channel disabled\n", m_name.c_str());
		}
	}
}

std::tuple<bool,bool> MixerChannel::update(uint64_t _time_span_us, bool _prebuffering)
{
	assert(m_update_clbk);

	bool active=false,enabled=false;
	if(m_enabled) {
		bool first_upd = m_first_update;
		/* channel can be disabled in the callback, so I update m_first_update
		 * before calling the update
		 */
		m_first_update = false;
		enabled = m_update_clbk(_time_span_us, _prebuffering, first_upd);
		if(enabled || m_out_buffer.frames()>0) {
			active = true;
		}
	} else {
		enabled = false;
		/* On the previous iteration the channel could have been disabled
		 * but its input buffer could have some samples left to process
		 */
		if(m_in_buffer.frames()) {
			input_finish(_time_span_us);
			active = true;
		}
	}

	return std::make_tuple(active,enabled);
}

void MixerChannel::reset_SRC()
{
#if HAVE_LIBSAMPLERATE
	if(m_SRC_state == nullptr) {
		const SDL_AudioSpec &spec = m_mixer->get_audio_spec();
		int err;
		m_SRC_state = src_new(SRC_SINC_FASTEST, spec.channels, &err);
		if(m_SRC_state == nullptr) {
			PERRF(LOG_MIXER, "unable to initialize SRC state: %d\n", err);
		}
	} else {
		src_reset(m_SRC_state);
		m_new_data = true;
	}
#endif
}

void MixerChannel::set_in_spec(const AudioSpec &_spec)
{
	if(m_in_buffer.spec() != _spec)	{
		m_in_buffer.set_spec(_spec);
		// 5 sec. worth of data, how much is enough?
		m_in_buffer.reserve_us(5e6);
		reset_SRC();
	}
}

void MixerChannel::set_out_spec(const AudioSpec &_spec)
{
	if(m_out_buffer.spec() != _spec) {
		/* the output buffer is forced to float format
		 */
		m_out_buffer.set_spec({AUDIO_FORMAT_F32, _spec.channels, _spec.rate});
		m_out_buffer.reserve_us(5e6);
		reset_SRC();
	}
}

void MixerChannel::play(const AudioBuffer &_wave)
{
	if(_wave.spec() != m_in_buffer.spec()) {
		PDEBUGF(LOG_V1, LOG_MIXER, "%s: can't play, incompatible audio format\n",
				m_name.c_str());
		return;
	}
	m_in_buffer.add_frames(_wave);
	PDEBUGF(LOG_V1, LOG_MIXER, "%s: wave play: %d frames (%dus), in buf: %d samples (%dus)\n",
			m_name.c_str(),
			_wave.frames(), _wave.duration_us(),
			m_in_buffer.samples(), m_in_buffer.duration_us()
			);
}

void MixerChannel::play(const AudioBuffer &_wave, uint64_t _time_dist)
{
	play_frames(_wave, _wave.frames(), _time_dist);
}

void MixerChannel::play(const AudioBuffer &_wave, float _volume, uint64_t _time_dist)
{
	/* this work buffers can be static only because the current implementation
	 * of the mixer is single threaded.
	 */
	static AudioBuffer temp;
	temp = _wave;
	temp.apply_volume(_volume);
	play_frames(temp, temp.frames(), _time_dist);
}

void MixerChannel::play_frames(const AudioBuffer &_wave, unsigned _frames_cnt, uint64_t _time_dist)
{
	/* This function plays the given sound sample at the specified time distance
	 * from the start of the samples input buffer, filling with silence if needed.
	 */
	if(_wave.spec() != m_in_buffer.spec()) {
		PDEBUGF(LOG_V1, LOG_MIXER, "%s: can't play, incompatible audio format\n",
				m_name.c_str());
		return;
	}
	unsigned inbuf_frames = m_in_buffer.spec().us_to_frames(_time_dist);
	m_in_buffer.resize_frames_silence(inbuf_frames);
	m_in_buffer.add_frames(_wave, _frames_cnt);
	PDEBUGF(LOG_V1, LOG_MIXER, "%s: wave play: dist: %d frames (%dus), wav: %d frames (%dus), in buf: %d samples (%dus)\n",
			m_name.c_str(),
			inbuf_frames, _time_dist,
			_frames_cnt, _wave.spec().frames_to_us(_frames_cnt),
			m_in_buffer.samples(), m_in_buffer.duration_us()
			);
}

void MixerChannel::play_loop(const AudioBuffer &_wave)
{
	if(m_in_buffer.duration_us() < m_mixer->heartbeat()) {
		play(_wave);
	}
}

void MixerChannel::pop_out_frames(unsigned _frames_to_pop)
{
	m_out_buffer.pop_frames(_frames_to_pop);
}

void MixerChannel::flush()
{
	m_in_buffer.clear();
	m_out_buffer.clear();
	reset_SRC();
}

void MixerChannel::input_finish(uint64_t _time_span_us)
{
	if(!m_mixer->is_enabled()) {
		m_in_buffer.clear();
		return;
	}
	unsigned in_frames;
	if(_time_span_us>0) {
		in_frames = m_in_buffer.us_to_frames(_time_span_us);
	} else {
		in_frames = m_in_buffer.frames();
	}

	if(in_frames == 0) {
		PDEBUGF(LOG_V2, LOG_MIXER, "channel active but empty\n");
		return;
	}

	/* input data -> convert ch/format/rate -> add to output data
	 * The following procedure could be more efficent using m_out_buffer directly
	 * but that would require a convoluted conversion function that works on a
	 * single destination buffer. I'd rather have a slightly less efficent but
	 * readable and concise procedure.
	 */
	/* these work buffers can be static only because the current implementation
	 * of the mixer is single threaded.
	 */
	static AudioBuffer dest[2];
	unsigned bufidx = 0;
	AudioBuffer *source=&m_in_buffer;

	if(m_in_buffer.channels() != m_out_buffer.channels()) {
		dest[0].set_spec({m_in_buffer.format(),m_out_buffer.channels(),m_in_buffer.rate()});
		source->convert_channels(dest[0], in_frames);
		source = &dest[0];
		bufidx = 1;
	}
	if(m_in_buffer.format() != AUDIO_FORMAT_F32) {
		dest[bufidx].set_spec({AUDIO_FORMAT_F32,m_out_buffer.channels(),m_in_buffer.rate()});
		source->convert_format(dest[bufidx], in_frames);
		source = &dest[bufidx];
	}
	if(m_in_buffer.rate() != m_out_buffer.rate()) {
		unsigned missing = source->convert_rate(m_out_buffer, in_frames, m_SRC_state);
		if(m_new_data && missing>0) {
			m_out_buffer.hold_frames<float>(missing);
		}
		m_new_data = false;
	} else {
		m_out_buffer.add_frames(*source, in_frames);
	}
	m_in_buffer.pop_frames(in_frames);
	PDEBUGF(LOG_V2, LOG_MIXER, "%s: finish (%dus): in: %d frames (%dus), out: %d frames (%dus)\n",
			m_name.c_str(), _time_span_us,
			in_frames, m_in_buffer.spec().frames_to_us(in_frames),
			m_out_buffer.frames(), m_out_buffer.duration_us());
}

bool MixerChannel::check_disable_time(uint64_t _now_us)
{
	if(m_disable_time && (_now_us - m_disable_time >= m_disable_timeout)) {
		PDEBUGF(LOG_V1, LOG_MIXER, "%s: disabling channel after %d us of silence\n",
				m_name.c_str(), (_now_us - m_disable_time));
		enable(false);
		return true;
	}
	return false;
}

void MixerChannel::register_capture_clbk(std::function<void(bool _enable)> _fn)
{
	m_capture_clbk = _fn;
}

void MixerChannel::on_capture(bool _enable)
{
	m_capture_clbk(_enable);
}
