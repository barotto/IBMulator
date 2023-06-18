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
#include "mverb.h"
#include "timers.h"

class Mixer;

#define EFFECTS_MIN_DUR_US SEC_TO_USEC(2)

typedef std::function<bool(
		uint64_t _time_span_us,
		bool     _prebuffering,
		bool     _first_update
	)> MixerChannel_handler;

using MixerFilterChain = std::vector<std::shared_ptr<Dsp::Filter>>;
using EmVerb = MVerb<float>;

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
	enum AudioType
	{
		DAC, SYNTH, NOISE
	};
	enum ResamplingType
	{
		SINC, LINEAR, ZOH
	};
	enum class ReverbPreset {
		// using DOSBox-staging's convenient definitions.
		None, Tiny, Small, Medium, Large, Huge
	};

private:
	Mixer *m_mixer = nullptr;
	//enabling/disabling can be performed by the machine thread
	std::atomic<bool> m_enabled = false;
	std::string m_name;
	int m_id = 0;
	MixerChannel_handler m_update_clbk;
	std::atomic<uint64_t> m_disable_time = 2_s;
	uint64_t m_disable_timeout = 0;
	bool m_first_update = true;
	uint64_t m_last_time_span_ns = 0;
	AudioBuffer m_in_buffer;
	AudioBuffer m_out_buffer;
	uint64_t m_in_time = 0;
	bool m_new_data = true;
	std::function<void(bool)> m_capture_clbk;
	std::atomic<float> m_volume_r = 1.f;  // 0 .. +1
	std::atomic<float> m_volume_l = 1.f;  // 0 .. +1
	std::atomic<float> m_balance = 0.f; // -1 .. +1
	Category m_category = AUDIOCARD;
	AudioType m_audiotype = DAC;
	double m_fr_rem = 0.0;
	MixerFilterChain m_filters;
	std::mutex m_filters_mutex;
	ResamplingType m_resampling_type = SINC;
#if HAVE_LIBSAMPLERATE
	SRC_STATE *m_SRC_state = nullptr;
	int m_src_converter = SRC_SINC_MEDIUM_QUALITY;
#endif
	struct Reverb {
		struct ReverbConfig {
			float predelay;
			float early_mix;
			float size;
			float density;
			float bandwidth_freq;
			float decay;
			float dampening_freq;
			float synth_gain;
			float pcm_gain;
			float noise_gain;
			float hpf_freq;
		};

		EmVerb mverb = {};
		bool enabled = false;
		MixerFilterChain highpass;

		void config(ReverbConfig _config, double _rate, AudioType _type);
	} m_reverb;

public:
	MixerChannel(Mixer *_mixer, MixerChannel_handler _callback, const std::string &_name, int _id,
			Category _cat, AudioType _audiotype);
	~MixerChannel();

	// The machine thread can call only these methods:
	void enable(bool _enabled);
	inline bool is_enabled() { return m_enabled; }
	void set_resampling_type(ResamplingType _type) { m_resampling_type = _type; }
	void set_volume(float _vol) { m_volume_l = _vol; m_volume_r = _vol; }
	void set_volume(float _vol_l, float _vol_r) { m_volume_l = _vol_l; m_volume_r = _vol_r; }
	void set_balance(float _balance) { m_balance = clamp(_balance, -1.f, 1.f); }
	void set_reverb(std::string _preset);
	bool is_reverb_enabled() const { return m_reverb.enabled; }
	float volume() const { return m_volume_l; }
	float volume_l() const { return m_volume_l; }
	float volume_r() const { return m_volume_r; }
	float balance() const { return m_balance; }

	MixerFilterChain create_filters(std::string _filters_def);
	void set_filters(std::string _filters_def);
	void set_filters(MixerFilterChain _filters);
	bool are_filters_active() const { return !m_filters.empty(); }

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
	void play_silence_us(unsigned _us);
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

	static float db_to_factor(float _db) {
		return std::pow(10.0f, _db * 0.05f);
	}

private:
	void destroy_resampler();
	void reset_filters();
};


#endif
