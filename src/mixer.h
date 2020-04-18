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

#ifndef IBMULATOR_MIXER_H
#define IBMULATOR_MIXER_H

#include "shared_queue.h"
#include "pacer.h"
#include "hwbench.h"
#include "audio/ring_buffer.h"
#include "audio/mixerchannel.h"
#include "audio/wav.h"
#include <thread>
#include <cmath>
#include <SDL.h>

class Machine;
extern Mixer g_mixer;


#define MIXER_FREQUENCY 48000
#define MIXER_CHANNELS  1
#define MIXER_FORMAT    AUDIO_S16
#define MIXER_MIN_RATE  8000
#define MIXER_MAX_RATE  49716


typedef std::function<void()> Mixer_fun_t;
typedef std::function<void(const std::vector<int16_t> &_data, int _category)> AudioSinkHandler;

class Mixer
{
private:
	RingBuffer m_out_buffer; // the SDL multithread-safe output buffer
	std::vector<float> m_out_mix;
	std::vector<float> m_ch_mix[ec_to_i(MixerChannelCategory::MAX)];
	size_t m_mix_bufsize;
	WAVFile m_wav;
	int m_start_time;

	uint64_t m_prebuffer_us;

	Machine *m_machine;
	Pacer m_pacer;
	HWBench m_bench;
	uint64_t m_heartbeat_us;

	bool m_quit; //how about an std::atomic?
	SDL_AudioStatus m_audio_status;
	std::atomic<bool> m_paused;
	SDL_AudioDeviceID m_device;
	SDL_AudioSpec m_audio_spec;
	int m_frame_size;

	shared_queue<Mixer_fun_t> m_cmd_queue;

	std::map<std::string, std::shared_ptr<MixerChannel>> m_mix_channels;

	bool m_audio_capture;
	float m_global_volume;
	std::array<float,3> m_channels_volume;

	std::array<AudioSinkHandler,2> m_sinks;
	std::mutex m_sinks_mutex;
	int m_capture_sink;
	
	std::shared_ptr<MixerChannel> m_silence_channel;
	
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

	int register_sink(AudioSinkHandler _sink);
	void unregister_sink(int _id);
	
	void calibrate(const Pacer &_c);
	unsigned heartbeat_us() const { return m_heartbeat_us; }
	inline HWBench & get_bench() { return m_bench; }
	inline size_t get_buffer_read_avail() const { return m_out_buffer.get_read_avail(); }
	inline SDL_AudioStatus get_audio_status() const { return SDL_GetAudioDeviceStatus(m_device); }
	uint64_t get_buffer_read_avail_us() const;
	inline const SDL_AudioSpec & get_audio_spec() { return m_audio_spec; }

	template <int Channels>
	static std::vector<std::shared_ptr<Dsp::Filter>> create_filters(double _rate, std::string _filters_def);
	
	bool is_paused() const { return m_paused; }

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
	void open_audio_device(int _frequency, SDL_AudioFormat _format, int _channels, int _samples);
	void close_audio_device();
	size_t mix_channels(const std::vector<std::pair<MixerChannel*,bool>> &_channels, uint64_t _time_span_us);
	void mix_channels(std::vector<float> &_buf, const std::vector<std::pair<MixerChannel*,bool>> &_channels, int _chcat, size_t _mixlen);
	void send_packet(size_t _len);
	void send_to_sinks(const std::vector<int16_t> &_data, int _category);
	void start_capture();
	void stop_capture();
	void audio_sink(const std::vector<int16_t> &_data, int _category);
	static void sdl_callback(void *userdata, Uint8 *stream, int len);
	bool create_silence_samples(uint64_t _time_span_us, bool, bool);
};


extern template std::vector<std::shared_ptr<Dsp::Filter>> Mixer::create_filters<1>(double _rate, std::string _filters_def);
extern template std::vector<std::shared_ptr<Dsp::Filter>> Mixer::create_filters<2>(double _rate, std::string _filters_def);

#endif
