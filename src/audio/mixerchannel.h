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

#ifndef IBMULATOR_MIXERCHANNEL_H
#define IBMULATOR_MIXERCHANNEL_H

#include <atomic>
#include <vector>
#include "audiobuffer.h"

class Mixer;

typedef std::function<int(
		int _mix_time_slice,
		bool _prebuffering,
		bool _first_update
	)> MixerChannel_handler;


class MixerChannel
{
private:
	Mixer *m_mixer;
	//enabling/disabling can be performed by the machine thread
	std::atomic<bool> m_enabled;
	std::string m_name;
	MixerChannel_handler m_update_clbk;
	std::atomic<uint64_t> m_disable_time;
	uint64_t m_disable_timeout;
	bool m_first_update;
	AudioBuffer m_in_buffer;
	AudioBuffer m_out_buffer;
	uint64_t m_in_time;
	SRC_STATE *m_SRC_state;
	std::function<void(bool)> m_capture_clbk;

public:
	MixerChannel(Mixer *_mixer, MixerChannel_handler _callback, const std::string &_name);
	~MixerChannel();

	// The machine thread can call only these methods:
	void enable(bool _enabled);
	inline bool is_enabled() { return m_enabled; }

	// The mixer thread can call also these methods:
	void set_input_spec(const AudioSpec &_spec);
	void set_output_spec(const AudioSpec &_spec);
	template<typename T> void add_samples(const vector<T> &_data);
	template<typename T> void add_samples(const vector<T> &_data, unsigned _count);
	template<typename T> unsigned fill_samples(unsigned _samples, T _value);
	template<typename T> unsigned fill_samples_us(uint64_t _duration_us, T _value);
	template<typename T>
	unsigned fill_frames_fade(unsigned _frames, T _v0, T _v1);
	template<typename T>
	unsigned fill_frames_fade(unsigned _frames, T _v0l, T _v0r, T _v1);
	void play(const AudioBuffer &_sample, uint64_t _at_time);
	void input_start(uint64_t _time);
	void input_finish(uint64_t _time=0);
	const float & get_out_data() { return m_out_buffer.at<float>(0); }
	inline int out_frames_count() { return m_out_buffer.frames(); }
	inline int in_frames_count() { return m_in_buffer.frames(); }
	void pop_out_frames(unsigned _count);

	int update(int _mix_tslice, bool _prebuffering);
	void set_disable_time(uint64_t _time) { m_disable_time = _time; }
	bool check_disable_time(uint64_t _now_us);
	void set_disable_timeout(uint64_t _timeout_us) { m_disable_timeout = _timeout_us; }

	void register_capture_clbk(std::function<void(bool _enable)> _fn);
	void on_capture(bool _enable);

private:
	void reset_SRC();
};


template<typename T>
void MixerChannel::add_samples(const vector<T> &_data)
{
	if(!_data.empty()) {
		m_in_buffer.add_samples(_data);
	}
}

template<typename T>
void MixerChannel::add_samples(const vector<T> &_data, unsigned _count)
{
	if(!_data.empty()) {
		m_in_buffer.add_samples(_data, _count);
	}
}

template<typename T>
unsigned MixerChannel::fill_samples(unsigned _samples, T _value)
{
	if(_samples>0) {
		m_in_buffer.fill_samples(_samples, _value);
	}
	return _samples;
}

template<typename T>
unsigned MixerChannel::fill_samples_us(uint64_t _duration_us, T _value)
{
	if(_duration_us>0) {
		return m_in_buffer.fill_samples_us(_duration_us, _value);
	}
	return 0;
}

template<typename T>
unsigned MixerChannel::fill_frames_fade(unsigned _frames, T _v0, T _v1)
{
	if(_frames==0) {
		return 0;
	}
	double s = (double(_v1) - double(_v0)) / _frames;
	double v = double(_v0);
	unsigned i = m_in_buffer.frames();
	m_in_buffer.resize_frames(i+_frames);
	for(; i<m_in_buffer.frames(); ++i,v+=s) {
		m_in_buffer.at<T>(i) = T(v);
	}
	return _frames;
}

template<typename T>
unsigned MixerChannel::fill_frames_fade(unsigned _frames, T _v0l, T _v0r, T _v1)
{
	if(_frames==0) {
		return 0;
	}
	double sl = (double(_v1) - double(_v0l)) / _frames;
	double sr = (double(_v1) - double(_v0r)) / _frames;
	double vl = double(_v0l);
	double vr = double(_v0r);
	unsigned i = m_in_buffer.frames();
	m_in_buffer.resize_frames(i+_frames);
	for(; i<m_in_buffer.frames(); ++i,vl+=sl,vr+=sr) {
		m_in_buffer.at<T>(i*2)   = T(vl);
		m_in_buffer.at<T>(i*2+1) = T(vr);
	}
	return _frames;
}

#endif
