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
#include "state_save.h"
#include "filesys.h"
#include <dirent.h>
#include <sys/stat.h>
#include <regex>

#include <RmlUi/Core.h>

event_map_t StateSave::ms_evt_map = {
	GUI_EVT( "cancel",   "click",   StateSave::on_cancel ),
	GUI_EVT( "close",    "click",   StateSave::on_cancel ),
	GUI_EVT( "list",     "click",   StateSave::on_entry  ),
	GUI_EVT( "new_save", "click",   StateSave::on_entry  ),
	GUI_EVT( "*",        "keydown", Window::on_keydown   )
};

void StateSave::update()
{
	StateDialog::update();

	if(m_dirty) {
		m_list_el->SetInnerRML("");
		m_list_el->AppendChild(StateDialog::DirEntry::create_element(
			m_wnd, "", {"new_save_entry", "NEW SAVE", "", 0} ));
		for(auto &de : ms_cur_dir) {
			m_list_el->AppendChild(de.create_element(m_wnd));
		}
		m_dirty = false;
	}
}

void StateSave::on_cancel(Rml::Event &_ev)
{
	if(m_cancel_callbk) {
		m_cancel_callbk();
	}
	StateDialog::on_cancel(_ev);
}

void StateSave::on_entry(Rml::Event &_ev)
{
	auto id = _ev.GetTargetElement()->GetId();
	if(id.empty() || id == "list") {
		return;
	}
	PDEBUGF(LOG_V2, LOG_GUI, "StateSave: id:%s\n", id.c_str());

	if(!m_save_callbk) {
		assert(false);
		return;
	}
	if(id == QUICKSAVE_RECORD) {
		m_save_callbk({QUICKSAVE_RECORD, QUICKSAVE_DESC, "", 0});
	} else if(id == "new_save" || id == "new_save_entry") {
		m_save_callbk({});
	} else {
		try {
			StateRecord &state = ms_rec_map.at(id);
			m_save_callbk(state.info());
		} catch(...) {
			PDEBUGF(LOG_V0, LOG_GUI, "StateSave: invalid entry id!\n");
			hide();
		}
	}
}

