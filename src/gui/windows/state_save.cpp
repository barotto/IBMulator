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
	GUI_EVT( "cancel",   "click",     StateDialog::on_cancel ),
	GUI_EVT( "close",    "click",     StateDialog::on_cancel ),
	GUI_EVT( "entries",  "click",     StateSave::on_entry ),
	GUI_EVT( "entries",  "mouseover", StateDialog::on_entry_over ),
	GUI_EVT( "entries",  "mouseout",  StateDialog::on_entry_out ),
	GUI_EVT( "new_save", "click",     StateSave::on_entry ),
	GUI_EVT( "mode",     "click",     StateDialog::on_mode ),
	GUI_EVT( "order",    "click",     StateDialog::on_order),
	GUI_EVT( "*",        "keydown",   Window::on_keydown )
};

void StateSave::on_entry(Rml::Event &_ev)
{
	auto id = _ev.GetTargetElement()->GetId();
	if(id.empty() || id == "entries") {
		return;
	}
	PDEBUGF(LOG_V2, LOG_GUI, "StateSave: id:%s\n", id.c_str());

	if(!m_action_callbk) {
		assert(false);
		return;
	}
	if(id == QUICKSAVE_RECORD) {
		m_action_callbk({QUICKSAVE_RECORD, QUICKSAVE_DESC, "", 0});
	} else if(id == "new_save" || id == "new_save_entry") {
		m_action_callbk({});
	} else {
		try {
			StateRecord &state = ms_rec_map.at(id);
			m_action_callbk(state.info());
		} catch(...) {
			PDEBUGF(LOG_V0, LOG_GUI, "StateSave: invalid entry id!\n");
			hide();
		}
	}
}
