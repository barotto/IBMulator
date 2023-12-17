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

#include "ibmulator.h"
#include "mixer.h"
#include "machine.h"
#include "appconfig.h"


MixerChannel::MixerChannel(Mixer *_mixer, MixerChannel_handler _callback,
		const std::string &_name, int _id, Category _cat, AudioType _audiotype)
:
m_mixer(_mixer),
m_name(_name),
m_id(_id),
m_category(_cat),
m_audiotype(_audiotype),
m_update_clbk(_callback),
m_capture_clbk([](bool){})
{
}

MixerChannel::~MixerChannel()
{
	destroy_resampling();
}

void MixerChannel::apply_config(AppConfig &_config)
{
	if(m_cfg_applied) {
		return;
	}

	PDEBUGF(LOG_V0, LOG_MIXER, "Applying configuration to channel %s...\n", name());

	for(auto & conf : m_cfg_map) {
		auto [section, key] = conf.second;
		switch(conf.first) {
			case ConfigParameter::Volume: {
				float level = _config.get_real(section, key, -1.0) / 100.f;
				level = std::min(level, MIXER_MAX_VOLUME);
				if(level < .0) {
					if(m_features & MixerChannel::HasAutoVolume) {
						// defer
					} else {
						set_volume_master(1.f);
					}
				} else {
					set_volume_master(level);
				}
				break;
			}
			case ConfigParameter::Balance: {
				float val = _config.get_real_or_default(section, key) / 100.f;
				set_balance(val);
				break;
			}
			case ConfigParameter::Filter: {
				auto filters = _config.get_string_or_default(section, key);
				if(filters == "auto") {
					// defer
				} else if(filters.empty()) {
					m_filter.config = { FilterPreset::None, "none", "" };
				} else {
					set_filter(filters);
				}
				enable_filter(true);
				break;
			}
			case ConfigParameter::Reverb: {
				auto preset = _config.get_string_or_default(section, key);
				if(preset == "auto") {
					// defer
				} else {
					set_reverb(preset, true);
				}
				break;
			}
			case ConfigParameter::Chorus: {
				auto preset = _config.get_string_or_default(section, key);
				if(preset == "auto") {
					// defer
				} else {
					set_chorus(preset, true);
				}
				break;
			}
			case ConfigParameter::Crossfeed: {
				auto preset = _config.get_string_or_default(section, key);
				if(preset == "auto") {
					// defer
				} else {
					set_crossfeed(preset, true);
				}
				break;
			}
			case ConfigParameter::Resampling: {
				auto resampling = _config.get_string_or_default(section, key);
				if(resampling == "auto") {
					// defer
				} else {
					set_resampling_type(resampling);
				}
				break;
			}
			case ConfigParameter::FilterParams:
				break;
		}
	}
}

void MixerChannel::apply_auto_values(AppConfig &_config)
{
	if(m_cfg_applied) {
		return;
	}

	for(auto & conf : m_cfg_map) {
		auto [section, key] = conf.second;
		switch(conf.first) {
			case ConfigParameter::Volume: {
				float level = _config.get_real(section, key, -1.0);
				if(level < .0 && (m_features & MixerChannel::HasAutoVolume)) {
					set_volume_auto(true);
				}
				break;
			}
			case ConfigParameter::Balance: {
				break;
			}
			case ConfigParameter::Filter: {
				auto filters = _config.get_string_or_default(section, key);
				if(filters == "auto") {
					set_filter_auto(true);
				}
				break;
			}
			case ConfigParameter::Reverb: {
				auto preset = _config.get_string_or_default(section, key);
				if(preset == "auto") {
					set_reverb_auto(true);
					enable_reverb(true);
				}
				break;
			}
			case ConfigParameter::Chorus: {
				auto preset = _config.get_string_or_default(section, key);
				if(preset == "auto") {
					set_chorus_auto(true);
					enable_chorus(true);
				}
				break;
			}
			case ConfigParameter::Crossfeed: {
				auto preset = _config.get_string_or_default(section, key);
				if(preset == "auto") {
					set_crossfeed_auto(true);
					enable_crossfeed(true);
				}
				break;
			}
			case ConfigParameter::Resampling: {
				auto resampling = _config.get_string_or_default(section, key);
				if(resampling == "auto") {
					set_resampling_auto(true);
				}
				break;
			}
			case ConfigParameter::FilterParams:
				break;
		}
	}

	m_cfg_applied = true;
}

void MixerChannel::store_config(INIFile &_config)
{
	for(auto & conf : m_cfg_map) {
		auto [section, key] = conf.second;
		switch(conf.first) {
			case ConfigParameter::Volume: {
				if(is_volume_auto()) {
					_config.set_string(section, key, "auto");
				} else {
					double value = std::round(m_volume.master_left * 100.f);
					_config.set_real(section, key, value);
				}
				break;
			}
			case ConfigParameter::Balance: {
				double value = std::round(m_balance * 100.f);
				_config.set_real(section, key, value);
				break;
			}
			case ConfigParameter::Filter: {
				_config.set_string(section, key, filter_def());
				break;
			}
			case ConfigParameter::Reverb: {
				_config.set_string(section, key, reverb_def());
				break;
			}
			case ConfigParameter::Chorus: {
				_config.set_string(section, key, chorus_def());
				break;
			}
			case ConfigParameter::Crossfeed: {
				_config.set_string(section, key, crossfeed_def());
				break;
			}
			case ConfigParameter::Resampling: {
				_config.set_string(section, key, resampling_def());
				break;
			}
			case ConfigParameter::FilterParams:
				break;
		}
	}
}

void MixerChannel::enable(bool _enabled)
{
	if(m_enabled != _enabled) {
		m_enabled = _enabled;
		m_disable_time = 0;
		if(_enabled) {
			//reset_filters();
			PDEBUGF(LOG_V1, LOG_MIXER, "%s: channel enabled\n", m_name.c_str());
		} else {
			m_first_update = true;
			m_volume.meter.db[0] = VUMeter::min;
			m_volume.meter.db[1] = VUMeter::min;
			PDEBUGF(LOG_V1, LOG_MIXER, "%s: channel disabled\n", m_name.c_str());
		}
	}
}

void MixerChannel::add_parameter_cb(ConfigParameter _parameter, std::string _name, CfgEventCb _cb)
{
	// WARNING:
	// callbacks can't be used to update the UI, as it would require a locking system that
	// doesn't exist at the moment.
	// TODO?
	for(auto & f : m_parameter_cb[_parameter]) {
		if(f.first == _name) {
			f.second = _cb;
			return;
		}
	}
	m_parameter_cb[_parameter].emplace_back(_name, _cb);
}

void MixerChannel::remove_parameter_cb(ConfigParameter _parameter, std::string _name)
{
	m_parameter_cb[_parameter].remove_if([&](const std::pair<std::string,CfgEventCb> &e){
		return e.first == _name;
	});
}

void MixerChannel::add_autoval_cb(ConfigParameter _parameter, CfgEventCb _cb)
{
	m_autoval_cb[_parameter].emplace_back(_cb);
}

void MixerChannel::run_autoval_cb(ConfigParameter _parameter)
{
	for(auto & f : m_autoval_cb[_parameter]) {
		f();
	}
}

void MixerChannel::run_parameter_cb(ConfigParameter _parameter)
{
	for(auto & f : m_parameter_cb[_parameter]) {
		f.second();
	}
}

std::tuple<bool,bool> MixerChannel::update(uint64_t _time_span_ns, bool _prebuffering)
{
	assert(m_update_clbk);
	m_last_time_span_ns = _time_span_ns;
	bool active=false,enabled=false;
	if(m_enabled) {
		bool first_upd = m_first_update;
		/* channel can be disabled in the callback, so I update m_first_update
		 * before calling the update
		 */
		m_first_update = false;
		enabled = m_update_clbk(_time_span_ns, _prebuffering, first_upd);
		if(enabled || m_out_buffer.frames()>0) {
			active = true;
		}
		PDEBUGF(LOG_V2, LOG_MIXER, "%s: updated, enabled=%d, active=%d\n",
				m_name.c_str(), enabled, active);
	} else {
		enabled = false;
		/* On the previous iteration the channel could have been disabled
		 * but its input buffer could have some samples left to process
		 */
		if(m_in_buffer.frames()) {
			input_finish(_time_span_ns);
			enabled = true;
		}
		if(m_out_buffer.frames()) {
			active = true;
		} else {
			reset_filters();
		}
	}

	return std::make_tuple(active,enabled);
}

void MixerChannel::create_resampling(int _channels)
{
#if HAVE_LIBSAMPLERATE
	int src_type;
	switch(m_resampling.type) {
		case SINC: src_type = SRC_SINC_MEDIUM_QUALITY; break;
		case LINEAR: src_type = SRC_LINEAR; break;
		case ZOH: src_type = SRC_ZERO_ORDER_HOLD; break;
	}
	if(src_type != m_resampling.SRC_converter) {
		destroy_resampling();
	}
	if(m_resampling.SRC_state == nullptr) {
		int err;
		m_resampling.SRC_state = src_new(src_type, _channels, &err);
		if(m_resampling.SRC_state == nullptr) {
			PERRF(LOG_MIXER, "unable to initialize SRC state: %d\n", err);
		} else {
			const char *src_type_str = "UNKNOWN";
			switch(src_type) {
				case SRC_SINC_BEST_QUALITY: src_type_str = "SRC_SINC_BEST_QUALITY"; break;
				case SRC_SINC_MEDIUM_QUALITY: src_type_str = "SRC_SINC_MEDIUM_QUALITY"; break;
				case SRC_SINC_FASTEST: src_type_str = "SRC_SINC_FASTEST"; break;
				case SRC_ZERO_ORDER_HOLD: src_type_str = "SRC_ZERO_ORDER_HOLD"; break;
				case SRC_LINEAR: src_type_str = "SRC_LINEAR"; break;
				default: break;
			}
			PDEBUGF(LOG_V1, LOG_MIXER, "%s: SRC converter type set to %d (%s)\n", m_name.c_str(), src_type, src_type_str);
		}
		m_resampling.SRC_converter = src_type;
	} else {
		src_reset(m_resampling.SRC_state);
	}
#endif
}

void MixerChannel::destroy_resampling()
{
#if HAVE_LIBSAMPLERATE
	if(m_resampling.SRC_state != nullptr) {
		src_delete(m_resampling.SRC_state);
		m_resampling.SRC_state = nullptr;
	}
#endif
}

void MixerChannel::reset_filters()
{
	std::lock_guard<std::mutex> lock(m_mutex);

	for(auto &f : m_filter.chain) {
		f->reset();
	}

	if(m_reverb.enabled) {
		m_reverb.highpass[0]->reset();
		m_reverb.mverb.reset();
	}

	if(m_crossfeed.enabled) {
		m_crossfeed.bs2b.clear();
	}

	create_resampling(in_spec().channels);

	m_new_data = true;
}

void MixerChannel::set_in_spec(const AudioSpec &_spec)
{
	if(m_in_buffer.spec() != _spec) {
		unsigned ch = m_in_buffer.spec().channels;
		m_in_buffer.set_spec(_spec);
		if(ch != _spec.channels) {
			{
			std::lock_guard<std::mutex> lock(m_mutex);
			destroy_resampling();
			create_resampling(_spec.channels);
			}
			reset_filters();
		}
		// 5 sec. worth of data, how much is enough?
		m_in_buffer.reserve_us(5e6);
	}
}

void MixerChannel::set_out_spec(const AudioSpec &_spec)
{
	if(m_out_buffer.spec() != _spec) {
		/* the output buffer is forced to float format
		 */
		m_out_buffer.set_spec({AUDIO_FORMAT_F32, _spec.channels, _spec.rate});
		m_out_buffer.reserve_us(5e6);
		reset_filters();
	}
	m_volume.meter.set_rate(_spec.rate);
}

void MixerChannel::play(const AudioBuffer &_wave)
{
	if(_wave.spec() != m_in_buffer.spec()) {
		PDEBUGF(LOG_V1, LOG_MIXER, "%s: can't play, incompatible audio format\n",
				m_name.c_str());
		return;
	}
	m_in_buffer.add_frames(_wave);
	PDEBUGF(LOG_V1, LOG_MIXER, "%s: wave play: %d frames (%.2fus), in buf: %d samples (%.2fus)\n",
			m_name.c_str(),
			_wave.frames(), _wave.duration_us(),
			m_in_buffer.samples(), m_in_buffer.duration_us()
			);
}

void MixerChannel::play(const AudioBuffer &_wave, uint64_t _time_dist_us)
{
	play_frames(_wave, _wave.frames(), _time_dist_us);
}

void MixerChannel::play(const AudioBuffer &_wave, float _volume, uint64_t _time_dist_us)
{
	/* this work buffers can be static only because the current implementation
	 * of the mixer is single threaded.
	 */
	static AudioBuffer temp;
	temp = _wave;
	temp.apply_volume(_volume);
	play_frames(temp, temp.frames(), _time_dist_us);
}

void MixerChannel::play_frames(const AudioBuffer &_wave, unsigned _frames_cnt, uint64_t _time_dist_us)
{
	/* This function plays the given sound sample at the specified time distance
	 * from the start of the samples input buffer, filling with silence if needed.
	 */
	if(_wave.spec() != m_in_buffer.spec()) {
		PDEBUGF(LOG_V1, LOG_MIXER, "%s: can't play, incompatible audio format\n",
				m_name.c_str());
		return;
	}
	unsigned inbuf_frames = round(m_in_buffer.spec().us_to_frames(_time_dist_us));
	m_in_buffer.resize_frames_silence(inbuf_frames);
	m_in_buffer.add_frames(_wave, _frames_cnt);
	PDEBUGF(LOG_V1, LOG_MIXER, "%s: wave play: dist: %u frames (%lluus), wav: %u frames (%.2fus), in buf: %u samples (%.2fus)\n",
			m_name.c_str(),
			inbuf_frames, _time_dist_us,
			_frames_cnt, _wave.spec().frames_to_us(_frames_cnt),
			m_in_buffer.samples(), m_in_buffer.duration_us()
			);
}

void MixerChannel::play_silence(unsigned _frames, uint64_t _time_dist_us)
{
	unsigned inbuf_frames = round(m_in_buffer.spec().us_to_frames(_time_dist_us));
	m_in_buffer.resize_frames_silence(inbuf_frames);
	m_in_buffer.fill_frames_silence(_frames);
}

void MixerChannel::play_silence_us(unsigned _us)
{
	unsigned duration_frames = round(m_in_buffer.spec().us_to_frames(_us));
	m_in_buffer.fill_frames_silence(duration_frames);
}

void MixerChannel::play_loop(const AudioBuffer &_wave)
{
	if(m_in_buffer.duration_us() < m_mixer->heartbeat_us()) {
		play(_wave);
	}
}

void MixerChannel::pop_out_frames(unsigned _frames_to_pop)
{
	m_out_buffer.pop_frames(_frames_to_pop);
}

template <int Channels>
MixerFilterChain MixerChannel::create_filter(double _rate, std::string _filters_def)
{
	MixerFilterChain filters;

	auto filters_toks = str_parse_tokens(_filters_def, "\\|");

	for(auto &filter_str : filters_toks) {

		PDEBUGF(LOG_V2, LOG_MIXER, "Filter definition: %s\n", filter_str.c_str());

		auto filter_toks = str_parse_tokens(filter_str, "\\,");
		if(filter_toks.empty()) {
			PDEBUGF(LOG_V2, LOG_MIXER, "Invalid filter definition: %s\n", filter_str.c_str());
			continue;
		}

		std::string fname = str_trim(str_to_lower(filter_toks[0]));

		const std::map<std::string, Dsp::Kind> filter_types = {
			{ "lowpass",   Dsp::kindLowPass   },
			{ "highpass",  Dsp::kindHighPass  },
			{ "bandpass",  Dsp::kindBandPass  },
			{ "bandstop",  Dsp::kindBandStop  },
			{ "lowshelf",  Dsp::kindLowShelf  },
			{ "highshelf", Dsp::kindHighShelf },
			{ "bandshelf", Dsp::kindBandShelf }
		};

		if(filter_types.find(fname) == filter_types.end()) {
			PERRF(LOG_MIXER, "Invalid filter: %s\n", fname.c_str());
			continue;
		}

		std::shared_ptr<Dsp::Filter> filter;

		switch(filter_types.at(fname)) {
			case Dsp::kindLowPass   : filter = std::make_shared<Dsp::FilterDesign<Dsp::Butterworth::Design::LowPass  <50>,Channels>>(); break;
			case Dsp::kindHighPass  : filter = std::make_shared<Dsp::FilterDesign<Dsp::Butterworth::Design::HighPass <50>,Channels>>(); break;
			case Dsp::kindBandPass  : filter = std::make_shared<Dsp::FilterDesign<Dsp::Butterworth::Design::BandPass <50>,Channels>>(); break;
			case Dsp::kindBandStop  : filter = std::make_shared<Dsp::FilterDesign<Dsp::Butterworth::Design::BandStop <50>,Channels>>(); break;
			case Dsp::kindLowShelf  : filter = std::make_shared<Dsp::FilterDesign<Dsp::Butterworth::Design::LowShelf <50>,Channels>>(); break;
			case Dsp::kindHighShelf : filter = std::make_shared<Dsp::FilterDesign<Dsp::Butterworth::Design::HighShelf<50>,Channels>>(); break;
			case Dsp::kindBandShelf : filter = std::make_shared<Dsp::FilterDesign<Dsp::Butterworth::Design::BandShelf<50>,Channels>>(); break;
			default: throw std::logic_error("invalid filter kind");
		}

		PDEBUGF(LOG_V2, LOG_MIXER, "Filter: %s\n", filter->getName().c_str());

		std::map<std::string, Dsp::ParamInfo> param_types = {
			{ "order",     Dsp::ParamInfo::defaults(Dsp::idOrder) },
			{ "fc",        Dsp::ParamInfo::defaults(Dsp::idFrequency) },
			{ "frequency", Dsp::ParamInfo::defaults(Dsp::idFrequency) },
			{ "cutoff",    Dsp::ParamInfo::defaults(Dsp::idFrequency) },
			{ "center",    Dsp::ParamInfo::defaults(Dsp::idFrequency) },
			{ "bw",        Dsp::ParamInfo::defaults(Dsp::idBandwidthHz) },
			{ "gain",      Dsp::ParamInfo::defaults(Dsp::idGain) }
		};

		// remove the filter name, parse parameters
		filter_toks.erase(filter_toks.begin());

		Dsp::Params fparams;
		fparams.setToDefaults();
		fparams[Dsp::idSampleRate] = _rate;

		for(auto &filter_par : filter_toks) {

			auto param_toks = str_parse_tokens(filter_par, "\\=");
			if(param_toks.size() != 2) {
				PERRF(LOG_MIXER, "invalid filter parameter definition: %s\n", filter_par.c_str());
				continue;
			}

			std::string pname = str_trim(str_to_lower(param_toks[0]));

			if(param_types.find(pname) == param_types.end()) {
				PERRF(LOG_MIXER, "invalid filter parameter name: %s\n", pname.c_str());
				continue;
			}

			try {
				fparams[param_types.at(pname).getId()] = str_parse_real_num(param_toks[1]);
			} catch(std::exception &) {
				PERRF(LOG_MIXER, "invalid filter parameter value: %s\n", param_toks[1].c_str());
				continue;
			}

			PDEBUGF(LOG_V2, LOG_MIXER, "  %s = %.3f\n",
					param_types.at(pname).getName(),
					fparams[param_types.at(pname).getId()]
					);
		}

		filter->setParams(fparams);

		filters.push_back(filter);
	}

	return filters;
}

template MixerFilterChain MixerChannel::create_filter<1>(double _rate, std::string _filters_def);
template MixerFilterChain MixerChannel::create_filter<2>(double _rate, std::string _filters_def);

MixerFilterChain MixerChannel::create_filter(std::string _filter_def) noexcept
{
	MixerFilterChain filters;
	try {
		// filters are applied after rate and channels conversion
		if(g_mixer.audio_spec().channels == 1) {
			filters = create_filter<1>(double(g_mixer.audio_spec().freq), _filter_def);
		} else if(g_mixer.audio_spec().channels == 2) {
			filters = create_filter<2>(double(g_mixer.audio_spec().freq), _filter_def);
		} else {
			PDEBUGF(LOG_V0, LOG_MIXER, "Invalid number of audio channels creating filter: %d\n", g_mixer.audio_spec().channels);
		}
	} catch(std::exception &) {
		return MixerFilterChain();
	}
	return filters;
}

const MixerChannel::FilterConfig & MixerChannel::filter()
{
	if(m_filter.config_dirty) {
		update_filter_config();
	}
	return m_filter.config;
}

const std::map<MixerChannel::FilterPreset, MixerChannel::FilterConfig> MixerChannel::FilterPresetConfigs = {
	{ FilterPreset::None, { FilterPreset::None, "none", "" } },
	{ FilterPreset::PC_Speaker_1, {
		FilterPreset::PC_Speaker_1,
		"pc-speaker-1",
		"LowPass,order=2,fc=6000|HighPass,order=2,fc=300"
	} },
	{ FilterPreset::PC_Speaker_2, {
		FilterPreset::PC_Speaker_2,
		"pc-speaker-2",
		"LowPass,order=5,fc=5000|HighPass,order=5,fc=500"
	} },
	{ FilterPreset::LPF_3_2k, {
		FilterPreset::LPF_3_2k,
		"lpf-3.2k",
		"LowPass,order=2,fc=3200"
	} },
	{ FilterPreset::LPF_8k, {
		FilterPreset::LPF_8k,
		"lpf-8k",
		"LowPass,order=1,fc=8000"
	} },
	{ FilterPreset::LPF_12k, {
		FilterPreset::LPF_12k,
		"lpf-12k",
		"LowPass,order=1,fc=12000"
	} }
};

void MixerChannel::set_filter(std::string _filter_def) noexcept
{
	static const std::map<std::string, FilterPreset> presets {
		{ "none", FilterPreset::None },
		{ "pc-speaker-1", FilterPreset::PC_Speaker_1 }, { "pc-speaker", FilterPreset::PC_Speaker_1 }, { "pcspeaker", FilterPreset::PC_Speaker_1 },
		{ "pc-speaker-2", FilterPreset::PC_Speaker_2 }, { "pcspeaker2", FilterPreset::PC_Speaker_2 },
		{ "lpf-3.2k", FilterPreset::LPF_3_2k }, { "lpf3.2k", FilterPreset::LPF_3_2k },
		{ "lpf-8k", FilterPreset::LPF_8k }, { "lpf8k", FilterPreset::LPF_8k },
		{ "lpf-12k", FilterPreset::LPF_12k }, { "lpf12k", FilterPreset::LPF_12k },
		{ "lpf", FilterPreset::LPF_8k },
	};

	auto preset = presets.find(_filter_def);
	if(preset != presets.end()) {
		m_filter.config = FilterPresetConfigs.find(preset->second)->second;
		m_filter.config_dirty = false;
	} else if(_filter_def == "custom") {
		m_filter.config = { FilterPreset::Custom, "custom", m_filter.config.definition };
		run_parameter_cb(ConfigParameter::Filter);
		return;
	} else {
		m_filter.config = { FilterPreset::Custom, "custom", _filter_def };
		m_filter.config_dirty = false;
	}

	if(_filter_def.empty()) {
		{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_filter.chain.clear();
		}
		run_parameter_cb(ConfigParameter::Filter);
		return;
	}

	auto chain = create_filter(m_filter.config.definition);

	if(chain.empty()) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_filter.chain.clear();
		m_filter.config = FilterPresetConfigs.find(FilterPreset::None)->second;
	} else {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_filter.chain = chain;
	}

	for(auto & f : m_filter.chain) {
		PINFOF(LOG_V1, LOG_MIXER, "%s: adding DSP filter '%s'\n", m_name.c_str(), f->getName().c_str());
	}

	run_parameter_cb(ConfigParameter::Filter);
}

std::string MixerChannel::filter_def()
{
	if(is_filter_auto()) {
		return "auto";
	}
	if(m_filter.config.preset == FilterPreset::Custom) {
		if(m_filter.config_dirty) {
			update_filter_config();
		}
		return m_filter.config.definition;
	}
	return m_filter.config.name;
}

void MixerChannel::set_filter(const FilterConfig &_config) noexcept
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_filter.config = _config;
		m_filter.chain = create_filter(m_filter.config.definition);
		m_filter.enabled = is_filter_set();
		m_filter.config_dirty = false;
	} // lock_guard

	for(auto & f : m_filter.chain) {
		PINFOF(LOG_V1, LOG_MIXER, "%s: adding DSP filter '%s'\n", m_name.c_str(), f->getName().c_str());
	}
	
	run_parameter_cb(ConfigParameter::Filter);
}

void MixerChannel::copy_filter_params(const MixerFilterChain &_filter) noexcept
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for(size_t i=0; i<_filter.size(); i++) {
			if(i >= m_filter.chain.size()) {
				break;
			}
			if(m_filter.chain[i]->getKind() != _filter[i]->getKind()) {
				PDEBUGF(LOG_V0, LOG_MIXER, "%s: trying to copy parameter values from incompatible filter\n", m_name.c_str());
				break;
			}
			m_filter.chain[i]->setParams(_filter[i]->getParams());
		}
	}

	run_parameter_cb(ConfigParameter::FilterParams);
}

size_t MixerChannel::add_filter(std::string _def)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		MixerFilterChain filter = create_filter(_def);
		if(filter.empty()) {
			throw std::logic_error("invalid filter definition");
		}
		m_filter.chain.push_back(filter[0]);
		m_filter.config_dirty = true;
	} // lock_guard

	run_parameter_cb(ConfigParameter::Filter);

	return m_filter.chain.size() - 1;
}

void MixerChannel::remove_filter(size_t _idx)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		if(_idx >= m_filter.chain.size()) {
			throw std::runtime_error("invalid filter index");
		}
		m_filter.chain.erase(m_filter.chain.begin() + _idx);
		m_filter.config_dirty = true;
	} // lock_guard

	run_parameter_cb(ConfigParameter::Filter);
}

void MixerChannel::set_filter_kind(size_t _idx, std::string _def)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
	
		if(_idx >= m_filter.chain.size()) {
			throw std::logic_error("invalid dsp filter index");
		}
		MixerFilterChain filter = create_filter(_def);
		if(filter.empty()) {
			throw std::logic_error("invalid filter definition");
		}
		filter[0]->setParams(m_filter.chain[_idx]->getParams());
		m_filter.chain[_idx].swap(filter[0]);
		m_filter.config_dirty = true;
	} // lock_guard

	run_parameter_cb(ConfigParameter::Filter);
}

void MixerChannel::set_filter_param(size_t _idx, Dsp::ParamID _param, double _value)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if(_idx >= m_filter.chain.size()) {
			throw std::runtime_error("invalid filter index");
		}
		m_filter.chain[_idx]->setParam(_param, _value);
		m_filter.config_dirty = true;
	} // lock_guard

	run_parameter_cb(ConfigParameter::FilterParams);
}

void MixerChannel::update_filter_config()
{
	m_filter.config.definition = "";
	m_filter.config_dirty = false;
	if(!is_filter_set()) {
		return;
	}
	std::lock_guard<std::mutex> lock(m_mutex);
	std::vector<std::string> defs(m_filter.chain.size());
	for(auto & f : m_filter.chain) {
		defs.emplace_back(f->getDefinitionString());
	}
	m_filter.config.definition = str_implode(defs, "|");
}

double MixerChannel::get_filter_param(size_t _idx, Dsp::ParamID _param)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if(_idx >= m_filter.chain.size()) {
		throw std::runtime_error("invalid filter index");
	}
	return m_filter.chain[_idx]->getParam(_param);
}

void MixerChannel::flush()
{
	m_in_buffer.clear();
	m_out_buffer.clear();
}

void MixerChannel::input_finish(uint64_t _time_span_ns)
{
	unsigned in_frames;
	if(_time_span_ns > 0) {
		if(m_first_update) {
			m_fr_rem = 0.0;
		}
		double frames = m_in_buffer.ns_to_frames(_time_span_ns) + m_fr_rem;
		in_frames = unsigned(frames);
		m_fr_rem = frames - in_frames;
	} else {
		in_frames = m_in_buffer.frames();
	}

	if(in_frames == 0) {
		PDEBUGF(LOG_V2, LOG_MIXER, "%s: channel is active but empty\n", m_name.c_str());
		return;
	}

	// input buffer -> convert format&rate -> convert ch -> filters -> output buffer
	
	// these work buffers can be static only because the current implementation
	// of the mixer is single threaded.
	static AudioBuffer dest[2];
	unsigned bufidx = 0;
	AudioBuffer *source = &m_in_buffer;

	// 1. convert format
	if(m_in_buffer.format() != AUDIO_FORMAT_F32) {
		dest[bufidx].set_spec({AUDIO_FORMAT_F32, m_in_buffer.channels(), m_in_buffer.rate()});
		source->convert_format(dest[bufidx], in_frames);
		source = &dest[bufidx];
		bufidx = 1 - bufidx;
	}

	// 2. convert rate, processed frames can be different than in_frames
	int frames;
	if(m_in_buffer.rate() != m_out_buffer.rate()) {
		std::lock_guard<std::mutex> lock(m_mutex);
		dest[bufidx].set_spec({AUDIO_FORMAT_F32, m_in_buffer.channels(), m_out_buffer.rate()});
		unsigned missing = source->convert_rate(dest[bufidx], in_frames, m_resampling.SRC_state);
		if(m_new_data && missing>1) {
			PDEBUGF(LOG_V2, LOG_MIXER, "%s: adding %d samples\n", m_name.c_str(), missing);
			m_out_buffer.hold_frames<float>(missing);
		}
		m_new_data = false;
		source = &dest[bufidx];
		bufidx = 1 - bufidx;
		frames = source->frames();
	} else {
		frames = in_frames;
	}

	if(frames) {

		// 3. convert channels
		if(m_in_buffer.channels() != m_out_buffer.channels()) {
			dest[bufidx].set_spec(m_out_buffer.spec());
			source->convert_channels(dest[bufidx], frames);
			source = &dest[bufidx];
			bufidx = 1 - bufidx;
		}

		// 4. process filters
		if(m_filter.enabled) {
			std::lock_guard<std::mutex> lock(m_mutex);
			for(auto &f : m_filter.chain) {
				f->process(frames, &(source->at<float>(0)));
			}
		}

		if(m_out_buffer.spec().channels == 2) {
			// 5. apply crossfeed
			if(m_crossfeed.enabled) {
				std::lock_guard<std::mutex> lock(m_mutex);
				float *data = &(source->at<float>(0));
				m_crossfeed.bs2b.cross_feed(data, frames);
			}

			// 6. apply chorus
			if(m_chorus.enabled) {
				std::lock_guard<std::mutex> lock(m_mutex);
				m_chorus.engine.process(frames, &(source->at<float>(0)));
			}

			// 7. apply reverb
			if(m_reverb.enabled) {
				std::lock_guard<std::mutex> lock(m_mutex);
				dest[bufidx].set_spec(m_out_buffer.spec());
				dest[bufidx].add_frames(*source);
				m_reverb.highpass[0]->process(frames, &(dest[bufidx].at<float>(0)));
				m_reverb.mverb.process(frames, &(dest[bufidx].at<float>(0)), &(source->at<float>(0)));
			}
		}

		// 8. apply volume
		float volume[2] = { m_volume.factor_left, m_volume.factor_right };
		float *chdata = &(source->at<float>(0));
		for(size_t i=0; i<source->samples(); i++) {
			int c = i % m_out_buffer.spec().channels;
			chdata[i] *= volume[c];
			m_volume.meter.update(c, std::abs(chdata[i]));
		}

		// 9. add to output buffer
		m_out_buffer.add_frames(*source, frames);
	}

	// remove processed frames from input buffer
	m_in_buffer.pop_frames(in_frames);

	PDEBUGF(LOG_V2, LOG_MIXER, "%s: finish (%lluns): in: %d frames (%.2fus), out: %d frames (%.2fus), rem: %.2f\n",
			m_name.c_str(), _time_span_ns,
			in_frames, m_in_buffer.spec().frames_to_us(in_frames),
			m_out_buffer.frames(), m_out_buffer.duration_us(),
			m_fr_rem);
}

bool MixerChannel::check_disable_time(uint64_t _now_ns)
{
	uint64_t diable_time = m_disable_time.load();
	if(m_disable_time && (diable_time < _now_ns) && (_now_ns - diable_time >= m_disable_timeout)) {
		PDEBUGF(LOG_V1, LOG_MIXER, "%s: disabling channel after %llu ns of silence (now: %llu, dis.time: %llu, dis.timeout: %llu)\n",
				m_name.c_str(), (_now_ns - diable_time),
				_now_ns, diable_time, m_disable_timeout);
		enable(false);
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

void MixerChannel::VUMeter::set_rate(double _rate)
{
	gain_rate = std::abs(min) / (_rate / 1000.0 * increase_ms);
	leak_rate = std::abs(min) / (_rate / 1000.0 * decay_ms);
}

void MixerChannel::VUMeter::update(int _channel, float _amplitude)
{
	double value = factor_to_db(_amplitude);
	if(value > db[_channel]) {
		db[_channel] += gain_rate;
	} else if(value < db[_channel]) {
		db[_channel] -= leak_rate;
	}
	db[_channel] = clamp(db[_channel], min, max);
}

void MixerChannel::update_volume_factors()
{
	if(m_balance < 0.f) {
		// left channel is full volume
		m_volume.factor_left = 1.f;
		m_volume.factor_right = 1.f + m_balance;
	} else if(m_balance > 0.f) {
		// right channel is full volume
		m_volume.factor_left = 1.f - m_balance;
		m_volume.factor_right = 1.f;
	} else {
		// center
		m_volume.factor_left = 1.f;
		m_volume.factor_right = 1.f;
	}

	m_volume.factor_left = m_volume.factor_left * volume_multiplier(m_volume.sub_left * m_volume.master_left);
	m_volume.factor_right = m_volume.factor_right * volume_multiplier(m_volume.sub_right * m_volume.master_right);
}

void MixerChannel::set_volume_master(float _both)
{
	set_volume_master(_both, _both);
}

void MixerChannel::set_volume_master(float _left, float _right)
{
	m_volume.master_left = std::max(0.f, std::min(_left, MIXER_MAX_VOLUME));
	m_volume.master_right = std::max(0.f, std::min(_right, MIXER_MAX_VOLUME));
	update_volume_factors();

	run_parameter_cb(ConfigParameter::Volume);
}

void MixerChannel::set_volume_sub(float _left, float _right)
{
	m_volume.sub_left = _left;
	m_volume.sub_right = _right;
	update_volume_factors();

	run_parameter_cb(ConfigParameter::Volume);
}

void MixerChannel::set_volume_auto(bool _enabled)
{
	m_volume.auto_set = _enabled;

	run_autoval_cb(ConfigParameter::Volume);
}

void MixerChannel::set_balance(float _balance)
{
	m_balance = clamp(_balance, -1.f, 1.f);
	update_volume_factors();

	run_parameter_cb(ConfigParameter::Balance);
}

void MixerChannel::set_reverb_auto(bool _enabled)
{
	m_reverb.auto_set = _enabled;
	m_reverb.enabled = false;

	run_autoval_cb(ConfigParameter::Reverb);
}

void MixerChannel::enable_reverb(bool _enable)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if(m_reverb.config.preset != ReverbPreset::None) {
		m_reverb.enabled = _enable;
	}
}

void MixerChannel::set_chorus_auto(bool _enabled)
{
	m_chorus.auto_set = _enabled;
	m_chorus.enabled = false;

	run_autoval_cb(ConfigParameter::Chorus);
}

void MixerChannel::enable_chorus(bool _enable)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if(m_chorus.config.preset != ChorusPreset::None) {
		m_chorus.enabled = _enable;
	}
}

void MixerChannel::set_crossfeed_auto(bool _enabled)
{
	m_crossfeed.auto_set = _enabled;
	m_crossfeed.enabled = false;

	run_autoval_cb(ConfigParameter::Crossfeed);
}

void MixerChannel::enable_crossfeed(bool _enable)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if(m_crossfeed.config.preset != CrossfeedPreset::None) {
		m_crossfeed.enabled = _enable;
	}
}

void MixerChannel::set_resampling_auto(bool _enabled)
{
	m_resampling.auto_set = _enabled;

	run_autoval_cb(ConfigParameter::Resampling);
}

void MixerChannel::set_filter_auto(bool _enabled)
{
	m_filter.auto_set = _enabled;

	run_autoval_cb(ConfigParameter::Filter);
}

void MixerChannel::enable_filter(bool _enable)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_filter.enabled = _enable;
}

void MixerChannel::set_resampling_type(MixerChannel::ResamplingType _type)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	m_resampling.type = _type;

	create_resampling(in_spec().channels);

	run_parameter_cb(ConfigParameter::Resampling);
}

void MixerChannel::set_resampling_type(std::string _preset)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	m_resampling.type = SINC;

	try {
		auto [resampler, preset]  = parse_preset<ResamplingType>(_preset, {
			{ "sinc", MixerChannel::SINC },
			{ "linear", MixerChannel::LINEAR },
			{ "hold", MixerChannel::ZOH },
		}, "sinc");

		m_resampling.type = resampler;

		PINFOF(LOG_V1, LOG_MIXER, "%s: resampling set to '%s'\n", m_name.c_str(), _preset.c_str());
	} catch(std::runtime_error &e) {
		PERRF(LOG_MIXER, "%s: resampler: %s\n", m_name.c_str(), e.what());
	}

	create_resampling(in_spec().channels);

	run_parameter_cb(ConfigParameter::Resampling);
}

std::string MixerChannel::resampling_def() const
{
	if(is_resampling_auto()) {
		return "auto";
	}
	switch(m_resampling.type) {
		case MixerChannel::SINC: return "sinc";
		case MixerChannel::LINEAR: return "linear";
		case MixerChannel::ZOH: return "zoh";
	}
	return "";
}

template<typename T>
std::pair<T, std::vector<std::string>> MixerChannel::parse_preset(std::string _preset_def, 
			const std::map<std::string, T> _presets, std::string _default_preset)
{
	_preset_def = str_trim(_preset_def);
	if(_preset_def.empty()) {
		return {};
	}

	auto toks = str_parse_tokens(_preset_def, "[\\s,]");
	if(toks.empty()) {
		return {};
	}

	std::string pname = "none";
	try {
		if(INIFile::parse_bool(toks[0])) {
			pname = _default_preset;
		} else {
			return {};
		}
	} catch(std::exception &) {
		pname = str_to_lower(toks[0]);
	}

	toks[0] = pname;

	auto preset = _presets.find(toks[0]);
	if(preset == _presets.end()) {
		throw std::runtime_error(str_format("invalid preset '%s'", toks[0].c_str()));
	}

	return {preset->second, toks};
}

MixerChannel::ReverbConfig MixerChannel::reverb_preset_to_config(ReverbPreset _preset)
{
	switch(_preset) {                                                // DELAY  MIX    SIZE   DEN    BWFREQ DECAY  DAMP   SYNTH  PCM    NOISE  HIPASS
	case ReverbPreset::None:   return { ReverbPreset::None,   "none",   0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,   0.0f  };
	case ReverbPreset::Tiny:   return { ReverbPreset::Tiny,   "tiny",   0.00f, 1.00f, 0.05f, 0.50f, 0.50f, 0.00f, 1.00f, 0.87f, 0.87f, 0.87f, 200.0f };
	case ReverbPreset::Small:  return { ReverbPreset::Small,  "small",  0.00f, 1.00f, 0.17f, 0.42f, 0.50f, 0.50f, 0.70f, 0.40f, 0.08f, 0.40f, 200.0f };
	case ReverbPreset::Medium: return { ReverbPreset::Medium, "medium", 0.00f, 0.75f, 0.50f, 0.50f, 0.95f, 0.42f, 0.21f, 0.54f, 0.07f, 0.54f, 170.0f };
	case ReverbPreset::Large:  return { ReverbPreset::Large,  "large",  0.00f, 0.75f, 0.75f, 0.50f, 0.95f, 0.52f, 0.21f, 0.70f, 0.05f, 0.70f, 140.0f };
	case ReverbPreset::Huge:   return { ReverbPreset::Huge,   "huge",   0.00f, 0.75f, 0.75f, 0.50f, 0.95f, 0.52f, 0.21f, 0.85f, 0.05f, 0.85f, 140.0f };
	}
	assert(false);
	return {};
}

MixerChannel::ReverbParams MixerChannel::parse_reverb_def(std::string _reverb_def)
{
	ReverbParams params = { ReverbPreset::None, -1.f };

	auto [preset, par_list] = parse_preset<ReverbPreset>(_reverb_def, {
		{ "none", ReverbPreset::None },
		{ "tiny", ReverbPreset::Tiny },
		{ "small", ReverbPreset::Small },
		{ "medium", ReverbPreset::Medium },
		{ "large", ReverbPreset::Large },
		{ "huge", ReverbPreset::Huge }
	}, "medium");

	params.preset = preset;

	if(par_list.size() > 1) {
		try {
			params.gain = str_parse_int_num(par_list[1]);
			params.gain = clamp(params.gain, 0.f, 100.f);
			params.gain = params.gain / 100.f;
		} catch(std::runtime_error &) {}
	}

	return params;
}

void MixerChannel::set_reverb(std::string _reverb_def, bool _enable) noexcept
{
	try {
		ReverbParams params = parse_reverb_def(_reverb_def);
		set_reverb(params, _enable);
	} catch(std::runtime_error &e) {
		PERRF(LOG_MIXER, "%s: reverb: %s\n", m_name.c_str(), e.what());
	}
}

void MixerChannel::set_reverb(const ReverbParams &_params, bool _enable) noexcept
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		bool was_enabled = m_reverb.enabled || _enable;
		m_reverb.enabled = false;
		m_reverb.config = reverb_preset_to_config(ReverbPreset::None);

		try {
			ReverbConfig config = reverb_preset_to_config(_params.preset);

			if(config.preset != ReverbPreset::None) {
				m_reverb.set_config(config, out_spec().rate, m_audiotype);
				if(_params.gain >= 0) {
					m_reverb.set_gain(_params.gain);
				}
				m_reverb.enabled = was_enabled;
				PINFOF(LOG_V1, LOG_MIXER, "%s: reverb set to '%s',%.0f\n", m_name.c_str(),
						config.name, m_reverb.mverb.getParameter(EmVerb::GAIN) * 100.f);
			} else {
				PDEBUGF(LOG_V1, LOG_MIXER, "%s: reverb set to None\n", m_name.c_str());
			}
		} catch(std::runtime_error &e) {
			PERRF(LOG_MIXER, "%s: reverb: %s\n", m_name.c_str(), e.what());
		}
	} // lock_guard

	run_parameter_cb(ConfigParameter::Reverb);
}

void MixerChannel::set_reverb_gain(float _gain) noexcept
{
	m_reverb.set_gain(_gain);
	run_parameter_cb(ConfigParameter::Reverb);
}

void MixerChannel::reset_reverb_gain()
{
	switch(m_audiotype) {
		case SYNTH: m_reverb.mverb.setParameter(EmVerb::GAIN, m_reverb.config.synth_gain); break;
		case DAC: m_reverb.mverb.setParameter(EmVerb::GAIN, m_reverb.config.pcm_gain); break;
		case NOISE: m_reverb.mverb.setParameter(EmVerb::GAIN, m_reverb.config.noise_gain); break;
	}
	m_reverb.gain = -1.f;
}

std::string MixerChannel::reverb_def() const
{
	if(is_reverb_auto()) {
		return "auto";
	}
	return reverb().definition();
}

std::string MixerChannel::ReverbParams::definition()
{
	auto config = MixerChannel::reverb_preset_to_config(preset);
	if(preset != ReverbPreset::None && gain >= .0f) {
		return str_format("%s %d", config.name, int(gain * 100.f));
	} else {
		return config.name;
	}
}

float MixerChannel::reverb_gain() const
{
	return m_reverb.mverb.getParameter(EmVerb::GAIN);
}

void MixerChannel::Reverb::set_config(ReverbConfig _config, double _rate, AudioType _type)
{
	config = _config;

	mverb.setParameter(EmVerb::PREDELAY, _config.predelay);
	mverb.setParameter(EmVerb::EARLYMIX, _config.early_mix);
	mverb.setParameter(EmVerb::SIZE, _config.size);
	mverb.setParameter(EmVerb::DENSITY, _config.density);
	mverb.setParameter(EmVerb::BANDWIDTHFREQ, _config.bandwidth_freq);
	mverb.setParameter(EmVerb::DECAY, _config.decay);
	mverb.setParameter(EmVerb::DAMPINGFREQ, _config.dampening_freq);
	mverb.setParameter(EmVerb::MIX, 1.f);
	switch(_type) {
		case SYNTH: mverb.setParameter(EmVerb::GAIN, _config.synth_gain); break;
		case DAC: mverb.setParameter(EmVerb::GAIN, _config.pcm_gain); break;
		case NOISE: mverb.setParameter(EmVerb::GAIN, _config.noise_gain); break;
	}

	mverb.setSampleRate(_rate);

	highpass = create_filter<2>(_rate, str_format("highpass,order=1,fc=%f",_config.hpf_freq));
}

void MixerChannel::Reverb::set_gain(float _gain)
{
	gain = clamp(_gain, 0.f, 1.f);
	mverb.setParameter(EmVerb::GAIN, _gain);
}

MixerChannel::ChorusConfig MixerChannel::chorus_preset_to_config(ChorusPreset _preset)
{
	switch (_preset) {                                               // C2EN   SYNTH  PCM  NOISE
	case ChorusPreset::None:   return { ChorusPreset::None,   "none",   false, 0.f,   0.f,   0.f };
	case ChorusPreset::Light:  return { ChorusPreset::Light,  "light",  false, 0.33f, 0.20f, 0.f };
	case ChorusPreset::Normal: return { ChorusPreset::Normal, "normal", false, 0.54f, 0.35f, 0.f };
	case ChorusPreset::Strong: return { ChorusPreset::Strong, "strong", false, 0.75f, 0.45f, 0.f };
	case ChorusPreset::Heavy:  return { ChorusPreset::Heavy,  "heavy",  true,  0.75f, 0.65f, 0.f };
	}
	assert(false);
	return {};
}

void MixerChannel::set_chorus(std::string _preset, bool _enable) noexcept
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		bool was_enabled = m_chorus.enabled || _enable;
		m_chorus.enabled = false;
		m_chorus.config = chorus_preset_to_config(ChorusPreset::None);

		try {
			auto [preset, chorus] = parse_preset<ChorusPreset>(_preset, {
				{ "none",   ChorusPreset::None   },
				{ "light",  ChorusPreset::Light  },
				{ "normal", ChorusPreset::Normal },
				{ "strong", ChorusPreset::Strong },
				{ "heavy",  ChorusPreset::Heavy  }
			}, "normal");

			ChorusConfig config = chorus_preset_to_config(preset);

			if(config.preset != ChorusPreset::None) {
				m_chorus.set_config(config, out_spec().rate, m_audiotype);
				m_chorus.enabled = was_enabled;

				PINFOF(LOG_V1, LOG_MIXER, "%s: chorus set to '%s'\n", m_name.c_str(), chorus[0].c_str());
			}
		} catch(std::runtime_error &e) {
			PERRF(LOG_MIXER, "%s: chorus: %s\n", m_name.c_str(), e.what());
		}
	} // lock_guard

	run_parameter_cb(ConfigParameter::Chorus);
}

void MixerChannel::Chorus::set_config(ChorusConfig _config, double _rate, AudioType _type)
{
	config = _config;

	switch(_type) {
		case SYNTH: engine.setGain(_config.synth_gain); break;
		case DAC: engine.setGain(_config.pcm_gain); break;
		case NOISE: engine.setGain(_config.noise_gain); break;
	}

	engine.setSampleRate(static_cast<float>(_rate));
	engine.setEnablesChorus(true, _config.chorus2_enabled);
}

std::string MixerChannel::chorus_def() const
{
	if(is_chorus_auto()) {
		return "auto";
	}
	return m_chorus.config.name;
}

MixerChannel::CrossfeedConfig MixerChannel::crossfeed_preset_to_config(CrossfeedPreset _preset)
{
	switch (_preset) {                                                 // CFCUT FEED (dB*10)
	case CrossfeedPreset::None:  return { CrossfeedPreset::None,  "none",    0,  0 };
	case CrossfeedPreset::Bauer: return { CrossfeedPreset::Bauer, "bauer", 700, 45 };
	case CrossfeedPreset::Meier: return { CrossfeedPreset::Meier, "meier", 650, 95 };
	case CrossfeedPreset::Moy:   return { CrossfeedPreset::Moy,   "moy",   700, 60 };
	}
	assert(false);
	return {};
}

void MixerChannel::set_crossfeed(std::string _preset, bool _enable) noexcept
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		bool was_enabled = m_crossfeed.enabled || _enable;
		m_crossfeed.enabled = false;
		m_crossfeed.config = crossfeed_preset_to_config(CrossfeedPreset::None);

		try {
			auto [preset, cross] = parse_preset<CrossfeedPreset>(_preset, {
				{ "none",  CrossfeedPreset::None  },
				{ "bauer", CrossfeedPreset::Bauer },
				{ "meier", CrossfeedPreset::Meier },
				{ "meyer", CrossfeedPreset::Meier },
				{ "moy",   CrossfeedPreset::Moy   },
				{ "moi",   CrossfeedPreset::Moy   }
			}, "bauer");

			CrossfeedConfig config = crossfeed_preset_to_config(preset);

			if(config.preset != CrossfeedPreset::None) {
				m_crossfeed.set_config(config, out_spec().rate);
				m_crossfeed.enabled = was_enabled;

				PINFOF(LOG_V1, LOG_MIXER, "%s: crossfeed set to '%s'\n", m_name.c_str(), cross[0].c_str());
			}
		} catch(std::runtime_error &e) {
			PERRF(LOG_MIXER, "%s: crossfeed: %s\n", m_name.c_str(), e.what());
		}
	} // lock_guard

	run_parameter_cb(ConfigParameter::Crossfeed);
}

void MixerChannel::Crossfeed::set_config(CrossfeedConfig _config, double _rate)
{
	config = _config;

	bs2b.set_srate(uint32_t(round(_rate)));
	bs2b.set_level(( _config.freq_cut_hz | ( _config.feed_db << 16 ) ));
}

std::string MixerChannel::crossfeed_def() const
{
	if(is_crossfeed_auto()) {
		return "auto";
	}
	return m_crossfeed.config.name;
}
