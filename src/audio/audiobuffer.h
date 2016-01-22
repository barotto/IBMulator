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

#ifndef IBMULATOR_AUDIOBUFFER_H
#define IBMULATOR_AUDIOBUFFER_H

#include <vector>
#if HAVE_LIBSAMPLERATE
#include <samplerate.h>
#else
typedef void SRC_STATE;
#endif
#include <SDL2/SDL_audio.h>
#include "wav.h"

//direct mapping to SDL2 defines
//you can therefore use SDL2 format macros
enum AudioFormat
{
	AUDIO_FORMAT_U8 = AUDIO_U8,
	AUDIO_FORMAT_S16 = AUDIO_S16,
	AUDIO_FORMAT_F32 = AUDIO_F32
};

struct AudioSpec
{
	AudioFormat format;
	unsigned channels;
	unsigned rate;

	bool operator!=(const AudioSpec &_s) const {
		return (format!=_s.format) || (channels!=_s.channels) || (rate!=_s.rate);
	}
};

class AudioBuffer
{
private:
	std::vector<uint8_t> m_data;
	AudioSpec m_spec;

public:
	AudioBuffer();
	AudioBuffer(const AudioSpec &_spec);
	//if dtor is defined then create move operator

	void set_spec(const AudioSpec &_spec);
	AudioFormat format() const { return m_spec.format; }
	unsigned channels() const { return m_spec.channels; }
	unsigned rate() const { return m_spec.rate; }
	const AudioSpec & spec() const { return m_spec; }
	unsigned sample_size() const { return SDL_AUDIO_BITSIZE(m_spec.format)/8; }
	unsigned frame_size() const { return sample_size()*m_spec.channels; }
	unsigned frames() const { return (m_data.size()/frame_size()); }
	unsigned samples() const { return (m_data.size()/sample_size()); }
	void resize_frames(unsigned _num_frames);
	void resize_samples(unsigned _num_samples);
	void clear();
	template<typename T> void add_samples(const vector<T> &_data);
	template<typename T> void add_samples(const vector<T> &_data, unsigned _count);
	void add_frames(const AudioBuffer &_source, unsigned _frames_count);
	void pop_frames(unsigned _frames_to_pop);
	template<typename T> unsigned fill_samples(unsigned _samples, T _value);
	template<typename T> unsigned fill_samples_us(uint64_t _duration_us, T _value);
	void convert_format(AudioBuffer &_dest, unsigned _frames_count);
	void convert_channels(AudioBuffer &_dest, unsigned _frames_count);
	void convert_rate(AudioBuffer &_dest, unsigned _frames_count, SRC_STATE *_src);

	// direct sample access no checks
	template<typename T> T& operator[](unsigned _pos) {
		return reinterpret_cast<T&>(*(&m_data[_pos*sample_size()]));
	}
	// direct sample access with checks
	template<typename T> const T& at(unsigned _pos) const;
	template<typename T> T& at(unsigned _pos);


	//TODO? an audio file interface can be defined but I don't think anything
	//other than WAVs will ever be used.
	void load(const WAVFile &_wav);

private:
	static void u8_to_f32(const std::vector<uint8_t> &_source,
			std::vector<uint8_t> &_dest, unsigned _samples);
	static void s16_to_f32(const std::vector<uint8_t> &_source,
			std::vector<uint8_t> &_dest, unsigned _samples);
	template<typename T>
	static void convert_channels(const AudioBuffer &_source, AudioBuffer &_dest,
			unsigned _frames);
};


template<typename T>
const T& AudioBuffer::at(unsigned _pos) const
{
	if(sizeof(T) != sample_size()) {
		throw std::logic_error("invalid type");
	}
	unsigned byteidx = _pos*sample_size();
	if(byteidx+sample_size() > m_data.size()) {
		throw std::out_of_range("");
	}
	return reinterpret_cast<const T&>(*(&m_data[byteidx]));
}

template<typename T>
T& AudioBuffer::at(unsigned _pos)
{
	return const_cast<T&>(static_cast<const AudioBuffer*>(this)->at<T>(_pos));
}

template<typename T>
void AudioBuffer::add_samples(const vector<T> &_data)
{
	add_samples(_data, _data.size());
}

template<typename T>
void AudioBuffer::add_samples(const vector<T> &_data, unsigned _count)
{
	if(sizeof(T) != sample_size()) {
		throw std::logic_error("invalid type");
	}
	auto start = reinterpret_cast<const uint8_t*>(&(*_data.begin()));
	auto end = start + std::min(sizeof(T)*_count, sizeof(T)*_data.size());
	m_data.insert(m_data.end(), start, end);
}

template<typename T>
unsigned AudioBuffer::fill_samples(unsigned _samples, T _value)
{
	unsigned i = samples();
	resize_samples(i+_samples);
	for(; i<samples(); ++i) {
		(*this).operator[]<T>(i) = _value;
	}
	return _samples;
}

template<typename T>
unsigned AudioBuffer::fill_samples_us(uint64_t _duration_us, T _value)
{
	unsigned samples = (double(_duration_us) * double(m_spec.rate)/1e6) * m_spec.channels;
	fill_samples(samples, _value);
	return samples;
}


#endif
