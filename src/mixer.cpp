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
m_start_time(0),
m_prebuffer(50),
m_machine(nullptr),
m_heartbeat(10000),
m_next_beat_diff(0),
m_quit(false),
m_audio_status(SDL_AUDIO_STOPPED),
m_paused(false),
m_device(0),
m_frame_size(512),
m_audio_capture(false),
m_global_volume(1.f)
{
	m_out_buffer.set_size(MIXER_BUFSIZE);
	memset(&m_device_spec, 0, sizeof(SDL_AudioSpec));
	//sane defaults used to initialise the channels before the audio device
	m_device_spec.freq = 48000;
	m_device_spec.channels = MIXER_CHANNELS;
	m_device_spec.format = AUDIO_S16;
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
		PDEBUGF(LOG_V1, LOG_MIXER, "buffer underrun\n");
		memset(&stream[bytes], mixer->m_device_spec.silence, len-bytes);
	}
}

void Mixer::calibrate(const Chrono &_c)
{
	m_main_chrono.calibrate(_c);
}

void Mixer::init(Machine *_machine)
{
	m_machine = _machine;
	m_main_chrono.start();
	m_bench.init(&m_main_chrono, 1000);

	m_paused = true;

	if(SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		PERRF(LOG_MIXER, "Unable to init SDL audio: %s\n", SDL_GetError());
		throw std::exception();
	}

	int i, count = SDL_GetNumAudioDevices(0);
	if(count == 0) {
		PERRF(LOG_MIXER, "Unable to find any audio device\n");
		return;
	}
	for(i=0; i<count; ++i) {
		PINFOF(LOG_V1, LOG_MIXER, "Audio device %d: %s\n", i, SDL_GetAudioDeviceName(i, 0));
	}
	m_paused = false;
}

void Mixer::start_capture()
{
	if(!is_enabled()) {
		PERRF(LOG_MIXER, "unable to start audio recording\n");
		return;
	}
	std::string path = g_program.config().get_file(PROGRAM_SECTION, PROGRAM_CAPTURE_DIR, FILE_TYPE_USER);
	path = FileSys::get_next_filename(path, "sound_", ".wav");
	if(!path.empty()) {
		try {
			m_wav.open_write(path.c_str(), m_device_spec.freq, SDL_AUDIO_BITSIZE(m_device_spec.format), m_device_spec.channels);
			std::string mex = "started audio recording to " + path;
			PINFOF(LOG_V0, LOG_MIXER, "%s\n", mex.c_str());
			GUI::instance()->show_message(mex.c_str());
		} catch(std::exception &e) { }
	}
	for(auto ch : m_mix_channels) {
		ch.second->on_capture(true);
	}
	m_audio_capture = true;
}

void Mixer::stop_capture()
{
	if(!is_enabled()) {
		return;
	}
	m_wav.close();
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
		stop_wave_playback();
	}
	bool capture = m_audio_capture;
	if(m_audio_capture) {
		stop_capture();
	}

	int frequency = g_program.config().get_int(MIXER_SECTION, MIXER_RATE);
	m_prebuffer = g_program.config().get_int(MIXER_SECTION, MIXER_PREBUFFER); //msecs
	int samples = g_program.config().get_int(MIXER_SECTION, MIXER_SAMPLES);
	m_frame_size = 0;

	try {
		start_wave_playback(frequency, MIXER_BIT_DEPTH, MIXER_CHANNELS, samples);

		m_frame_size = m_device_spec.channels * (SDL_AUDIO_BITSIZE(m_device_spec.format) / 8);
		m_heartbeat = round(1e6 / (double(m_device_spec.freq) / 512.0));

		PINFOF(LOG_V1, LOG_MIXER, "Mixer beat period: %u usec\n", m_heartbeat);

		m_prebuffer = clamp(m_prebuffer, int(m_heartbeat/1000), int(m_heartbeat/100)); //msecs

		int buf_len = std::max(m_prebuffer*2, 1000); //msecs
		int buf_frames = (m_device_spec.freq * buf_len) / 1000;
		m_out_buffer.set_size(buf_frames * m_frame_size);
		m_mix_buffer.resize(buf_frames * m_device_spec.channels);

		PDEBUGF(LOG_V1, LOG_MIXER, "prebuffer: %d msec., ring buffer: %d bytes\n",
				m_prebuffer, buf_frames * m_frame_size);

		for(auto ch : m_mix_channels) {
			ch.second->set_out_spec({AUDIO_FORMAT_F32,
				unsigned(m_device_spec.channels),unsigned(m_device_spec.freq)});
		}
	} catch(std::exception &e) {
		PERRF(LOG_MIXER, "wave audio output disabled\n");
	}

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
	m_next_beat_diff = 0;
	PDEBUGF(LOG_V1, LOG_MIXER, "Mixer thread started\n");
	main_loop();
}

void Mixer::main_loop()
{
	std::vector<std::pair<MixerChannel*,bool>> active_channels;

	uint64_t time_span_us;

	while(true) {
		time_span_us = m_main_chrono.elapsed_usec();
		uint64_t time_slept = 0;
		if(time_span_us < m_heartbeat) {
			uint64_t sleep = m_heartbeat - time_span_us;
			uint64_t t0 = m_main_chrono.get_usec();
			std::this_thread::sleep_for( std::chrono::microseconds(sleep + m_next_beat_diff) );
			m_main_chrono.start();
			uint64_t t1 = m_main_chrono.get_usec();
			assert(t1 >= t0);
			time_slept = (t1 - t0);
			time_span_us += time_slept;
			m_next_beat_diff = (sleep+m_next_beat_diff) - time_slept;
		} else {
			m_main_chrono.start();
		}

		m_bench.beat_start();

		Mixer_fun_t fn;
		while(m_cmd_queue.try_and_pop(fn)) {
			fn();
		}

		if(m_quit) {
			return;
		} else if(m_paused) {
			continue;
		} else if(!is_enabled()) {
			for(auto ch : m_mix_channels) {
				if(ch.second->is_enabled()) {
					ch.second->update(time_span_us, false);
				}
				ch.second->flush();
			}
			continue;
		}

		active_channels.clear();

		m_audio_status = SDL_GetAudioDeviceStatus(m_device);
		bool prebuffering = m_audio_status == SDL_AUDIO_PAUSED;

		//update the registered channels
		for(auto ch : m_mix_channels) {
			bool active,enabled;
			std::tie(active,enabled) = ch.second->update(time_span_us, prebuffering);
			if(active) {
				active_channels.push_back(std::pair<MixerChannel*,bool>(ch.second.get(),enabled));
			}
		}

		if(!active_channels.empty()) {
			size_t mix_size = mix_channels(active_channels, time_span_us);
			if(mix_size>0) {
				send_packet(mix_size);
			}
			if(m_audio_status == SDL_AUDIO_PAUSED) {
				int elapsed = m_main_chrono.get_msec() - m_start_time;
				if(m_start_time == 0) {
					m_start_time = m_main_chrono.get_msec();
					PDEBUGF(LOG_V1, LOG_MIXER, "prebuffering %d msecs\n", m_prebuffer);
				} else if(get_buffer_len() >= m_prebuffer*1000) {
					SDL_PauseAudioDevice(m_device, 0);
					PDEBUGF(LOG_V1, LOG_MIXER, "playing (%d msecs elapsed, %d bytes/%d usecs of data)\n",
							elapsed, m_out_buffer.get_read_avail(), get_buffer_len());
					m_start_time = 0;
				} else {
					PDEBUGF(LOG_V2, LOG_MIXER, "buffer size: %d ms\n", get_buffer_len());
				}
			} else {
				assert(m_start_time==0);
				double buf_len_s = m_prebuffer/1000.0 + (m_heartbeat*3)/1e6;
				size_t buf_limit = size_t(buf_len_s*m_device_spec.freq) * m_frame_size;
				if(m_out_buffer.get_read_avail() > buf_limit) {
					buf_limit = m_out_buffer.shrink_data(buf_limit);
					PDEBUGF(LOG_V1, LOG_MIXER, "out buffer overrun, limited to %d bytes\n", buf_limit);
				} else {
					buf_len_s = m_prebuffer/1000.0 - (m_heartbeat*3)/1e6;
					buf_len_s = std::max(m_heartbeat/1e6, buf_len_s);
					buf_limit = size_t(buf_len_s*m_device_spec.freq) * m_frame_size;
					if(m_out_buffer.get_read_avail() <= buf_limit) {
						PDEBUGF(LOG_V1, LOG_MIXER, "out buffer underrun\n", buf_limit);
						SDL_PauseAudioDevice(m_device, 1);
					}
				}
			}
		} else {
			m_start_time = 0;
			if(m_audio_status==SDL_AUDIO_PLAYING && m_out_buffer.get_read_avail() == 0) {
				SDL_PauseAudioDevice(m_device, 1);
				PDEBUGF(LOG_V1, LOG_MIXER, "paused\n");
			} else if(m_audio_status==SDL_AUDIO_PAUSED && m_out_buffer.get_read_avail() != 0) {
				SDL_PauseAudioDevice(m_device, 0);
				PDEBUGF(LOG_V1, LOG_MIXER, "playing\n");
			}
		}

		m_audio_status = SDL_GetAudioDeviceStatus(m_device);
		m_bench.beat_end();
	}
}

void Mixer::start_wave_playback(int _frequency, int _bits, int _channels, int _samples)
{
	SDL_AudioSpec want;
	SDL_zero(want);

	PDEBUGF(LOG_V1, LOG_MIXER, "start wave playback: %u, %u, %u, %u\n",
		  _frequency, _bits, _channels, _samples);

	want.freq = _frequency;

	if(_bits == 16) {
		want.format = AUDIO_S16;
	} else if(_bits == 8) {
		want.format = AUDIO_U8;
	} else {
		PERRF(LOG_MIXER, "invalid bit depth %d\n", _bits);
		throw std::exception();
	}

	want.channels = _channels;
	//_samples must be a power of 2
	if((_samples & (_samples - 1)) == 0) {
		want.samples = _samples;
	} else {
		//want.samples = pow(2.0,(ceil(log2(double(_samples)))));
		want.samples = 1 << (1+int(floor(log2(_samples))));
	}
	want.callback = Mixer::sdl_callback;
	want.userdata = this;

	m_audio_status = SDL_AUDIO_STOPPED;
	m_device = SDL_OpenAudioDevice(nullptr, 0, &want, &m_device_spec, 0);
	if(m_device == 0) {
		PERRF(LOG_MIXER, "Failed to open audio: %s\n", SDL_GetError());
		throw std::exception();
	} else if(want.freq != m_device_spec.freq || m_device_spec.format != want.format) {
		PERRF(LOG_MIXER, "We didn't get the requested audio format\n");
		SDL_CloseAudioDevice(m_device);
		m_device = 0;
	    throw std::exception();
	}

	SDL_PauseAudioDevice(m_device, 1);
	m_audio_status = SDL_GetAudioDeviceStatus(m_device);

	PINFOF(LOG_V0, LOG_MIXER, "Mixing at %u Hz, %u bit, %u channels, %u samples\n",
			m_device_spec.freq, _bits, m_device_spec.channels, m_device_spec.samples);
}

void Mixer::stop_wave_playback()
{
	if(m_device) {
		SDL_CloseAudioDevice(m_device);
		m_device = 0;
	}
}

size_t Mixer::mix_channels(const std::vector<std::pair<MixerChannel*,bool>> &_channels,
		uint64_t _time_span_us)
{
	size_t mixlen = std::numeric_limits<size_t>::max();
	unsigned frames;
	static double missing = 0.0;
	if(m_audio_status == SDL_AUDIO_PAUSED) {
		//the mixer is prebuffering
		missing = 0.0;
	}
	double reqframes = us_to_frames(_time_span_us, m_device_spec.freq) + missing;
	for(auto ch : _channels) {
		frames = std::min(unsigned(reqframes), ch.first->out().frames());
		mixlen = std::min(mixlen, size_t(frames*m_device_spec.channels));
	}
	missing = reqframes - mixlen;

	PDEBUGF(LOG_V2, LOG_MIXER, "mixspan: %llu, mixlen: %d (req.: %.2f), missing: %.2f\n",
			_time_span_us, mixlen, reqframes*m_device_spec.channels, missing);

	if(mixlen==0) {
		return 0;
	}
	mixlen = std::min(mixlen, m_mix_buffer.size());
	frames = mixlen / m_device_spec.channels;
	std::fill(m_mix_buffer.begin(), m_mix_buffer.begin()+mixlen, 0.f);
	for(auto ch : _channels) {
		const float *chdata = &ch.first->out().at<float>(0);
		unsigned chframes = ch.first->out().frames();
		float cat_volume = m_channels_volume[static_cast<int>(ch.first->category())];
		if(cat_volume > 1.f) {
			cat_volume = (exp(cat_volume) - 1.f)/(M_E - 1.f);
		}
		float ch_volume = ch.first->volume();
		if(ch_volume > 1.f) {
			ch_volume = (exp(ch_volume) - 1.f)/(M_E - 1.f);
		}
		for(size_t i=0; i<mixlen; i++) {
			float v1,v2;
			if(i<chframes) {
				v1 = chdata[i] * ch_volume * cat_volume;
			} else {
				v1 = 0.f;
			}
			v2 = m_mix_buffer[i];
			m_mix_buffer[i] = v1 + v2;
		}
		ch.first->pop_out_frames(frames);
	}

	return mixlen;
}

bool Mixer::send_packet(size_t _len)
{
	if(m_device == 0) {
		return false;
	}
	bool ret = true;
	std::vector<int16_t> buf(_len);
	size_t bytes = _len;
	float volume = m_global_volume;
	if(volume > 1.f) {
		volume = (exp(volume) - 1.f)/(M_E - 1.f);
	}
	//convert from float
	switch(SDL_AUDIO_BITSIZE(m_device_spec.format)) {
		case 16:
			for(size_t i=0; i<_len; i++) {
				int32_t ival = (m_mix_buffer[i]*volume) * 32768.f;
				if(ival < -32768) {
					ival = -32768;
				}
				if(ival > 32767) {
					ival = 32767;
				}
				buf[i] = ival;
			}
			bytes = _len*2;
			break;
		default:
			PERRF_ABORT(LOG_MIXER, "unsupported bit depth\n");
			return false;
	}

	PDEBUGF(LOG_V2, LOG_MIXER, "buf write: %d frames, %d bytes, buf fullness: %d\n",
			_len / m_device_spec.channels, bytes, (m_out_buffer.get_read_avail() + bytes));

	if(m_out_buffer.write((uint8_t*)&buf[0], bytes) < bytes) {
		PERRF(LOG_MIXER, "audio buffer overflow\n");
		ret = false;
	}

	if(m_wav.is_open()) {
		try {
			m_wav.write((uint8_t*)&buf[0], bytes);
		} catch(std::exception &e) {
			cmd_stop_capture();
		}
	}

	return ret;
}

std::shared_ptr<MixerChannel> Mixer::register_channel(MixerChannel_handler _callback,
		const std::string &_name)
{
	auto ch = std::make_shared<MixerChannel>(this, _callback, _name);
	m_mix_channels[_name] = ch;
	ch->set_out_spec({AUDIO_FORMAT_F32, unsigned(m_device_spec.channels),
		unsigned(m_device_spec.freq)});
	return ch;
}

void Mixer::unregister_channel(std::shared_ptr<MixerChannel> _channel)
{
	m_mix_channels.erase(_channel->name());
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

int Mixer::get_buffer_len() const
{
	double bytes = m_out_buffer.get_read_avail();
	double usec_per_frame = 1000000.0 / double(m_device_spec.freq);
	double frames_in_buffer = bytes / m_frame_size;
	int time_left = frames_in_buffer * usec_per_frame;
	return time_left;
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
	});
}

void Mixer::cmd_resume()
{
	m_cmd_queue.push([this] () {
		if(!m_paused) {
			return;
		}
		m_paused = false;
		m_start_time = m_main_chrono.get_msec() - m_prebuffer/2;
	});
}

void Mixer::cmd_quit()
{
	m_cmd_queue.push([this] () {
		m_quit = true;
		if(m_audio_capture) {
			stop_capture();
		}
		stop_wave_playback();
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
