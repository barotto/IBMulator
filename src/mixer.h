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

#include <SDL2/SDL.h>
#include "shared_queue.h"
#include "chrono.h"
#include "hwbench.h"
#include "ring_buffer.h"
#include "wav.h"
#include <thread>
#include <atomic>
#include <samplerate.h>

class Machine;
class Mixer;
extern Mixer g_mixer;

#define MIXER_WAVEPACKETSIZE  8192
#define MIXER_BUFSIZE  MIXER_WAVEPACKETSIZE * 8
#define MIXER_CHANNELS  1
#define MIXER_BIT_DEPTH 16

#define MIXER_FORMAT_U8  AUDIO_U8
#define MIXER_FORMAT_S16 AUDIO_S16

typedef std::function<void()> Mixer_fun_t;
typedef std::function<int(
		int _mix_time_slice,
		bool _prebuffering,
		bool _first_update
	)> MixerChannel_handler;

class MixerChannel
{
	Mixer *m_mixer;
	//enabling/disabling can be performed by the machine thread
	std::atomic<bool> m_enabled;
	std::string m_name;
	MixerChannel_handler m_update_clbk;
	std::atomic<uint64_t> m_disable_time;
	uint64_t m_disable_timeout;
	bool m_first_update;
	std::vector<uint8_t> m_in_buffer;
	std::vector<float> m_out_buffer;
	int m_out_frames;
	SRC_STATE *m_SRC_state;
	std::function<void(bool)> m_capture_clbk;

public:

	MixerChannel(Mixer *_mixer, MixerChannel_handler _callback, const std::string &_name);
	~MixerChannel();

	void enable(bool _enabled);
	inline bool is_enabled() { return m_enabled.load(); }

	void add_samples(uint8_t *_data, size_t _len);
	template<typename T>
	int fill_samples(int _duration_us, int _rate, T _value);
	template<typename T>
	int fill_samples(int _samples, T _value);
	int fill_samples_fade_u8m(int _samples, uint8_t _start, uint8_t _end);
	void mix_samples(int _rate, uint16_t _format, uint16_t _channels);
	inline const std::vector<float> & get_out_buffer() { return m_out_buffer; }
	inline int get_out_frames() { return m_out_frames; }
	void pop_frames(int _count);

	int update(int _mix_tslice, bool _prebuffering);
	void set_disable_time(uint64_t _time) { m_disable_time.store(_time); }
	bool check_disable_time(uint64_t _now_us);
	void set_disable_timeout(uint64_t _timeout_us) { m_disable_timeout = _timeout_us; }

	void register_capture_clbk(std::function<void(bool _enable)> _fn);
	void on_capture(bool _enable);
};

template<typename T>
int MixerChannel::fill_samples(int _duration_us, int _rate, T _value)
{
	int samples = double(_duration_us) * double(_rate)/1e6;
	std::vector<T> buf(samples);
	std::fill(buf.begin(), buf.begin()+samples, _value);
	add_samples(&buf[0], samples*sizeof(T));
	return samples;
}

template<typename T>
int MixerChannel::fill_samples(int _samples, T _value)
{
	std::vector<T> buf(_samples);
	std::fill(buf.begin(), buf.begin()+_samples, _value);
	add_samples(&buf[0], _samples*sizeof(T));
	return _samples;
}

class Mixer
{
private:

	RingBuffer m_out_buffer;
	std::vector<float> m_mix_buffer;
	WAVFile m_wav;
	int m_start;

	int m_frequency;
	int m_prebuffer;

	Machine *m_machine;
	Chrono m_main_chrono;
	HWBench m_bench;
	uint m_heartbeat;
	int64_t m_next_beat_diff;

	bool m_quit; //how about an std::atomic?
	std::atomic<bool> m_enabled;
	SDL_AudioDeviceID m_device;
	SDL_AudioSpec m_device_spec;
	int m_bytes_per_frame;


	shared_queue<Mixer_fun_t> m_cmd_queue;

	std::map<std::string, std::shared_ptr<MixerChannel>> m_mix_channels;

	bool m_audio_capture;

	void config_changed();
	void start_wave_playback(int _frequency, int _bits, int _channels, int _samples);
	void stop_wave_playback();
	size_t mix_channels(const std::vector<MixerChannel*> &_channels);
	bool send_packet(float *_data, size_t _len);

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
			const std::string &_name);
	void unregister_channel(const std::string &_name);

	void calibrate(const Chrono &_c);
	inline HWBench & get_bench() { return m_bench; }
	inline size_t get_buffer_read_avail() const { return m_out_buffer.get_read_avail(); }
	inline SDL_AudioStatus get_audio_status() const { return SDL_GetAudioDeviceStatus(m_device); }
	int get_buffer_len() const;
	inline const SDL_AudioSpec & get_audio_spec() { return m_device_spec; }

	bool is_paused() const { return !m_enabled.load(); }

	void sig_config_changed();
	void cmd_quit();
	void cmd_pause();
	void cmd_resume();
	void cmd_start_capture();
	void cmd_stop_capture();
	void cmd_toggle_capture();

	static int us_to_samples(int _us, int _rate) {
		return round(double(_us) * double(_rate)/1e6);
	}
};

#endif
