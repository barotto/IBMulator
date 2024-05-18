/*
 * Copyright (C) 2015-2024  Marco Bortolin
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
	using namespace std::placeholders;
	DriveFX::install(
		std::bind(&HardDriveFX::create_spin_samples, this, _1, _2), spin_name.c_str(),
		std::bind(&HardDriveFX::create_seek_samples, this, _1, _2), seek_name.c_str(),
		spec
	);

	m_buffers = SoundFX::load_samples(spec, ms_samples);

	m_channels.seek->register_config_map({
		{ MixerChannel::ConfigParameter::Volume, { SOUNDFX_SECTION, SOUNDFX_HDD_SEEK }},
		{ MixerChannel::ConfigParameter::Balance, { SOUNDFX_SECTION, SOUNDFX_HDD_BALANCE }}
	});
	m_channels.spin->register_config_map({
		{ MixerChannel::ConfigParameter::Volume, { SOUNDFX_SECTION, SOUNDFX_HDD_SPIN }},
		{ MixerChannel::ConfigParameter::Balance, { SOUNDFX_SECTION, SOUNDFX_HDD_BALANCE }}
	});
}

uint64_t HardDriveFX::spin_up_time_us() const
{
	return round(m_buffers[HDD_SPIN_UP].duration_us());
}

void HardDriveFX::create_seek_samples(uint64_t _time_span_ns, bool _first_upd)
{
	// Mixer thread

	std::lock_guard<std::mutex> clr_lock(m_clear_mutex);

	SoundFX::play_timed_events<SeekEvent, shared_deque<SeekEvent>>(
		_time_span_ns, _first_upd,
		*m_channels.seek, m_seek_events,
		[this](SeekEvent &_evt, uint64_t _time_span) {
			AudioBuffer *wave = &m_buffers[HDD_SEEK];
			double absdist = fabs(_evt.distance);
			if(absdist>0.2) {
				wave = &m_buffers[HDD_SEEK_LONG];
			}
			m_channels.seek->play_with_vol_adj(*wave, lerp(0.8,1.4,absdist), _time_span);
		});
}

void HardDriveFX::create_spin_samples(uint64_t _time_span_ns, bool _first_upd)
{
	// Mixer thread

	UNUSED(_first_upd);

	bool spin = m_spinning;
	bool change_state = m_spin_change;
	m_spin_change = false;

	SoundFX::play_motor(_time_span_ns, *m_channels.spin, spin, change_state,
		m_buffers[HDD_SPIN_UP], m_buffers[HDD_SPIN], m_buffers[HDD_SPIN_DOWN]);
}
