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
#include "bs2b/bs2bclass.h"
#include "mverb.h"
#include "timers.h"
#include "chorus/ChorusEngine.h"
#include "appconfig.h"

class Mixer;

#define EFFECTS_MIN_DUR_US SEC_TO_USEC(2)
#define EFFECTS_MIN_DUR_NS SEC_TO_NSEC(2)

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
		MASTER = -1,
		AUDIOCARD,
		SOUNDFX,
		GUI
	};
	static constexpr int CategoryCount = 3;

	enum AudioType
	{
		DAC, SYNTH, NOISE
	};

	enum ResamplingType
	{
		SINC, LINEAR, ZOH
	};

	enum class ReverbPreset
	{
		None, Tiny, Small, Medium, Large, Huge
	};

	struct ReverbConfig {
		ReverbPreset preset;
		const char *name;
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

	struct ReverbParams {
		ReverbPreset preset;
		float gain;

		std::string definition();
	};

	enum class ChorusPreset
	{
		None, Light, Normal, Strong, Heavy
	};

	struct ChorusConfig {
		ChorusPreset preset;
		const char *name;
		bool chorus2_enabled;
		float synth_gain;
		float pcm_gain;
		float noise_gain;
	};

	enum class CrossfeedPreset
	{
		None, Bauer, Moy, Meier 
	};

	struct CrossfeedConfig {
		CrossfeedPreset preset;
		const char *name;
		uint32_t freq_cut_hz;
		uint32_t feed_db; // as dB*10
	};

	enum class FilterPreset
	{
		None, PCSpeaker, Custom
	};

	struct FilterConfig {
		FilterPreset preset;
		const char *name;
		std::string definition;
	};

	enum Feature {
		HasVolume = 1,
		HasAutoVolume = 2,
		HasReverb = 4,
		HasAutoReverb = 8,
		HasChorus = 16,
		HasAutoChorus = 32,
		HasFilter = 64,
		HasAutoFilter = 128,
		HasCrossfeed = 256,
		HasBalance = 512,
		HasResamplingType = 1024,
		HasAutoResamplingType = 2048,
		HasStereoSource = 4096
	};

	enum class ConfigParameter {
		Volume,
		Balance,
		Filter,
		FilterParams,
		Reverb,
		Chorus,
		Crossfeed,
		Resampling
	};

	using ConfigMap = std::map<ConfigParameter, AppConfig::ConfigPair>; 
	using CfgEventCb = std::function<void()>;

	struct VUMeter {
		static constexpr double step = 6.0;
		static constexpr double min = -48.0;
		static constexpr double range = step * 9.0;
		static constexpr double max = min + range;
		static constexpr double increase_ms = 30.0;
		static constexpr double decay_ms = 300.0;

		double db[2] = { min, min };
		double gain_rate = 0.0;
		double leak_rate = 0.0;

		void set_rate(double _rate);
		void update(int _channel, float _amplitude);
	};

private:
	Mixer *m_mixer = nullptr;
	std::string m_name;
	int m_id = 0;
	Category m_category = AUDIOCARD;
	AudioType m_audiotype = DAC;
	uint32_t m_features = 0;
	std::atomic<bool> m_enabled = false;
	MixerChannel_handler m_update_clbk;
	std::atomic<uint64_t> m_disable_time = 0;
	uint64_t m_disable_timeout = EFFECTS_MIN_DUR_NS;
	bool m_first_update = true;
	double m_fr_rem = 0.0;
	uint64_t m_last_time_span_ns = 0;
	AudioBuffer m_in_buffer;
	AudioBuffer m_out_buffer;
	uint64_t m_in_time = 0;
	bool m_new_data = true;
	std::function<void(bool)> m_capture_clbk;

	std::mutex m_mutex;

	struct Volume {
		std::atomic<float> master_left = 1.f;
		std::atomic<float> master_right = 1.f;
		std::atomic<float> sub_left = 1.f;  // 0 .. +1
		std::atomic<float> sub_right = 1.f;  // 0 .. +1
		std::atomic<float> factor_left = 1.f;
		std::atomic<float> factor_right = 1.f;
		bool auto_set = false;
		bool muted = false;
		bool force_muted = false;
		VUMeter meter;
	} m_volume;

	std::atomic<float> m_balance = 0.f; // -1 .. +1

	struct Filter {
		MixerFilterChain chain;
		FilterConfig config;
		std::atomic<bool> enabled = false;
		bool auto_set = false;
		bool config_dirty = false;
	} m_filter;

	struct Resampling {
		ResamplingType type = SINC;
		bool auto_set = false;
#if HAVE_LIBSAMPLERATE
		SRC_STATE *SRC_state = nullptr;
		int SRC_converter = SRC_SINC_MEDIUM_QUALITY;
#endif
	} m_resampling;

	struct Reverb {
		ReverbConfig config = {	ReverbPreset::None, "none",
			.0f,.0f,.0f,.0f,.0f,.0f,
			.0f,.0f,.0f,.0f,.0f
		};
		float gain = -1.f;
		EmVerb mverb = {};
		MixerFilterChain highpass;
		std::atomic<bool> enabled = false;
		bool auto_set = false;

		void set_config(ReverbConfig _config, double _rate, AudioType _type);
		void set_gain(float);
	} m_reverb;

	struct Chorus {
		ChorusConfig config = { ChorusPreset::None, "none",
			false, .0f, .0f, .0f
		};
		TAL::ChorusEngine engine = TAL::ChorusEngine(48000);
		std::atomic<bool> enabled = false;
		bool auto_set = false;

		void set_config(ChorusConfig _config, double _rate, AudioType _type);
	} m_chorus;

	struct Crossfeed {
		CrossfeedConfig config = { CrossfeedPreset::None, "none",
			0, 0
		};
		bs2b_base bs2b;
		std::atomic<bool> enabled = false;
		bool auto_set = false;

		void set_config(CrossfeedConfig _config, double _rate);
	} m_crossfeed;

	ConfigMap m_cfg_map;
	bool m_cfg_applied = false;
	std::map<ConfigParameter, std::list<CfgEventCb>> m_autoval_cb;
	std::map<ConfigParameter, std::list<std::pair<std::string,CfgEventCb>>> m_parameter_cb;

public:
	MixerChannel(Mixer *_mixer, MixerChannel_handler _callback, const std::string &_name, int _id,
			Category _cat, AudioType _audiotype);
	~MixerChannel();

	int id() const { return m_id; }
	Category category() const { return m_category; }
	const char* name() const { return m_name.c_str(); }

	void set_features(uint32_t _features) { m_features = _features; }
	uint32_t features() const { return m_features; }
	void register_config_map(ConfigMap _cfg) { m_cfg_map = _cfg; }
	const ConfigMap & config_map() const { return m_cfg_map; }
	void apply_config(AppConfig &);
	void apply_auto_values(AppConfig &);
	void store_config(INIFile &);
	void add_parameter_cb(ConfigParameter _parameter, std::string _name, CfgEventCb _cb);
	void remove_parameter_cb(ConfigParameter _parameter, std::string _name);
	void add_autoval_cb(ConfigParameter _parameter, CfgEventCb _cb);
	
	//
	// All threads can call these methods:
	//
	void enable(bool _enabled);
	bool is_enabled() const { return m_enabled; }

	// resampling
	void set_resampling_type(std::string _preset);
	void set_resampling_type(MixerChannel::ResamplingType _type);
	void set_resampling_auto(bool _enabled);
	bool is_resampling_auto() const { return m_resampling.auto_set; }

	// volume
	float volume_master_left() const { return m_volume.master_left; }
	float volume_master_right() const { return m_volume.master_right; }
	float volume_sub_left() const { return m_volume.sub_left; }
	float volume_sub_right() const { return m_volume.sub_right; }
	void set_volume_master(float _both);
	void set_volume_master(float _left, float _right);
	void set_volume_sub(float _left, float _right);
	void set_muted(bool _muted) { m_volume.muted = _muted; }
	bool is_muted() const { return m_volume.muted; }
	void set_force_muted(bool _muted) { m_volume.force_muted = _muted; }
	bool is_force_muted() const { return m_volume.force_muted; }
	void set_volume_auto(bool _enabled);
	bool is_volume_auto() const { return m_volume.auto_set; }
	const VUMeter & vu_meter() const { return m_volume.meter; }

	// balance
	float balance() const { return m_balance; }
	void set_balance(float _balance);

	// reverb
	ReverbParams reverb() const { return { m_reverb.config.preset, m_reverb.gain }; }
	float reverb_gain() const;
	void set_reverb(std::string _preset, bool _enable = false) noexcept;
	void set_reverb(const ReverbParams &_params, bool _enable = false) noexcept;
	void set_reverb_gain(float _gain) noexcept;
	void reset_reverb_gain();
	void enable_reverb(bool _enable);
	bool is_reverb_enabled() const { return m_reverb.enabled; }
	void set_reverb_auto(bool _enabled);
	bool is_reverb_auto() const { return m_reverb.auto_set; }
	static ReverbConfig reverb_preset_to_config(ReverbPreset _preset);
	static ReverbParams parse_reverb_def(std::string _reverb_def);

	// chorus
	const ChorusConfig & chorus() const { return m_chorus.config; }
	void set_chorus(std::string _preset, bool _enable = false) noexcept;
	void enable_chorus(bool _enable);
	bool is_chorus_enabled() const { return m_chorus.enabled; }
	void set_chorus_auto(bool _enabled);
	bool is_chorus_auto() const { return m_chorus.auto_set; }
	static ChorusConfig chorus_preset_to_config(ChorusPreset _preset);

	// crossfeed
	const CrossfeedConfig & crossfeed() const { return m_crossfeed.config; }
	void set_crossfeed(std::string _preset, bool _enable = false) noexcept;
	void enable_crossfeed(bool _enable);
	bool is_crossfeed_enabled() const { return m_crossfeed.enabled; }
	void set_crossfeed_auto(bool _enabled);
	bool is_crossfeed_auto() const { return m_crossfeed.auto_set; }
	static CrossfeedConfig crossfeed_preset_to_config(CrossfeedPreset _preset);

	// filters
	const FilterConfig & filter();
	const MixerFilterChain & filter_chain() const { return m_filter.chain; }
	void set_filter(std::string _preset) noexcept;
	void set_filter(const FilterConfig &_config) noexcept;
	void copy_filter_params(const MixerFilterChain &_filter) noexcept;
	void enable_filter(bool _enable);
	bool is_filter_enabled() const { return m_filter.enabled; }
	void set_filter_auto(bool _enabled);
	bool is_filter_auto() const { return m_filter.auto_set; }
	bool is_filter_set() const { return !m_filter.chain.empty(); }
	size_t add_filter(std::string _def);
	void remove_filter(size_t _idx);
	void set_filter_kind(size_t _idx, std::string _def);
	double get_filter_param(size_t _idx, Dsp::ParamID _param); 
	void set_filter_param(size_t _idx, Dsp::ParamID _param, double _value);

	static MixerFilterChain create_filter(std::string _filters_def) noexcept;
	template <int Channels>
	static MixerFilterChain create_filter(double _rate, std::string _filters_def);

	//
	// The mixer thread can call also these methods:
	//
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

	std::tuple<bool,bool> update(uint64_t _time_span_ns, bool _prebuffering);
	void set_disable_time(uint64_t _time) { m_disable_time = _time; }
	bool check_disable_time(uint64_t _now_ns);
	void set_disable_timeout(uint64_t _timeout_ns) { m_disable_timeout = _timeout_ns; }

	void register_capture_clbk(std::function<void(bool _enable)> _fn);
	void on_capture(bool _enable);

	static float db_to_factor(float _db) {
		return std::pow(10.0f, _db * 0.05f);
	}

	static float factor_to_db(float _factor) {
		return 20.f * std::log10(_factor);
	}

	static constexpr float volume_multiplier(float _value) {
		if(_value > 1.f) {
			return (exp(_value) - 1.f) / (M_E - 1.f);
		}
		return _value;
	}

private:
	void update_volume_factors();

	void run_autoval_cb(ConfigParameter _parameter);
	void run_parameter_cb(ConfigParameter _parameter);

	std::string reverb_def() const;
	std::string chorus_def() const;
	std::string crossfeed_def() const;
	std::string filter_def();
	std::string resampling_def() const;

	void create_resampling(int _channels);
	void destroy_resampling();

	void reset_filters();
	void update_filter_config();

	template<typename T>
	static std::pair<T, std::vector<std::string>> parse_preset(std::string _preset_def, 
			const std::map<std::string, T> _presets, std::string _default_preset);
};

extern template MixerFilterChain MixerChannel::create_filter<1>(double _rate, std::string _filters_def);
extern template MixerFilterChain MixerChannel::create_filter<2>(double _rate, std::string _filters_def);

#endif
