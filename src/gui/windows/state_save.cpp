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
	GUI_EVT( "entries",  "dblclick",  StateDialog::on_action ),
	GUI_EVT( "mode",     "click",     StateDialog::on_mode ),
	GUI_EVT( "order",    "click",     StateDialog::on_order),
	GUI_EVT( "asc_desc", "click",     StateDialog::on_asc_desc),
	GUI_EVT( "action",   "click",     StateDialog::on_action ),
	GUI_EVT( "delete",   "click",     StateDialog::on_delete ),
	GUI_EVT( "*",        "keydown",   Window::on_keydown )
};

void StateSave::update()
{
	if(m_dirty) {
		StateDialog::update();
		auto newsave = StateDialog::DirEntry::create_element(
				m_wnd, "", {"new_save_entry", "NEW SAVE", "", 0, STATE_RECORD_VERSION} );
		auto first = m_entries_el->GetFirstChild();
		if(first) {
			m_entries_el->InsertBefore(std::move(newsave), first);
		} else {
			m_entries_el->AppendChild(std::move(newsave));
		}
	} else {
		StateDialog::update();
	}
}

void StateSave::action_on_record(std::string _rec_name)
{
	PDEBUGF(LOG_V2, LOG_GUI, "StateSave: id:%s\n", _rec_name.c_str());

	if(!m_action_callbk) {
		assert(false);
		return;
	}
	if(_rec_name == QUICKSAVE_RECORD) {
		m_action_callbk({QUICKSAVE_RECORD, QUICKSAVE_DESC, "", 0, 0});
	} else if(_rec_name == "new_save" || _rec_name == "new_save_entry") {
		m_action_callbk({});
	} else {
		try {
			StateRecord &state = ms_rec_map.at(_rec_name);
			m_action_callbk(state.info());
		} catch(...) {
			PDEBUGF(LOG_V0, LOG_GUI, "StateSave: invalid slot id!\n");
			hide();
		}
	}
}

void StateSave::on_entry(Rml::Event &_ev)
{
	Rml::Element *el = _ev.GetTargetElement();
	Rml::Element *entry = el->GetParentNode();
	std::string id = entry->GetId();

	if(el->IsClassSet("action") || id == "new_save_entry") {
		entry_deselect();
		action_on_record(id);
		return;
	}
	if(el->IsClassSet("delete")) {
		delete_record(id);
		return;
	}
	if(el->IsClassSet("target") && m_entries_el->IsClassSet("list")) {
		entry_select(id, entry);
	}
}
