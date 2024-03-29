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
	GUI_EVT( "cancel",  "click",     StateDialog::on_cancel ),
	GUI_EVT( "close",   "click",     StateDialog::on_cancel ),
	GUI_EVT( "entries", "click",     StateLoad::on_entry ),
	GUI_EVT( "entries", "dblclick",  StateDialog::on_action ),
	GUI_EVT( "entries", "keydown",   StateDialog::on_entries ),
	GUI_EVT( "mode",    "click",     StateDialog::on_mode ),
	GUI_EVT( "order",   "click",     StateDialog::on_order ),
	GUI_EVT( "asc_desc","click",     StateDialog::on_asc_desc),
	GUI_EVT( "action",  "click",     StateDialog::on_action ),
	GUI_EVT( "delete",  "click",     StateDialog::on_delete ),
	GUI_EVT( "*",       "keydown",   StateDialog::on_keydown ),
};

void StateLoad::create(std::string _mode, std::string _order, int _zoom)
{
	StateDialog::create(_mode, _order, _zoom);
	get_element("extra_btns")->RemoveChild(get_element("new_save"));
}

void StateLoad::action_on_record(std::string _rec_name)
{
	PDEBUGF(LOG_V2, LOG_GUI, "StateLoad: id:%s\n", _rec_name.c_str());

	if(!m_action_callbk) {
		assert(false);
		return;
	}
	try {
		auto &rec = ms_rec_map.at(_rec_name);
		if(rec.info().version == STATE_RECORD_VERSION) {
			m_action_callbk(rec.info());
		}
	} catch(std::out_of_range &) {
		PDEBUGF(LOG_V0, LOG_GUI, "StateLoad: invalid slot id!\n");
	}
}

void StateLoad::on_entry(Rml::Event &_ev)
{
	Rml::Element *el = _ev.GetTargetElement();
	if(el->GetId() == "entries") {
		entry_deselect();
		return;
	}
	Rml::Element *entry = el->GetParentNode();

	if(el->IsClassSet("action")) {
		entry_select(entry);
		action_on_record(entry->GetId());
		return;
	}
	if(el->IsClassSet("delete")) {
		delete_record(entry->GetId());
		return;
	}
	if(el->IsClassSet("target")) {
		entry_select(entry);
	}
}

void StateLoad::on_keydown(Rml::Event &_ev)
{
	auto id = get_key_identifier(_ev);
	switch(id) {
		case Rml::Input::KeyIdentifier::KI_L:
			if(!m_selected_name.empty()) {
				action_on_record(m_selected_name);
			}
			break;
		default:
			StateDialog::on_keydown(_ev);
			return;
	}
	_ev.StopImmediatePropagation();
}
