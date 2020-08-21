/*
 * Copyright (C) 2015-2020  Marco Bortolin
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
#include "audiobuffer.h"


AudioBuffer::AudioBuffer()
{
	/* sensible defaults */
	set_spec({AUDIO_FORMAT_S16, 1, 44100});
}

AudioBuffer::AudioBuffer(const AudioSpec &_spec)
{
	set_spec(_spec);
}

void AudioBuffer::set_spec(const AudioSpec &_spec)
{
	m_spec.format = _spec.format;
	m_spec.channels = std::min(2U,_spec.channels);
	m_spec.rate = _spec.rate;
	m_data.clear();
}

void AudioBuffer::resize_frames(unsigned _num_frames)
{
	const unsigned ss = sample_size();
	const unsigned ch = channels();
	m_data.resize(ss*ch*_num_frames);
}

void AudioBuffer::resize_samples(unsigned _num_samples)
{
	const unsigned ss = sample_size();
	m_data.resize(ss*_num_samples);
}

void AudioBuffer::resize_frames_silence(unsigned _new_frame_size)
{
	if(_new_frame_size == frames()) {
		return;
	} else if(_new_frame_size < frames()) {
		resize_frames(_new_frame_size);
	} else {
		fill_frames_silence(_new_frame_size - frames());
	}
}

void AudioBuffer::clear()
{
	m_data.clear();
}

void AudioBuffer::reserve_us(uint64_t _us)
{
	unsigned bytes = round(m_spec.us_to_samples(_us)) * sample_size();
	reserve_bytes(bytes);
}

void AudioBuffer::reserve_frames(unsigned _frames)
{
	reserve_bytes(frame_size() * _frames);
}

void AudioBuffer::reserve_bytes(uint64_t _bytes)
{
	m_data.reserve(_bytes);
}

void AudioBuffer::add_frames(const AudioBuffer &_source)
{
	add_frames(_source, _source.frames());
}

void AudioBuffer::add_frames(const AudioBuffer &_source, unsigned _frames_count)
{
	if(_source.spec() != m_spec) {
		throw std::logic_error("sound buffers must have the same spec");
	}
	_frames_count = std::min(_frames_count, _source.frames());
	if(_frames_count == 0) {
		return;
	}
	unsigned datalen = _frames_count * frame_size();
	auto srcstart = _source.m_data.begin();
	m_data.insert(m_data.end(), srcstart, srcstart+datalen);
}

void AudioBuffer::pop_frames(unsigned _frames_to_pop)
{
	if(_frames_to_pop < frames()) {
		auto first = m_data.begin() + _frames_to_pop*frame_size();
		std::copy(first, m_data.end(), m_data.begin());
		resize_frames(frames()-_frames_to_pop);
	} else {
		clear();
	}
}

unsigned AudioBuffer::fill_frames_silence(unsigned _frames)
{
	return fill_samples_silence(m_spec.frames_to_samples(_frames));
}

unsigned AudioBuffer::fill_samples_silence(unsigned _samples)
{
	switch(m_spec.format) {
		case AUDIO_FORMAT_U8:
			return fill_samples<uint8_t>(_samples, 128);
		case AUDIO_FORMAT_S16:
			return fill_samples<int16_t>(_samples, 0);
		case AUDIO_FORMAT_F32:
			return fill_samples<float>(_samples, 0.f);
		default:
			throw std::logic_error("unsupported format");
	}
}

unsigned AudioBuffer::fill_us_silence(uint64_t _duration_us)
{
	return fill_samples_silence(round(m_spec.us_to_samples(_duration_us)));
}

unsigned AudioBuffer::fill_ns_silence(uint64_t _duration_ns)
{
	return fill_samples_silence(round(m_spec.ns_to_samples(_duration_ns)));
}

void AudioBuffer::convert(const AudioSpec &_new_spec)
{
	if(_new_spec == m_spec) {
		return;
	}

	AudioSpec new_spec = _new_spec;
	AudioBuffer dest[2];
	unsigned bufidx = 0;
	AudioBuffer *source = this;

	if(source->rate() != new_spec.rate) {
#if HAVE_LIBSAMPLERATE
		if(source->format() != AUDIO_FORMAT_F32) {
			dest[1].set_spec({AUDIO_FORMAT_F32, source->channels(), source->rate()});
			source->convert_format(dest[1], source->frames());
			source = &dest[1];
		}
		dest[0].set_spec({source->format(), source->channels(), new_spec.rate});
		source->convert_rate(dest[0], source->frames(), nullptr);
		source = &dest[0];
		bufidx = 1;
#else
		new_spec.rate = source->rate();
#endif
	}
	if(source->channels() != new_spec.channels) {
		dest[bufidx].set_spec({source->format(),new_spec.channels,source->rate()});
		source->convert_channels(dest[bufidx], source->frames());
		source = &dest[bufidx];
		bufidx = (bufidx + 1) % 2;
	}
	if(source->format() != new_spec.format) {
		dest[bufidx].set_spec({new_spec.format,source->channels(),source->rate()});
		source->convert_format(dest[bufidx], source->frames());
		source = &dest[bufidx];
	}

	if(new_spec != m_spec) {
		m_data = source->m_data;
		m_spec = new_spec;
	}
}

void AudioBuffer::convert_format(AudioBuffer &_dest, unsigned _frames_count)
{
	AudioSpec destspec{_dest.format(), m_spec.channels, m_spec.rate};

	if(_dest.spec() != destspec) {
		throw std::logic_error("destination must have same channels and rate");
	}

	_frames_count = std::min(frames(),_frames_count);

	if(m_spec.format == destspec.format) {
		_dest.add_frames(*this,_frames_count);
		return;
	}

	const unsigned samples_count = m_spec.frames_to_samples(_frames_count);
	std::vector<uint8_t> buffer, *data=&buffer;
	switch(m_spec.format) {
		case AUDIO_FORMAT_U8:
			u8_to_f32(m_data, buffer, samples_count);
			break;
		case AUDIO_FORMAT_S16:
			s16_to_f32(m_data, buffer, samples_count);
			break;
		case AUDIO_FORMAT_F32:
			data = &m_data;
			break;
		default:
			throw std::logic_error("unsupported source format");
	}
	switch(destspec.format) {
		case AUDIO_FORMAT_S16:
			f32_to_s16(*data, _dest.m_data, samples_count);
			break;
		case AUDIO_FORMAT_F32:
			_dest.m_data.swap(buffer);
			break;
		default:
			throw std::logic_error("unsupported destination format");
	}
}


void AudioBuffer::convert_channels(AudioBuffer &_dest, unsigned _frames_count)
{
	AudioSpec destspec{m_spec.format, _dest.channels(), m_spec.rate};
	if(_dest.spec() != destspec) {
		throw std::logic_error("unsupported format");
	}

	_frames_count = std::min(frames(),_frames_count);
	if(m_spec.channels == destspec.channels) {
		_dest.add_frames(*this,_frames_count);
		return;
	}

	switch(m_spec.format) {
		case AUDIO_FORMAT_U8:
			convert_channels<uint8_t>(*this,_dest,_frames_count);
			break;
		case AUDIO_FORMAT_S16:
			convert_channels<int16_t>(*this,_dest,_frames_count);
			break;
		case AUDIO_FORMAT_F32:
			convert_channels<float>(*this,_dest,_frames_count);
			break;
		default:
			throw std::logic_error("unsupported format");
	}
}

unsigned AudioBuffer::convert_rate(AudioBuffer &_dest, unsigned _frames_count, SRC_STATE *_SRC)
{
	AudioSpec destspec{AUDIO_FORMAT_F32, m_spec.channels, _dest.rate()};
	if(m_spec.format != AUDIO_FORMAT_F32 || _dest.spec() != destspec) {
		throw std::logic_error("unsupported format");
	}
	_frames_count = std::min(frames(),_frames_count);
	double rate_ratio = destspec.rate / m_spec.rate;
	unsigned out_frames = unsigned(ceil(double(_frames_count) * rate_ratio));
	if(out_frames==0) {
		return 0;
	}

	unsigned destpos = _dest.samples();
	unsigned destframes = _dest.frames();

	_dest.resize_frames(_dest.frames()+out_frames);
	unsigned missing = 0;

#if HAVE_LIBSAMPLERATE
	SRC_DATA srcdata;
	srcdata.data_in = &at<float>(0);
	srcdata.data_out = &_dest.at<float>(destpos);
	srcdata.input_frames = _frames_count;
	srcdata.output_frames = out_frames;
	srcdata.src_ratio = rate_ratio;
	int srcresult;
	if(_SRC != nullptr) {
		src_set_ratio(_SRC, rate_ratio);
		srcdata.end_of_input = 0;
		srcresult = src_process(_SRC, &srcdata);
	} else {
		srcdata.end_of_input = 1;
		srcresult = src_simple(&srcdata, SRC_SINC_BEST_QUALITY, destspec.channels) ;
	}
	if(srcresult != 0) {
		throw std::runtime_error(std::string("error resampling: ") + src_strerror(srcresult));
	}
	assert(srcdata.output_frames_gen>=0 && unsigned(srcdata.output_frames_gen)<=out_frames);
	if(unsigned(srcdata.output_frames_gen) != out_frames) {
		_dest.resize_frames(destframes + srcdata.output_frames_gen);
		missing = out_frames - srcdata.output_frames_gen;
	}
	PDEBUGF(LOG_V2, LOG_MIXER, "Audio buf convert rate: fr-in: %d, req.fr-out: %d, gen: %d, missing: %d\n",
			_frames_count, out_frames, srcdata.output_frames_gen, missing);
#else
	for(unsigned i=destpos; i<_dest.samples(); ++i) {
		_dest.operator[]<float>(i) = 0.f;
	}
#endif
	return missing;
}

double AudioBuffer::us_to_frames(uint64_t _us)
{
	return std::min(double(frames()), m_spec.us_to_frames(_us));
}

double AudioBuffer::ns_to_frames(uint64_t _ns)
{
	return std::min(double(frames()), m_spec.ns_to_frames(_ns));
}

double AudioBuffer::us_to_samples(uint64_t _us)
{
	return std::min(double(samples()), m_spec.us_to_samples(_us));
}

void AudioBuffer::apply_volume(float _volume)
{
	auto fn = [=](float _sample) -> float{
		return _sample * _volume;
	};
	switch(m_spec.format) {
		case AUDIO_FORMAT_U8:
			apply_u8(fn);
			break;
		case AUDIO_FORMAT_S16:
			apply_s16(fn);
			break;
		case AUDIO_FORMAT_F32:
			apply_f32(fn);
			break;
		default:
			throw std::logic_error("unsupported format");
	}
}

void AudioBuffer::load(const WAVFile &_wav)
{
	if(!_wav.is_open()) {
		throw std::logic_error("file is not open");
	}
	if(_wav.format() != WAV_FORMAT_PCM && _wav.format() != WAV_FORMAT_IEEE_FLOAT) {
		throw std::logic_error("unsupported data format");
	}
	if(_wav.channels() > 2) {
		throw std::logic_error("unsupported number of channels");
	}
	AudioFormat format;
	switch(_wav.bits()) {
		case 8:
			format = AUDIO_FORMAT_U8;
			break;
		case 16:
			format = AUDIO_FORMAT_S16;
			break;
		case 32:
			if(_wav.format() == WAV_FORMAT_IEEE_FLOAT) {
				format = AUDIO_FORMAT_F32;
			} else {
				throw std::logic_error("unsupported data format");
			}
			break;
		default:
			throw std::logic_error("unsupported data format");
	}
	set_spec({format,  _wav.channels(), double(_wav.rate())});
	m_data = _wav.read_audio_data();
}

void AudioBuffer::u8_to_f32(const std::vector<uint8_t> &_source,
		std::vector<uint8_t> &_dest, unsigned _samples_count)
{
	unsigned d = _dest.size();
	_dest.resize(_dest.size()+_samples_count*4);
	for(unsigned s=0; s<_samples_count; ++s) {
		reinterpret_cast<float&>(_dest[d+s*4]) = u8_to_f32(_source[s]);
	}
}

void AudioBuffer::s16_to_f32(const std::vector<uint8_t> &_source,
		std::vector<uint8_t> &_dest, unsigned _samples_count)
{
	unsigned d = _dest.size();
	_dest.resize(_dest.size()+_samples_count*4);
	for(unsigned s=0; s<_samples_count; ++s) {
		int16_t s16sample = reinterpret_cast<const int16_t&>(_source[s*2]);
		reinterpret_cast<float&>(_dest[d+s*4]) = s16_to_f32(s16sample);
	}
}

void AudioBuffer::f32_to_s16(const std::vector<uint8_t> &_source,
		std::vector<uint8_t> &_dest, unsigned _samples_count)
{
	unsigned d = _dest.size();
	_dest.resize(_dest.size()+_samples_count*2);
	for(unsigned s=0; s<_samples_count; ++s) {
		float f32sample = reinterpret_cast<const float&>(_source[s*4]);
		reinterpret_cast<int16_t&>(_dest[d+s*2]) = f32_to_s16(f32sample);
	}
}

template<typename T>
void AudioBuffer::convert_channels(const AudioBuffer &_source, AudioBuffer &_dest,
		unsigned _frames_count)
{
	unsigned d = _dest.samples();
	_dest.resize_frames(_dest.frames()+_frames_count);
	if(_source.channels()==1 && _dest.channels()==2) {
		//from mono to stereo
		for(unsigned i=0; i<_frames_count; ++i) {
			T ch1 = _source.at<T>(i);
			_dest.operator[]<T>(d+i*2)   = ch1;
			_dest.operator[]<T>(d+i*2+1) = ch1;
		}
	} else if(_source.channels()==2 && _dest.channels()==1) {
		//from stereo to mono
		for(unsigned i=0; i<_frames_count; ++i) {
			double ch1 = double(_source.at<T>(i*2));
			double ch2 = double(_source.at<T>(i*2+1));
			_dest.operator[]<T>(d+i) = T((ch1 + ch2) / 2.0);
		}
	}
}

void AudioBuffer::apply_u8(std::function<float(float)> _fn)
{
	for(unsigned i=0; i<samples(); ++i) {
		float result = _fn(u8_to_f32(m_data[i]));
		m_data[i] = f32_to_u8(result);
	}
}

void AudioBuffer::apply_s16(std::function<float(float)> _fn)
{
	int16_t *data = &at<int16_t>(0);
	for(unsigned i=0; i<samples(); ++i) {
		float result = _fn(s16_to_f32(data[i]));
		data[i] = f32_to_u8(result);
	}
}

void AudioBuffer::apply_f32(std::function<float(float)> _fn)
{
	float *data = &at<float>(0);
	for(unsigned i=0; i<samples(); ++i) {
		data[i] = _fn(data[i]);
	}
}
