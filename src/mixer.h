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

#ifndef IBMULATOR_MIXER_H
#define IBMULATOR_MIXER_H

#include "SDL.h"
#include "shared_queue.h"
#include "chrono.h"
#include "hwbench.h"
#include "ring_buffer.h"
#include "wav.h"
#include <thread>
#include <atomic>

class Machine;
class Mixer;
extern Mixer g_mixer;

#define MIXER_WAVEPACKETSIZE  8192
#define MIXER_BUFSIZE  MIXER_WAVEPACKETSIZE * 8

//#define MIXER_BUFSIZE (16*1024)

typedef std::function<void()> Mixer_fun_t;
typedef std::function<void(uint64_t time)> MixerChannel_handler;

class MixerChannel
{
	Mixer *m_mixer;
	//enabling/disabling can be performed by the machine thread
	std::atomic<bool> m_enabled;
	uint16_t m_rate;
	std::string m_name;
	MixerChannel_handler m_callback;

public:

	MixerChannel(MixerChannel_handler _callback, uint16_t _rate, const std::string &_name);

	inline void enable(bool _enabled) { m_enabled = _enabled; }
	inline bool is_enabled() { return m_enabled; }

	void add_samples(uint8_t *_data, size_t _len);

	void update(uint64_t _time);
};

class Mixer
{
private:

	RingBuffer m_buffer;
	WAVFile m_wav;
	uint64_t m_start;

	int m_frequency;
	int m_bit_depth;
	int m_channels;
	int m_prebuffer;
	int m_bytes_per_sample;

	Machine *m_machine;
	Chrono m_main_chrono;
	HWBench m_bench;
	uint m_heartbeat;
	int64_t m_next_beat_diff;

	bool m_quit; //how about an std::atomic?
	std::atomic<bool> m_enabled;
	SDL_AudioDeviceID m_device;
	SDL_AudioSpec m_device_spec;


	shared_queue<Mixer_fun_t> m_cmd_queue;

	std::map<std::string, std::shared_ptr<MixerChannel>> m_mix_channels;

	bool m_audio_capture;

	void config_changed();
	void start_wave_playback(int _frequency, int _bits, int _channels, int _samples);
	void stop_wave_playback();

	void start_capture();
	void stop_capture();

	static void sdl_callback(void *userdata, Uint8 *stream, int len);

public:
	Mixer();
	~Mixer();

	void init(Machine *);
	void start();
	void main_loop();

	std::shared_ptr<MixerChannel> register_channel(MixerChannel_handler _callback,
			uint16_t _rate, const std::string &_name);
	void unregister_channel(const std::string &_name);


	void calibrate(const Chrono &_c);
	inline HWBench & get_bench() { return m_bench; }
	inline size_t get_buffer_read_avail() const { return m_buffer.get_read_avail(); }
	inline SDL_AudioStatus get_audio_status() const { return SDL_GetAudioDeviceStatus(m_device); }
	inline uint64_t get_buffer_len() const {
		size_t bytes = m_buffer.get_read_avail();
		size_t bytes_per_sample = 1;
		uint64_t usec_per_sample = 1000000/m_frequency;
		size_t samples_in_buffer = bytes/bytes_per_sample;
		uint64_t time_left = samples_in_buffer * usec_per_sample;
		return time_left;
	}

	bool send_wave_packet(uint8_t *_data, size_t _len);
	bool is_paused() const { return !m_enabled.load(); }

	void sig_config_changed();
	void cmd_quit();
	void cmd_pause();
	void cmd_resume();
	void cmd_start_capture();
	void cmd_stop_capture();
	void cmd_toggle_capture();
};

#endif
