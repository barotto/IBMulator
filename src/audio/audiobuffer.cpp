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

void AudioBuffer::clear()
{
	m_data.clear();
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

void AudioBuffer::convert_format(AudioBuffer &_dest, unsigned _frames_count)
{
	AudioSpec destspec{_dest.format(), m_spec.channels, m_spec.rate};
	// only 2 conversions implemented, currently we don't need more
	if(_dest.spec() != destspec || destspec.format!=AUDIO_FORMAT_F32) {
		throw std::logic_error("unsupported format");
	}

	_frames_count = std::min(frames(),_frames_count);
	if(m_spec.format == destspec.format) {
		_dest.add_frames(*this,_frames_count);
		return;
	}

	const unsigned samples_count = _frames_count*m_spec.channels;

	switch(m_spec.format) {
		case AUDIO_FORMAT_U8:
			u8_to_f32(m_data, _dest.m_data, samples_count);
			break;
		case AUDIO_FORMAT_S16:
			s16_to_f32(m_data, _dest.m_data, samples_count);
			break;
		default:
			throw std::logic_error("unsupported format");
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

void AudioBuffer::convert_rate(AudioBuffer &_dest, unsigned _frames_count, SRC_STATE *_SRC)
{
	AudioSpec destspec{AUDIO_FORMAT_F32, m_spec.channels, _dest.rate()};
	if(m_spec.format != AUDIO_FORMAT_F32 || _dest.spec() != destspec) {
		throw std::logic_error("unsupported format");
	}
	_frames_count = std::min(frames(),_frames_count);
	double rate_ratio = double(destspec.rate)/double(m_spec.rate);
	unsigned out_frames = unsigned(ceil(double(_frames_count) * rate_ratio));
	if(out_frames==0) {
		return;
	}

	unsigned destpos = _dest.samples();
	unsigned destframes = _dest.frames();

	_dest.resize_frames(_dest.frames()+out_frames);

#if HAVE_LIBSAMPLERATE
	assert(_SRC != nullptr);
	SRC_DATA srcdata;
	srcdata.data_in = &at<float>(0);
	srcdata.data_out = &_dest.at<float>(destpos);
	srcdata.input_frames = _frames_count;
	srcdata.output_frames = out_frames;
	srcdata.src_ratio = rate_ratio;
	srcdata.end_of_input = 0;
	int srcresult = src_process(_SRC, &srcdata);
	if(srcresult != 0) {
		throw std::runtime_error(std::string("error resampling: ") + src_strerror(srcresult));
	}
	assert(srcdata.output_frames_gen>=0 && srcdata.output_frames_gen<=out_frames);
	if(srcdata.output_frames_gen != out_frames) {
		_dest.resize_frames(destframes + srcdata.output_frames_gen);
	}
#else
	for(unsigned i=destpos; i<_dest.samples(); ++i) {
		_dest.operator[]<float>(i) = 0.f;
	}
#endif
}

void AudioBuffer::load(const WAVFile &_wav)
{
	if(!_wav.is_open()) {
		throw std::logic_error("file is not open");
	}
	if(_wav.format() != WAV_FORMAT_PCM || _wav.format() != WAV_FORMAT_IEEE_FLOAT) {
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
	set_spec({format,  _wav.channels(), _wav.rate()});
	m_data = _wav.read();
}

void AudioBuffer::u8_to_f32(const std::vector<uint8_t> &_source,
		std::vector<uint8_t> &_dest, unsigned _samples_count)
{
	unsigned d = _dest.size();
	_dest.resize(_dest.size()+_samples_count*4);
	for(unsigned s=0; s<_samples_count; ++s) {
		reinterpret_cast<float&>(_dest[d+s*4]) = (float(_source[s]) - 128.f) / 128.f;
	}
}

void AudioBuffer::s16_to_f32(const std::vector<uint8_t> &_source,
		std::vector<uint8_t> &_dest, unsigned _samples_count)
{
	unsigned d = _dest.size();
	_dest.resize(_dest.size()+_samples_count*4);
	for(unsigned s=0; s<_samples_count; ++s) {
		int16_t s16sample = reinterpret_cast<const int16_t&>(_source[s*2]);
		reinterpret_cast<float&>(_dest[d+s*4]) = float(s16sample) / 32768.f ;
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
