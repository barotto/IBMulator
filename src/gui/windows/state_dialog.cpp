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
#include "state_dialog.h"
#include "filesys.h"
#include "utils.h"
#include "gui/gui.h"
#include "program.h"
#include <dirent.h>
#include <sys/stat.h>
#include <regex>
#include <RmlUi/Core.h>

void StateDialog::create(std::string _mode, std::string _order, int _zoom)
{
	Window::create();

	m_panel_el = get_element("panel");
	m_panel_screen_el = get_element("panel_screen");
	m_panel_config_el = get_element("panel_config");
	m_buttons_entry_el = get_element("buttons_entry");
	m_action_button_el = get_element("action");

	m_max_zoom = MAX_ZOOM;
	m_min_zoom = MIN_ZOOM;
	ItemsDialog::create(_mode, _zoom, "entries", "entries");

	if(_order == "date") {
		m_order = Order::BY_DATE;
		get_element("order_date")->SetAttribute("checked", true);
	} else if(_order == "title" || _order == "desc") {
		m_order = Order::BY_DESC;
		get_element("order_title")->SetAttribute("checked", true);
	} else if(_order == "slot") {
		m_order = Order::BY_SLOT;
		get_element("order_slot")->SetAttribute("checked", true);
	} else {
		m_order = Order::BY_DATE;
		get_element("order_date")->SetAttribute("checked", true);
	}
}

void StateDialog::show()
{
	Window::show();
	m_entries_focus = true;
}

void StateDialog::on_focus(Rml::Event &_ev)
{
	Window::on_focus(_ev);

	if(_ev.GetTargetElement() == m_wnd) {
		m_shown = true;
	}
}

void StateDialog::update()
{
	Window::update();

	std::string prev_selected;
	bool first_focus = m_dirty;
	if(m_dirty) {
		prev_selected = m_selected_name;
		entry_deselect();
		m_entries_el->SetInnerRML("");
		unsigned count = 0;
		unsigned extra_entry = m_top_entry.version != 0;
		switch(m_order) {
			case Order::BY_DATE: {
				count = ms_cur_dir_date.size() + extra_entry;
				unsigned curr = extra_entry;
				if(m_order_ascending) {
					for(auto &de : ms_cur_dir_date) {
						m_entries_el->AppendChild(de.create_element(m_wnd, curr++, count));
					}
				} else {
					auto it = ms_cur_dir_date.rbegin();
					for(;it != ms_cur_dir_date.rend(); it++) {
						m_entries_el->AppendChild(it->create_element(m_wnd, curr++, count));
					}
				}
				break;
			}
			case Order::BY_DESC: {
				count = ms_cur_dir_desc.size() + extra_entry;
				unsigned curr = extra_entry;
				if(m_order_ascending) {
					for(auto &de : ms_cur_dir_desc) {
						m_entries_el->AppendChild(de.create_element(m_wnd, curr++, count));
					}
				} else {
					auto it = ms_cur_dir_desc.rbegin();
					for(;it != ms_cur_dir_desc.rend(); it++) {
						m_entries_el->AppendChild(it->create_element(m_wnd, curr++, count));
					}
				}
				break;
			}
			case Order::BY_SLOT: {
				count = ms_cur_dir_slot.size() + extra_entry;
				unsigned curr = extra_entry;
				if(m_order_ascending) {
					for(auto &de : ms_cur_dir_slot) {
						m_entries_el->AppendChild(de.create_element(m_wnd, curr++, count));
					}
				} else {
					auto it = ms_cur_dir_slot.rbegin();
					for(;it != ms_cur_dir_slot.rend(); it++) {
						m_entries_el->AppendChild(it->create_element(m_wnd, curr++, count));
					}
				}
				break;
			}
		}
		if(m_top_entry.version) {
			auto top_entry = StateDialog::DirEntry::create_element(
					m_wnd, "", m_top_entry, 0, count );
			auto first = m_entries_el->GetFirstChild();
			if(first) {
				m_entries_el->InsertBefore(std::move(top_entry), first);
			} else {
				m_entries_el->AppendChild(std::move(top_entry));
			}
		}
		m_dirty = false;
		if(!prev_selected.empty()) {
			auto entry_el = m_entries_el->GetElementById(prev_selected);
			if(entry_el) {
				entry_select(prev_selected, entry_el);
			}
		}
	}
	if(!m_lazy_select.empty()) {
		auto entry_el = m_entries_el->GetElementById(m_lazy_select);
		if(entry_el) {
			entry_select(m_lazy_select, entry_el);
			m_entries_el->Focus();
		}
		m_lazy_select = "";
	} else if(prev_selected.empty() && (first_focus || m_entries_focus)) {
		m_entries_el->Focus();
	}
	if(m_dirty_scroll) {
		if(m_selected_entry) {
			scroll_vertical_into_view(m_selected_entry);
		} else {
			m_entries_cont_el->SetScrollTop(0);
		}
		m_dirty_scroll--;
	}
	m_entries_focus = false;
}

void StateDialog::set_current_dir(const std::string &_path)
{
	ms_cur_dir_date.clear();
	ms_cur_dir_desc.clear();
	ms_cur_dir_slot.clear();
	ms_rec_map.clear();
	if(!_path.empty()) {
		ms_cur_path = _path;
	}

	if(_path.empty() && ms_cur_path.empty()) {
		return;
	}

	DIR *dir;
	if((dir = FileSys::opendir(ms_cur_path.c_str())) == nullptr) {
		throw std::runtime_error(
			str_format("Cannot open directory '%s' for reading", ms_cur_path.c_str()).c_str()
		);
	}

	std::regex re("^" STATE_RECORD_BASE ".*", std::regex::ECMAScript|std::regex::icase);
	struct dirent *ent;
	while((ent = readdir(dir)) != nullptr) {
		std::string dirname = FileSys::to_utf8(ent->d_name);
		if(!std::regex_search(dirname, re)) {
			continue;
		}
		try {
			auto p = ms_rec_map.emplace(std::make_pair(dirname, StateRecord(ms_cur_path, dirname)));
			if(p.second) {
				ms_cur_dir_date.emplace(&(p.first->second));
				ms_cur_dir_desc.emplace(&(p.first->second));
				ms_cur_dir_slot.emplace(&(p.first->second));
			}
		} catch(std::runtime_error &e) {
			PDEBUGF(LOG_V1, LOG_GUI, "  %s\n", e.what());
		}
	}

	closedir(dir);
}

void StateDialog::set_selection(std::string _slot_id)
{
	m_lazy_select = _slot_id;
}

std::pair<const StateRecord*,Rml::Element*> StateDialog::get_sr_entry(Rml::Element *target_el)
{
	if(target_el->GetId() == "entries") {
		return std::make_pair(nullptr,nullptr);
	}

	Rml::Element *entry_el = ItemsDialog::get_entry(target_el);
	if(!entry_el) {
		return std::make_pair(nullptr,nullptr);
	}

	auto pair = ms_rec_map.find(entry_el->GetId());
	if(pair == ms_rec_map.end()) {
		return std::make_pair(nullptr,nullptr);
	}

	return std::make_pair(&pair->second, entry_el);
}

std::pair<const StateRecord*,Rml::Element*> StateDialog::get_sr_entry(Rml::Event &_ev)
{
	return StateDialog::get_sr_entry(_ev.GetTargetElement());
}

void StateDialog::entry_select(Rml::Element *_entry_el)
{
	entry_deselect();
	auto entry = ItemsDialog::get_entry(_entry_el);
	entry_select(entry->GetId(), entry, false);
}

void StateDialog::entry_select(std::string _name, Rml::Element *_entry, bool _tts_append)
{
	ItemsDialog::entry_select(_entry);

	m_selected_name = _name;

	m_panel_config_el->SetInnerRML("");
	
	auto pair = ms_rec_map.find(_name);
	if(pair == ms_rec_map.end()) {
		return;
	}

	const StateRecord *sr = &pair->second;

	if(!sr->screen().empty()) {
		// adding an additional '/' because RmlUI strips it off for unknown reasons
		// https://github.com/mikke89/RmlUi/issues/161
		m_panel_screen_el->SetInnerRML("<img src=\"/" + sr->screen() + "\" />");
		m_panel_screen_el->SetClass("invisible", false);
	}
	if(sr->info().version != STATE_RECORD_VERSION) {
		m_panel_config_el->SetInnerRML("INVALID VERSION (" + str_to_html(StateRecord::get_version_to_release_string(sr->info().version)) + ")");
		m_panel_config_el->SetClass("invisible", false);
	} else if(!sr->info().config_desc.empty()) {
		m_panel_config_el->SetInnerRML(str_to_html(sr->info().config_desc));
		m_panel_config_el->SetClass("invisible", false);
	}
	m_buttons_entry_el->SetClass("invisible", false);
	if(sr->info().version != STATE_RECORD_VERSION) {
		m_action_button_el->SetClass("invisible", true);
	} else {
		m_action_button_el->SetClass("invisible", false);
	}

	if(m_entries_el->IsPseudoClassSet("focus")) {
		if(m_moving_selection) {
			m_gui->tts().stop();
		} else {
			speak_entry(sr, _entry, _tts_append);
		}
	}
}

void StateDialog::entry_deselect()
{
	ItemsDialog::entry_deselect();

	m_selected_name = "";

	m_buttons_entry_el->SetClass("invisible", true);
	m_panel_screen_el->SetInnerRML("");
	m_panel_screen_el->SetClass("invisible", true);
	m_panel_config_el->SetInnerRML("");
	m_panel_config_el->SetClass("invisible", true);
}

void StateDialog::set_mode(std::string _mode)
{
	auto old_mode = get_mode();
	ItemsDialog::set_mode(_mode);

	m_panel_el->SetClassNames(_mode);

	if(!m_selected_name.empty()) {
		m_dirty_scroll = 2;
	}

	if(is_visible() && old_mode != _mode) {
		m_gui->tts().enqueue(get_mode() + " view active.");
	}
}

void StateDialog::on_mode(Rml::Event &_ev)
{
	set_mode(Window::get_form_input_value(_ev));
}

void StateDialog::on_order(Rml::Event &_ev)
{
	std::string value = Window::get_form_input_value(_ev);
	if(!value.empty()) {
		if(value == "date") {
			m_order = Order::BY_DATE;
		} else if(value == "title") {
			m_order = Order::BY_DESC;
		} else if(value == "slot") {
			m_order = Order::BY_SLOT;
		} else {
			PERRF(LOG_GUI, "Invalid order: %s\n", value.c_str());
			return;
		}
		m_dirty = true;
		m_dirty_scroll = 2;
	}
}

void StateDialog::on_asc_desc(Rml::Event &_ev)
{
	std::string value = Window::get_form_input_value(_ev);
	if(!value.empty()) {
		if(value == "asc") {
			m_order_ascending = true;
		} else if(value == "desc") {
			m_order_ascending = false;
		} else {
			PERRF(LOG_GUI, "Invalid order: %s\n", value.c_str());
			return;
		}
		m_dirty = true;
		m_dirty_scroll = 2;
	}
}

void StateDialog::on_entries(Rml::Event &_ev)
{
	auto id = get_key_identifier(_ev);
	switch(id) {
		case Rml::Input::KeyIdentifier::KI_RETURN:
		case Rml::Input::KeyIdentifier::KI_NUMPADENTER:
		{
			if(!m_selected_name.empty()) {
				action_on_record(m_selected_name);
			}
			break;
		}
		default:
			on_keydown(_ev);
			return;
	}
	_ev.StopImmediatePropagation();
}

void StateDialog::on_entries_focus(Rml::Event &)
{
	speak_entries(m_shown);
}

bool StateDialog::would_handle(Rml::Input::KeyIdentifier _key, int _mod)
{
	return (
		( _mod == 0 && _key == Rml::Input::KeyIdentifier::KI_DELETE ) ||
		ItemsDialog::would_handle(_key, _mod)
	);
}

void StateDialog::on_keydown(Rml::Event &_ev)
{
	auto id = get_key_identifier(_ev);
	switch(id) {
		case Rml::Input::KeyIdentifier::KI_DELETE:
		{
			if(!m_selected_name.empty()) {
				delete_record(m_selected_name);
			}
			break;
		}
		default:
			ItemsDialog::on_keydown(_ev);
			return;
	}
	_ev.StopImmediatePropagation();
}

void StateDialog::on_keyup(Rml::Event &_ev)
{
	if(m_moving_selection && m_selected_entry) {
		speak_entry(get_sr_entry(m_selected_entry).first, m_selected_entry, true);
	}
	ItemsDialog::on_keyup(_ev);
}

void StateDialog::on_cancel(Rml::Event &_ev)
{
	if(m_cancel_callbk) {
		m_cancel_callbk();
	}
	Window::on_cancel(_ev);
	m_shown = false;
}

void StateDialog::on_action(Rml::Event &)
{
	if(!m_selected_name.empty()) {
		action_on_record(m_selected_name);
	}
}

void StateDialog::on_delete(Rml::Event &)
{
	if(!m_selected_name.empty()) {
		delete_record(m_selected_name);
	}
}

void StateDialog::delete_record(std::string _name)
{
	if(m_delete_callbk) {
		try {
			auto &rec = ms_rec_map.at(_name);
			m_delete_callbk(rec.info());
			reload_current_dir();
		} catch(std::out_of_range &) {
			PDEBUGF(LOG_V0, LOG_GUI, "StateDialog: invalid slot id!\n");
		}
	}
}

void StateDialog::set_zoom(int _amount)
{
	ItemsDialog::set_zoom(_amount);

	m_dirty_scroll = 2;
}

void StateDialog::speak_entries(bool _describe)
{
	m_gui->tts().enqueue(get_mode() + " view.");
	if(_describe) {
		speak_content(true);
	}
	if(m_selected_entry) {
		speak_entry(get_sr_entry(m_selected_entry).first, m_selected_entry, true);
	} else if(!is_empty()) {
		m_gui->tts().enqueue("none selected.", TTS::Priority::Low);
	}
}

void StateDialog::speak_entry(const StateRecord *_sr, Rml::Element *_entry_el, bool _append)
{
	if(!_sr) {
		return;
	}

	unsigned idx = _entry_el->GetAttribute("data-index")->Get<unsigned>(0);
	unsigned count = _entry_el->GetAttribute("data-count")->Get<unsigned>(0);
	std::string text;
	if(!_sr->user_desc().empty()) {
		// the user provided description
		text += "Title: " + _sr->user_desc() + "\n";
	}
	// the slot name
	text += "Slot: " + _sr->name() + "\n";
	// the slot time
	text += str_format_time(_sr->mtime(), "Date: %x at %H:%M") + "\n";
	// the slot description
	if(_sr->info().version != STATE_RECORD_VERSION) {
		text += "Invalid version.";
	} else {
		text += _sr->info().config_desc;
	}
	m_gui->tts().enqueue(
		str_format("%u of %u:\n%s", idx+1, count, text.c_str()),
			_append ? TTS::Priority::Low : TTS::Priority::Normal, TTS::BREAK_LINES);
}

void StateDialog::speak_content(bool _append)
{
	std::string content;
	if(!ms_rec_map.empty()) {
		content = str_format("%u %s", ms_rec_map.size(), ms_rec_map.size()>1 ? "items" : "item");
	} else {
		content = "empty";
	}
	m_gui->tts().enqueue(content, _append ? TTS::Priority::Low : TTS::Priority::Normal);
}

void StateDialog::speak_element(Rml::Element *_el, bool _with_label, bool _describe, TTS::Priority _pri)
{
	assert(_el);

	Window::speak_element(_el, _with_label, _describe, _pri);

	if(_el->GetId() == "entries") {
		speak_entries(_describe);
	}
}

Rml::ElementPtr StateDialog::DirEntry::create_element(
		Rml::ElementDocument *_doc,
		const std::string &_screen,
		const StateRecord::Info &_info,
		unsigned _idx, unsigned _count
)
{
	Rml::ElementPtr child = _doc->CreateElement("div");
	child->SetClassNames("entry");
	if(_info.version != STATE_RECORD_VERSION) {
		child->SetClass("version_mismatch", true);
	}
	child->SetId(_info.name);
	child->SetAttribute("data-index", _idx); 
	child->SetAttribute("data-count", _count);

	std::string inner;
	inner += "<div class=\"data\">";
		inner += "<div class=\"screen\">";
		if(!_screen.empty()) {
			// adding an additional '/' because RmlUI strips it off for unknown reasons
			// https://github.com/mikke89/RmlUi/issues/161
			inner += "<img src=\"/" + _screen + "\" />";
		}
		inner += "</div>";
		inner += "<div class=\"desc\">" + str_to_html(_info.user_desc) + "</div>";
		if(_info.mtime) {
			inner += "<div class=\"date\">" + str_format_time(_info.mtime, "%x %H:%M") + "</div>";
		}
		if(_info.name != "new_save_entry") {
			inner += "<div class=\"name\">" + str_to_html(_info.name) + "</div>";
		}
		if(_info.version != STATE_RECORD_VERSION) {
			inner += "<div class=\"config\">INVALID VERSION (" + str_to_html(StateRecord::get_version_to_release_string(_info.version)) + ")</div>";
		} else {
			if(!_info.config_desc.empty()) {
				inner += "<div class=\"config\"><div>" + str_to_html(_info.config_desc) + "</div></div>";
			}
		}
	inner += "</div>";
	inner += "<div class=\"target\"></div>";
	inner += "<div class=\"action\"></div>";
	inner += "<div class=\"delete\"></div>";
	child->SetInnerRML(inner);

	return child;
}

Rml::ElementPtr StateDialog::DirEntry::create_element(Rml::ElementDocument *_doc, unsigned _idx, unsigned _count) const
{
	return create_element(
		_doc,
		rec->screen(),
		rec->info(),
		_idx, _count
	);
}
