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
m_capture_clbk([](bool){})
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
	m_enabled = _enabled;
	m_disable_time = 0;
	if(_enabled) {
		PDEBUGF(LOG_V1, LOG_MIXER, "%s channel enabled\n", m_name.c_str());
	} else {
		PDEBUGF(LOG_V1, LOG_MIXER, "%s channel disabled\n", m_name.c_str());
		m_first_update = true;
	}
}

int MixerChannel::update(int _mix_tslice, bool _prebuffering)
{
	assert(m_update_clbk);
	int samples = m_update_clbk(_mix_tslice, _prebuffering, m_first_update);
	m_first_update = false;
	return samples;
}

void MixerChannel::reset_SRC()
{
#if HAVE_LIBSAMPLERATE
	if(m_SRC_state == nullptr) {
		const SDL_AudioSpec &spec = m_mixer->get_audio_spec();
		int err;
		m_SRC_state = src_new(SRC_SINC_MEDIUM_QUALITY, spec.channels, &err);
		if(m_SRC_state == nullptr) {
			PERRF(LOG_MIXER, "unable to initialize SRC state: %d\n", err);
		}
	} else {
		src_reset(m_SRC_state);
	}
#endif
}

void MixerChannel::set_input_spec(const AudioSpec &_spec)
{
	if(m_in_buffer.spec() != _spec)	{
		m_in_buffer.set_spec(_spec);
		reset_SRC();
	}
}

void MixerChannel::set_output_spec(const AudioSpec &_spec)
{
	if(m_out_buffer.spec() != _spec) {
		/* the output buffer is forced to float format
		 */
		m_out_buffer.set_spec({AUDIO_FORMAT_F32, _spec.channels, _spec.rate});
		reset_SRC();
	}
}

void MixerChannel::input_start(uint64_t _time)
{
	m_in_time = _time;
}

void MixerChannel::play(const AudioBuffer &_sample, uint64_t _time)
{
	/* This function plays the given sound sample at the specified time, filling
	 * with silence if needed, basing the calculations on the buffer start time
	 * specified with input_start()
	 */
	//TODO
}

void MixerChannel::pop_out_frames(unsigned _frames_to_pop)
{
	m_out_buffer.pop_frames(_frames_to_pop);
}

void MixerChannel::input_finish(uint64_t _time)
{
	if(!m_mixer->is_enabled()) {
		m_in_buffer.clear();
		return;
	}
	unsigned in_frames;
	if(_time > 0) {
		assert(m_in_time<=_time);
		uint64_t span = _time - m_in_time;
		in_frames = round(double(span) * double(m_in_buffer.rate())/1e9);
		in_frames = std::min(m_in_buffer.frames(), in_frames);
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
	static AudioBuffer dest[2];
	unsigned bufidx = 0;
	AudioBuffer *source=&m_in_buffer;
	dest[0].clear();
	dest[1].clear();

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
		source->convert_rate(m_out_buffer, in_frames, m_SRC_state);
	} else {
		m_out_buffer.add_frames(*source, in_frames);
	}
	m_in_buffer.pop_frames(in_frames);
}

bool MixerChannel::check_disable_time(uint64_t _now_us)
{
	if(m_disable_time && (_now_us - m_disable_time >= m_disable_timeout)) {
		enable(false);
		PDEBUGF(LOG_V2, LOG_MIXER, "%s channel disabled, after %d usec of silence\n",
				m_name.c_str(), (_now_us - m_disable_time));
		m_disable_time = 0;
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
