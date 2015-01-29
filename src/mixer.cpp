/*
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ibmulator.h"
#include "mixer.h"
#include "program.h"
#include "machine.h"
#include "SDL.h"
#include "wav.h"
#include <cmath>

Mixer g_mixer;

MixerChannel::MixerChannel(MixerChannel_handler _callback, uint16_t _rate, const std::string &_name)
:
m_enabled(false),
m_rate(_rate),
m_name(_name),
m_callback(_callback)
{

}


void MixerChannel::update(uint64_t _time)
{
	ASSERT(m_callback);
	m_callback(_time);
}

void MixerChannel::add_samples(uint8_t *_data, size_t _size)
{
	g_mixer.send_wave_packet(_data, _size);
}

Mixer::Mixer()
:
m_device(0),
m_heartbeat(MIXER_HEARTBEAT),
m_audio_capture(false)
{
	m_buffer.set_size(MIXER_BUFSIZE);
}

Mixer::~Mixer()
{
}

void Mixer::sdl_callback(void *userdata, Uint8 *stream, int len)
{
	Mixer * mixer = static_cast<Mixer*>(userdata);

	memset(stream, mixer->m_device_spec.silence, len);

	mixer->m_buffer.read(stream, len);
	if(mixer->m_wav.is_open()) {
		mixer->m_wav.save(stream,len);
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
	if(!MULTITHREADED) {
		m_heartbeat = g_program.get_beat_time_usec();
	}
	PINFOF(LOG_V1, LOG_MIXER, "Mixer beat period: %u usec\n", m_heartbeat);

	m_enabled.store(false);

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

	config_changed();
	m_enabled.store(true);
}

void Mixer::start_capture()
{
	std::string path = g_program.config().get_file(PROGRAM_SECTION, PROGRAM_CAPTURE_DIR, false);
	path = Program::get_next_filename(path, "sound_", ".wav");

	if(!path.empty()) {
		path += ".wav";
		SDL_LockAudioDevice(m_device);
		m_wav.open(path.c_str(), m_frequency, m_bit_depth, m_channels);
		SDL_UnlockAudioDevice(m_device);
		m_audio_capture = true;
		PINFOF(LOG_V0, LOG_MIXER, "started audio capturing to '%s'\n", path.c_str());
	}
}

void Mixer::stop_capture()
{
	SDL_LockAudioDevice(m_device);
	m_wav.close();
	SDL_UnlockAudioDevice(m_device);

	m_audio_capture = false;
	PINFOF(LOG_V0, LOG_MIXER, "stopped audio capturing\n");
}

void Mixer::config_changed()
{
	//before the config can change the audio playback must be stopped
	if(m_device) {
		RASSERT(SDL_GetAudioDeviceStatus(m_device)!=SDL_AUDIO_PLAYING);
		stop_wave_playback();
	}
	if(m_audio_capture) {
		stop_capture();
	}

	m_frequency = g_program.config().get_int(MIXER_SECTION, MIXER_RATE);
	m_bit_depth = 16;
	m_channels = 1;
	RASSERT(m_bit_depth == 8 || m_bit_depth == 16);
	m_bytes_per_sample = m_channels * (m_bit_depth==8?1:2);
	int samples = g_program.config().get_int(MIXER_SECTION, MIXER_SAMPLES);

	m_prebuffer = g_program.config().get_int(MIXER_SECTION, MIXER_PREBUFFER); //msecs
	int buf_len = std::max(m_prebuffer*2, 1000); //msecs
	int buf_size = (m_bytes_per_sample * m_frequency * buf_len) / 1000;

	m_buffer.set_size(buf_size);

	try {
		start_wave_playback(m_frequency, m_bit_depth, m_channels, samples);
	} catch(std::exception &e) {
		PERRF(LOG_MIXER, "wave audio output disabled\n");
	}

	if(m_audio_capture) {
		start_capture();
	}

	PDEBUGF(LOG_V1, LOG_MIXER, "prebuffer: %d msec., ring buffer: %d bytes\n",
			m_prebuffer, buf_size);

	m_prebuffer *= 1000; //msec to usec
}

void Mixer::start()
{
	m_quit = false;
	m_start = 0;
	m_next_beat_diff = 0;
	if(MULTITHREADED) {
		PDEBUGF(LOG_V2, LOG_MACHINE, "Mixer thread started\n");
		main_loop();
	}
}

void Mixer::main_loop()
{
	#if MULTITHREADED
	while(true) {
		uint64_t time = m_main_chrono.elapsed_usec();
		if(time < m_heartbeat) {
			uint64_t sleep = m_heartbeat - time;
			uint64_t t0 = m_main_chrono.get_usec();
			std::this_thread::sleep_for( std::chrono::microseconds(sleep + m_next_beat_diff) );
			m_main_chrono.start();
			uint64_t t1 = m_main_chrono.get_usec();
			m_next_beat_diff = (sleep+m_next_beat_diff) - (t1 - t0);
		} else {
			m_main_chrono.start();
		}
	#endif

		m_bench.beat_start();

		Mixer_fun_t fn;
		while(m_cmd_queue.try_and_pop(fn)) {
			fn();
		}

		if(m_quit) {
			return;
		}

		bool enable_audio = false;
		bool mixer_enabled = m_enabled.load();
		if(mixer_enabled) {
			//update the registered channels
			uint64_t time = m_machine->get_virt_time_ns_mt();

			for(auto ch : m_mix_channels) {
				if(ch.second->is_enabled()) {
					enable_audio = true;
					ch.second->update(time);
				}
			}
		}
		if(enable_audio) {
			if(SDL_GetAudioDeviceStatus(m_device)==SDL_AUDIO_PAUSED) {
				uint64_t now = m_main_chrono.get_usec();
				uint64_t elapsed = now - m_start;
				if(m_start == 0) {
					m_start = m_main_chrono.get_usec();
					PINFOF(LOG_V1, LOG_MIXER, "prebuffering %llu usecs\n", m_prebuffer);
				} else if(elapsed > (uint)m_prebuffer) {
					SDL_PauseAudioDevice(m_device, 0);
					PINFOF(LOG_V1, LOG_MIXER, "playing (%llu usecs elapsed)\n", elapsed);
					m_start = 0;
				}
			}
		} else {
			if(!mixer_enabled) {
				if(m_device && SDL_GetAudioDeviceStatus(m_device)==SDL_AUDIO_PLAYING) {
					SDL_PauseAudioDevice(m_device, 0);
				}
			} else {
				m_start = 0;
				SDL_AudioStatus audio_status = SDL_GetAudioDeviceStatus(m_device);
				if(audio_status==SDL_AUDIO_PLAYING && m_buffer.get_read_avail() == 0) {
					SDL_PauseAudioDevice(m_device, 1);
					PINFOF(LOG_V1, LOG_MIXER, "paused\n");
				} else if(audio_status==SDL_AUDIO_PAUSED && m_buffer.get_read_avail() != 0) {
					SDL_PauseAudioDevice(m_device, 0);
					PINFOF(LOG_V1, LOG_MIXER, "playing\n");
				}
			}
		}

		m_bench.beat_end();

	#if MULTITHREADED
	}
	#endif
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

	m_device = SDL_OpenAudioDevice(NULL, 0, &want, &m_device_spec, 0);
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

	PINFOF(LOG_V0, LOG_MIXER, "Mixing at %uHz, %u bit, %u channels, %u samples\n",
			m_device_spec.freq, _bits, m_device_spec.channels, m_device_spec.samples);
}

void Mixer::stop_wave_playback()
{
	if(m_device) {
		SDL_CloseAudioDevice(m_device);
		m_device = 0;
	}
}

bool Mixer::send_wave_packet(uint8_t *_data, size_t _len)
{
	if(m_device == 0) {
		return false;
	}
	bool ret = true;
	SDL_LockAudioDevice(m_device);
	if(m_buffer.write(_data, _len) < _len) {
		PERRF(LOG_MIXER, "audio buffer overflow\n");
		ret = false;
	}
	SDL_UnlockAudioDevice(m_device);
	return ret;
}

std::shared_ptr<MixerChannel> Mixer::register_channel(MixerChannel_handler _callback,
		uint16_t _rate, const std::string &_name)
{
	std::shared_ptr<MixerChannel> ch = std::make_shared<MixerChannel>(
			_callback, _rate, _name
			);
	m_mix_channels[_name] = ch;
	return ch;
}

void Mixer::unregister_channel(const std::string &_ch_name)
{
	auto ch = m_mix_channels.find(_ch_name);
	if(ch != m_mix_channels.end()) {
		ch->second.reset();
	}
}

void Mixer::sig_config_changed()
{
	//this signal should be preceded by a pause command
	m_cmd_queue.push([this] () {
		std::unique_lock<std::mutex> lock(g_program.ms_lock);
		config_changed();
		g_program.ms_cv.notify_one();
	});
}

void Mixer::cmd_pause()
{
	m_cmd_queue.push([this] () {
		m_enabled.store(false);
		if(m_device && SDL_GetAudioDeviceStatus(m_device)==SDL_AUDIO_PLAYING) {
			SDL_PauseAudioDevice(m_device, 0);
		}
	});
}

void Mixer::cmd_resume()
{
	m_cmd_queue.push([this] () {
		m_enabled.store(true);
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
