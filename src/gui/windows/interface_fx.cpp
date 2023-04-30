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

const SoundFX::samples_t InterfaceFX::ms_samples[2] = {
	{
	{"5.25 disk insert", FDD_SAMPLES_DIR "5_25_disk_insert.wav"},
	{"5.25 disk eject",  FDD_SAMPLES_DIR "5_25_disk_eject.wav"}
	},{
	{"3.5 disk insert", FDD_SAMPLES_DIR "3_5_disk_insert.wav"},
	{"3.5 disk eject",  FDD_SAMPLES_DIR "3_5_disk_eject.wav"}
	}
};

void InterfaceFX::init(Mixer *_mixer)
{
	using namespace std::placeholders;
	AudioSpec spec({AUDIO_FORMAT_F32, 1, 48000});
	GUIFX::init(_mixer,
		std::bind(&InterfaceFX::create_sound_samples, this, _1, _2, _3),
		"GUI interface", spec);
	m_buffers[FDD_5_25] = SoundFX::load_samples(spec, ms_samples[FDD_5_25]);
	m_buffers[FDD_3_5] = SoundFX::load_samples(spec, ms_samples[FDD_3_5]);
}

void InterfaceFX::use_floppy(FDDType _fdd_type, SampleType _how)
{
	if(m_channel->volume()<=FLT_MIN) {
		return;
	}
	m_event = _fdd_type << 8 | _how;
	m_channel->enable(true);
}

bool InterfaceFX::create_sound_samples(uint64_t, bool, bool)
{
	// Mixer thread
	unsigned evt = m_event & 0xff;
	unsigned sub = (m_event >> 8) & 0xff;
	if(evt != 0xff) {
		assert(sub < 2);
		assert(evt < m_buffers[sub].size());
		m_channel->flush();
		m_channel->play(m_buffers[sub][evt], 0);
	}
	// possible event miss, but i don't care, they are very slow anyway
	m_event = -1;
	m_channel->enable(false);
	return false;
}
