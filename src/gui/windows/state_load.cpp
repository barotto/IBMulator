/*
 * Copyright (C) 2021  Marco Bortolin
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
#include "state_load.h"
#include "filesys.h"
#include <dirent.h>
#include <sys/stat.h>
#include <regex>

#include <RmlUi/Core.h>

event_map_t StateLoad::ms_evt_map = {
	GUI_EVT( "cancel", "click",   StateLoad::on_cancel ),
	GUI_EVT( "close",  "click",   StateLoad::on_cancel ),
	GUI_EVT( "list",   "click",   StateLoad::on_entry  ),
	GUI_EVT( "*",      "keydown", Window::on_keydown   )
};

void StateLoad::update()
{
	StateDialog::update();
	if(m_dirty) {
		m_list_el->SetInnerRML("");
		for(auto &de : ms_cur_dir) {
			m_list_el->AppendChild(de.create_element(m_wnd));
		}
		m_dirty = false;
	}
}

void StateLoad::on_entry(Rml::Event &_ev)
{
	auto id = _ev.GetTargetElement()->GetId();
	if(id.empty() || id == "list") {
		return;
	}
	PDEBUGF(LOG_V2, LOG_GUI, "StateLoad: id:%s\n", id.c_str());

	if(!m_load_callbk) {
		assert(false);
		return;
	}
	try {
		m_load_callbk(ms_rec_map.at(id).info());
	} catch(std::out_of_range &) {
		PDEBUGF(LOG_V0, LOG_GUI, "StateLoad: invalid id!\n");
	}
}

void StateLoad::on_cancel(Rml::Event &_ev)
{
	if(m_cancel_callbk) {
		m_cancel_callbk();
	}
	Window::on_cancel(_ev);
}