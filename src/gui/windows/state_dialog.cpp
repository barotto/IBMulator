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
}

void StateDialog::update()
{
	Window::update();
	if(ms_dirty) {
		Rml::ReleaseTextures();
		ms_dirty = false;
	}
	if(m_dirty) {
		m_entries_el->SetInnerRML("");
		/*
		m_entries_el->AppendChild(StateDialog::DirEntry::create_element(
			m_wnd, "", {"new_save_entry", "NEW SAVE", "", 0} ));
		*/
		switch(m_order) {
			case Order::BY_DATE: {
				for(auto &de : ms_cur_dir_date) {
					m_entries_el->AppendChild(de.create_element(m_wnd));
				}
				break;
			}
			case Order::BY_DESC: {
				for(auto &de : ms_cur_dir_desc) {
					m_entries_el->AppendChild(de.create_element(m_wnd));
				}
				break;
			}
			case Order::BY_SLOT: {
				for(auto &de : ms_cur_dir_slot) {
					m_entries_el->AppendChild(de.create_element(m_wnd));
				}
				break;
			}
		}
		m_dirty = false;
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
	if((dir = opendir(ms_cur_path.c_str())) == nullptr) {
		throw std::runtime_error(
			str_format("Cannot open directory '%s' for reading", ms_cur_path.c_str()).c_str()
		);
	}

	std::regex re("^" STATE_RECORD_BASE ".*", std::regex::ECMAScript|std::regex::icase);
	struct dirent *ent;
	while((ent = readdir(dir)) != nullptr) {
		std::string dirname = ent->d_name;
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

void StateDialog::on_entry_over(Rml::Event &_ev)
{
	auto id = _ev.GetTargetElement()->GetId();
	if(id.empty() || id == "entries") {
		return;
	}

	try {
		auto &rec = ms_rec_map.at(id);
		m_panel_config_el->SetInnerRML("");
		if(!rec.screen().empty()) {
			// adding an additional '/' because RmlUI strips it off for unknown reasons
			// https://github.com/mikke89/RmlUi/issues/161
			m_panel_screen_el->SetInnerRML("<img src=\"/" + rec.screen() + "\" />");
			m_panel_screen_el->SetClass("visible", true);
		}
		if(!rec.info().config_desc.empty()) {
			m_panel_config_el->SetInnerRML(rec.info().config_desc);
			m_panel_config_el->SetClass("visible", true);
		}
	} catch(std::out_of_range &) {
		PDEBUGF(LOG_V0, LOG_GUI, "StateDialog: invalid id!\n");
	}
}

void StateDialog::on_entry_out(Rml::Event &_ev)
{
	m_panel_screen_el->SetClass("visible", false);
	m_panel_config_el->SetClass("visible", false);
}

void StateDialog::on_mode(Rml::Event &_ev)
{
	std::string value = Window::get_form_input_value(_ev);
	if(!value.empty()) {
		m_entries_el->SetClassNames(value);
		m_panel_el->SetClassNames(value);
	}
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
		update();
	}
}

void StateDialog::on_cancel(Rml::Event &_ev)
{
	if(m_cancel_callbk) {
		m_cancel_callbk();
	}
	Window::on_cancel(_ev);
}

Rml::ElementPtr StateDialog::DirEntry::create_element(
		Rml::ElementDocument *_doc,
		const std::string &_screen,
		const StateRecord::Info &_info
)
{
	Rml::ElementPtr child = _doc->CreateElement("div");
	child->SetClassNames("entry");

	std::string inner;
	inner += "<div class=\"data\">";
		inner += "<div class=\"screen\">";
		if(!_screen.empty()) {
			// adding an additional '/' because RmlUI strips it off for unknown reasons
			// https://github.com/mikke89/RmlUi/issues/161
			inner += "<img src=\"/" + _screen + "\" />";
		}
		inner += "</div>";
		inner += "<div class=\"desc\">" + _info.user_desc + "</div>";
		if(_info.mtime) {
			inner += "<div class=\"date\">" + str_format_time(_info.mtime, "%x %R") + "</div>";
		}
		if(_info.name != "new_save_entry") {
			inner += "<div class=\"name\">" + _info.name + "</div>";
		}
		if(!_info.config_desc.empty()) {
			inner += "<div class=\"config\">" + _info.config_desc + "</div>";
		}
		inner += "<div class=\"action\"></div>";
	inner += "</div>";
	inner += "<div class=\"target\" id=\"" + _info.name + "\"></div>";
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
