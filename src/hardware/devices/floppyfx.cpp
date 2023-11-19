/*
 * Copyright (C) 2015-2023  Marco Bortolin
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
#include "floppyfx.h"
#include "gui/gui.h"


std::vector<AudioBuffer> FloppyFX::ms_buffers[2];
const SoundFX::samples_t FloppyFX::ms_samples[2] = {
	{ // 5_25
	{"5.25 drive spin",          FDD_SAMPLES_DIR "5_25_drive_spin.wav"},
	{"5.25 drive spin start",    FDD_SAMPLES_DIR "5_25_drive_spin_start.wav"},
	{"5.25 drive spin stop",     FDD_SAMPLES_DIR "5_25_drive_spin_stop.wav"},
	{"5.25 drive seek step",     FDD_SAMPLES_DIR "5_25_drive_seek_step.wav"},
	{"5.25 drive seek up",       FDD_SAMPLES_DIR "5_25_drive_seek_up.wav"},
	{"5.25 drive seek down",     FDD_SAMPLES_DIR "5_25_drive_seek_down.wav"},
	{"5.25 drive seek boot",     ""},
	{"5.25 drive snatch",        ""},
	{"5.25 drive snatch boot",   ""}
	},{ // 3_5
	{"3.5 drive spin",          FDD_SAMPLES_DIR "3_5_drive_spin.wav"},
	{"3.5 drive spin start",    FDD_SAMPLES_DIR "3_5_drive_spin_start.wav"},
	{"3.5 drive spin stop",     FDD_SAMPLES_DIR "3_5_drive_spin_stop.wav"},
	{"3.5 drive seek step",     FDD_SAMPLES_DIR "3_5_drive_seek_step.wav"},
	{"3.5 drive seek up",       FDD_SAMPLES_DIR "3_5_drive_seek_up.wav"},
	{"3.5 drive seek down",     FDD_SAMPLES_DIR "3_5_drive_seek_down.wav"},
	{"3.5 drive seek boot",     FDD_SAMPLES_DIR "3_5_drive_boot.wav"},
	{"3.5 drive snatch",        FDD_SAMPLES_DIR "3_5_drive_snatch.wav"},
	{"3.5 drive snatch boot",   FDD_SAMPLES_DIR "3_5_drive_boot_disk.wav"}
	}
};

FloppyFX::FloppyFX()
:
DriveFX()
{
}

FloppyFX::~FloppyFX()
{
}

void FloppyFX::install(const std::string &_drive, FloppyFX::FDDType _fdd_type)
{
	m_fdd_type = _fdd_type;

	AudioSpec spec({AUDIO_FORMAT_F32, 1, 48000});

	using namespace std::placeholders;
	DriveFX::install(
		std::bind(&FloppyFX::create_spin_samples, this, _1, _2, _3),
		str_format("%s: spin (%s)", _drive.c_str(), _fdd_type == FDD_5_25 ? "5.25\"" : "3.5\"").c_str(),
		std::bind(&FloppyFX::create_seek_samples, this, _1, _2, _3),
		str_format("%s: seek (%s)", _drive.c_str(), _fdd_type == FDD_5_25 ? "5.25\"" : "3.5\"").c_str(),
		spec
	);

	if(ms_buffers[m_fdd_type].empty()) {
		ms_buffers[m_fdd_type] = SoundFX::load_samples(spec, ms_samples[m_fdd_type]);
	}

	m_channels.seek->register_config_map({
		{ MixerChannel::ConfigParameter::Volume, { SOUNDFX_SECTION, SOUNDFX_FDD_SEEK }},
		{ MixerChannel::ConfigParameter::Balance, { SOUNDFX_SECTION, SOUNDFX_FDD_BALANCE }}
	});
	m_channels.spin->register_config_map({
		{ MixerChannel::ConfigParameter::Volume, { SOUNDFX_SECTION, SOUNDFX_FDD_SPIN }},
		{ MixerChannel::ConfigParameter::Balance, { SOUNDFX_SECTION, SOUNDFX_FDD_BALANCE }}
	});
}

void FloppyFX::reset()
{
	m_booting = 0;
	m_snatch = false;
}

void FloppyFX::spin(bool _spinning, bool _change_state)
{
	if(_spinning) {
		if(_change_state) {
			m_spin_time = g_machine.get_virt_time_us();
		} else {
			m_spin_time = 0;
		}
	} else if(!_spinning && _change_state) {
		//the BIOS sometimes activate the motor and deactivate it after only a bunch of us
		if(g_machine.get_virt_time_us() < m_spin_time+25) {
			m_channels.spin->enable(false);
			return;
		}
	}
	DriveFX::spin(_spinning, _change_state);
}

bool FloppyFX::boot(bool _wdisk)
{
	if(_wdisk) {
		if(!ms_samples[m_fdd_type][FDD_SNATCH_BOOT].file.empty()) {
			// this will run when the drive starts the motor with the disk inserted
			m_booting = !ms_samples[m_fdd_type][FDD_SNATCH_BOOT].file.empty();
			spin(true, true);
			return true;
		}
	} else {
		if(!ms_samples[m_fdd_type][FDD_SEEK_BOOT].file.empty()) {
			// this will run when the drive starts the recalibrate's first seek without a disk
			SeekEvent event = {};
			event.time = g_machine.get_virt_time_us();
			event.distance = 0.0;
			event.userdata = FDD_SEEK_BOOT;
			m_seek_events.push(event);
			m_channels.seek->enable(true);
			return true;
		}
	}
	return false;
}

//this method is called by the Mixer thread
bool FloppyFX::create_seek_samples(uint64_t _time_span_ns, bool /*_prebuf*/, bool _first_upd)
{
	std::lock_guard<std::mutex> clr_lock(m_clear_mutex);

	return SoundFX::play_timed_events<SeekEvent, shared_deque<SeekEvent>>(
		_time_span_ns, _first_upd,
		*m_channels.seek, m_seek_events,
		[this](SeekEvent &_evt, uint64_t _time_span) {
			const AudioBuffer *wave;
			if(_evt.userdata == FDD_SEEK_BOOT) {
				m_channels.seek->play(ms_buffers[m_fdd_type][_evt.userdata], _time_span);
				m_booting = _evt.time + round(ms_buffers[m_fdd_type][_evt.userdata].duration_us());
				PDEBUGF(LOG_V1, LOG_AUDIO, "%s: booting until %llu us\n",
						m_channels.seek->name(), m_booting);
				return;
			}
			if(_evt.time < m_booting) {
				PDEBUGF(LOG_V1, LOG_AUDIO, "%s: seek event ignored\n", m_channels.seek->name());
				return;
			}
			double absdist = fabs(_evt.distance);
			if(absdist > 1.0) {
				absdist = 1.0;
			}
			if(_evt.distance > 0.0) {
				wave = &ms_buffers[m_fdd_type][FDD_SEEK_UP];
			} else {
				wave = &ms_buffers[m_fdd_type][FDD_SEEK_DOWN];
			}
			unsigned frames = wave->frames() * absdist;
			uint64_t duration = round(wave->spec().frames_to_us(frames));
			m_channels.seek->play_frames(*wave, frames, _time_span);
			m_channels.seek->play(ms_buffers[m_fdd_type][FDD_SEEK_STEP], 1.0-absdist, _time_span+duration);
		});
}

//this method is called by the Mixer thread
bool FloppyFX::create_spin_samples(uint64_t _time_span_ns, bool, bool)
{
	bool spin = m_spinning;
	bool change_state = m_spin_change;
	AudioBuffer *spinup;
	if(m_fdd_type == FDD_3_5 && spin && change_state && m_snatch) {
		PDEBUGF(LOG_V1, LOG_AUDIO, "%s: snatch\n", m_channels.spin->name());
		if(m_booting) {
			spinup = &ms_buffers[m_fdd_type][FDD_SNATCH_BOOT];
			m_booting = 0;
		} else {
			spinup = &ms_buffers[m_fdd_type][FDD_SNATCH];
		}
		m_snatch = false;
	} else {
		spinup = &ms_buffers[m_fdd_type][FDD_SPIN_UP];
	}
	m_spin_change = false;

	return SoundFX::play_motor(_time_span_ns, *m_channels.spin, spin, change_state,
			*spinup, ms_buffers[m_fdd_type][FDD_SPIN], ms_buffers[m_fdd_type][FDD_SPIN_DOWN]);
}
