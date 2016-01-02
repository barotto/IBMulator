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
#if HAVE_LIBSAMPLERATE
#include <samplerate.h>
#endif

class Mixer;

typedef std::function<int(
		int _mix_time_slice,
		bool _prebuffering,
		bool _first_update
	)> MixerChannel_handler;

class MixerChannel
{
	Mixer *m_mixer;
	//enabling/disabling can be performed by the machine thread
	std::atomic<bool> m_enabled;
	std::string m_name;
	MixerChannel_handler m_update_clbk;
	std::atomic<uint64_t> m_disable_time;
	uint64_t m_disable_timeout;
	bool m_first_update;
	std::vector<uint8_t> m_in_buffer;
	std::vector<float> m_out_buffer;
	int m_out_frames;
#if HAVE_LIBSAMPLERATE
	SRC_STATE *m_SRC_state;
#endif
	std::function<void(bool)> m_capture_clbk;
	std::recursive_mutex m_lock;

public:

	MixerChannel(Mixer *_mixer, MixerChannel_handler _callback, const std::string &_name);
	~MixerChannel();

	// The machine thread can call only these methods:
	void enable(bool _enabled);
	inline bool is_enabled() { return m_enabled.load(); }

	// The mixer thread can call also these methods:
	void add_samples(uint8_t *_data, size_t _len);
	template<typename T>
	int fill_samples(int _duration_us, int _rate, T _value);
	template<typename T>
	int fill_samples(int _samples, T _value);
	int fill_samples_fade_u8m(int _samples, uint8_t _start, uint8_t _end);
	void mix_samples(int _rate, uint16_t _format, uint16_t _channels);
	inline const std::vector<float> & get_out_buffer() { return m_out_buffer; }
	inline int get_out_frames() { return m_out_frames; }
	void pop_frames(int _count);

	int update(int _mix_tslice, bool _prebuffering);
	void set_disable_time(uint64_t _time) { m_disable_time.store(_time); }
	bool check_disable_time(uint64_t _now_us);
	void set_disable_timeout(uint64_t _timeout_us) { m_disable_timeout = _timeout_us; }

	void register_capture_clbk(std::function<void(bool _enable)> _fn);
	void on_capture(bool _enable);

	void lock()   { m_lock.lock(); }
	void unlock() { m_lock.unlock(); }
};

template<typename T>
int MixerChannel::fill_samples(int _duration_us, int _rate, T _value)
{
	int samples = double(_duration_us) * double(_rate)/1e6;
	std::vector<T> buf(samples);
	std::fill(buf.begin(), buf.begin()+samples, _value);
	add_samples(&buf[0], samples*sizeof(T));
	return samples;
}

template<typename T>
int MixerChannel::fill_samples(int _samples, T _value)
{
	std::vector<T> buf(_samples);
	std::fill(buf.begin(), buf.begin()+_samples, _value);
	add_samples(&buf[0], _samples*sizeof(T));
	return _samples;
}

#endif
