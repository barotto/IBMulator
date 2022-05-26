/*
 * Copyright (C) 2015-2022  Marco Bortolin
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
#include <cfloat>
#include <future>

const SoundFX::samples_t HardDriveFX::ms_samples = {
	{"HDD spin up",   HDD_SAMPLES_DIR "drive_spin_up.wav"},
	{"HDD spin down", HDD_SAMPLES_DIR "drive_spin_down.wav"},
	{"HDD spin",      HDD_SAMPLES_DIR "drive_spin.wav"},
	{"HDD seek",      HDD_SAMPLES_DIR "drive_seek.wav"},
	{"HDD seek",      HDD_SAMPLES_DIR "drive_seek_long.wav"}
};


HardDriveFX::HardDriveFX()
:
DriveFX()
{
}

HardDriveFX::~HardDriveFX()
{
}

void HardDriveFX::install(const std::string &_name)
{
	/* mixer channels operate in float format, but rate and channels count depend
	 * on the current state of the mixer. can't anticipate.
	 */
	AudioSpec spec({AUDIO_FORMAT_F32, 1, 48000});

	std::string spin_name = _name + " spin";
	std::string seek_name = _name + " seek";
	DriveFX::install(
		std::bind(&HardDriveFX::create_spin_samples, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
		spin_name.c_str(),
		std::bind(&HardDriveFX::create_seek_samples, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
		seek_name.c_str(),
		spec
	);

	m_buffers = SoundFX::load_samples(spec, ms_samples);
}

uint64_t HardDriveFX::spin_up_time_us() const
{
	return round(m_buffers[HDD_SPIN_UP].duration_us());
}

void HardDriveFX::config_changed()
{
	float volume = g_program.config().get_real(SOUNDFX_SECTION, SOUNDFX_VOLUME);

	m_channels.seek->set_volume(g_program.config().get_real(SOUNDFX_SECTION, SOUNDFX_HDD_SEEK) * volume);
	m_channels.spin->set_volume(g_program.config().get_real(SOUNDFX_SECTION, SOUNDFX_HDD_SPIN) * volume);
}

//this method is called by the Mixer thread
bool HardDriveFX::create_seek_samples(uint64_t _time_span_ns, bool /*_prebuf*/, bool _first_upd)
{
	std::lock_guard<std::mutex> clr_lock(m_clear_mutex);

	return SoundFX::play_timed_events<SeekEvent, shared_deque<SeekEvent>>(
		_time_span_ns, _first_upd,
		*m_channels.seek, m_seek_events,
		[this](SeekEvent &_evt, uint64_t _time_span) {
			AudioBuffer *wave = &m_buffers[HDD_SEEK];
			double absdist = fabs(_evt.distance);
			if(absdist>0.2) {
				wave = &m_buffers[HDD_SEEK_LONG];
			}
			m_channels.seek->play(*wave, lerp(0.8,1.4,absdist), _time_span);
		});
}

//this method is called by the Mixer thread
bool HardDriveFX::create_spin_samples(uint64_t _time_span_ns, bool, bool)
{
	bool spin = m_spinning;
	bool change_state = m_spin_change;
	m_spin_change = false;

	return SoundFX::play_motor(_time_span_ns, *m_channels.spin, spin, change_state,
		m_buffers[HDD_SPIN_UP], m_buffers[HDD_SPIN], m_buffers[HDD_SPIN_DOWN]);
}
