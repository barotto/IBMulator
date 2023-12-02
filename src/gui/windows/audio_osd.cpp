/*
 * Copyright (C) 2023  Marco Bortolin
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
#include "gui.h"
#include "mixer.h"
#include "audio_osd.h"
#include "program.h"
#include <RmlUi/Core.h>

event_map_t AudioOSD::ms_evt_map = {};

AudioOSD::AudioOSD(GUI *_gui, Mixer *_mixer)
:
Window(_gui, "audio_osd.rml"),
m_mixer(_mixer)
{
}

void AudioOSD::create()
{
	Window::create();

	m_timeout = g_program.config().get_real_or_default(DIALOGS_SECTION, DIALOGS_OSD_TIMEOUT) * NSEC_PER_SECOND;

	m_timeout_timer = m_gui->timers().register_timer(
		[this](uint64_t) { hide(); },
		"Audio OSD"
	);

	m_divs.volume_progress = get_element("ch_vol_progress");
	m_divs.volume_value = get_element("ch_vol_value");
	m_divs.volume_name = get_element("ch_vol_name");

	m_divs.volume_progress->SetAttribute("max", str_format("%d", int(MIXER_MAX_VOLUME * 100)));
}

void AudioOSD::show()
{
	Window::show();
	m_gui->timers().activate_timer(m_timeout_timer, m_timeout, false);
}

void AudioOSD::hide()
{
	Window::hide();
	m_gui->timers().deactivate_timer(m_timeout_timer);
}

void AudioOSD::update()
{
	float mix_value;
	if(m_channel_id < 0) {
		mix_value = m_mixer->volume_master();
	} else {
		mix_value = m_mixer->volume_cat(static_cast<MixerChannel::Category>(m_channel_id));
	}
	if(m_cur_volume != mix_value) {
		m_cur_volume = mix_value;
		int val_shown = round(mix_value * 100.0);
		m_divs.volume_value->SetInnerRML(str_format("%d", val_shown));
		m_divs.volume_progress->SetAttribute("value", val_shown);
	}
}

void AudioOSD::set_channel_id(int _id)
{
	if(!m_wnd) {
		create();
	}
	m_channel_id = _id;
	if(m_channel_id < 0) {
		m_divs.volume_name->SetInnerRML("Master");
	} else {
		switch(m_channel_id) {
			case MixerChannel::Category::AUDIOCARD: m_divs.volume_name->SetInnerRML("Audio cards"); break;
			case MixerChannel::Category::SOUNDFX: m_divs.volume_name->SetInnerRML("Sound FX"); break;
			default: m_divs.volume_name->SetInnerRML(""); break;
		}
	}
}
