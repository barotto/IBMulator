/*
 * Copyright (C) 2015-2024  Marco Bortolin
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
#include "ring_buffer.h"
#include "audio/mixerchannel.h"
#include "audio/wav.h"
#include "audio/midi.h"
#include <thread>
#include <cmath>
#include <SDL.h>

class Machine;
extern Mixer g_mixer;


#define MIXER_FREQUENCY 48000
#define MIXER_CHANNELS  2
#define MIXER_FORMAT    AUDIO_S16
#define MIXER_MIN_RATE  8000
#define MIXER_MAX_RATE  49716
#define MIXER_MAX_VOLUME 1.5f
#define MIXER_MAX_VOLUME_STR "150"

typedef std::function<void()> Mixer_fun_t;
typedef std::function<void(const std::vector<int16_t> &_data, int _category)> AudioSinkHandler;

class Mixer
{
private:
	RingBuffer m_out_buffer; // the SDL multithread-safe output buffer
	std::vector<float> m_out_mix;
	std::vector<float> m_ch_mix[MixerChannel::CategoryCount];
	size_t m_mix_bufsize_fr;
	size_t m_mix_bufsize_sa;
	size_t m_mix_bufsize_by;
	WAVFile m_wav;
	int m_start_time;
	uint64_t m_prev_vtime = 0;
	bool m_audiocards_enabled = false;
	bool m_audiocards_capture = false;

	struct {
		uint64_t main_us = 0;
		size_t   main_fr = 0;
		uint64_t ch_us = 0;
		size_t   ch_fr = 0;
	} m_prebuffer;

	Machine *m_machine;
	Pacer m_pacer;
	HWBench m_bench;
	uint64_t m_heartbeat_us;
	uint64_t m_elapsed_time_us;

	bool m_quit; //how about an std::atomic?
	SDL_AudioStatus m_audio_status;
	std::atomic<bool> m_paused;
	SDL_AudioDeviceID m_device;
	SDL_AudioSpec m_audio_spec;
	int m_frame_size;

	shared_queue<Mixer_fun_t> m_cmd_queue;

	std::map<std::string, std::shared_ptr<MixerChannel>> m_mix_channels;

	struct Volume {
		std::atomic<float> master = 1.f;
		std::atomic<float> category[MixerChannel::CategoryCount] = { 1.f, 1.f, 1.f };
		std::atomic<bool> muted = false;
		std::atomic<bool> muted_category[MixerChannel::CategoryCount] = { false, false, false };
		MixerChannel::VUMeter meter;
		MixerChannel::VUMeter meter_category[MixerChannel::CategoryCount];
	} m_volume;

	struct Reverb {
		MixerChannel::ReverbParams params = { MixerChannel::ReverbPreset::None, -1 };
		bool enabled = false;
	} m_reverb[MixerChannel::CategoryCount];

	std::array<AudioSinkHandler,2> m_sinks;
	std::mutex m_sinks_mutex;
	int m_capture_sink;
	
	std::shared_ptr<MixerChannel> m_silence_channel;
	
	std::unique_ptr<MIDI> m_midi;
	std::thread m_midi_thread;
	
public:
	Mixer();
	~Mixer();

	void init(Machine *);
	void config_changed(bool _launch = false) noexcept;
	void start();
	void shutdown();

	std::shared_ptr<MixerChannel> register_channel(MixerChannelHandler _callback,
			const std::string &_name, MixerChannel::Category, MixerChannel::AudioType);
	void unregister_channel(std::shared_ptr<MixerChannel> _channel);

	const SDL_AudioSpec & audio_spec() const { return m_audio_spec; }

	int register_sink(AudioSinkHandler _sink);
	void unregister_sink(int _id);
	
	void calibrate(const Pacer &_c);
	unsigned heartbeat_us() const { return m_heartbeat_us; }
	uint64_t elapsed_time_us() const { return m_elapsed_time_us; }
	inline HWBench & get_bench() { return m_bench; }
	inline size_t get_buffer_read_avail() const { return m_out_buffer.get_read_avail(); }
	inline SDL_AudioStatus get_audio_status() const { return SDL_GetAudioDeviceStatus(m_device); }
	uint64_t get_buffer_read_avail_us() const;
	size_t get_buffer_read_avail_fr() const;
	inline const SDL_AudioSpec & get_audio_spec() { return m_audio_spec; }

	size_t ch_prebuffer_fr() const { return m_prebuffer.ch_fr; }

	bool is_paused() const { return m_paused; }
	bool is_recording() const { return m_audiocards_capture; }

	std::shared_ptr<MixerChannel> get_channel(const char *_name);
	std::vector<std::shared_ptr<MixerChannel>> get_channels();
	std::vector<std::shared_ptr<MixerChannel>> get_channels(MixerChannel::Category _cat);

	MIDI * midi() { return m_midi.get(); }

	float volume_master() const { return m_volume.master; }
	float volume_cat(MixerChannel::Category _cat) const { return m_volume.category[_cat]; }
	void set_volume_master(float _level);
	void set_volume_cat(MixerChannel::Category _cat, float _level);
	void set_muted(bool _muted) { m_volume.muted = _muted; }
	void set_muted_cat(MixerChannel::Category _cat, bool _muted) { m_volume.muted_category[_cat] = _muted; }
	bool is_muted() const { return m_volume.muted; }
	const MixerChannel::VUMeter & vu_meter() const { return m_volume.meter; }
	const MixerChannel::VUMeter & vu_meter_cat(MixerChannel::Category _cat) const { return m_volume.meter_category[_cat]; }

	const MixerChannel::ReverbParams & reverb(MixerChannel::Category _cat) const { return m_reverb[_cat].params; }
	float reverb_gain(MixerChannel::Category _cat) const { return m_reverb[_cat].params.gain; }
	void set_reverb(MixerChannel::Category _cat, std::string _preset);
	void set_reverb(MixerChannel::Category _cat, const MixerChannel::ReverbParams &);
	void set_reverb_gain(MixerChannel::Category _cat, float _gain);
	bool is_reverb_enabled(MixerChannel::Category _cat) const { return m_reverb[_cat].enabled; }
	void enable_reverb(MixerChannel::Category _cat, bool _enable);

	void load_profile(const std::string &_path);
	void save_profile(const std::string &_path);

	void sig_config_changed(std::mutex &_mutex, std::condition_variable &_cv);
	void cmd_quit();
	void cmd_pause();
	void cmd_pause_and_signal(std::mutex &_mutex, std::condition_variable &_cv);
	void cmd_resume();
	void cmd_stop_audiocards_and_signal(std::mutex &_mutex, std::condition_variable &_cv);
	void cmd_start_audiocards();
	void cmd_save_state(StateBuf &_state, std::mutex &_mutex, std::condition_variable &_cv);
	void cmd_restore_state(StateBuf &_state, std::mutex &_mutex, std::condition_variable &_cv);
	void cmd_start_capture();
	void cmd_stop_capture();
	void cmd_toggle_capture();

private:
	void main_loop();
	void pause();
	void open_audio_device(int _frequency, SDL_AudioFormat _format, int _channels, int _samples);
	void close_audio_device();
	void mix_channels(uint64_t _time_span_ns, const std::vector<MixerChannel*> &_channels, double _vtime_ratio);
	void mix_stereo(std::vector<float> &_buf, const std::vector<MixerChannel*> &_channels, int _chcat, size_t _frames);
	void limit_audio_data(const std::vector<MixerChannel*> &_channels, double _audio_factor);
	void send_to_sinks(const std::vector<int16_t> &_data, int _category);
	void start_capture();
	void stop_capture();
	void audio_sink(const std::vector<int16_t> &_data, int _category);
	static void sdl_callback(void *userdata, Uint8 *stream, int len);
	void create_silence_samples(uint64_t _time_span_us, bool _first_upd);
	void stop_midi();
};


#endif
