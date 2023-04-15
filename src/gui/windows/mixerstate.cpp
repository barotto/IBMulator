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

#include "ibmulator.h"
#include "program.h"
#include "gui.h"
#include "mixer.h"
#include "mixerstate.h"
#include <RmlUi/Core.h>

event_map_t MixerState::ms_evt_map = {
	GUI_EVT( "close", "click", DebugTools::DebugWindow::on_cancel ),
	GUI_EVT( "*",   "keydown", Window::on_keydown )
};

MixerState::MixerState(GUI *_gui, Rml::Element *_button, Mixer *_mixer)
:
DebugTools::DebugWindow(_gui, "mixerstate.rml", _button),
m_mixer(_mixer)
{
}

MixerState::~MixerState()
{
}

void MixerState::create()
{
	DebugTools::DebugWindow::create();

	m_divs.state = get_element("state");
	m_divs.channels = get_element("channels");
}

void MixerState::update()
{
	if(!m_enabled) {
		return;
	}
	
	HWBench &mixb = m_mixer->get_bench();
	std::stringstream ss;
	ss.str("");
	ss << "Mode: " <<
			m_mixer->get_audio_spec().freq << " Hz, " <<
			SDL_AUDIO_BITSIZE(m_mixer->get_audio_spec().format) << " bit, " << 
			(m_mixer->get_audio_spec().channels==1?"mono":"stereo") << 
			"<br />";
	
	ss << "Curr. FPS: " << mixb.avg_fps << "<br />";
	ss << "State: ";
	switch(m_mixer->get_audio_status()) {
		case SDL_AUDIO_STOPPED: ss << "stopped"; break;
		case SDL_AUDIO_PLAYING: ss << "playing"; break;
		case SDL_AUDIO_PAUSED: ss << "paused"; break;
		default: ss << "unknown!"; break;
	}
	ss << "<br />";
	ss << "Buffer size: " << m_mixer->get_buffer_read_avail() << "<br />";
	ss << "Delay (us): " << m_mixer->get_buffer_read_avail_us() << "<br />";
	
	m_divs.state->SetInnerRML(ss.str().c_str());
	
	for(auto &ch : m_channels) {
		if(ch.ch->is_enabled()) {
			ch.enabled->SetClass("enabled", true);
		} else {
			ch.enabled->SetClass("enabled", false);
		}
		ss.str("");
		ss << ch.ch->in().spec().channels << "c ";
		switch(ch.ch->in().spec().format) {
			case AUDIO_FORMAT_U8: ss << "U8"; break;
			case AUDIO_FORMAT_S16: ss << "S16"; break;
			case AUDIO_FORMAT_F32: ss << "F32"; break;
		}
		ss << " " << unsigned(round(ch.ch->in().spec().rate)) << "Hz";
		ch.in_format->SetInnerRML(ss.str().c_str());
		static std::string str(20,0);
		ch.in_frames->SetInnerRML(str_format(str, "%d", ch.ch->in().frames()));
		ch.in_us->SetInnerRML(str_format(str, "%.0f",ch.ch->in().duration_us()));
		ch.out_frames->SetInnerRML(str_format(str, "%d",ch.ch->out().frames()));
		ch.out_us->SetInnerRML(str_format(str, "%.0f",ch.ch->out().duration_us()));
	}
}

void MixerState::config_changed(bool)
{
	PDEBUGF(LOG_V0, LOG_GUI, "MixerState\n");
	auto chs = m_mixer->dbg_get_channels();
	std::stringstream ss;
	ss << "<tr><th class=\"normal\">Channels</th>";
	ss << "<th>in format</th>";
	ss << "<th class=\"data\">in frames</th>";
	ss << "<th class=\"data\">in us</th>";
	ss << "<th class=\"data\">out frames</th>";
	ss << "<th class=\"data\">out us</th>";
	ss << "</tr>";
	for(auto ch : chs) {
		ss << "<tr><th id=\"" << ch->id() << "\">" << ch->name() << "</th>";
		ss << "<td id=\"" << ch->id() << "_inf\"></td>";
		ss << "<td class=\"data\" id=\"" << ch->id() << "_infr\"></td>";
		ss << "<td class=\"data\" id=\"" << ch->id() << "_inus\"></td>";
		ss << "<td class=\"data\" id=\"" << ch->id() << "_outfr\"></td>";
		ss << "<td class=\"data\" id=\"" << ch->id() << "_outus\"></td>";
		ss << "</tr>";
	}
	m_divs.channels->SetInnerRML(ss.str().c_str());

	m_channels.clear();
	for(auto &ch : chs) {
		m_channels.push_back({
			ch,
			get_element(str_format("%d",ch->id())),
			get_element(str_format("%d_inf",ch->id())),
			get_element(str_format("%d_infr",ch->id())), get_element(str_format("%d_inus",ch->id())),
			get_element(str_format("%d_outfr",ch->id())), get_element(str_format("%d_outus",ch->id())),
		});
	}
}