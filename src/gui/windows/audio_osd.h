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

#ifndef IBMULATOR_GUI_AUDIO_OSD_H
#define IBMULATOR_GUI_AUDIO_OSD_H

#include "window.h"
#include "mixer.h"


class AudioOSD : public Window
{
private:
	Mixer *m_mixer;
	std::vector<std::shared_ptr<MixerChannel>> m_channels;
	int m_channel_id = -1;
	uint64_t m_timeout = 0;
	TimerID m_timeout_timer = NULL_TIMER_ID;
	bool m_vu_meter = false;
	struct {
		Rml::Element *volume_progress = nullptr;
		Rml::Element *volume_value = nullptr;
		Rml::Element *volume_name = nullptr;
		Rml::Element *vu_left = nullptr;
		Rml::Element *vu_right = nullptr;
	} m_divs = {};
	static constexpr float ms_max_volume = MIXER_MAX_VOLUME * 100.f;

	static event_map_t ms_evt_map;

public:
	AudioOSD(GUI *_gui, Mixer *_mixer);

	void create();
	void config_changed(bool);
	void show();
	void hide() {}
	void update();
	bool is_visible();

	void set_channel(int _id);
	void next_channel();
	void prev_channel();
	MixerChannel * current_channel() const;
	int current_channel_id() const { return m_channel_id; }

	void change_volume(float _amount);

	event_map_t & get_event_map() { return AudioOSD::ms_evt_map; }

private:
	void update_channel_name();
	void update_vu_meter(Rml::Element *_meter, double _db);
};


#endif