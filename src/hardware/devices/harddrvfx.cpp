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
#include "gui/gui.h"
#include <cfloat>
#include <future>

const SoundFX::samples_t HardDriveFX::ms_samples = {
	{"HDD spin up",   "sounds" FS_SEP "hdd" FS_SEP "drive_spin_up.wav"},
	{"HDD spin down", "sounds" FS_SEP "hdd" FS_SEP "drive_spin_down.wav"},
	{"HDD spin",      "sounds" FS_SEP "hdd" FS_SEP "drive_spin.wav"},
	{"HDD seek",      "sounds" FS_SEP "hdd" FS_SEP "drive_seek.wav"},
	{"HDD seek",      "sounds" FS_SEP "hdd" FS_SEP "drive_seek_long.wav"}
};

const uint64_t CHANNELS_TIMEOUT = 1000000;


HardDriveFX::HardDriveFX()
:
m_spinning(false),
m_spin_up_down(false)
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
	AudioSpec spec({AUDIO_FORMAT_F32, 1, 48000});

	m_channels.spin = g_mixer.register_channel(
		std::bind(&HardDriveFX::create_spin_samples, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
		"HDD-spin");
	m_channels.spin->set_category(MixerChannelCategory::SOUNDFX);
	m_channels.spin->set_disable_timeout(CHANNELS_TIMEOUT);
	m_channels.spin->set_in_spec(spec);

	m_channels.seek = g_mixer.register_channel(
		std::bind(&HardDriveFX::create_seek_samples, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
		"HDD-seek");
	m_channels.seek->set_category(MixerChannelCategory::SOUNDFX);
	m_channels.seek->set_disable_timeout(CHANNELS_TIMEOUT);
	m_channels.seek->set_in_spec(spec);

	m_buffers = SoundFX::load_samples(spec, ms_samples);

	config_changed();
}

void HardDriveFX::seek(int _c0, int _c1, int _tot_cyls)
{
	if(m_channels.seek->volume()<=FLT_MIN) {
		return;
	}
	assert(_c0>=0 && _c1>=0 && _tot_cyls>0);
	SeekEvent event;
	event.distance = double(_c1 - _c0)/(_tot_cyls-1);
	if(event.distance > 0.f) {
		event.time = g_machine.get_virt_time_us();
		m_seek_events.push(event);
		PDEBUGF(LOG_V1, LOG_AUDIO, "HDD-seek: dist:%.4f (%d sect.), time:%lld\n",
				event.distance,
				(_c1 - _c0),
				event.time);
		if(!m_channels.seek->is_enabled()) {
			m_channels.seek->enable(true);
		}
	}
}

void HardDriveFX::spin(bool _spinning, bool _up_down_fx)
{
	if(m_channels.spin->volume()<=FLT_MIN) {
		return;
	}
	m_spinning = _spinning;
	m_spin_up_down = _up_down_fx;
	if((m_spinning || m_spin_up_down) && !m_channels.spin->is_enabled()) {
		m_channels.spin->enable(true);
	}
}

uint64_t HardDriveFX::spin_up_time() const
{
	return m_buffers[HDD_SPIN_UP].duration_us();
}

void HardDriveFX::clear_events()
{
	std::lock_guard<std::mutex> clr_lock(m_clear_mutex);
	m_seek_events.clear();
}

void HardDriveFX::config_changed()
{
	float volume;
	// HDD fx are on only for normal and realistic GUI modes
	if(GUI::mode() == GUI_MODE_COMPACT) {
		volume = 0.f;
	} else {
		volume = g_program.config().get_real(SOUNDFX_SECTION, SOUNDFX_VOLUME);
	}
	m_channels.seek->set_volume(g_program.config().get_real(SOUNDFX_SECTION, SOUNDFX_HDD_SEEK) * volume);
	m_channels.spin->set_volume(g_program.config().get_real(SOUNDFX_SECTION, SOUNDFX_HDD_SPIN) * volume);
}

//this method is called by the Mixer thread
bool HardDriveFX::create_seek_samples(uint64_t _time_span_us, bool /*_prebuf*/, bool _first_upd)
{
	std::lock_guard<std::mutex> clr_lock(m_clear_mutex);

	return SoundFX::play_timed_events<SeekEvent, shared_deque<SeekEvent>>(
		_time_span_us, _first_upd,
		*m_channels.seek, m_seek_events,
		[this](SeekEvent &_evt, uint64_t _time_span) {
			AudioBuffer *wave = &m_buffers[HDD_SEEK];
			if(_evt.distance>0.2) {
				wave = &m_buffers[HDD_SEEK_LONG];
			}
			m_channels.seek->play(*wave, lerp(0.8,1.4,_evt.distance), _time_span);
		});
}

//this method is called by the Mixer thread
bool HardDriveFX::create_spin_samples(uint64_t _time_span_us, bool, bool)
{
	bool spin = m_spinning;
	bool change_state = m_spin_up_down;
	m_spin_up_down = false;

	return SoundFX::play_motor(_time_span_us, *m_channels.spin, spin, change_state,
		m_buffers[HDD_SPIN_UP], m_buffers[HDD_SPIN], m_buffers[HDD_SPIN_DOWN]);
}
