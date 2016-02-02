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
#include "audiospec.h"
#include "wav.h"
#include "utils.h"


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
	uint64_t duration_us() const { return m_spec.frames_to_us(frames()); }
	void resize_frames(unsigned _num_frames);
	void resize_samples(unsigned _num_samples);
	void resize_frames_silence(unsigned _num_frames);
	void clear();
	void reserve_us(uint64_t _us);
	template<typename T> void add_samples(const std::vector<T> &_data);
	template<typename T> void add_samples(const std::vector<T> &_data, unsigned _count);
	void add_frames(const AudioBuffer &_source);
	void add_frames(const AudioBuffer &_source, unsigned _frames_count);
	void pop_frames(unsigned _frames_to_pop);
	template<typename T> unsigned fill_samples(unsigned _samples, T _value);
	template<typename T> unsigned fill_samples_us(uint64_t _duration_us, T _value);
	unsigned fill_frames_silence(unsigned _samples);
	unsigned fill_samples_silence(unsigned _samples);
	unsigned fill_us_silence(uint64_t _duration_us);
	template<typename T> unsigned fill_frames_fade(unsigned _frames, T _v0, T _v1);
	template<typename T> unsigned fill_frames_fade(unsigned _frames, T _v0l, T _v0r, T _v1);
	void convert(const AudioSpec &_new_spec);
	void convert_format(AudioBuffer &_dest, unsigned _frames_count);
	void convert_channels(AudioBuffer &_dest, unsigned _frames_count);
	void convert_rate(AudioBuffer &_dest, unsigned _frames_count, SRC_STATE *_src);
	unsigned us_to_frames(uint64_t _us);
	unsigned us_to_samples(uint64_t _us);
	void apply_volume(float _volume);

	// direct sample access no checks
	template<typename T> const T& operator[](unsigned _pos) const;
	template<typename T> T& operator[](unsigned _pos);
	// direct sample access with checks
	template<typename T> const T& at(unsigned _pos) const;
	template<typename T> T& at(unsigned _pos);

	//TODO? an audio file interface can be defined but I don't think anything
	//other than WAVs will ever be used.
	void load(const WAVFile &_wav);

private:
	constexpr static float u8_to_f32(uint8_t _s) {
		return (float(_s) - 128.f) / 128.f;
	}
	constexpr static float s16_to_f32(int16_t _s) {
		return float(_s) / 32768.f;
	}
	inline static uint8_t f32_to_u8(float _s) {
		return uint8_t(clamp((_s*128.f + 128.f), 0.f, 255.f));
	}
	inline static int16_t f32_to_s16(float _s) {
		return int16_t(clamp((_s*32768.f), -32768.f, 32767.f));
	}
	static void u8_to_f32(const std::vector<uint8_t> &_source,
			std::vector<uint8_t> &_dest, unsigned _samples);
	static void s16_to_f32(const std::vector<uint8_t> &_source,
			std::vector<uint8_t> &_dest, unsigned _samples);
	static void f32_to_s16(const std::vector<uint8_t> &_source,
			std::vector<uint8_t> &_dest, unsigned _samples);
	template<typename T>
	static void convert_channels(const AudioBuffer &_source, AudioBuffer &_dest,
			unsigned _frames);
	template<typename T> void apply(std::function<double(double)>);
	void apply_u8(std::function<float(float)> _fn);
	void apply_s16(std::function<float(float)>);
	void apply_f32(std::function<float(float)>);

};


template<typename T> const T& AudioBuffer::operator[](unsigned _pos) const
{
	return reinterpret_cast<const T&>(*(&m_data[_pos*sample_size()]));
}

template<typename T> T& AudioBuffer::operator[](unsigned _pos)
{
	return const_cast<T&>(static_cast<const AudioBuffer*>(this)->operator[]<T>(_pos));
}

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
void AudioBuffer::add_samples(const std::vector<T> &_data)
{
	add_samples(_data, _data.size());
}

template<typename T>
void AudioBuffer::add_samples(const std::vector<T> &_data, unsigned _count)
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
	unsigned samples = m_spec.us_to_samples(_duration_us);
	fill_samples(samples, _value);
	return samples;
}

template<typename T>
unsigned AudioBuffer::fill_frames_fade(unsigned _frames, T _v0, T _v1)
{
	if(_frames==0) {
		return 0;
	}
	double s = (double(_v1) - double(_v0)) / _frames;
	double v = double(_v0);
	unsigned i = frames();
	resize_frames(i+_frames);
	for(; i<frames(); ++i,v+=s) {
		at<T>(i) = T(v);
	}
	return _frames;
}

template<typename T>
unsigned AudioBuffer::fill_frames_fade(unsigned _frames, T _v0l, T _v0r, T _v1)
{
	if(_frames==0) {
		return 0;
	}
	double sl = (double(_v1) - double(_v0l)) / _frames;
	double sr = (double(_v1) - double(_v0r)) / _frames;
	double vl = double(_v0l);
	double vr = double(_v0r);
	unsigned i = frames();
	resize_frames(i+_frames);
	for(; i<frames(); ++i,vl+=sl,vr+=sr) {
		at<T>(i*2)   = T(vl);
		at<T>(i*2+1) = T(vr);
	}
	return _frames;
}

#endif
