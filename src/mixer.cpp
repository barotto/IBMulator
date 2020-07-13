/*
 * Copyright (C) 2015-2020  Marco Bortolin
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
#define _USE_MATH_DEFINES
#include <cmath>
#include <cctype>
#include "filesys.h"
#include "mixer.h"
#include "program.h"
#include "machine.h"
#include "gui/gui.h"
#include "utils.h"
#include "audio/wav.h"
#include <SDL.h>

Mixer g_mixer;


Mixer::Mixer()
:
m_mix_bufsize(0),
m_start_time(0),
m_prebuffer_us(50000),
m_machine(nullptr),
m_heartbeat_us(10000),
m_quit(false),
m_audio_status(SDL_AUDIO_STOPPED),
m_paused(false),
m_device(0),
m_frame_size(512),
m_audio_capture(false),
m_global_volume(1.f),
m_capture_sink(-1)
{
	SDL_zero(m_audio_spec);
	//sane defaults used to initialise the channels before the audio device
	m_audio_spec.freq = MIXER_FREQUENCY;
	m_audio_spec.channels = MIXER_CHANNELS;
	m_audio_spec.format = MIXER_FORMAT;
}

Mixer::~Mixer()
{
}

void Mixer::sdl_callback(void *userdata, Uint8 *stream, int len)
{
	Mixer * mixer = static_cast<Mixer*>(userdata);
	size_t bytes = mixer->m_out_buffer.read(stream, len);
	if(bytes<unsigned(len)) {
		/* buffer underrun is normal when the audio ring buffer is emptying and
		 * channels are all disabled.
		 */
		PDEBUGF(LOG_V1, LOG_MIXER, "Device buffer underrun\n");
		memset(&stream[bytes], mixer->m_audio_spec.silence, len-bytes);
	}
}

void Mixer::calibrate(const Pacer &_p)
{
	m_pacer.calibrate(_p);
}

void Mixer::init(Machine *_machine)
{
	m_machine = _machine;
	m_pacer.start();
	m_bench.init(&m_pacer.chrono(), 1000);

	m_paused = true;

	using namespace std::placeholders;
	m_silence_channel = register_channel(
		std::bind(&Mixer::create_silence_samples, this, _1, _2, _3),
		"Silence"
	);
	
	if(SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		PERRF(LOG_MIXER, "Unable to init SDL audio: %s\n", SDL_GetError());
		throw std::exception();
	}

	m_paused = false;
	
	int i, count = SDL_GetNumAudioDevices(0);
	if(count == 0) {
		PERRF(LOG_MIXER, "Unable to find any audio device\n");
		return;
	}
	for(i=0; i<count; ++i) {
		PINFOF(LOG_V1, LOG_MIXER, "Audio device %d: %s\n", i, SDL_GetAudioDeviceName(i, 0));
		PINFOF(LOG_V1, LOG_MIXER, "  Driver: %s\n", SDL_GetAudioDriver(i));
	}
}

void Mixer::start_capture()
{
	std::string path = g_program.config().get_file(CAPTURE_SECTION, CAPTURE_DIR, FILE_TYPE_USER);
	path = FileSys::get_next_filename(path, "sound_", ".wav");
	if(!path.empty()) {
		try {
			m_wav.open_write(path.c_str(), m_audio_spec.freq, SDL_AUDIO_BITSIZE(m_audio_spec.format), m_audio_spec.channels);
			m_capture_sink = register_sink(
				std::bind(&Mixer::audio_sink, this, std::placeholders::_1, std::placeholders::_2)
			);
			std::string mex = "started audio recording to " + path;
			PINFOF(LOG_V0, LOG_MIXER, "%s\n", mex.c_str());
			GUI::instance()->show_message(mex.c_str());
		} catch(std::exception &e) {
			if(m_wav.is_open()) {
				m_wav.close();
			}
		}
	}
	for(auto ch : m_mix_channels) {
		ch.second->on_capture(true);
	}
	m_audio_capture = true;
}

void Mixer::stop_capture()
{
	if(m_wav.is_open()) {
		try {
			m_wav.close();
		} catch(std::runtime_error &e) {
			PINFOF(LOG_V0, LOG_MIXER, "wav file error: %s\n", e.what());
		}
		unregister_sink(m_capture_sink);
	}
	m_audio_capture = false;
	for(auto ch : m_mix_channels) {
		ch.second->on_capture(false);
	}
	PINFOF(LOG_V0, LOG_MIXER, "audio recording stopped\n");
	GUI::instance()->show_message("audio recording stopped");
}

void Mixer::config_changed()
{
	//before the config can change the audio playback must be stopped
	if(m_device) {
		SDL_PauseAudioDevice(m_device, 0);
		close_audio_device();
	}
	bool capture = m_audio_capture;
	if(m_audio_capture) {
		stop_capture();
	}

	int frequency = g_program.config().get_int(MIXER_SECTION, MIXER_RATE);
	int prebuf_ms = g_program.config().get_int(MIXER_SECTION, MIXER_PREBUFFER); // msec
	m_prebuffer_us = prebuf_ms * 1000; // usec
	int samples = g_program.config().get_int(MIXER_SECTION, MIXER_SAMPLES);
	m_frame_size = 0;

	try {
		open_audio_device(frequency, MIXER_FORMAT, MIXER_CHANNELS, samples);
	} catch(std::exception &e) {
		PERRF(LOG_MIXER, "Audio output disabled\n");
		m_audio_spec.freq = MIXER_FREQUENCY;
		m_audio_spec.format = MIXER_FORMAT;
		m_audio_spec.channels = MIXER_CHANNELS;
		m_audio_spec.silence = 0;
	}
	
	PINFOF(LOG_V0, LOG_MIXER, "Mixing at %u Hz, %u bit, %u channels, %u samples\n",
			m_audio_spec.freq, SDL_AUDIO_BITSIZE(m_audio_spec.format), m_audio_spec.channels, m_audio_spec.samples);
	
	m_frame_size = m_audio_spec.channels * (SDL_AUDIO_BITSIZE(m_audio_spec.format) / 8);
	m_heartbeat_us = round(1e6 / (double(m_audio_spec.freq) / 512.0));
	m_pacer.set_heartbeat(m_heartbeat_us * 1000);

	PINFOF(LOG_V1, LOG_MIXER, "Mixer beat period: %u usec\n", m_heartbeat_us);
	
	m_prebuffer_us = clamp(m_prebuffer_us, m_heartbeat_us, m_heartbeat_us*10);
	
	int64_t buf_len_us = std::max(m_prebuffer_us*2, uint64_t(1000000U));
	int64_t buf_frames = (m_audio_spec.freq * buf_len_us) / 1000000;
	m_out_buffer.set_size(buf_frames * m_frame_size);
	
	m_mix_bufsize = buf_frames * m_audio_spec.channels;
	for(auto &buf : m_ch_mix) {
		buf.resize(m_mix_bufsize);
	}
	m_out_mix.resize(m_mix_bufsize);

	PINFOF(LOG_V1, LOG_MIXER, "  Prebuffer: %d msec., ring buffer: %d bytes\n",
			prebuf_ms, buf_frames * m_frame_size);

	for(auto ch : m_mix_channels) {
		ch.second->flush();
		ch.second->set_out_spec({AUDIO_FORMAT_F32,
			unsigned(m_audio_spec.channels), double(m_audio_spec.freq)});
	}
	
	m_silence_channel->set_in_spec({AUDIO_FORMAT_F32, 1, double(m_audio_spec.freq)});
	m_silence_channel->set_out_spec({AUDIO_FORMAT_F32, unsigned(m_audio_spec.channels), double(m_audio_spec.freq)});
	
	// let the GUI interfaces set the AUDIO category volume
	m_channels_volume[static_cast<int>(MixerChannelCategory::SOUNDFX)] =
			g_program.config().get_real(SOUNDFX_SECTION, SOUNDFX_VOLUME);

	if(capture) {
		start_capture();
	}
}

void Mixer::start()
{
	m_quit = false;
	m_start_time = 0;
	PDEBUGF(LOG_V1, LOG_MIXER, "Mixer thread started\n");
	main_loop();
}

void Mixer::main_loop()
{
	std::vector<std::pair<MixerChannel*,bool>> active_channels;

	m_bench.start();
	
	while(true) {
		
		m_bench.frame_start();
		
		uint64_t time_span_us = m_pacer.wait() / 1000;

		Mixer_fun_t fn;
		while(m_cmd_queue.try_and_pop(fn)) {
			fn();
		}
		while(m_paused) {
			m_cmd_queue.wait_and_pop(fn);
			fn();
		}
		if(m_quit) {
			return;
		}
		
		m_bench.load_start();
		
		m_audio_status = SDL_GetAudioDeviceStatus(m_device);
		
		active_channels.clear();
		bool prebuffering = m_audio_status == SDL_AUDIO_PAUSED;
		for(auto ch : m_mix_channels) {
			bool active,enabled;
			std::tie(active,enabled) = ch.second->update(time_span_us, prebuffering);
			if(active) {
				active_channels.push_back(std::pair<MixerChannel*,bool>(ch.second.get(),enabled));
			}
		}

		if(!active_channels.empty()) {
			// channels are active, mix them!
			size_t mix_size = mix_channels(active_channels, time_span_us);
			if(mix_size > 0) {
				// if there's audio data available, send it to device output buffer and sinks
				send_packet(mix_size);
			}
			
			if(m_audio_status == SDL_AUDIO_PAUSED) {
				int elapsed = m_pacer.chrono().get_usec() - m_start_time;
				if(m_start_time == 0) {
					// audio starting to get prebuffered
					m_start_time = m_pacer.chrono().get_usec();
					PDEBUGF(LOG_V1, LOG_MIXER, "Prebuffering for %d us\n", m_prebuffer_us);
				} else if(get_buffer_read_avail_us() >= m_prebuffer_us) {
					// audio prebuffered enough, start output to audio device
					SDL_PauseAudioDevice(m_device, 0);
					PDEBUGF(LOG_V1, LOG_MIXER, "Device playing (%d us elapsed, %d bytes/%d us of data)\n",
						elapsed, m_out_buffer.get_read_avail(), get_buffer_read_avail_us());
					m_start_time = 0;
				} else {
					// audio is currently prebuffering
					PDEBUGF(LOG_V2, LOG_MIXER, "  buffer size: %d us\n", get_buffer_read_avail_us());
				}
			} else if(m_audio_status == SDL_AUDIO_PLAYING) {
				assert(m_start_time==0);
				double buf_len_s = m_prebuffer_us/1e6 + (m_heartbeat_us*3)/1e6;
				size_t buf_limit = size_t(buf_len_s*m_audio_spec.freq) * m_frame_size;
				if(m_out_buffer.get_read_avail() > buf_limit) {
					// audio device is not reading its buffer fast enough, drop some data
					buf_limit = m_out_buffer.shrink_data(buf_limit);
					PDEBUGF(LOG_V1, LOG_MIXER, "Device buffer overrun, limited to %d bytes\n", buf_limit);
				} else {
					buf_len_s = m_prebuffer_us/1e6 - (m_heartbeat_us*3)/1e6;
					buf_len_s = std::max(m_heartbeat_us/1e6, buf_len_s);
					buf_limit = size_t(buf_len_s*m_audio_spec.freq) * m_frame_size;
					if(m_out_buffer.get_read_avail() <= buf_limit) {
						// we can't keep up with audio device demands so
						// restart prebuffering
						PDEBUGF(LOG_V1, LOG_MIXER, "Device buffer underrun\n", buf_limit);
						SDL_PauseAudioDevice(m_device, 1);
					}
				}
			}
		} else {
			// there's no active channels
			m_start_time = 0;
			if(m_audio_status == SDL_AUDIO_PLAYING && m_out_buffer.get_read_avail() == 0) {
				// audio device buffer has been emptied, pause the device 
				SDL_PauseAudioDevice(m_device, 1);
				PDEBUGF(LOG_V1, LOG_MIXER, "Device paused\n");
			} else if(m_audio_status == SDL_AUDIO_PAUSED && m_out_buffer.get_read_avail() != 0) {
				// there's data in the output buffer, but the device is not active.
				// it happens when channels deactivate before the prebuffering period is over
				SDL_PauseAudioDevice(m_device, 0);
				PDEBUGF(LOG_V1, LOG_MIXER, "Device playing (%d bytes/%d us of data)\n",
					m_out_buffer.get_read_avail(), get_buffer_read_avail_us());
			}
		}

		m_audio_status = SDL_GetAudioDeviceStatus(m_device);
		m_bench.frame_end();
	}
}

void Mixer::open_audio_device(int _frequency, SDL_AudioFormat _format, int _channels, int _samples)
{
	SDL_AudioSpec want;
	SDL_zero(want);

	PDEBUGF(LOG_V1, LOG_MIXER, "Opening audio device: freq.=%u, bits=%u, ch=%u, samples=%u\n",
		  _frequency, SDL_AUDIO_BITSIZE(_format), _channels, _samples);

	want.freq = _frequency;
	want.format = _format;
	want.channels = _channels;
	//_samples must be a power of 2
	if((_samples & (_samples - 1)) == 0) {
		want.samples = _samples;
	} else {
		want.samples = 1 << (1+int(floor(log2(_samples))));
	}
	want.callback = Mixer::sdl_callback;
	want.userdata = this;

	m_audio_status = SDL_AUDIO_STOPPED;
	m_device = SDL_OpenAudioDevice(nullptr, 0, &want, &m_audio_spec, 0);

	if(m_device == 0) {
		PERRF(LOG_MIXER, "Failed to open audio device: %s\n", SDL_GetError());
		throw std::exception();
	} else if(m_audio_spec.format != want.format) {
		PERRF(LOG_MIXER, "Audio format not supported\n");
		close_audio_device();
		throw std::exception();
	} else if(want.freq != m_audio_spec.freq) {
		PWARNF(LOG_MIXER, "Requested frequency of %d Hz not accepted by the audio driver, using %d Hz\n",
			want.freq, m_audio_spec.freq);
	}

	SDL_PauseAudioDevice(m_device, 1);
	m_audio_status = SDL_GetAudioDeviceStatus(m_device);
}

void Mixer::close_audio_device()
{
	if(m_device) {
		SDL_CloseAudioDevice(m_device);
		m_device = 0;
		m_audio_status = SDL_AUDIO_STOPPED;
	}
}

size_t Mixer::mix_channels(const std::vector<std::pair<MixerChannel*,bool>> &_channels,
		uint64_t _time_span_us)
{
	size_t samples = std::numeric_limits<size_t>::max();
	static double missing_frames = 0.0;
	if(m_audio_status == SDL_AUDIO_PAUSED || m_audio_status == SDL_AUDIO_STOPPED) {
		// the mixer is prebuffering or output device doesn't work
		missing_frames = 0.0;
	}
	double reqframes = us_to_frames(_time_span_us, m_audio_spec.freq) + missing_frames;
	PDEBUGF(LOG_V2, LOG_MIXER, "Mixing %d channels:\n", _channels.size());
	for(auto ch : _channels) {
		PDEBUGF(LOG_V2, LOG_MIXER, "  %s: %d samples avail\n", ch.first->name(), ch.first->out().frames());
		unsigned chframes = std::min(unsigned(reqframes), ch.first->out().frames());
		samples = std::min(samples, size_t(chframes*m_audio_spec.channels));
	}
	missing_frames = reqframes - samples/m_audio_spec.channels;

	PDEBUGF(LOG_V2, LOG_MIXER, "  mixspan: %llu us, samples: %d (req.: %.2f), missing frames: %.2f\n",
			_time_span_us, samples, reqframes*m_audio_spec.channels, missing_frames);

	if(samples == 0) {
		return 0;
	}
	samples = std::min(samples, m_mix_bufsize);
	
	float volume = m_global_volume;
	if(volume > 1.f) {
		volume = (exp(volume) - 1.f)/(M_E - 1.f);
	}
	
	std::fill(m_out_mix.begin(), m_out_mix.begin()+samples, 0.f);
	for(int cat=0; cat<ec_to_i(MixerChannelCategory::MAX); cat++) {
		mix_channels(m_ch_mix[cat], _channels, cat, samples);
		for(size_t i=0; i<samples; i++) {
			m_out_mix[i] += m_ch_mix[cat][i] * volume;
		}
	}

	return samples;
}

void Mixer::mix_channels(std::vector<float> &_buf,
	const std::vector<std::pair<MixerChannel*,bool>> &_channels,
	int _chcat, size_t _samples)
{
	unsigned frames = _samples / m_audio_spec.channels;
	std::fill(_buf.begin(), _buf.begin()+_samples, 0.f);
	for(auto ch : _channels) {
		int chcat = ec_to_i(ch.first->category());
		if(chcat != _chcat) {
			continue;
		}
		const float *chdata = &ch.first->out().at<float>(0);
		unsigned chsamples = ch.first->out().samples();
		float cat_volume = m_channels_volume[chcat];
		if(cat_volume > 1.f) {
			cat_volume = (exp(cat_volume) - 1.f)/(M_E - 1.f);
		}
		float ch_volume = ch.first->volume();
		if(ch_volume > 1.f) {
			ch_volume = (exp(ch_volume) - 1.f)/(M_E - 1.f);
		}
		for(size_t i=0; i<_samples; i++) {
			float v1,v2;
			if(i<chsamples) {
				v1 = chdata[i] * ch_volume * cat_volume;
			} else {
				v1 = 0.f;
			}
			v2 = _buf[i];
			_buf[i] = v1 + v2;
		}
		ch.first->pop_out_frames(frames);
	}
}

void Mixer::send_to_sinks(const std::vector<int16_t> &_data, int _category)
{
	std::lock_guard<std::mutex> lock(m_sinks_mutex);
	
	// _category will be MixerChannelCategory::MAX for the global mix
	for(auto sink : m_sinks) {
		if(sink != nullptr) {
			try {
				sink(_data, _category);
			} catch(...) {}
		}
	}
}

void Mixer::send_packet(size_t _samples)
{
	if(m_audio_spec.format != AUDIO_S16) {
		PDEBUGF(LOG_V0, LOG_MIXER, "Unsupported audio format\n");
		return;
	}
	
	float volume = m_global_volume;
	if(volume > 1.f) {
		volume = (exp(volume) - 1.f)/(M_E - 1.f);
	}
	
	// fixed signed 16 bit sample size
	const size_t bytes = _samples * 2;
	std::vector<int16_t> tmpbuf(_samples);
	
	for(int cat=0; cat<ec_to_i(MixerChannelCategory::MAX); cat++) {
		assert(m_ch_mix[cat].size() >= _samples);
		for(size_t i=0; i<_samples; i++) {
			tmpbuf[i] = AudioBuffer::f32_to_s16(m_ch_mix[cat][i]);
		}
		send_to_sinks(tmpbuf, cat);
	}

	assert(m_out_mix.size() >= _samples);
	for(size_t i=0; i<_samples; i++) {
		tmpbuf[i] = AudioBuffer::f32_to_s16(m_out_mix[i]);
	}
	send_to_sinks(tmpbuf, ec_to_i(MixerChannelCategory::MAX));
	
	if(m_device && m_audio_status != SDL_AUDIO_STOPPED) {
		if(m_out_buffer.write((uint8_t*)&tmpbuf[0], bytes) < bytes) {
			PERRF(LOG_MIXER, "Audio buffer overflow\n");
		}
	}
	
	PDEBUGF(LOG_V2, LOG_MIXER, "buf write: %d frames, %d bytes, buf fullness: %d\n",
			_samples / m_audio_spec.channels, bytes, m_out_buffer.get_read_avail());
}

void Mixer::audio_sink(const std::vector<int16_t> &_data, int _category)
{
	if(_category == ec_to_i(MixerChannelCategory::AUDIO)) {
		try {
			m_wav.write_audio_data(&_data[0], _data.size()*2);
		} catch(std::exception &e) {
			stop_capture();
		}
	}
}

std::shared_ptr<MixerChannel> Mixer::register_channel(MixerChannel_handler _callback,
		const std::string &_name)
{
	static int chcount = 0;
	chcount++;
	auto ch = std::make_shared<MixerChannel>(this, _callback, _name, chcount);
	m_mix_channels[_name] = ch;
	ch->set_out_spec({AUDIO_FORMAT_F32, unsigned(m_audio_spec.channels),
		double(m_audio_spec.freq)});
	return ch;
}

void Mixer::unregister_channel(std::shared_ptr<MixerChannel> _channel)
{
	m_mix_channels.erase(_channel->name());
}

int Mixer::register_sink(AudioSinkHandler _sink)
{
	// called by multiple threads
	std::lock_guard<std::mutex> lock(m_sinks_mutex);
	
	for(size_t i=0; i<m_sinks.size(); i++) {
		if(m_sinks[i] == nullptr) {
			m_sinks[i] = _sink;
			m_silence_channel->enable(true);
			return int(i);
		}
	}
	throw std::exception();
}

void Mixer::unregister_sink(int _id)
{
	// called by multiple threads
	std::lock_guard<std::mutex> lock(m_sinks_mutex);
	
	if(_id>=0 && _id<int(m_sinks.size())) {
		m_sinks[_id] = nullptr;
	}
	bool empty = false;
	for(auto sink : m_sinks) {
		empty |= (sink==nullptr);
	}
	m_silence_channel->enable(!empty);
}

void Mixer::sig_config_changed(std::mutex &_mutex, std::condition_variable &_cv)
{
	//this signal should be preceded by a pause command
	m_cmd_queue.push([&] () {
		std::unique_lock<std::mutex> lock(_mutex);
		config_changed();
		_cv.notify_one();
	});
}

uint64_t Mixer::get_buffer_read_avail_us() const
{
	double bytes = m_out_buffer.get_read_avail();
	double usec_per_frame = 1000000.0 / double(m_audio_spec.freq);
	double frames_in_buffer = bytes / m_frame_size;
	uint64_t time_left = frames_in_buffer * usec_per_frame;
	return time_left;
}

bool Mixer::create_silence_samples(uint64_t _time_span_us, bool _prebuf, bool _firstupd)
{
	// this channel will render silence basing its timing on the machine.
	// it's active when there are sinks registered, so that they can record
	// audio cards output with a constant passage of time.
	
	UNUSED(_prebuf);
	
	static uint64_t prev_mtime_us = 0;
	static double gen_frames_rem = .0;
	
	uint64_t cur_mtime_us = g_machine.get_virt_time_us_mt();
	uint64_t elapsed_us = 0;
	
	if(_firstupd) {
		elapsed_us = _time_span_us;
	} else {
		assert(cur_mtime_us >= prev_mtime_us);
		elapsed_us = cur_mtime_us - prev_mtime_us;
	}
	prev_mtime_us = cur_mtime_us;
	
	double elapsed_frames = m_silence_channel->in_spec().us_to_frames(elapsed_us);
	elapsed_frames += gen_frames_rem;
	
	unsigned needed_frames = round(m_silence_channel->in_spec().us_to_frames(_time_span_us));
	unsigned gen_frames = elapsed_frames;
	gen_frames_rem = elapsed_frames - double(gen_frames);
	
	m_silence_channel->in().fill_frames_silence(gen_frames);
	m_silence_channel->input_finish();
	
	PDEBUGF(LOG_V2, LOG_MIXER, "Silence: mix time: %04d us, frames: %d, machine time: %d us, created frames: %d\n",
			_time_span_us, needed_frames, elapsed_us, gen_frames);

	return true;
}

template <int Channels>
std::vector<std::shared_ptr<Dsp::Filter>> Mixer::create_filters(double _rate, std::string _filters_def)
{
	std::vector<std::shared_ptr<Dsp::Filter>> filters;
	
	auto filters_toks = AppConfig::parse_tokens(_filters_def, "\\|");
	
	for(auto filter_str : filters_toks) {
		
		PDEBUGF(LOG_V2, LOG_MIXER, "Filter definition: %s\n", filter_str.c_str());
		
		auto filter_toks = AppConfig::parse_tokens(filter_str, "\\,");
		if(filter_toks.empty()) {
			PDEBUGF(LOG_V2, LOG_MIXER, "Invalid filter definition: %s\n", filter_str.c_str());
			continue;
		}
		
		std::string fname = str_trim(str_to_lower(filter_toks[0]));
		
		const std::map<std::string, int> filter_types = {
			{"lowpass",   1},
			{"highpass",  2}, 
			{"bandpass",  3},
			{"bandstop",  4}, 
			{"lowshelf",  5}, 
			{"highshelf", 6},
			{"bandshelf", 7}
		};
		
		if(filter_types.find(fname) == filter_types.end()) {
			PERRF(LOG_MIXER, "Invalid filter: %s\n", fname.c_str());
			continue;
		}
		
		std::shared_ptr<Dsp::Filter> filter;

		switch(filter_types.at(fname)) {
			case 1: filter = std::make_shared<Dsp::FilterDesign<Dsp::Butterworth::Design::LowPass  <50>,Channels>>(); break;
			case 2: filter = std::make_shared<Dsp::FilterDesign<Dsp::Butterworth::Design::HighPass <50>,Channels>>(); break;
			case 3: filter = std::make_shared<Dsp::FilterDesign<Dsp::Butterworth::Design::BandPass <50>,Channels>>(); break;
			case 4: filter = std::make_shared<Dsp::FilterDesign<Dsp::Butterworth::Design::BandStop <50>,Channels>>(); break;
			case 5: filter = std::make_shared<Dsp::FilterDesign<Dsp::Butterworth::Design::LowShelf <50>,Channels>>(); break;
			case 6: filter = std::make_shared<Dsp::FilterDesign<Dsp::Butterworth::Design::HighShelf<50>,Channels>>(); break;
			case 7: filter = std::make_shared<Dsp::FilterDesign<Dsp::Butterworth::Design::BandShelf<50>,Channels>>(); break;
			default:
				PERRF(LOG_MIXER, "Invalid filter: %s\n", fname.c_str());
				continue;
		}
		
		PDEBUGF(LOG_V1, LOG_MIXER, "Filter: %s\n", filter->getName().c_str());
		
		std::map<std::string, Dsp::ParamInfo> param_types = {
			{"order",  Dsp::ParamInfo::defaultOrderParam()},
			{"cutoff", Dsp::ParamInfo::defaultFrequencyParam()},
			{"center", Dsp::ParamInfo::defaultFrequencyParam()},
			{"bw",     Dsp::ParamInfo::defaultBandwidthHzParam()},
			{"gain",   Dsp::ParamInfo::defaultGainParam()}
		};
		
		// remove the filter name, parse parameters
		filter_toks.erase(filter_toks.begin());
		
		Dsp::Params fparams;
		fparams.clear();
		fparams[Dsp::idSampleRate] = _rate;
		
		for(auto filter_par : filter_toks) {
			
			auto param_toks = AppConfig::parse_tokens(filter_par, "\\=");
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
				fparams[param_types[pname].getId()] = AppConfig::parse_real(param_toks[1]);
			} catch(std::exception &) {
				PERRF(LOG_MIXER, "invalid filter parameter value: %s\n", param_toks[1].c_str());
				continue;
			}
			
			PDEBUGF(LOG_V1, LOG_MIXER, "  %s = %.3f\n",
					param_types[pname].getName(),
					fparams[param_types[pname].getId()]
					);
		}
		
		filter->setParams(fparams);
		
		filters.push_back(filter);
	}
	
	return filters;
}

template std::vector<std::shared_ptr<Dsp::Filter>> Mixer::create_filters<1>(double _rate, std::string _filters_def);
template std::vector<std::shared_ptr<Dsp::Filter>> Mixer::create_filters<2>(double _rate, std::string _filters_def);

void Mixer::cmd_pause()
{
	m_cmd_queue.push([this] () {
		m_paused = true;
		if(m_device && m_audio_status==SDL_AUDIO_PLAYING) {
			SDL_PauseAudioDevice(m_device, 1);
		}
		PDEBUGF(LOG_V1, LOG_MIXER, "Mixing paused\n");
	});
}

void Mixer::cmd_resume()
{
	m_cmd_queue.push([this] () {
		if(!m_paused) {
			return;
		}
		m_paused = false;
		// audio device status is "paused".
		// if channels are active then prebuffering will be reactivated.
		m_start_time = m_pacer.chrono().get_usec();
		m_pacer.start();
		m_bench.start();
		PDEBUGF(LOG_V1, LOG_MIXER, "Mixing resumed\n");
	});
}

void Mixer::cmd_quit()
{
	m_cmd_queue.push([this] () {
		m_paused = false;
		m_quit = true;
		if(m_audio_capture) {
			stop_capture();
		}
		close_audio_device();
		SDL_AudioQuit();
	});
}

void Mixer::cmd_start_capture()
{
	m_cmd_queue.push([this] () {
		start_capture();
	});
}

void Mixer::cmd_stop_capture()
{
	m_cmd_queue.push([this] () {
		stop_capture();
	});
}

void Mixer::cmd_toggle_capture()
{
	m_cmd_queue.push([this] () {
		if(m_audio_capture) {
			stop_capture();
		} else {
			start_capture();
		}
	});
}

void Mixer::cmd_set_global_volume(float _volume)
{
	m_cmd_queue.push([=] () {
		m_global_volume = _volume;
	});
}

void Mixer::cmd_set_category_volume(MixerChannelCategory _cat, float _volume)
{
	m_cmd_queue.push([=] () {
		m_channels_volume[static_cast<int>(_cat)] = std::max(0.f,_volume);
	});
}

std::vector<std::shared_ptr<MixerChannel>> Mixer::dbg_get_channels()
{
	// not mt safe.
	std::vector<std::shared_ptr<MixerChannel>> chs;
	for(auto ch : m_mix_channels) {
		chs.push_back(ch.second);
	}
	return chs;
}
