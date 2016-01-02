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
m_out_frames(0),
#if HAVE_LIBSAMPLERATE
m_SRC_state(nullptr),
#endif
m_capture_clbk([](bool){})
{
	m_in_buffer.reserve(MIXER_BUFSIZE);
	m_out_buffer.resize(MIXER_BUFSIZE);
}

MixerChannel::~MixerChannel()
{
#if HAVE_LIBSAMPLERATE
	if(m_SRC_state != NULL) {
		src_delete(m_SRC_state);
	}
#endif
}

void MixerChannel::enable(bool _enabled)
{
	m_enabled.store(_enabled);
	m_disable_time.store(0);
	if(_enabled) {
		PDEBUGF(LOG_V1, LOG_MIXER, "%s channel enabled\n", m_name.c_str());
	} else {

		std::lock_guard<std::recursive_mutex> lock(m_lock);

		PDEBUGF(LOG_V1, LOG_MIXER, "%s channel disabled\n", m_name.c_str());
#if HAVE_LIBSAMPLERATE
		if(m_SRC_state != NULL) {
			src_reset(m_SRC_state);
		}
#endif
		m_out_frames = 0;
		m_in_buffer.clear();
		m_first_update = true;
	}
}

int MixerChannel::update(int _mix_tslice, bool _prebuffering)
{
	ASSERT(m_update_clbk);
	int samples = m_update_clbk(_mix_tslice, _prebuffering, m_first_update);
	m_first_update = false;
	return samples;
}

void MixerChannel::add_samples(uint8_t *_data, size_t _size)
{
	if(_size > 0) {
		m_in_buffer.insert(m_in_buffer.end(), _data, _data+_size);
	}
}

int MixerChannel::fill_samples_fade_u8m(int _samples, uint8_t _start, uint8_t _end)
{
	if(_samples == 0) {
		return 0;
	}
	double value = _start;
	double step = (double(_end) - value) / _samples;
	for(int i=0; i<_samples; i++,value+=step) {
		m_in_buffer.push_back(uint8_t(value));
	}
	return _samples;
}

void MixerChannel::mix_samples(int _rate, uint16_t _format, uint16_t _channels)
{
	const SDL_AudioSpec &spec = m_mixer->get_audio_spec();
	int in_size = m_in_buffer.size();

	if(in_size == 0) {
		PDEBUGF(LOG_V2, LOG_MIXER, "channel active but empty\n");
		return;
	}

	//to floats in [-1,1]
	std::vector<float> in_buf, SRC_in_buf;
	switch(_format) {
		case MIXER_FORMAT_U8:
			in_buf.resize(in_size);
			for(int i=0; i<in_size; i++) {
				float fvalue = float(m_in_buffer[i]);
				in_buf[i] = (fvalue - 128.f) / 128.f;
			}
			break;
		case MIXER_FORMAT_S16:
			in_size = in_size / 2;
			in_buf.resize(in_size);
			for(int i=0; i<in_size; i++) {
				float fvalue = float(*(int16_t*)(&m_in_buffer[i*2]));
				in_buf[i] = fvalue / 32768.f;
			}
			break;
		default:
			PERRF_ABORT(LOG_MIXER, "unsupported sample format\n");
			return;
	}

	m_in_buffer.clear();

	int in_frames = in_size / _channels;

	if(spec.channels != _channels) {
		int SRC_in_size = in_frames * spec.channels;
		SRC_in_buf.resize(SRC_in_size);
		if(_channels==1 && spec.channels==2) {
			PDEBUGF(LOG_V2, LOG_MIXER, "from mono to stereo\n");
			for(int i=0; i<in_size; i++) {
				float v = in_buf[i];
				SRC_in_buf[i*2]   = v;
				SRC_in_buf[i*2+1] = v;
			}
		} else if(_channels==2 && spec.channels==1) {
			PDEBUGF(LOG_V2, LOG_MIXER, "from stereo to mono\n");
			for(int i=0; i<SRC_in_size; i++) {
				SRC_in_buf[i] = (in_buf[i*2] + in_buf[i*2+1]) / 2.f;
			}
		} else {
			PERRF_ABORT(LOG_MIXER, "unsupported number of channels\n");
			return;
		}
	} else {
		SRC_in_buf = in_buf;
	}

#if HAVE_LIBSAMPLERATE
	if(spec.freq != _rate) {
		PDEBUGF(LOG_V2, LOG_MIXER, "resampling from %dHz to %dHz\n", _rate, spec.freq);
		double ratio = double(spec.freq) / double(_rate);
		int offset = m_out_frames * spec.channels;
		int out_frames = int(ceil(double(in_frames) * ratio));
		int out_size = out_frames * spec.channels;
		if(m_SRC_state == NULL) {
			int err;
			m_SRC_state = src_new(SRC_SINC_MEDIUM_QUALITY, spec.channels, &err);
			if(m_SRC_state == NULL) {
				PERRF_ABORT(LOG_MIXER, "unable to initialize SRC state: %d\n", err);
				return;
			}
		}
		if(m_out_buffer.size() < unsigned(offset+out_size)) {
			m_out_buffer.resize(offset+out_size);
		}
		SRC_DATA srcdata;
		srcdata.data_in = &SRC_in_buf[0];
		srcdata.data_out = &m_out_buffer[offset];
		srcdata.input_frames = in_frames;
		srcdata.output_frames = out_frames;
		srcdata.src_ratio = double(spec.freq) / double(_rate);
		srcdata.end_of_input = 0;
		int result = src_process(m_SRC_state, &srcdata);
		if(result != 0) {
			PERRF(LOG_MIXER, "error resampling: %s (%d)\n", src_strerror(result), result);
			return;
		}
		if(srcdata.output_frames_gen != out_frames) {
			PDEBUGF(LOG_V2, LOG_MIXER, "frames in=%d, frames out=%d\n",
					m_out_frames, srcdata.output_frames_gen);
			ASSERT(srcdata.output_frames_gen < out_frames);
			out_frames = srcdata.output_frames_gen;
		}
		m_out_frames += out_frames;
	} else {
#else
	if(spec.freq == _rate) {
#endif
		int offset = m_out_frames * spec.channels;
		int out_size = (in_frames+m_out_frames) * spec.channels;
		if(m_out_buffer.size() < unsigned(out_size)) {
			m_out_buffer.resize(out_size);
		}
		std::copy(SRC_in_buf.begin(), SRC_in_buf.end(), m_out_buffer.begin() + offset);
		m_out_frames += in_frames;
	}
}

void MixerChannel::pop_frames(int _frames_to_pop)
{
	const SDL_AudioSpec &spec = m_mixer->get_audio_spec();
	int leftover = m_out_frames - _frames_to_pop;
	if(leftover > 0) {
		auto start = m_out_buffer.begin() + _frames_to_pop*spec.channels;
		auto end = start + leftover*spec.channels;
		std::copy(start, end, m_out_buffer.begin());
		m_out_frames = leftover;
	} else {
		m_out_frames = 0;
	}
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
