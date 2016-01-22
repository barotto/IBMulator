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
#include "filesys.h"
#include "mixer.h"
#include "program.h"
#include "machine.h"
#include "gui/gui.h"
#include "utils.h"
#include "audio/wav.h"
#include <SDL2/SDL.h>
#include <cmath>

Mixer g_mixer;


Mixer::Mixer()
:
m_heartbeat(MIXER_HEARTBEAT),
m_device(0),
m_audio_capture(false),
m_global_volume(1.f)
{
	m_out_buffer.set_size(MIXER_BUFSIZE);
	memset(&m_device_spec, 0, sizeof(SDL_AudioSpec));
	//sane defaults used to initialise the channels before the audio device
	m_device_spec.freq = 44100;
	m_device_spec.channels = 1;
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
		PDEBUGF(LOG_V2, LOG_MIXER, "buffer underrun\n");
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
	if(!MULTITHREADED) {
		m_heartbeat = g_program.get_beat_time_usec();
	}
	PINFOF(LOG_V1, LOG_MIXER, "Mixer beat period: %u usec\n", m_heartbeat);

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

	config_changed();
	m_paused = false;
}

void Mixer::start_capture()
{
	std::string path = g_program.config().get_file(PROGRAM_SECTION, PROGRAM_CAPTURE_DIR, FILE_TYPE_USER);
	path = FileSys::get_next_filename(path, "sound_", ".wav");
	if(!path.empty()) {
		try {
			m_wav.open_write(path.c_str(), m_device_spec.freq, SDL_AUDIO_BITSIZE(m_device_spec.format), m_device_spec.channels);
			std::string mex = "started audio recording to " + path;
			PINFOF(LOG_V0, LOG_MIXER, "%s\n", mex.c_str());
			g_gui.show_message(mex.c_str());
		} catch(std::exception &e) { }
	}
	for(auto ch : m_mix_channels) {
		ch.second->on_capture(true);
	}
	m_audio_capture = true;
}

void Mixer::stop_capture()
{
	m_wav.close();
	m_audio_capture = false;
	for(auto ch : m_mix_channels) {
		ch.second->on_capture(false);
	}
	PINFOF(LOG_V0, LOG_MIXER, "audio recording stopped\n");
	g_gui.show_message("audio recording stopped");
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
	int samples = g_program.config().get_int(MIXER_SECTION, MIXER_SAMPLES);
	m_prebuffer = g_program.config().get_int(MIXER_SECTION, MIXER_PREBUFFER); //msecs
	int buf_len = std::max(m_prebuffer*2, 1000); //msecs
	int buf_frames = (frequency * buf_len) / 1000;
	m_bytes_per_frame = 0;

	try {
		start_wave_playback(frequency, MIXER_BIT_DEPTH, MIXER_CHANNELS, samples);
		m_bytes_per_frame = m_device_spec.channels * (SDL_AUDIO_BITSIZE(m_device_spec.format) / 8);
		m_out_buffer.set_size(buf_frames * m_bytes_per_frame);
		m_mix_buffer.resize(buf_frames * m_device_spec.channels);
		for(auto ch : m_mix_channels) {
			ch.second->set_output_spec({AUDIO_FORMAT_F32,
				unsigned(m_device_spec.channels),unsigned(m_device_spec.freq)});
		}
	} catch(std::exception &e) {
		PERRF(LOG_MIXER, "wave audio output disabled\n");
	}

	if(capture) {
		start_capture();
	}

	PDEBUGF(LOG_V1, LOG_MIXER, "prebuffer: %d msec., ring buffer: %d bytes\n",
			m_prebuffer, buf_frames * m_bytes_per_frame);
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
	std::vector<MixerChannel*> active_channels;
	unsigned time_slice;

	#if MULTITHREADED
	while(true) {
		time_slice = m_main_chrono.elapsed_usec();
		if(time_slice < m_heartbeat) {
			uint64_t sleep = m_heartbeat - time_slice;
			uint64_t t0 = m_main_chrono.get_usec();
			std::this_thread::sleep_for( std::chrono::microseconds(sleep + m_next_beat_diff) );
			m_main_chrono.start();
			uint64_t t1 = m_main_chrono.get_usec();
			int time_slept = (t1 - t0);
			time_slice += time_slept;
			m_next_beat_diff = (sleep+m_next_beat_diff) - time_slept;
		} else {
			m_main_chrono.start();
		}
	#else
		time_slice = m_main_chrono.elapsed_usec();
		m_main_chrono.start();
	#endif

		m_bench.beat_start();

		Mixer_fun_t fn;
		while(m_cmd_queue.try_and_pop(fn)) {
			fn();
		}

		if(m_quit) {
			return;
		}

		if(m_paused) {
			if(m_device && SDL_GetAudioDeviceStatus(m_device)==SDL_AUDIO_PLAYING) {
				SDL_PauseAudioDevice(m_device, 0);
				//TODO should we purge all the channels buffers?
			}
#if MULTITHREADED
			continue;
#else
			return;
#endif
		} else if(!is_enabled()) {
			for(auto ch : m_mix_channels) {
				if(ch.second->is_enabled()) {
					ch.second->update(time_slice, false);
				}
			}
#if MULTITHREADED
			continue;
#else
			return;
#endif
		}

		active_channels.clear();
		SDL_AudioStatus audio_status = SDL_GetAudioDeviceStatus(m_device);

		//update the registered channels
		for(auto ch : m_mix_channels) {
			if(ch.second->is_enabled()) {
				ch.second->update(time_slice, audio_status == SDL_AUDIO_PAUSED);
				if(ch.second->is_enabled() || ch.second->out_frames_count()>0) {
					active_channels.push_back(ch.second.get());
				}
			} else {
				/* On the previous iteration the channel could have been disabled
				 * but its input buffer could have some samples left to process
				 */
				if(ch.second->in_frames_count()) {
					ch.second->input_finish();
					active_channels.push_back(ch.second.get());
				}
			}
		}

		if(!active_channels.empty()) {
			size_t mix_size = mix_channels(active_channels);
			if(mix_size>0) {
				send_packet(&m_mix_buffer[0], mix_size);
			}
			if(audio_status == SDL_AUDIO_PAUSED) {
				int elapsed = m_main_chrono.get_msec() - m_start;
				if(m_start == 0) {
					m_start = m_main_chrono.get_msec();
					PDEBUGF(LOG_V1, LOG_MIXER, "prebuffering %d msecs\n", m_prebuffer);
				} else if(elapsed+(MIXER_HEARTBEAT/1000) > m_prebuffer) {
					SDL_PauseAudioDevice(m_device, 0);
					PDEBUGF(LOG_V1, LOG_MIXER, "playing (%d msecs elapsed, %d bytes/%d usecs of data)\n",
							elapsed, m_out_buffer.get_read_avail(), get_buffer_len());
					m_start = 0;
				}
			} else {
				assert(m_start==0);
			}
		} else {
			m_start = 0;
			if(audio_status==SDL_AUDIO_PLAYING && m_out_buffer.get_read_avail() == 0) {
				SDL_PauseAudioDevice(m_device, 1);
				PDEBUGF(LOG_V1, LOG_MIXER, "paused\n");
			} else if(audio_status==SDL_AUDIO_PAUSED && m_out_buffer.get_read_avail() != 0) {
				SDL_PauseAudioDevice(m_device, 0);
				PDEBUGF(LOG_V1, LOG_MIXER, "playing\n");
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

size_t Mixer::mix_channels(const std::vector<MixerChannel*> &_channels)
{
	size_t mixlen = std::numeric_limits<size_t>::max();
	int frames;
	for(auto ch : _channels) {
		frames = ch->out_frames_count();
		mixlen = std::min(mixlen, size_t(frames*m_device_spec.channels));
	}
	if(mixlen==0) {
		return 0;
	}
	mixlen = std::min(mixlen, m_mix_buffer.size());
	frames = mixlen / m_device_spec.channels;
	auto ch = _channels.begin();
	const float *chdata = &(*ch)->get_out_data();
	std::copy(chdata, chdata+mixlen, m_mix_buffer.begin());
	(*ch)->pop_out_frames(frames);
	ch++;
	for(; ch != _channels.end(); ch++) {
		chdata = &(*ch)->get_out_data();
		for(size_t i=0; i<mixlen; i++) {
			float v1 = chdata[i];
			float v2 = m_mix_buffer[i];
			m_mix_buffer[i] = v1 + v2;
		}
		(*ch)->pop_out_frames(frames);
	}

	return mixlen;
}

bool Mixer::send_packet(float *_data, size_t _len)
{
	if(m_device == 0) {
		return false;
	}
	bool ret = true;
	std::vector<int16_t> buf(_len);
	size_t bytes = _len;
	//convert from float
	switch(SDL_AUDIO_BITSIZE(m_device_spec.format)) {
		case 16:
			for(size_t i=0; i<_len; i++) {
				int32_t ival = (_data[i]*m_global_volume) * 32768.f;
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

	SDL_LockAudioDevice(m_device);
	PDEBUGF(LOG_V2, LOG_MIXER, "buf write: %d frames, %d bytes, buf fullness: %d\n",
			_len / m_device_spec.channels, bytes, m_out_buffer.get_read_avail() + bytes);
	if(m_out_buffer.get_size()>=bytes && m_out_buffer.get_write_avail()<bytes) {
		PERRF(LOG_MIXER, "audio buffer overflow\n");
		m_out_buffer.clear();
	}
	if(m_out_buffer.write((uint8_t*)&buf[0], bytes) < bytes) {
		PERRF(LOG_MIXER, "audio packet too big\n");
		ret = false;
	}
	SDL_UnlockAudioDevice(m_device);

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
	ch->set_output_spec({AUDIO_FORMAT_F32, unsigned(m_device_spec.channels),
		unsigned(m_device_spec.freq)});
	return ch;
}

void Mixer::sig_config_changed()
{
#if MULTITHREADED
	//this signal should be preceded by a pause command
	m_cmd_queue.push([this] () {
		std::unique_lock<std::mutex> lock(g_program.ms_lock);
#endif
		config_changed();
#if MULTITHREADED
		g_program.ms_cv.notify_one();
	});
#endif
}

int Mixer::get_buffer_len() const
{
	double bytes = m_out_buffer.get_read_avail();
	double usec_per_frame = 1000000.0 / double(m_device_spec.freq);
	double frames_in_buffer = bytes / m_bytes_per_frame;
	int time_left = frames_in_buffer * usec_per_frame;
	return time_left;
}

void Mixer::cmd_pause()
{
#if MULTITHREADED
	m_cmd_queue.push([this] () {
#endif
		m_paused = true;
		if(m_device && SDL_GetAudioDeviceStatus(m_device)==SDL_AUDIO_PLAYING) {
			SDL_PauseAudioDevice(m_device, 0);
		}
#if MULTITHREADED
	});
#endif
}

void Mixer::cmd_resume()
{
#if MULTITHREADED
	m_cmd_queue.push([this] () {
#endif
		m_paused = false;
#if MULTITHREADED
	});
#endif
}

void Mixer::cmd_quit()
{
#if MULTITHREADED
	m_cmd_queue.push([this] () {
#endif
		m_quit = true;
		if(m_audio_capture) {
			stop_capture();
		}
		stop_wave_playback();
		SDL_AudioQuit();
#if MULTITHREADED
	});
#endif
}

void Mixer::cmd_start_capture()
{
#if MULTITHREADED
	m_cmd_queue.push([this] () {
#endif
		start_capture();
#if MULTITHREADED
	});
#endif
}

void Mixer::cmd_stop_capture()
{
#if MULTITHREADED
	m_cmd_queue.push([this] () {
#endif
		stop_capture();
#if MULTITHREADED
	});
#endif
}

void Mixer::cmd_toggle_capture()
{
#if MULTITHREADED
	m_cmd_queue.push([this] () {
#endif
		if(m_audio_capture) {
			stop_capture();
		} else {
			start_capture();
		}
#if MULTITHREADED
	});
#endif
}

void Mixer::cmd_set_global_volume(float _volume)
{
#if MULTITHREADED
	m_cmd_queue.push([=] () {
#endif
		m_global_volume = _volume;
#if MULTITHREADED
	});
#endif
}
