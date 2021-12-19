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
#include "state_dialog.h"
#include "filesys.h"
#include "utils.h"
#include "gui/gui.h"
#include "program.h"
#include <dirent.h>
#include <sys/stat.h>
#include <regex>
#include <RmlUi/Core.h>

void StateDialog::create()
{
	Window::create();

	m_entries_el = get_element("entries");
	m_panel_el = get_element("panel");
	m_panel_screen_el = get_element("panel_screen");
	m_panel_config_el = get_element("panel_config");
	m_buttons_entry_el = get_element("buttons_entry");
	m_action_button_el = get_element("action");
	
	set_zoom(0);
}

void StateDialog::update()
{
	Window::update();

	if(m_dirty) {
		auto prev_selected = m_selected_id;
		entry_deselect();
		m_entries_el->SetInnerRML("");
		switch(m_order) {
			case Order::BY_DATE: {
				if(m_order_ascending) {
					for(auto &de : ms_cur_dir_date) {
						m_entries_el->AppendChild(de.create_element(m_wnd));
					}
				} else {
					auto it = ms_cur_dir_date.rbegin();
					for(;it != ms_cur_dir_date.rend(); it++) {
						m_entries_el->AppendChild(it->create_element(m_wnd));
					}
				}
				break;
			}
			case Order::BY_DESC: {
				if(m_order_ascending) {
					for(auto &de : ms_cur_dir_desc) {
						m_entries_el->AppendChild(de.create_element(m_wnd));
					}
				} else {
					auto it = ms_cur_dir_desc.rbegin();
					for(;it != ms_cur_dir_desc.rend(); it++) {
						m_entries_el->AppendChild(it->create_element(m_wnd));
					}
				}
				break;
			}
			case Order::BY_SLOT: {
				if(m_order_ascending) {
					for(auto &de : ms_cur_dir_slot) {
						m_entries_el->AppendChild(de.create_element(m_wnd));
					}
				} else {
					auto it = ms_cur_dir_slot.rbegin();
					for(;it != ms_cur_dir_slot.rend(); it++) {
						m_entries_el->AppendChild(it->create_element(m_wnd));
					}
				}
				break;
			}
		}
		m_dirty = false;
		if(prev_selected != "") {
			auto entry = m_entries_el->GetElementById(prev_selected);
			if(entry) {
				entry_select(prev_selected, entry);
			}
		}
	}
	if(!m_lazy_select.empty()) {
		auto entry_el = m_entries_el->GetElementById(m_lazy_select);
		if(entry_el) {
			entry_select(m_lazy_select, entry_el);
		}
		m_lazy_select = "";
	}
	if(m_dirty_scroll) {
		if(m_selected_entry) {
			scroll_vertical_into_view(m_selected_entry);
		}
		m_dirty_scroll--;
	}
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

void StateDialog::entry_select(std::string _slot_id)
{
	m_lazy_select = _slot_id;
}

void StateDialog::entry_select(std::string _rec_name, Rml::Element *_entry)
{
	try {
		entry_deselect();

		auto &rec = ms_rec_map.at(_rec_name);

		m_selected_entry = _entry;
		m_selected_entry->SetClass("selected", true);
		m_selected_id = _rec_name;

		m_panel_config_el->SetInnerRML("");
		if(!rec.screen().empty()) {
			// adding an additional '/' because RmlUI strips it off for unknown reasons
			// https://github.com/mikke89/RmlUi/issues/161
			m_panel_screen_el->SetInnerRML("<img src=\"/" + rec.screen() + "\" />");
			m_panel_screen_el->SetClass("invisible", false);
		}
		if(rec.info().version != STATE_RECORD_VERSION) {
			m_panel_config_el->SetInnerRML("INVALID VERSION");
			m_panel_config_el->SetClass("invisible", false);
		} else if(!rec.info().config_desc.empty()) {
			m_panel_config_el->SetInnerRML(str_to_html(rec.info().config_desc));
			m_panel_config_el->SetClass("invisible", false);
		}
		m_buttons_entry_el->SetClass("invisible", false);
		if(rec.info().version != STATE_RECORD_VERSION) {
			m_action_button_el->SetClass("invisible", true);
		} else {
			m_action_button_el->SetClass("invisible", false);
		}

		scroll_vertical_into_view(_entry);

	} catch(std::out_of_range &) {
		PDEBUGF(LOG_V0, LOG_GUI, "StateDialog: invalid id!\n");
	}
}

void StateDialog::entry_deselect()
{
	if(m_selected_entry) {
		m_selected_entry->SetClass("selected", false);
		m_selected_id = "";
	}
	m_selected_entry = nullptr;
	m_panel_screen_el->SetInnerRML("");
	m_panel_screen_el->SetClass("invisible", true);
	m_panel_config_el->SetInnerRML("");
	m_panel_config_el->SetClass("invisible", true);
	m_buttons_entry_el->SetClass("invisible", true);
}

void StateDialog::on_mode(Rml::Event &_ev)
{
	std::string value = Window::get_form_input_value(_ev);
	if(!value.empty()) {
		m_entries_el->SetClass("list", false);
		m_entries_el->SetClass("grid", false);
		m_entries_el->SetClass(value, true);
		m_panel_el->SetClassNames(value);
	}
	if(m_selected_id != "") {
		auto el = m_entries_el->GetElementById(m_selected_id);
		if(el) {
			m_selected_entry = el;
			m_selected_entry->SetClass("selected", true);
			m_dirty_scroll = 2;
			return;
		}
	}
	entry_deselect();
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

void StateDialog::on_cancel(Rml::Event &_ev)
{
	if(m_cancel_callbk) {
		m_cancel_callbk();
	}
	Window::on_cancel(_ev);
}

void StateDialog::on_action(Rml::Event &)
{
	if(!m_selected_id.empty()) {
		action_on_record(m_selected_id);
	}
}

void StateDialog::on_delete(Rml::Event &)
{
	if(!m_selected_id.empty()) {
		delete_record(m_selected_id);
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
	m_entries_el->SetClass(str_format("zoom-%d", m_zoom), false);
	m_zoom += _amount;
	m_zoom = std::min(2, m_zoom);
	m_zoom = std::max(0, m_zoom);
	m_entries_el->SetClass(str_format("zoom-%d", m_zoom), true);
	m_dirty_scroll = 2;
}

void StateDialog::on_keydown(Rml::Event &_ev)
{
	switch(get_key_identifier(_ev)) {
		case Rml::Input::KeyIdentifier::KI_OEM_MINUS:
		case Rml::Input::KeyIdentifier::KI_SUBTRACT:
			set_zoom(-1);
			break;
		case Rml::Input::KeyIdentifier::KI_OEM_PLUS:
		case Rml::Input::KeyIdentifier::KI_ADD:
			set_zoom(1);
			break;
		default:
			Window::on_keydown(_ev);
			break;
	}
}

Rml::ElementPtr StateDialog::DirEntry::create_element(
		Rml::ElementDocument *_doc,
		const std::string &_screen,
		const StateRecord::Info &_info
)
{
	Rml::ElementPtr child = _doc->CreateElement("div");
	child->SetClassNames("entry");
	if(_info.version != STATE_RECORD_VERSION) {
		child->SetClass("version_mismatch", true);
	}
	child->SetId(_info.name);

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
			inner += "<div class=\"config\">INVALID VERSION</div>";
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

Rml::ElementPtr StateDialog::DirEntry::create_element(Rml::ElementDocument *_doc) const
{
	return create_element(
		_doc,
		rec->screen(),
		rec->info()
	);
}
