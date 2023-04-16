/*
 * Copyright (C) 2020-2023  Marco Bortolin
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
		Rml::Element *state, *channels;
	} m_divs = {};
	struct Channel {
		std::shared_ptr<MixerChannel> ch;
		Rml::Element *enabled;
		Rml::Element *in_format, *in_frames, *in_us;
		Rml::Element *out_frames, *out_us;
	};
	std::vector<Channel> m_channels;
	
	static event_map_t ms_evt_map;

public:

	MixerState(GUI *_gui, Rml::Element *_button, Mixer *_mixer);
	~MixerState();

	virtual void create();
	virtual void update();
	virtual void config_changed(bool);
	
	event_map_t & get_event_map() { return MixerState::ms_evt_map; }
};

#endif
