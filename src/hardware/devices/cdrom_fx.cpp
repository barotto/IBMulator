/*
 * Copyright (C) 2024  Marco Bortolin
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
#include "cdrom_fx.h"
#include "gui/gui.h"


std::vector<AudioBuffer> CdRomFX::ms_buffers;
const SoundFX::samples_t CdRomFX::ms_samples = {
	{"CD-ROM spin",      CDROM_SAMPLES_DIR "spin.wav"},
	{"CD-ROM spin up",   CDROM_SAMPLES_DIR "spin_start.wav"},
	{"CD-ROM spin down", CDROM_SAMPLES_DIR "spin_stop.wav"},
	{"CD-ROM seek step", CDROM_SAMPLES_DIR "seek_step.wav"},
	{"CD-ROM seek out",  CDROM_SAMPLES_DIR "seek_out.wav"},
	{"CD-ROM seek in",   CDROM_SAMPLES_DIR "seek_in.wav"}
};


void CdRomFX::install(const std::string &_drive)
{
	AudioSpec spec({AUDIO_FORMAT_F32, 1, 48000});

	using namespace std::placeholders;
	DriveFX::install(
		std::bind(&CdRomFX::create_spin_samples, this, _1, _2, _3),
		str_format("%s: spin", _drive.c_str()).c_str(),
		std::bind(&CdRomFX::create_seek_samples, this, _1, _2, _3),
		str_format("%s: seek", _drive.c_str()).c_str(),
		spec
	);

	if(ms_buffers.empty()) {
		ms_buffers = SoundFX::load_samples(spec, ms_samples);
	}

	m_channels.seek->register_config_map({
		{ MixerChannel::ConfigParameter::Volume, { SOUNDFX_SECTION, SOUNDFX_CDROM_SEEK }},
		{ MixerChannel::ConfigParameter::Balance, { SOUNDFX_SECTION, SOUNDFX_DRIVES_BALANCE }}
	});
	m_channels.spin->register_config_map({
		{ MixerChannel::ConfigParameter::Volume, { SOUNDFX_SECTION, SOUNDFX_CDROM_SPIN }},
		{ MixerChannel::ConfigParameter::Balance, { SOUNDFX_SECTION, SOUNDFX_DRIVES_BALANCE }}
	});
}

uint64_t CdRomFX::duration_us(SampleType _sample) const
{
	return round(ms_buffers[static_cast<unsigned>(_sample)].duration_us());
}

bool CdRomFX::create_seek_samples(uint64_t _time_span_ns, bool /*_prebuf*/, bool _first_upd)
{
	// Mixer thread
	std::lock_guard<std::mutex> clr_lock(m_clear_mutex);

	return SoundFX::play_timed_events<SeekEvent, shared_deque<SeekEvent>>(
		_time_span_ns, _first_upd,
		*m_channels.seek, m_seek_events,
		[this](SeekEvent &_evt, uint64_t _time_span) {
			const AudioBuffer *wave;
			double absdist = fabs(_evt.distance);
			if(absdist > 1.0) {
				absdist = 1.0;
			}
			if(_evt.distance > 0.0) {
				wave = &ms_buffers[CD_SEEK_OUT];
			} else {
				wave = &ms_buffers[CD_SEEK_IN];
			}
			unsigned frames = wave->frames() * absdist;
			uint64_t duration = round(wave->spec().frames_to_us(frames));
			m_channels.seek->play_frames(*wave, 0, frames, _time_span);
			m_channels.seek->play_with_vol_adj(ms_buffers[CD_SEEK_STEP], (1.0 - absdist), (_time_span + duration));
		});
}

bool CdRomFX::create_spin_samples(uint64_t _time_span_ns, bool, bool)
{
	// Mixer thread
	bool spin = m_spinning;
	bool change_state = m_spin_change;
	m_spin_change = false;

	return SoundFX::play_motor(_time_span_ns, *m_channels.spin, spin, change_state,
			ms_buffers[CD_SPIN_UP], ms_buffers[CD_SPIN], ms_buffers[CD_SPIN_DOWN], true);
}
