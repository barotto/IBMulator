/*
 * Copyright (C) 2015-2023  Marco Bortolin
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
#include <memory>
#include "audiobuffer.h"
#include "dsp/Dsp.h"

class Mixer;

typedef std::function<bool(
		uint64_t _time_span_us,
		bool     _prebuffering,
		bool     _first_update
	)> MixerChannel_handler;


class MixerChannel
{
public:
	enum Category
	{
		AUDIOCARD = 0,
		SOUNDFX   = 1,
		GUI       = 2,

		MAX
	};

private:
	Mixer *m_mixer;
	//enabling/disabling can be performed by the machine thread
	std::atomic<bool> m_enabled;
	std::string m_name;
	int m_id;
	MixerChannel_handler m_update_clbk;
	std::atomic<uint64_t> m_disable_time;
	uint64_t m_disable_timeout;
	bool m_first_update;
	uint64_t m_last_time_span_ns;
	AudioBuffer m_in_buffer;
	AudioBuffer m_out_buffer;
	uint64_t m_in_time;
	SRC_STATE *m_SRC_state;
	bool m_new_data;
	std::function<void(bool)> m_capture_clbk;
	std::atomic<float> m_volume;
	Category m_category;
	double m_fr_rem;
	std::vector<std::shared_ptr<Dsp::Filter>> m_filters;

public:
	MixerChannel(Mixer *_mixer, MixerChannel_handler _callback, const std::string &_name, int _id, Category _cat);
	~MixerChannel();

	// The machine thread can call only these methods:
	void enable(bool _enabled);
	inline bool is_enabled() { return m_enabled; }
	void set_volume(float _vol) { m_volume = _vol; }
	float volume() const { return m_volume; }

	void set_filters(std::string _filters_def);
	void set_filters(std::vector<std::shared_ptr<Dsp::Filter>> _filters);
	
	// The mixer thread can call also these methods:
	void set_in_spec(const AudioSpec &_spec);
	void set_out_spec(const AudioSpec &_spec);
	const AudioSpec & in_spec() const { return m_in_buffer.spec(); }
	const AudioSpec & out_spec() const { return m_out_buffer.spec(); }
	void play(const AudioBuffer &_wave);
	void play(const AudioBuffer &_wave, uint64_t _time_dist);
	void play(const AudioBuffer &_wave, float _volume, uint64_t _time_dist);
	void play_frames(const AudioBuffer &_wave, unsigned _frames, uint64_t _time_dist);
	void play_loop(const AudioBuffer &_wave);
	void play_silence(unsigned _frames, uint64_t _time_dist_us);
	void input_finish(uint64_t _time_span_us=0);
	      AudioBuffer & in() { return m_in_buffer; }
	const AudioBuffer & out() { return m_out_buffer; }
	void pop_out_frames(unsigned _count);
	void flush();

	Category category() const { return m_category; }
	const char* name() const { return m_name.c_str(); }
	int id() const { return m_id; }

	std::tuple<bool,bool> update(uint64_t _time_span_ns, bool _prebuffering);
	void set_disable_time(uint64_t _time) { m_disable_time = _time; }
	bool check_disable_time(uint64_t _now_ns);
	void set_disable_timeout(uint64_t _timeout_ns) { m_disable_timeout = _timeout_ns; }

	void register_capture_clbk(std::function<void(bool _enable)> _fn);
	void on_capture(bool _enable);

private:
	void reset_filters();
};


#endif
