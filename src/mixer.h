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

#ifndef IBMULATOR_MIXER_H
#define IBMULATOR_MIXER_H

#include "shared_queue.h"
#include "chrono.h"
#include "hwbench.h"
#include "audio/ring_buffer.h"
#include "audio/mixerchannel.h"
#include "audio/wav.h"
#include <thread>
#include <cmath>
#include <SDL2/SDL.h>

class Machine;
extern Mixer g_mixer;

#define MIXER_WAVEPACKETSIZE  8192
#define MIXER_BUFSIZE  MIXER_WAVEPACKETSIZE * 8
#define MIXER_CHANNELS  1
#define MIXER_BIT_DEPTH 16
#define MIXER_MIN_RATE 8000
#define MIXER_MAX_RATE 49716
#define MIXER_TIME_TOLERANCE 1.45


typedef std::function<void()> Mixer_fun_t;

class Mixer
{
private:
	RingBuffer m_out_buffer;
	std::vector<float> m_mix_buffer;
	WAVFile m_wav;
	int m_start;

	int m_prebuffer;

	Machine *m_machine;
	Chrono m_main_chrono;
	HWBench m_bench;
	uint m_heartbeat;
	int64_t m_next_beat_diff;

	bool m_quit; //how about an std::atomic?
	SDL_AudioStatus m_audio_status;
	std::atomic<bool> m_paused;
	SDL_AudioDeviceID m_device;
	SDL_AudioSpec m_device_spec;
	int m_frame_size;

	shared_queue<Mixer_fun_t> m_cmd_queue;

	std::map<std::string, std::shared_ptr<MixerChannel>> m_mix_channels;

	bool m_audio_capture;
	float m_global_volume;
	std::array<float,3> m_channels_volume;

public:
	Mixer();
	~Mixer();

	void init(Machine *);
	void config_changed();
	void start();
	void main_loop();

	std::shared_ptr<MixerChannel> register_channel(MixerChannel_handler _callback,
			const std::string &_name);
	void unregister_channel(std::shared_ptr<MixerChannel> _channel);

	void calibrate(const Chrono &_c);
	unsigned heartbeat() const { return m_heartbeat; }
	inline HWBench & get_bench() { return m_bench; }
	inline size_t get_buffer_read_avail() const { return m_out_buffer.get_read_avail(); }
	inline SDL_AudioStatus get_audio_status() const { return SDL_GetAudioDeviceStatus(m_device); }
	int get_buffer_len() const;
	inline const SDL_AudioSpec & get_audio_spec() { return m_device_spec; }

	bool is_paused() const { return m_paused; }
	bool is_enabled() const { return (m_device!=0); }

	void sig_config_changed(std::mutex &_mutex, std::condition_variable &_cv);
	void cmd_quit();
	void cmd_pause();
	void cmd_resume();
	void cmd_start_capture();
	void cmd_stop_capture();
	void cmd_toggle_capture();
	void cmd_set_global_volume(float _volume);
	void cmd_set_category_volume(MixerChannelCategory _cat, float _volume);

private:
	void start_wave_playback(int _frequency, int _bits, int _channels, int _samples);
	void stop_wave_playback();
	size_t mix_channels(const std::vector<std::pair<MixerChannel*,bool>> &_channels, uint64_t _time_span_us);
	bool send_packet(size_t _len);
	void start_capture();
	void stop_capture();
	static void sdl_callback(void *userdata, Uint8 *stream, int len);
};

#endif
