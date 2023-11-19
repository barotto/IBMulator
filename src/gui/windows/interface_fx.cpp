/*
 * Copyright (C) 2015-2023  Marco Bortolin
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

#include "ibmulator.h"
#include "interface_fx.h"
#include "hardware/devices/drivefx.h"
#include "program.h"

const SoundFX::samples_t GUIDrivesFX::ms_samples[2] = {
	{
	{"5.25 disk insert", FDD_SAMPLES_DIR "5_25_disk_insert.wav"},
	{"5.25 disk eject",  FDD_SAMPLES_DIR "5_25_disk_eject.wav"}
	},{
	{"3.5 disk insert", FDD_SAMPLES_DIR "3_5_disk_insert.wav"},
	{"3.5 disk eject",  FDD_SAMPLES_DIR "3_5_disk_eject.wav"}
	}
};

void GUIDrivesFX::init(Mixer *_mixer)
{
	using namespace std::placeholders;
	AudioSpec spec({AUDIO_FORMAT_F32, 1, 48000});
	GUIFX::init(_mixer,
		std::bind(&GUIDrivesFX::create_sound_samples, this, _1, _2, _3),
		"GUI drives", spec);

	m_buffers[FDD_5_25] = SoundFX::load_samples(spec, ms_samples[FDD_5_25]);
	m_buffers[FDD_3_5] = SoundFX::load_samples(spec, ms_samples[FDD_3_5]);

	m_channel->register_config_map({
		{ MixerChannel::ConfigParameter::Volume, { SOUNDFX_SECTION, SOUNDFX_FDD_GUI }},
		{ MixerChannel::ConfigParameter::Balance, { SOUNDFX_SECTION, SOUNDFX_FDD_BALANCE }}
	});
}

void GUIDrivesFX::use_floppy(FDDType _fdd_type, SampleType _how)
{
	m_event = _fdd_type << 8 | _how;
	m_channel->enable(true);
}

bool GUIDrivesFX::create_sound_samples(uint64_t _time_span_ns, bool, bool)
{
	// Mixer thread
	unsigned evt = m_event & 0xff;
	unsigned sub = (m_event >> 8) & 0xff;
	if(evt != 0xff) {
		assert(sub < 2);
		assert(evt < m_buffers[sub].size());
		m_channel->flush();
		m_channel->play(m_buffers[sub][evt], 0);
		m_channel->play_silence_us(EFFECTS_MIN_DUR_US);
		m_channel->input_finish(_time_span_ns);
	}
	// possible event miss, but i don't care, they are very slow anyway
	m_event = -1;
	m_channel->enable(false);
	return false;
}


const SoundFX::samples_t GUISystemFX::ms_samples = {
	{"System power up",   "sounds" FS_SEP "system" FS_SEP "power_up.wav"},
	{"System power down", "sounds" FS_SEP "system" FS_SEP "power_down.wav"},
	{"System power on",   "sounds" FS_SEP "system" FS_SEP "power_on.wav"}
};

void GUISystemFX::init(Mixer *_mixer)
{
	AudioSpec spec({AUDIO_FORMAT_F32, 1, 48000});
	GUIFX::init(_mixer,
		std::bind(&GUISystemFX::create_sound_samples, this,
				std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
		"GUI system", spec);

	m_buffers = SoundFX::load_samples(spec, ms_samples);

	m_channel->register_config_map({
		{ MixerChannel::ConfigParameter::Volume, { SOUNDFX_SECTION, SOUNDFX_SYSTEM }},
		{ MixerChannel::ConfigParameter::Balance, { SOUNDFX_SECTION, SOUNDFX_SYSTEM_BALANCE }}
	});
}

void GUISystemFX::update(bool _power_on, bool _change_state)
{
	if((_power_on || _change_state)) {
		m_channel->enable(true);
	}
	m_power_on = _power_on;
	m_change_state = _change_state;
}

//this method is called by the Mixer thread
bool GUISystemFX::create_sound_samples(uint64_t _time_span_ns, bool, bool)
{
	bool power_on = m_power_on;
	bool change_state = m_change_state;
	m_change_state = false;

	return SoundFX::play_motor(_time_span_ns, *m_channel, power_on, change_state,
			m_buffers[POWER_UP], m_buffers[POWER_ON], m_buffers[POWER_DOWN]);
}
