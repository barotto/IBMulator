/*
 * Copyright (C) 2015-2020  Marco Bortolin
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
#include "machine.h"
#include "drivefx.h"
#include <cfloat>

const uint64_t CHANNELS_TIMEOUT = 1_s;


DriveFX::DriveFX()
:
SoundFX(),
m_spinning(false),
m_spin_change(false)
{
}

DriveFX::~DriveFX()
{
}

void DriveFX::install(MixerChannel_handler _spin_channel, const char *_spin_name,
		MixerChannel_handler _seek_channel, const char *_seek_name,
		const AudioSpec &_spec)
{
	m_channels.spin = g_mixer.register_channel(_spin_channel, _spin_name, MixerChannel::Category::SOUNDFX);
	m_channels.spin->set_disable_timeout(CHANNELS_TIMEOUT);
	m_channels.spin->set_in_spec(_spec);

	m_channels.seek = g_mixer.register_channel(_seek_channel, _seek_name, MixerChannel::Category::SOUNDFX);
	m_channels.seek->set_disable_timeout(CHANNELS_TIMEOUT);
	m_channels.seek->set_in_spec(_spec);
}

void DriveFX::remove()
{
	g_mixer.unregister_channel(m_channels.spin);
	g_mixer.unregister_channel(m_channels.seek);
}

void DriveFX::seek(int _c0, int _c1, int _tot_cyls)
{
	assert(_c0>=0 && _c1>=0 && _tot_cyls>0);
	if((_c0 == _c1) || m_channels.seek->volume()<=FLT_MIN) {
		return;
	}
	SeekEvent event;
	event.time = g_machine.get_virt_time_us();
	event.distance = double(_c1 - _c0)/(_tot_cyls-1);
	event.userdata = 0;
	m_seek_events.push(event);
	PDEBUGF(LOG_V1, LOG_AUDIO, "%s: seek dist:%.4f (%d sect.), time:%lld\n",
			m_channels.seek->name(),
			event.distance,
			(_c1 - _c0),
			event.time);

	m_channels.seek->enable(true);
}

void DriveFX::spin(bool _spinning, bool _change_state)
{
	if(m_channels.spin->volume()<=FLT_MIN) {
		return;
	}
	m_spinning = _spinning;
	m_spin_change = _change_state;
	if((m_spinning || m_spin_change)) {
		m_channels.spin->enable(true);
	}
}

void DriveFX::clear_events()
{
	std::lock_guard<std::mutex> clr_lock(m_clear_mutex);
	m_seek_events.clear();
}

