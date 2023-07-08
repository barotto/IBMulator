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


MixerChannel::MixerChannel(Mixer *_mixer, MixerChannel_handler _callback,
		const std::string &_name, int _id, Category _cat, AudioType _audiotype)
:
m_mixer(_mixer),
m_name(_name),
m_id(_id),
m_update_clbk(_callback),
m_capture_clbk([](bool){}),
m_category(_cat),
m_audiotype(_audiotype)
{
}

MixerChannel::~MixerChannel()
{
#if HAVE_LIBSAMPLERATE
	if(m_SRC_state != nullptr) {
		src_delete(m_SRC_state);
	}
#endif
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
			PDEBUGF(LOG_V1, LOG_MIXER, "%s: channel disabled\n", m_name.c_str());
		}
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

void MixerChannel::destroy_resampler()
{
#if HAVE_LIBSAMPLERATE
	if(m_SRC_state != nullptr) {
		src_delete(m_SRC_state);
		m_SRC_state = nullptr;
	}
#endif
}

void MixerChannel::reset_filters()
{
	for(auto &f : m_filters) {
		f->reset();
	}

	if(m_reverb.enabled) {
		m_reverb.highpass[0]->reset();
		m_reverb.mverb.reset();
	}

#if HAVE_LIBSAMPLERATE
	int src_type;
	switch(m_resampling_type) {
		case SINC: src_type = SRC_SINC_MEDIUM_QUALITY; break;
		case LINEAR: src_type = SRC_LINEAR; break;
		case ZOH: src_type = SRC_ZERO_ORDER_HOLD; break;
	}
	if(src_type != m_src_converter) {
		destroy_resampler();
	}
	if(m_SRC_state == nullptr) {
		int err;
		m_SRC_state = src_new(src_type, in_spec().channels, &err);
		if(m_SRC_state == nullptr) {
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
		m_src_converter = src_type;
	} else {
		src_reset(m_SRC_state);
	}
#endif

	m_new_data = true;
}

void MixerChannel::set_in_spec(const AudioSpec &_spec)
{
	if(m_in_buffer.spec() != _spec) {
		unsigned ch = m_in_buffer.spec().channels;
		m_in_buffer.set_spec(_spec);
		if(ch != _spec.channels) {
			destroy_resampler();
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

MixerFilterChain MixerChannel::create_filters(std::string _filters_def)
{
	MixerFilterChain filters;
	try {
		// filters are applied after rate and channels conversion
		if(m_out_buffer.spec().channels == 1) {
			filters = Mixer::create_filters<1>(m_out_buffer.spec().rate, _filters_def);
		} else if(m_out_buffer.spec().channels == 2) {
			filters = Mixer::create_filters<2>(m_out_buffer.spec().rate, _filters_def);
		} else {
			PDEBUGF(LOG_V0, LOG_AUDIO, "%s: invalid number of channels: %d\n", m_name.c_str(), m_out_buffer.spec().channels);
		}
	} catch(std::exception &) {
		return MixerFilterChain();
	}
	return filters;
}

void MixerChannel::set_filters(std::string _filters_def)
{
	// this is called by the machine thread.
	if(_filters_def.empty()) {
		std::lock_guard<std::mutex> lock(m_filters_mutex);
		m_filters.clear();
		return;
	}

	set_filters(create_filters(_filters_def));

	for(auto &f : m_filters) {
		PINFOF(LOG_V1, LOG_MIXER, "%s: adding DSP filter '%s'\n", m_name.c_str(), f->getName().c_str());
	}
}

void MixerChannel::set_filters(MixerFilterChain _filters)
{
	// this is called by the machine thread.
	std::lock_guard<std::mutex> lock(m_filters_mutex);
	m_filters = _filters;
	for(auto &f : m_filters) {
		f->reset();
	}
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
		dest[bufidx].set_spec({AUDIO_FORMAT_F32, m_in_buffer.channels(), m_out_buffer.rate()});
		unsigned missing = source->convert_rate(dest[bufidx], in_frames, m_SRC_state);
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
		{
			std::lock_guard<std::mutex> lock(m_filters_mutex);
			for(auto &f : m_filters) {
				f->process(frames, &(source->at<float>(0)));
			}
		}

		if(m_out_buffer.spec().channels == 2) {
			// 5. apply crossfeed
			if(m_crossfeed.enabled) {
				float *data = &(source->at<float>(0));
				m_crossfeed.bs2b.cross_feed(data, frames);
			}

			// 6. apply chorus
			if(m_chorus.enabled) {
				std::lock_guard<std::mutex> lock(m_filters_mutex);
				m_chorus.chorus_engine.process(frames, &(source->at<float>(0)));
			}

			// 7. apply reverb
			if(m_reverb.enabled) {
				std::lock_guard<std::mutex> lock(m_filters_mutex);
				dest[bufidx].set_spec(m_out_buffer.spec());
				dest[bufidx].add_frames(*source);
				m_reverb.highpass[0]->process(frames, &(dest[bufidx].at<float>(0)));
				m_reverb.mverb.process(frames, &(dest[bufidx].at<float>(0)), &(source->at<float>(0)));
			}
		}

		// 8. add to output buffer
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
	if(m_disable_time && (m_disable_time < _now_ns) && (_now_ns - m_disable_time >= m_disable_timeout)) {
		PDEBUGF(LOG_V1, LOG_MIXER, "%s: disabling channel after %llu ns of silence\n",
				m_name.c_str(), (_now_ns - m_disable_time));
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

void MixerChannel::set_reverb(std::string _preset)
{
	std::lock_guard<std::mutex> lock(m_filters_mutex);

	m_reverb.enabled = false;

	try {
		auto [preset, reverb] = parse_preset<ReverbPreset>(_preset, {
			{ "none", ReverbPreset::None },
			{ "tiny", ReverbPreset::Tiny },
			{ "small", ReverbPreset::Small },
			{ "medium", ReverbPreset::Medium },
			{ "large", ReverbPreset::Large },
			{ "huge", ReverbPreset::Huge }
		}, "medium");

		Reverb::ReverbConfig config;
		switch (preset) {                  // DELAY  MIX    SIZE   DEN    BWFREQ DECAY  DAMP   SYNTH  PCM    NOISE  HIPASS
		case ReverbPreset::None:   return;
		case ReverbPreset::Tiny:   config = { 0.00f, 1.00f, 0.05f, 0.50f, 0.50f, 0.00f, 1.00f, 0.87f, 0.87f, 0.87f, 200.0f }; break;
		case ReverbPreset::Small:  config = { 0.00f, 1.00f, 0.17f, 0.42f, 0.50f, 0.50f, 0.70f, 0.40f, 0.08f, 0.40f, 200.0f }; break;
		case ReverbPreset::Medium: config = { 0.00f, 0.75f, 0.50f, 0.50f, 0.95f, 0.42f, 0.21f, 0.54f, 0.07f, 0.54f, 170.0f }; break;
		case ReverbPreset::Large:  config = { 0.00f, 0.75f, 0.75f, 0.50f, 0.95f, 0.52f, 0.21f, 0.70f, 0.05f, 0.70f, 140.0f }; break;
		case ReverbPreset::Huge:   config = { 0.00f, 0.75f, 0.75f, 0.50f, 0.95f, 0.52f, 0.21f, 0.85f, 0.05f, 0.85f, 140.0f }; break;
		}
		float pgain = 0.f;
		if(reverb.size() > 1) {
			try {
				pgain = str_parse_int_num(reverb[1]);
				pgain = clamp(pgain, 0.f, 100.f);
				if(pgain == 0) {
					return;
				}
				pgain = pgain / 100.f;
			} catch(std::runtime_error &) {}
		}
		if(pgain > 0) {
			switch(m_audiotype) {
				case SYNTH: config.synth_gain = pgain; break;
				case DAC: config.pcm_gain = pgain; break;
				case NOISE: config.noise_gain = pgain; break;
			}
		}

		m_reverb.config(config, m_out_buffer.spec().rate, m_audiotype);

		PINFOF(LOG_V1, LOG_MIXER, "%s: reverb set to '%s',%.0f\n", m_name.c_str(),
				reverb[0].c_str(), m_reverb.mverb.getParameter(EmVerb::GAIN) * 100.f);
	} catch(std::runtime_error &e) {
		PERRF(LOG_MIXER, "%s: reverb: %s\n", m_name.c_str(), e.what());
	}
}

void MixerChannel::Reverb::config(ReverbConfig _config, double _rate, AudioType _type)
{
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

	highpass = Mixer::create_filters<2>(_rate, str_format("highpass,order=1,cutoff=%f",_config.hpf_freq));

	enabled = true;
}

void MixerChannel::set_chorus(std::string _preset)
{
	std::lock_guard<std::mutex> lock(m_filters_mutex);

	m_chorus.enabled = false;

	try {
		auto [preset, chorus] = parse_preset<ChorusPreset>(_preset, {
			{ "none",   ChorusPreset::None   },
			{ "light",  ChorusPreset::Light  },
			{ "normal", ChorusPreset::Normal },
			{ "strong", ChorusPreset::Strong },
			{ "heavy",  ChorusPreset::Heavy  }
		}, "normal");

		Chorus::ChorusConfig config;
		switch (preset) {                  // C2EN   SYNTH  PCM  NOISE
		case ChorusPreset::None: return;
		case ChorusPreset::Light:  config = { false, .33f, .20f, 0.f }; break;
		case ChorusPreset::Normal: config = { false, .54f, .35f, 0.f }; break;
		case ChorusPreset::Strong: config = { false, .75f, .45f, 0.f }; break;
		case ChorusPreset::Heavy:  config = { true,  .75f, .65f, 0.f }; break;
		}

		m_chorus.config(config, m_out_buffer.spec().rate, m_audiotype);

		PINFOF(LOG_V1, LOG_MIXER, "%s: chorus set to '%s'\n", m_name.c_str(), chorus[0].c_str());
	} catch(std::runtime_error &e) {
		PERRF(LOG_MIXER, "%s: chorus: %s\n", m_name.c_str(), e.what());
	}
}

void MixerChannel::Chorus::config(ChorusConfig _config, double _rate, AudioType _type)
{
	switch(_type) {
		case SYNTH: chorus_engine.setGain(_config.synth_gain); break;
		case DAC: chorus_engine.setGain(_config.pcm_gain); break;
		case NOISE: chorus_engine.setGain(_config.noise_gain); break;
	}

	chorus_engine.setSampleRate(static_cast<float>(_rate));
	chorus_engine.setEnablesChorus(true, _config.chorus2_enabled);
	
	enabled = true;
}

void MixerChannel::set_crossfeed(std::string _preset)
{
	std::lock_guard<std::mutex> lock(m_filters_mutex);

	m_crossfeed.enabled = false;

	try {
		auto [preset, cross] = parse_preset<CrossfeedPreset>(_preset, {
			{ "none",  CrossfeedPreset::None  },
			{ "bauer", CrossfeedPreset::Bauer },
			{ "meier", CrossfeedPreset::Meier },
			{ "meyer", CrossfeedPreset::Meier },
			{ "moy",   CrossfeedPreset::Moy   },
			{ "moi",   CrossfeedPreset::Moy   }
		}, "bauer");

		Crossfeed::CrossfeedConfig config;
		switch (preset) {                    // FCUT FEED (dB*10)  
		case CrossfeedPreset::None: return;
		case CrossfeedPreset::Bauer: config = { 700, 45 }; break;
		case CrossfeedPreset::Meier: config = { 650, 95 }; break;
		case CrossfeedPreset::Moy:   config = { 700, 60 }; break;
		}

		m_crossfeed.config(config, m_out_buffer.spec().rate);

		PINFOF(LOG_V1, LOG_MIXER, "%s: crossfeed set to '%s'\n", m_name.c_str(), cross[0].c_str());
	} catch(std::runtime_error &e) {
		PERRF(LOG_MIXER, "%s: crossfeed: %s\n", m_name.c_str(), e.what());
	}
}

void MixerChannel::Crossfeed::config(CrossfeedConfig _config, double _rate)
{
	bs2b.set_srate(uint32_t(round(_rate)));
	bs2b.set_level(( _config.freq_cut_hz | ( _config.feed_db << 16 ) ));

	enabled = true;
}