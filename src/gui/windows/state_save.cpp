/*
 * Copyright (C) 2021-2025  Marco Bortolin
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
#include "gui.h"
#include <dirent.h>
#include <sys/stat.h>
#include <regex>

#include <RmlUi/Core.h>

event_map_t StateSave::ms_evt_map = {
	GUI_EVT( "cancel",   "click",     StateDialog::on_cancel ),
	GUI_EVT( "close",    "click",     StateDialog::on_cancel ),
	GUI_EVT( "entries",  "click",     StateSave::on_entry ),
	GUI_EVT( "entries",  "dblclick",  StateDialog::on_action ),
	GUI_EVT( "entries",  "keydown",   StateDialog::on_entries ),
	GUI_EVT( "entries",  "focus",     StateDialog::on_entries_focus ),
	GUI_EVT( "mode",     "click",     StateDialog::on_mode ),
	GUI_EVT( "order",    "click",     StateDialog::on_order),
	GUI_EVT( "asc_desc", "click",     StateDialog::on_asc_desc),
	GUI_EVT( "new_save", "click",     StateSave::on_new_save),
	GUI_EVT( "action",   "click",     StateDialog::on_action ),
	GUI_EVT( "delete",   "click",     StateDialog::on_delete ),
	GUI_EVT( "*",        "keydown",   StateSave::on_keydown ),
	GUI_EVT( "*",        "keyup",     StateDialog::on_keyup )
};

StateSave::StateSave(GUI * _gui)
: StateDialog(_gui, "state_save.rml")
{
	m_top_entry = {"new_save_entry", "NEW SAVE", "", 0, STATE_RECORD_VERSION};
}

void StateSave::create(std::string _mode, std::string _order, int _zoom)
{
	StateDialog::create(_mode, _order, _zoom);
	m_action_button_el->SetAttribute("aria-label", "save state");
}

void StateSave::on_new_save(Rml::Event &)
{
	action_on_record("new_save");
}

void StateSave::entry_select(Rml::Element *_entry)
{
	StateDialog::entry_select(_entry);
}

void StateSave::entry_select(std::string _name, Rml::Element *_entry, bool _tts_append)
{
	StateDialog::entry_select(_name, _entry, _tts_append);

	if(_name == m_top_entry.name && m_entries_el->IsPseudoClassSet("focus")) {
		speak_entry(nullptr, _entry, _tts_append);
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

void StateSave::speak_entry(const StateRecord *_sr, Rml::Element *_entry_el, bool _append)
{
	unsigned idx = _entry_el->GetAttribute("data-index")->Get<unsigned>(0);
	if(idx == 0) {
		unsigned count = _entry_el->GetAttribute("data-count")->Get<unsigned>(0);
		m_gui->tts().enqueue(
			str_format("1 of %u: create a new save state", count), TTS::Priority::Normal);
	} else {
		StateDialog::speak_entry(_sr, _entry_el, _append);
	}
}

void StateSave::on_entry(Rml::Event &_ev)
{
	Rml::Element *el = _ev.GetTargetElement();
	if(el->GetId() == "entries") {
		entry_deselect();
		return;
	}
	Rml::Element *entry = el->GetParentNode();
	std::string id = entry->GetId();

	if(el->IsClassSet("action") || id == "new_save_entry") {
		if(id == "new_save_entry") {
			entry_deselect();
		} else {
			entry_select(entry);
		}
		action_on_record(id);
		return;
	}
	if(el->IsClassSet("delete")) {
		delete_record(id);
		return;
	}
	if(el->IsClassSet("target")) {
		entry_select(entry);
	}
}

bool StateSave::would_handle(Rml::Input::KeyIdentifier _key, int _mod)
{
	return (
		( _mod == Rml::Input::KM_CTRL && _key == Rml::Input::KeyIdentifier::KI_N ) ||
		( _mod == 0 && _key == Rml::Input::KeyIdentifier::KI_S ) ||
		StateDialog::would_handle(_key, _mod)
	);
}

void StateSave::on_keydown(Rml::Event &_ev)
{
	auto id = get_key_identifier(_ev);
	switch(id) {
		case Rml::Input::KeyIdentifier::KI_N:
			if(_ev.GetParameter<bool>("ctrl_key", false)) {
				action_on_record("new_save");
			}
			break;
		case Rml::Input::KeyIdentifier::KI_S:
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