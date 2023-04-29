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
#include "guifx.h"


GUIFX::GUIFX()
:
SoundFX()
{
}

GUIFX::~GUIFX()
{
}

void GUIFX::init(Mixer *_mixer, MixerChannel_handler _channel_fn, const char *_channel_name,
		const AudioSpec &_spec)
{
	m_channel = _mixer->register_channel(_channel_fn, _channel_name, MixerChannel::Category::SOUNDFX);
	m_channel->set_disable_timeout(1_s);
	m_channel->set_in_spec(_spec);

	float volume = g_program.config().get_real(SOUNDFX_SECTION, SOUNDFX_VOLUME);
	m_channel->set_volume(g_program.config().get_real(SOUNDFX_SECTION, SOUNDFX_SYSTEM) * volume);
	m_channel->set_balance(g_program.config().get_real(SOUNDFX_SECTION, SOUNDFX_SYSTEM_BALANCE, 0.0));
}

