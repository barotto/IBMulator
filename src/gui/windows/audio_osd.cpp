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
		[this](uint64_t) { 
			m_wnd->SetClass("hidden", true);
		},
		"Audio OSD"
	);

	m_divs.volume_progress = get_element("ch_vol_progress");
	m_divs.volume_value = get_element("ch_vol_value");
	m_divs.volume_name = get_element("ch_vol_name");
	m_divs.vu_left = get_element("ch_vu_left");
	m_divs.vu_right = get_element("ch_vu_right");
	m_divs.vu_left->SetAttribute("max", MixerChannel::VUMeter::range);
	m_divs.vu_left->SetAttribute("value", 0);
	m_divs.vu_right->SetAttribute("max", MixerChannel::VUMeter::range);
	m_divs.vu_right->SetAttribute("value", 0);

	m_divs.volume_progress->SetAttribute("max", int(MIXER_MAX_VOLUME * 100));

	update_channel_name();
}

void AudioOSD::config_changed(bool)
{
	m_channels.clear();

	for(auto &ch : m_mixer->get_channels(MixerChannel::AUDIOCARD)) {
		if(!ch->features() || !(ch->features() & MixerChannel::Feature::HasVolume)) {
			continue;
		}
		m_channels.push_back(ch);
	}

	for(auto &ch : m_mixer->get_channels(MixerChannel::SOUNDFX)) {
		if(!ch->features() || !(ch->features() & MixerChannel::Feature::HasVolume)) {
			continue;
		}
		m_channels.push_back(ch);
	}

	m_channel_id = MixerChannel::MASTER;

	if(m_divs.vu_left) {
		update_vu_meter(m_divs.vu_left, MixerChannel::VUMeter::min);
	}
	if(m_divs.vu_right) {
		update_vu_meter(m_divs.vu_right, MixerChannel::VUMeter::min);
	}
}

void AudioOSD::update_channel_name()
{
	if(m_channel_id < 0) {
		m_divs.volume_name->SetInnerRML("Master");
		m_divs.volume_name->SetClassNames("master");
	} else {
		switch(m_channel_id) {
			case MixerChannel::Category::AUDIOCARD: { 
				m_divs.volume_name->SetInnerRML("Audio cards");
				m_divs.volume_name->SetClassNames("category audiocard");
				break;
			}
			case MixerChannel::Category::SOUNDFX: {
				m_divs.volume_name->SetInnerRML("Sound FX");
				m_divs.volume_name->SetClassNames("category soundfx");
				break;
			}
			case MixerChannel::Category::GUI: {
				m_divs.volume_name->SetInnerRML("GUI");
				m_divs.volume_name->SetClassNames("category gui");
				break;
			}
			default: {
				int id = m_channel_id - MixerChannel::CategoryCount;
				if(id >= int(m_channels.size())) {
					return;
				}
				m_divs.volume_name->SetInnerRML(m_channels[id]->name());
				if(m_channels[id]->category() == MixerChannel::Category::AUDIOCARD) {
					m_divs.volume_name->SetClassNames("audiocard");
				} else {
					m_divs.volume_name->SetClassNames("soundfx");
				}
				break;
			}
		}
	}
}

void AudioOSD::next_channel()
{
	m_channel_id++;
	if(m_channel_id == MixerChannel::Category::GUI) {
		m_channel_id++;
	}
	m_channel_id = std::min(m_channel_id, MixerChannel::CategoryCount + (int(m_channels.size()) - 1));
	update_channel_name();
}

void AudioOSD::prev_channel()
{
	m_channel_id--;
	if(m_channel_id == MixerChannel::Category::GUI) {
		m_channel_id--;
	}
	m_channel_id = std::max(int(MixerChannel::MASTER), m_channel_id);
	update_channel_name();
}

MixerChannel * AudioOSD::current_channel() const
{
	int id = m_channel_id - MixerChannel::CategoryCount;
	if(id < 0 || id >= int(m_channels.size())) {
		return nullptr;
	}
	return m_channels[id].get();
}

void AudioOSD::set_channel(int _id)
{
	m_channel_id = _id;
	if(m_channel_id < 0) {
		m_channel_id = MixerChannel::MASTER;
	} else {
		m_channel_id = std::min(m_channel_id, MixerChannel::CategoryCount + (int(m_channels.size()) - 1));
		m_channel_id = std::max(int(MixerChannel::MASTER), m_channel_id);
	}
	update_channel_name();
}

void AudioOSD::show()
{
	Window::show();
	m_wnd->SetClass("hidden", false);
	m_gui->timers().activate_timer(m_timeout_timer, m_timeout, false);
	m_vu_meter = g_program.config().get_bool_or_default(DIALOGS_SECTION, DIALOGS_VU_METERS);
	m_wnd->SetClass("with_vu_meter", m_vu_meter);
}

void AudioOSD::update()
{
	float mix_value;
	const MixerChannel::VUMeter *vu = nullptr;
	bool auto_vol = false;
	if(m_channel_id < 0) {
		mix_value = m_mixer->volume_master();
		vu = &m_mixer->vu_meter();
	} else if(m_channel_id < MixerChannel::CategoryCount) {
		mix_value = m_mixer->volume_cat(static_cast<MixerChannel::Category>(m_channel_id));
		vu = &m_mixer->vu_meter_cat(static_cast<MixerChannel::Category>(m_channel_id));
	} else {
		int id = m_channel_id - MixerChannel::CategoryCount;
		if(id >= int(m_channels.size())) {
			return;
		}
		vu = &m_channels[id]->vu_meter();
		if(m_channels[id]->is_volume_auto()) {
			auto_vol = true;
			m_divs.volume_value->SetInnerRML("auto");
			if(m_channels[id]->features() & MixerChannel::HasStereoSource) {
				mix_value = (m_channels[id]->volume_master_left() + m_channels[id]->volume_master_right()) / 2.0;
			} else {
				mix_value = m_channels[id]->volume_master_left();
			}
		}
		mix_value = m_channels[id]->volume_master_left();
		if(m_channels[id]->is_enabled() || m_channels[id]->out().frames() || m_channels[id]->in().frames()) {
			m_divs.volume_name->SetClass("active", true);
		} else {
			m_divs.volume_name->SetClass("active", false);
		}
	}
	if(m_vu_meter && vu) {
		update_vu_meter(m_divs.vu_left, vu->db[0]);
		update_vu_meter(m_divs.vu_right, vu->db[1]);
	}

	int val_shown = round(mix_value * 100.0);
	if(!auto_vol) {
		m_divs.volume_value->SetInnerRML(str_format("%d", val_shown));
	}
	m_divs.volume_progress->SetAttribute("value", val_shown);
}

void AudioOSD::update_vu_meter(Rml::Element *_meter, double _db)
{
	int db = int(std::round(clamp(_db, MixerChannel::VUMeter::min, MixerChannel::VUMeter::max)));
	_meter->SetAttribute("value", db + int(std::abs(MixerChannel::VUMeter::min)));
	if(db >= 0) {
		_meter->SetClass("over", true);
	} else if(db >= -6) {
		_meter->SetClass("edge", true);
	} else {
		_meter->SetClass("edge", false);
		_meter->SetClass("over", false);
	}
}

bool AudioOSD::is_visible()
{
	if(m_wnd && m_wnd->IsVisible()) {
		float opacity = m_wnd->GetProperty(Rml::PropertyId::Opacity)->Get<float>();
		return opacity > 0.1;
	}
	return false;
}

void AudioOSD::change_volume(float _amount)
{
	MixerChannel *channel = current_channel();
	if(channel) {
		float current = channel->volume_master_left();
		if(channel->is_volume_auto()) {
			channel->set_volume_auto(false);
			if(channel->features() & MixerChannel::HasStereoSource) {
				current = (channel->volume_master_left() + channel->volume_master_right()) / 2.0;
			}
		}
		channel->set_volume_master(current + _amount);
	} else {
		int id = current_channel_id();
		if(id >= MixerChannel::CategoryCount) {
			return;
		}
		if(id < 0) {
			float current = m_mixer->volume_master();
			m_mixer->set_volume_master(current + _amount);
		} else {
			MixerChannel::Category category = static_cast<MixerChannel::Category>(id);
			float current = m_mixer->volume_cat(category);
			m_mixer->set_volume_cat(category, current + _amount);
		}
	}
}
