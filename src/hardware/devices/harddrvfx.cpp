/*
 * Copyright (C) 2015, 2016  Marco Bortolin
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
#include "harddrvfx.h"

#define SAMPLE_SEEK      "sounds" FS_SEP "hdd" FS_SEP "drive_seek.wav"
#define SAMPLE_SPIN      "sounds" FS_SEP "hdd" FS_SEP "drive_spin.wav"
#define SAMPLE_SPIN_UP   "sounds" FS_SEP "hdd" FS_SEP "drive_spin_up.wav"
#define SAMPLE_SPIN_DOWN "sounds" FS_SEP "hdd" FS_SEP "drive_spin_down.wav"
#define CHANNELS_TIMEOUT 1000000

HardDriveFX::HardDriveFX()
{
}

HardDriveFX::~HardDriveFX()
{
}

void HardDriveFX::init()
{
	/* mixer channels operate in float format, but rate and channels count depend
	 * on the current state of the mixer. can't anticipate.
	 */
	AudioSpec spec({AUDIO_FORMAT_F32, 1, 44100});

	/* SPIN fx
	 */
	m_channels.spin = g_mixer.register_channel(
		std::bind(&HardDriveFX::create_spin_samples, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
		"HDD-spin");
	m_channels.spin->set_disable_timeout(CHANNELS_TIMEOUT);
	m_channels.spin->set_in_spec(spec);

	PINFOF(LOG_V2, LOG_AUDIO, "loading " SAMPLE_SPIN " for HDD spin sound fx\n");
	load_wave(SAMPLE_SEEK, m_waves.spin, spec);
	PINFOF(LOG_V2, LOG_AUDIO, "loading " SAMPLE_SPIN_UP " for HDD spin-up sound fx\n");
	load_wave(SAMPLE_SEEK, m_waves.spin_up, spec);
	PINFOF(LOG_V2, LOG_AUDIO, "loading " SAMPLE_SPIN_DOWN " for HDD spin-down sound fx\n");
	load_wave(SAMPLE_SEEK, m_waves.spin_down, spec);

	/* SEEK fx
	 */
	m_channels.seek = g_mixer.register_channel(
		std::bind(&HardDriveFX::create_seek_samples, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
		"HDD-seek");
	m_channels.seek->set_disable_timeout(CHANNELS_TIMEOUT);
	m_channels.seek->set_in_spec(spec);

	PINFOF(LOG_V1, LOG_AUDIO, "loading " SAMPLE_SEEK " for HDD seek sound fx\n");
	load_wave(SAMPLE_SEEK, m_waves.seek, spec);
}

void HardDriveFX::load_wave(const char *_filename, AudioBuffer &_sample, const AudioSpec &_spec)
{
	try {
		std::string path = g_program.config().get_file_path(_filename, FILE_TYPE_ASSET);
		WAVFile wav;
		wav.open_read(path.c_str());
		_sample.load(wav);
		if(_spec != _sample.spec()) {
			PINFOF(LOG_V2, LOG_AUDIO, "converting from %s to %s\n",
					_sample.spec().to_string().c_str(),
					_spec.to_string().c_str());
			_sample.convert(_spec);
		}
	} catch(std::exception &e) {
		PERRF(LOG_AUDIO, "HardDrive FX: %s: %s\n", _filename, e.what());
	}
}

void HardDriveFX::seek(int _c0, int _c1)
{
	SeekEvent event;
	event.distance = _c1 - _c0;
	if(event.distance != 0) {
		event.time = g_machine.get_virt_time_us();
		m_seek_events.push(event);
		PDEBUGF(LOG_V1, LOG_AUDIO, "HDD-seek: dist:%d, time:%lld\n", event.distance, event.time);
		if(!m_channels.seek->is_enabled()) {
			m_channels.seek->enable(true);
		}
	}
}

void HardDriveFX::clear_events()
{
	std::lock_guard<std::mutex> clr_lock(m_clear_mutex);
	m_seek_events.clear();
}

//this method is called by the Mixer thread
void HardDriveFX::create_seek_samples(uint64_t _time_span_us, bool /*_prebuf*/, bool _first_upd)
{
	/* TODO this function can be generalised/templatised and used whenever there's
	 * the need to play pre-rendered samples for timed events.
	 */
	std::lock_guard<std::mutex> clr_lock(m_clear_mutex);

	static uint64_t audio_cue_time = 0;
	uint64_t mtime_us = g_machine.get_virt_time_us_mt();

	PDEBUGF(LOG_V2, LOG_AUDIO, "HDD-seek: mix span: %04d us (1st upd:%d), cue time:%lld us, events:%d\n",
			_time_span_us, _first_upd, audio_cue_time, m_seek_events.size());

	unsigned evtcnt = 0;

	do {
		SeekEvent event;
		bool empty = !m_seek_events.try_and_copy(event);
		if(empty || event.time > mtime_us) {
			break;
		} else {
			//play the seek sample
			evtcnt++;
			m_seek_events.try_and_pop();
			if(_first_upd) {
				audio_cue_time = event.time;
				_first_upd = false;
			}
			uint64_t time_span = event.time - audio_cue_time;
			m_channels.seek->play(m_waves.seek, time_span);
		}
	} while(1);

	unsigned buf_span;
	if(audio_cue_time==0) {
		buf_span = _time_span_us;
	} else {
		buf_span = mtime_us - audio_cue_time;
	}
	unsigned in_duration = m_channels.seek->in().duration_us();
	if(in_duration < buf_span) {
		unsigned fill_us = buf_span - in_duration;
		unsigned samples = m_channels.seek->in().fill_us_silence(fill_us);
		assert(samples == m_channels.seek->in().spec().us_to_samples(fill_us));
		PDEBUGF(LOG_V2, LOG_AUDIO, "HDD-seek: silence fill: %d frames (%d us)\n",
				m_channels.seek->in().spec().samples_to_frames(samples),
				fill_us
				);
	}
	audio_cue_time = mtime_us;
	m_channels.seek->input_finish(buf_span);
	if(evtcnt == 0) {
		m_channels.seek->check_disable_time(mtime_us);
	} else {
		m_channels.seek->set_disable_time(mtime_us);
	}
}

//this method is called by the Mixer thread
void HardDriveFX::create_spin_samples(uint64_t _time_span_us, bool _prebuf, bool _first_upd)
{
	return;
}
