/*
 * Copyright (C) 2023  Marco Bortolin
 *
 * This file is part of IBMulator
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
#include "program.h"
#include "machine.h"
#include "serialmodemfx.h"
#include "hardware/devices/pcspeaker.h"
#include <cfloat>


#define MODEM_SAMPLES_DIR "sounds/modem/"
#define MODEM_SAMPLES_DTMF_DIR MODEM_SAMPLES_DIR"dtmf/"
#define MODEM_SAMPLES_HANDSHAKE_DIR MODEM_SAMPLES_DIR"handshake/"

std::vector<AudioBuffer> SerialModemFX::ms_tones;
std::vector<AudioBuffer> SerialModemFX::ms_dtmf;

void SerialModemFX::install(unsigned _baud_rate)
{
	AudioSpec spec({AUDIO_FORMAT_F32, 1, 48000});

	if(!m_channel) {
		using namespace std::placeholders;
		m_channel = g_mixer.register_channel(std::bind(&SerialModemFX::create_samples, this, _1, _2, _3),
				"Serial Modem", MixerChannel::Category::SOUNDFX, MixerChannel::AudioType::NOISE);
		m_channel->set_in_spec(spec);
		m_channel->set_features(MixerChannel::HasVolume | MixerChannel::HasBalance);
		m_channel->add_autoval_cb(MixerChannel::ConfigParameter::Filter, std::bind(&SerialModemFX::auto_filters_cb, this));
		m_channel->register_config_map({
			{ MixerChannel::ConfigParameter::Volume, { SOUNDFX_SECTION, SOUNDFX_MODEM }},
			{ MixerChannel::ConfigParameter::Balance, { SOUNDFX_SECTION, SOUNDFX_MODEM_BALANCE }},
			{ MixerChannel::ConfigParameter::Filter, { SOUNDFX_SECTION, SOUNDFX_MODEM_FILTERS }}
		});
	}

	if(ms_tones.empty()) {
		// if country dir doesn't exist buffers will be empty, no exceptions thrown
		std::string path(MODEM_SAMPLES_DIR);
		path += g_program.config().get_string(SOUNDFX_SECTION, SOUNDFX_MODEM_COUNTRY, "us").substr(0,2);
		path += "/";
		SoundFX::samples_t tones;
		// keep same order as enum SampleType
		tones.push_back({"dial tone", path + "dial.wav"});
		tones.push_back({"busy tone", path + "busy.wav"});
		tones.push_back({"reorder tone", path + "reorder.wav"});
		tones.push_back({"ringing tone", path + "ringing.wav"});
		tones.push_back({"disconnect tone", path + "disconnect.wav"});
		tones.push_back({"incoming", MODEM_SAMPLES_DIR "incoming.wav"});

		auto htype = g_program.config().get_string(MODEM_SECTION, MODEM_HANDSHAKE);
		if(htype != "no") {
			std::string hdir = MODEM_SAMPLES_HANDSHAKE_DIR;
			if(htype != "full") {
				hdir += "short/";
			}
			if(_baud_rate > 56000) {
				_baud_rate = 56000;
			}
			tones.push_back({str_format("handshake %u", _baud_rate), str_format("%s%u.wav", hdir.c_str(), _baud_rate)});
		}

		ms_tones = SoundFX::load_samples(spec, tones);
		for(auto &buffer : ms_tones) {
			if(buffer.frames() == 0) {
				ms_tones.clear();
				remove();
				PERRF(LOG_MIXER, "MODEM: error loading the audio samples\n");
				throw std::runtime_error("invalid audio samples");
			}
		}
	}
	if(ms_dtmf.empty()) {
		SoundFX::samples_t dtmf;
		dtmf.push_back({"dtmf #", str_format("%spound.wav", MODEM_SAMPLES_DTMF_DIR)});
		dtmf.push_back({"dtmf *", str_format("%sstar.wav", MODEM_SAMPLES_DTMF_DIR)});
		for(char c='0'; c<='9'; c++) {
			dtmf.push_back({str_format("dtmf %c",c), str_format("%s%c.wav", MODEM_SAMPLES_DTMF_DIR, c)});
		};
		for(char c='a'; c<='d'; c++) {
			dtmf.push_back({str_format("dtmf %c",c), str_format("%s%c.wav", MODEM_SAMPLES_DTMF_DIR, c)});
		}
		ms_dtmf = SoundFX::load_samples(spec, dtmf);
		for(auto &buffer : ms_dtmf) {
			if(buffer.frames() == 0) {
				ms_dtmf.clear();
				remove();
				PERRF(LOG_MIXER, "MODEM: error loading the audio samples\n");
				throw std::runtime_error("invalid audio samples");
			}
		}
	}
}

void SerialModemFX::auto_filters_cb()
{
	if(m_channel->is_filter_auto()) {
		m_channel->set_filter("LowPass,order=5,fc=3000|HighPass,order=5,fc=600");
	}
}

void SerialModemFX::remove()
{
	g_mixer.unregister_channel(m_channel);
	m_channel.reset();
}

uint64_t SerialModemFX::dial(const std::string _str, int _ringing_ms)
{
	assert(m_channel);
	silence();

	uint64_t tm = g_machine.get_virt_time_us();
	m_events.push({tm, DIAL_TONE, MODEM_DIAL_TONE_US});
	tm += round(MODEM_DIAL_TONE_US);
	const size_t max_tones = 10;
	for(size_t i=0; i<_str.length() && i<max_tones; i++) {
		char c = tolower(_str[i]);
		switch(c) {
			case '0': case '1': case '2': case '3':case '4':
			case '5': case '6': case '7': case '8': case '9':
				c = DTMF_0 + (c - '0');
				break;
			case '*':
				c = DTMF_STAR;
				break;
			case '#':
				c = DTMF_POUND;
				break;
			case 'a': case 'b': case 'c': case 'd':
				c = DTMF_A + (c - 'a');
				break;
			default:
				 // limit to numbers
				c = DTMF_0 + c % 10;
				break;
		}
		c += ' ';
		m_events.push({tm, c, MODEM_DTMF_US});
		tm += round(MODEM_DTMF_US);
	}
	m_events.push({tm, NO_TONE, MODEM_NO_TONE_US});
	tm += round(MODEM_NO_TONE_US);
	uint64_t call_time = tm;

	double ringing_us = ms_tones[RINGING_TONE].duration_us();
	while(_ringing_ms > 0) {
		m_events.push({tm, RINGING_TONE, ringing_us});
		tm += round(ringing_us);
		_ringing_ms -= round(ringing_us / 1000.0);
	}

	if(m_enabled) {
		m_channel->enable(true);
	} else {
		m_events.clear();
	}

	return call_time * 1_us;
}

uint64_t SerialModemFX::enqueue(ToneType _tone, double _duration, int _repeats)
{
	assert(m_channel);
	silence();

	uint64_t tm = g_machine.get_virt_time_us();
	double duration = _duration;
	if(duration == .0) {
		duration = ms_tones[_tone].duration_us();
	}
	if(duration == .0) {
		return 0;
	}
	for(int i=0; i<_repeats; i++) {
		m_events.push({tm, _tone, duration});
		tm += duration;
	}

	if(m_enabled) {
		m_channel->enable(true);
	} else {
		m_events.clear();
	}

	return duration * 1_us * _repeats;
}

uint64_t SerialModemFX::busy()
{
	return enqueue(BUSY_TONE, 0, MODEM_RESULT_TONE_REPEATS);
}

uint64_t SerialModemFX::disconnect()
{
	return enqueue(DISCONNECT_TONE, 0, MODEM_RESULT_TONE_REPEATS);
}

uint64_t SerialModemFX::reorder()
{
	return enqueue(REORDER_TONE, 0, MODEM_RESULT_TONE_REPEATS);
}

uint64_t SerialModemFX::incoming()
{
	return enqueue(INCOMING_RING, MODEM_RINGINTERVAL_US, MODEM_RINGING_MAX);
}

uint64_t SerialModemFX::handshake()
{
	return enqueue(HANDSHAKE, 0, 1);
}

void SerialModemFX::set_volume(int level)
{
	float modem = g_program.config().get_real_or_default(SOUNDFX_SECTION, SOUNDFX_MODEM);

	float volume = 1.0;
	if(level == 0) volume = .0f;
	if(level == 1) volume = .30f;
	if(level == 2) volume = .60f;

	m_channel->set_volume_master(modem * volume);
}

void SerialModemFX::enable(bool _enabled)
{
	m_enabled = _enabled;
	if(!m_enabled) {
		silence();
	}
}

void SerialModemFX::silence()
{
	assert(m_channel);
	m_events.clear();
}

bool SerialModemFX::create_samples(uint64_t _time_span_ns, bool /*_prebuf*/, bool _first_upd)
{
	// This function is called by the Mixer thread

	if(_first_upd) {
		m_channel->flush();
	}

	return SoundFX::play_timed_events<ModemSound, shared_deque<ModemSound>>(
		_time_span_ns, _first_upd,
		*m_channel, m_events,
		[this](ModemSound &_evt, uint64_t _time_pos) {
			unsigned frames = round(m_channel->in_spec().us_to_frames(_evt.duration));
			if(_evt.code == NO_TONE) {
				m_channel->play_silence(frames, _time_pos);
			} else if(_evt.code < ' ') {
				m_channel->play_frames(ms_tones[_evt.code], frames, _time_pos);
			} else {
				m_channel->play_frames(ms_dtmf[_evt.code - ' '], frames, _time_pos);
			}
		});
}