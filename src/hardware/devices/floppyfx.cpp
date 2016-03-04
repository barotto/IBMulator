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
#include "floppyfx.h"
#include "gui/gui.h"

std::vector<AudioBuffer> FloppyFX::ms_buffers;
const SoundFX::samples_t FloppyFX::ms_samples = {
	{"FDD spin",          "sounds" FS_SEP "floppy" FS_SEP "drive_spin.wav"},
	{"FDD spin up",       "sounds" FS_SEP "floppy" FS_SEP "drive_spin_up.wav"},
	{"HDD spin down",     "sounds" FS_SEP "floppy" FS_SEP "drive_spin_down.wav"},
	{"FDD seek step",     "sounds" FS_SEP "floppy" FS_SEP "drive_seek_step.wav"},
	{"FDD seek up",       "sounds" FS_SEP "floppy" FS_SEP "drive_seek_up.wav"},
	{"FDD seek down",     "sounds" FS_SEP "floppy" FS_SEP "drive_seek_down.wav"},
	{"FDD snatch",        "sounds" FS_SEP "floppy" FS_SEP "drive_snatch.wav"},
	{"FDD boot",          "sounds" FS_SEP "floppy" FS_SEP "drive_boot.wav"},
	{"FDD boot (w/disk)", "sounds" FS_SEP "floppy" FS_SEP "drive_boot_disk.wav"}
};

FloppyFX::FloppyFX()
:
DriveFX()
{
}

FloppyFX::~FloppyFX()
{
}

void FloppyFX::init(const std::string &_drive)
{
	std::string spin = "FDD-spin-" + _drive;
	std::string seek = "FDD-seek-" + _drive;
	AudioSpec spec({AUDIO_FORMAT_F32, 1, 48000});

	DriveFX::init(
		std::bind(&FloppyFX::create_spin_samples, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
		spin.c_str(),
		std::bind(&FloppyFX::create_seek_samples, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
		seek.c_str(),
		spec
	);

	if(ms_buffers.empty()) {
		ms_buffers = SoundFX::load_samples(spec, ms_samples);
	}
}

void FloppyFX::config_changed()
{
	float volume = g_program.config().get_real(SOUNDFX_SECTION, SOUNDFX_VOLUME);

	m_channels.seek->set_volume(g_program.config().get_real(SOUNDFX_SECTION, SOUNDFX_FDD_SEEK) * volume);
	m_channels.spin->set_volume(g_program.config().get_real(SOUNDFX_SECTION, SOUNDFX_FDD_SPIN) * volume);
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
		if(g_machine.get_virt_time_us() < m_spin_time+50) {
			m_channels.spin->enable(false);
			return;
		}
	}
	DriveFX::spin(_spinning, _change_state);
}

void FloppyFX::boot(bool _wdisk)
{
	if(m_channels.seek->volume() <= FLT_MIN) {
		return;
	}
	SeekEvent event;
	event.time = g_machine.get_virt_time_us();
	event.distance = 0.0;
	if(_wdisk) {
		event.userdata = FDD_BOOT_DISK;
	} else {
		event.userdata = FDD_BOOT;
	}
	m_seek_events.push(event);
	if(!m_channels.seek->is_enabled()) {
		m_channels.seek->enable(true);
	}
}

//this method is called by the Mixer thread
bool FloppyFX::create_seek_samples(uint64_t _time_span_us, bool /*_prebuf*/, bool _first_upd)
{
	std::lock_guard<std::mutex> clr_lock(m_clear_mutex);

	return SoundFX::play_timed_events<SeekEvent, shared_deque<SeekEvent>>(
		_time_span_us, _first_upd,
		*m_channels.seek, m_seek_events,
		[this](SeekEvent &_evt, uint64_t _time_span) {
			const AudioBuffer *wave;
			if(_evt.userdata) {
				m_channels.seek->play(ms_buffers[_evt.userdata], _time_span);
				m_booting = _evt.time + ms_buffers[_evt.userdata].duration_us();
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s: booting until %llu\n",
						m_channels.seek->name(), m_booting);
				return;
			}
			if(_evt.time < m_booting) {
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s: seek event ignored\n", m_channels.seek->name());
				return;
			}
			double absdist = fabs(_evt.distance);
			if(absdist > 1.0) {
				absdist = 1.0;
			}
			if(_evt.distance > 0.0) {
				wave = &ms_buffers[FDD_SEEK_UP];
			} else {
				wave = &ms_buffers[FDD_SEEK_DOWN];
			}
			unsigned frames = wave->frames() * absdist;
			uint64_t duration = wave->spec().frames_to_us(frames);
			m_channels.seek->play_frames(*wave, frames, _time_span);
			m_channels.seek->play(ms_buffers[FDD_SEEK_STEP], 1.0-absdist, _time_span+duration);
		});
}

//this method is called by the Mixer thread
bool FloppyFX::create_spin_samples(uint64_t _time_span_us, bool, bool)
{
	bool spin = m_spinning;
	bool change_state = m_spin_change;
	AudioBuffer *spinup;
	if(spin && change_state && m_snatch) {
		spinup = &ms_buffers[FDD_SNATCH];
		m_snatch = false;
	} else {
		spinup = &ms_buffers[FDD_SPIN_UP];
	}
	m_spin_change = false;

	return SoundFX::play_motor(_time_span_us, *m_channels.spin, spin, change_state,
			*spinup, ms_buffers[FDD_SPIN], ms_buffers[FDD_SPIN_DOWN]);
}
