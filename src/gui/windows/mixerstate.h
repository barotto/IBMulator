/*
 * Copyright (C) 2020  Marco Bortolin
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

#ifndef IBMULATOR_GUI_MIXERSTATE_H
#define IBMULATOR_GUI_MIXERSTATE_H

#include "debugtools.h"

class Mixer;
class MixerChannel;
class GUI;

class MixerState : public DebugTools::DebugWindow
{
private:

	Mixer *m_mixer;
	struct {
		Rocket::Core::Element *state, *channels;
	} m_divs;
	struct Channel {
		std::shared_ptr<MixerChannel> ch;
		RC::Element *enabled;
		RC::Element *in_format, *in_frames, *in_us;
		RC::Element *out_frames, *out_us;
	};
	std::vector<Channel> m_channels;
	
	static event_map_t ms_evt_map;

public:

	MixerState(GUI *_gui, RC::Element *_button, Mixer *_mixer);
	~MixerState();

	void update();
	void config_changed();
	
	event_map_t & get_event_map() { return MixerState::ms_evt_map; }
};

#endif
