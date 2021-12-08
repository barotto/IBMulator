/*
 * Copyright (C) 2015-2021  Marco Bortolin
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
#include "gui.h"
#include "filesys.h"
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <regex>
#include <limits.h>
#include <stdlib.h>

#include <RmlUi/Core.h>
#include <RmlUi/Core/TypeConverter.h>

#ifdef _WIN32
#include "wincompat.h"
#endif

event_map_t FileSelect::ms_evt_map = {
	GUI_EVT( "cancel",  "click",   FileSelect::on_cancel ),
	GUI_EVT( "close",   "click",   FileSelect::on_cancel ),
	GUI_EVT( "entries", "click",   FileSelect::on_entry ),
	GUI_EVT( "entries", "dblclick",FileSelect::on_insert ),
	GUI_EVT( "insert",  "click",   FileSelect::on_insert ),
	GUI_EVT( "drive",   "click",   FileSelect::on_drive ),
	GUI_EVT( "mode",    "click",   FileSelect::on_mode ),
	GUI_EVT( "order",   "click",   FileSelect::on_order ),
	GUI_EVT( "asc_desc","click",   FileSelect::on_asc_desc),
	GUI_EVT( "reload",  "click",   FileSelect::on_reload),
	GUI_EVT( "home",    "click",   FileSelect::on_home),
	GUI_EVT( "dir_up",  "click",   FileSelect::on_up),
	GUI_EVT( "dir_prev","click",   FileSelect::on_prev),
	GUI_EVT( "dir_next","click",   FileSelect::on_next),
	GUI_EVT( "show_panel","click", FileSelect::on_show_panel),
	GUI_EVT( "*",       "keydown", FileSelect::on_keydown )
};

FileSelect::FileSelect(GUI * _gui)
:
Window(_gui, "fileselect.rml")
{
}

FileSelect::~FileSelect()
{
}

void FileSelect::create()
{
	Window::create();

	m_entries_el = get_element("entries");
	m_entries_cont_el = get_element("entries_container");
	m_panel_el = get_element("info_panel");
	m_buttons_entry_el = get_element("buttons_entry");
	m_wprotect = dynamic_cast<Rml::ElementFormControl*>(get_element("wprotect"));
	
	auto drive_el = get_element("drive");
	unsigned drives_mask = 0;
#ifdef _WIN32
	drives_mask = GetLogicalDrives();
#endif
	m_wnd->SetClass("drives", bool(drives_mask));
	if(drives_mask) {
		for(char drvlett = 'A'; drvlett <= 'Z'; drvlett++,drives_mask>>=1) {
			if(drives_mask & 1) {
				Rml::ElementPtr btn = m_wnd->CreateElement("input");
				btn->SetId(str_format("drive_%c", drvlett));
				btn->SetAttribute("type", "radio");
				btn->SetAttribute("name", "drive");
				btn->SetAttribute("value", str_format("%c", drvlett));
				btn->SetInnerRML(str_format("<span>%c</span>", drvlett));
				drive_el->AppendChild(std::move(btn));
				PDEBUGF(LOG_V1, LOG_GUI, "%c\n", drvlett);
			}
		}
	}
	
	m_path_el.cwd = get_element("cwd");
	m_path_el.up = get_element("dir_up");
	m_path_el.prev = get_element("dir_prev");
	m_path_el.next = get_element("dir_next");
	
	disable(m_path_el.prev);
	disable(m_path_el.next);
	
	set_zoom(0);
}

void FileSelect::update()
{
	Window::update();

	if(m_dirty) {
		set_disabled(m_path_el.up, get_up_path().empty());
		auto prev_selected = m_selected_id;
		entry_deselect();
		m_entries_el->SetInnerRML("");
		switch(m_order) {
			case Order::BY_DATE: {
				if(m_order_ascending) {
					for(auto de : m_cur_dir_date) {
						m_entries_el->AppendChild(de->create_element(m_wnd));
					}
				} else {
					if(m_dotdot) {
						m_entries_el->AppendChild(m_dotdot->create_element(m_wnd));
					}
					auto it = m_cur_dir_date.rbegin();
					for(;it != m_cur_dir_date.rend(); it++) {
						if((*it)->is_dir && (*it)->name != "..") {
							m_entries_el->AppendChild((*it)->create_element(m_wnd));
						}
					}
					it = m_cur_dir_date.rbegin();
					for(;it != m_cur_dir_date.rend(); it++) {
						if(!(*it)->is_dir) {
							m_entries_el->AppendChild((*it)->create_element(m_wnd));
						}
					}
				}
				break;
			}
			case Order::BY_NAME: {
				if(m_order_ascending) {
					for(auto de : m_cur_dir_name) {
						m_entries_el->AppendChild(de->create_element(m_wnd));
					}
				} else {
					if(m_dotdot) {
						m_entries_el->AppendChild(m_dotdot->create_element(m_wnd));
					}
					auto it = m_cur_dir_name.rbegin();
					for(;it != m_cur_dir_name.rend(); it++) {
						if((*it)->is_dir && (*it)->name != "..") {
							m_entries_el->AppendChild((*it)->create_element(m_wnd));
						}
					}
					it = m_cur_dir_name.rbegin();
					for(;it != m_cur_dir_name.rend(); it++) {
						if(!(*it)->is_dir) {
							m_entries_el->AppendChild((*it)->create_element(m_wnd));
						}
					}
				}
				break;
			}
		}
		m_dirty = false;
		if(prev_selected != "") {
			auto entry_el = m_entries_el->GetElementById(prev_selected);
			if(entry_el) {
				auto pair = m_de_map.find(prev_selected);
				if(pair != m_de_map.end()) {
					entry_select(&pair->second, entry_el);
				}
			}
		}
	}
	if(m_dirty_scroll) {
		if(m_selected_entry) {
			scroll_vertical_into_view(m_selected_entry, m_entries_cont_el);
		} else {
			m_entries_cont_el->SetScrollTop(0);
		}
		m_dirty_scroll--;
	}
}

void FileSelect::show()
{
	m_history.clear();
	m_history_idx = 0;
	disable(m_path_el.next);
	disable(m_path_el.prev);

	Window::show();
}

std::pair<FileSelect::DirEntry*,Rml::Element*> FileSelect::get_entry(Rml::Event &_ev)
{
	Rml::Element *entry_el = nullptr;
	Rml::Element *target_el = _ev.GetTargetElement();
	entry_el = target_el;
	while(entry_el && entry_el->GetId().empty()) {
		entry_el = entry_el->GetParentNode();
	}
	if(!entry_el) {
		return std::make_pair(nullptr,nullptr);
	}

	auto pair = m_de_map.find(entry_el->GetId());
	if(pair == m_de_map.end()) {
		return std::make_pair(nullptr,nullptr);
	}

	return std::make_pair(&pair->second, entry_el);
}

void FileSelect::on_entry(Rml::Event &_ev)
{
	auto [de, entry_el] = get_entry(_ev);
	if(!de) {
		entry_deselect();
		return;
	}

	if(de->is_dir) {
		if(de->name == "..") {
			on_up(_ev);
		} else {
			set_history();
			try {
				set_current_dir(m_cwd + FS_SEP + de->name);
			} catch(...) { }
		}
		return;
	} else {
		entry_select(de, entry_el);
	}
}

void FileSelect::on_insert(Rml::Event &_ev)
{
	auto [de, entry_el] = get_entry(_ev);

	if(_ev.GetType() != "dblclick") {
		if(m_selected_id.empty()) {
			return;
		}
		auto pair = m_de_map.find(m_selected_id);
		if(pair == m_de_map.end()) {
			entry_deselect();
			return;
		}
		de = &pair->second;
	}
	if(!de) {
		return;
	}

	std::string path = m_cwd;
	if(m_select_callbk != nullptr) {
		path += FS_SEP;
		path += de->name;
		bool wp = m_wprotect->GetAttribute("checked") != nullptr;
		m_select_callbk(path, wp);
	} else {
		hide();
	}
}

void FileSelect::on_home(Rml::Event &)
{
	if(m_home == m_cwd) {
		return;
	}
	set_history();
	try {
		set_current_dir(m_home);
	} catch(...) { }
}

void FileSelect::on_reload(Rml::Event &)
{
	reload();
}

void FileSelect::on_show_panel(Rml::Event &)
{
	bool active = !is_active(get_element("show_panel"));
	get_element("show_panel")->SetClass("active", active);
	m_wnd->SetClass("wpanel", active);
	m_dirty_scroll = 2;
}

std::string FileSelect::get_up_path()
{
	if(!m_valid_cwd) {
		return std::string();
	}
	std::string path = m_cwd;
	size_t pos = path.rfind(FS_SEP);
	if(pos == std::string::npos) {
		return std::string();
	}
	if(pos == 0) {
		// the root on unix
		pos = 1;
	} else if(pos < 3 && FS_PATH_MIN == 3) {
		pos = 3;
	}
	path = path.substr(0, pos);
	if(path == m_cwd) {
		return std::string();
	}
	return path;
}

void FileSelect::on_up(Rml::Event &)
{
	std::string path = get_up_path();
	if(path.empty()) {
		return;
	}
	set_history();
	try {
		set_current_dir(path);
	} catch(...) { }
}

void FileSelect::set_history()
{
	if(m_history_idx < m_history.size()) {
		m_history.erase(m_history.begin()+m_history_idx, m_history.end());
	}
	if(m_history.empty() || m_history.back() != m_cwd) {
		m_history.push_back(m_cwd);
	}

	m_history_idx = m_history.size();
	disable(m_path_el.next);
	set_disabled(m_path_el.prev, m_history_idx==0);

	PDEBUGF(LOG_V1, LOG_GUI, "Current history:\n");
	int h = 0;
	for(auto &p:m_history) {
		PDEBUGF(LOG_V1, LOG_GUI, "  %d:%s\n", h++, p.c_str());
	}
}

void FileSelect::on_prev(Rml::Event &)
{
	if(m_history_idx > 0) {
		if(m_history_idx == m_history.size()) {
			unsigned idx = m_history_idx - 1;
			set_history();
			try {
				set_current_dir(m_history[idx]);
			} catch(...) {}
			m_history_idx = idx;
			PDEBUGF(LOG_V1, LOG_GUI, "  history idx: %u\n", m_history_idx);
		} else {
			try {
				set_current_dir(m_history[m_history_idx-1]);
			} catch(...) {}
			m_history_idx--;
			PDEBUGF(LOG_V1, LOG_GUI, "  history idx: %u\n", m_history_idx);
		}
		set_disabled(m_path_el.prev, m_history_idx==0);
		enable(m_path_el.next);
	}
}

void FileSelect::on_next(Rml::Event &)
{
	if(!m_history.empty() && m_history_idx < m_history.size()-1) {
		try {
			set_current_dir(m_history[m_history_idx+1]);
		} catch(...) {}
		m_history_idx++;
		PDEBUGF(LOG_V1, LOG_GUI, "  history idx: %u\n", m_history_idx);
		set_disabled(m_path_el.next, m_history_idx>=m_history.size()-1);
		enable(m_path_el.prev);
	}
}

void FileSelect::entry_select(const DirEntry *_de, Rml::Element *_entry_el)
{
	try {
		entry_deselect();

		m_selected_entry = _entry_el;
		m_selected_entry->SetClass("selected", true);
		m_selected_id = _de->id;

		if(m_valid_cwd && m_inforeq_fn) {
			m_panel_el->SetInnerRML(m_inforeq_fn(m_cwd + FS_SEP + _de->name));
			m_panel_el->SetScrollTop(0);
		}

		m_buttons_entry_el->SetClass("invisible", false);

		scroll_vertical_into_view(_entry_el, m_entries_cont_el);

	} catch(std::out_of_range &) {
		PDEBUGF(LOG_V0, LOG_GUI, "StateDialog: invalid id!\n");
	}
}

void FileSelect::entry_deselect()
{
	if(m_selected_entry) {
		m_selected_entry->SetClass("selected", false);
		m_selected_id = "";
	}
	m_selected_entry = nullptr;

	m_buttons_entry_el->SetClass("invisible", true);
	if(m_inforeq_fn) {
		m_panel_el->SetInnerRML("Select a file for information");
		m_panel_el->SetScrollTop(0);
	}
}

void FileSelect::on_drive(Rml::Event &_ev)
{
	std::string value = Window::get_form_input_value(_ev);
	if(!value.empty()) {
		std::string path = value + ":" + FS_SEP;
		PDEBUGF(LOG_V1, LOG_GUI, "Accessing drive %s\n", path.c_str());
		set_history();
		try {
			set_current_dir(path);
		} catch(...) {
			PERRF(LOG_GUI, "Cannot open '%s'\n", path.c_str());
		}
	}
}

void FileSelect::on_mode(Rml::Event &_ev)
{
	std::string value = Window::get_form_input_value(_ev);
	if(!value.empty()) {
		m_entries_el->SetClass("list", false);
		m_entries_el->SetClass("grid", false);
		m_entries_el->SetClass(value, true);
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

void FileSelect::on_order(Rml::Event &_ev)
{
	std::string value = Window::get_form_input_value(_ev);
	if(!value.empty()) {
		if(value == "date") {
			m_order = Order::BY_DATE;
		} else if(value == "name") {
			m_order = Order::BY_NAME;
		} else {
			PERRF(LOG_GUI, "Invalid order: %s\n", value.c_str());
			return;
		}
		m_dirty = true;
		m_dirty_scroll = 2;
	}
}

void FileSelect::on_asc_desc(Rml::Event &_ev)
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

void FileSelect::on_cancel(Rml::Event &)
{
	if(m_cancel_callbk != nullptr) {
		m_cancel_callbk();
	} else {
		hide();
	}
}

Rml::ElementPtr FileSelect::DirEntry::create_element(Rml::ElementDocument *_doc) const
{
	Rml::ElementPtr child = _doc->CreateElement("div");
	child->SetClassNames("entry");
	child->SetId(id);

	std::string inner;

	inner += "<div class=\"icon\"><div class=\"";
	if(is_dir) {
		inner += "DIR";
	} else {
		switch(size) {
			case 160*1024:  inner += "floppy_160"; break;
			case 180*1024:  inner += "floppy_180"; break;
			case 320*1024:  inner += "floppy_320"; break;
			case 360*1024:  inner += "floppy_360"; break;
			case 1200*1024: inner += "floppy_1_20"; break;
			case 720*1024:  inner += "floppy_720"; break;
			case 1440*1024: inner += "floppy_1_44"; break;
			default: inner += "hdd"; break;
		}
	}
	inner += "\"></div></div>";
	inner += "<div class=\"name\">" + name + "</div>";
	inner += "<div class=\"date\">";
	if(mtime != 0) {
		// %R is not implemented in MinGW, don't use.
		inner += str_format_time(mtime, "%x %H:%M");
	}
	inner += "</div>";

	child->SetInnerRML(inner);

	return child;
}

void FileSelect::clear()
{
	entry_deselect();
	m_cur_dir_date.clear();
	m_cur_dir_name.clear();
	m_de_map.clear();
	m_dotdot = nullptr;
	m_dirty = true;
	m_dirty_scroll = 2;
}

void FileSelect::set_cwd(const std::string &_path)
{
	m_cwd = _path;
	m_path_el.cwd->SetInnerRML(m_cwd.c_str());
	m_valid_cwd = false;
	Rml::Element *drive_el = m_wnd->GetElementById(str_format("drive_%c", std::toupper(m_cwd[0])));
	if(drive_el) {
		drive_el->SetAttribute("checked", true);
	}
}

void FileSelect::set_current_dir(const std::string &_path)
{
	PDEBUGF(LOG_V0, LOG_GUI, "Opening %s\n", _path.c_str());
	clear();
	set_cwd(_path);
	
	char buf[PATH_MAX];
	if(FileSys::realpath(_path.c_str(), buf) == nullptr) {
		PERRF(LOG_GUI, "The path to '%s' cannot be resolved\n", _path.c_str());
		throw std::exception();
	}
	std::string new_cwd = FileSys::to_utf8(buf);
	if(new_cwd.size() > FS_PATH_MIN && new_cwd.rfind(FS_SEP) == new_cwd.size()-1) {
		new_cwd.pop_back();
	}
	set_cwd(new_cwd);

	// throws:
	read_dir(new_cwd, "(\\.img|\\.ima|\\.flp)$");
	m_valid_cwd = true;
}

void FileSelect::reload()
{
	try {
		set_current_dir(m_cwd);
	} catch(...) { }
}

void FileSelect::read_dir(std::string _path, std::string _ext)
{
	DIR *dir;
	struct dirent *ent;

	if((dir = FileSys::opendir(_path.c_str())) == nullptr) {
		PERRF(LOG_GUI, "Cannot open directory '%s' for reading\n", _path.c_str());
		throw std::exception();
	}

	std::regex re(_ext, std::regex::ECMAScript|std::regex::icase);
	unsigned id = 0;
	while((ent = readdir(dir)) != nullptr) {
		struct stat sb;
		DirEntry de;
		de.name = FileSys::to_utf8(ent->d_name);
		std::string fullpath = _path + FS_SEP + de.name;
		if(FileSys::stat(fullpath.c_str(), &sb) != 0) {
			continue;
		}
#ifndef _WIN32
		//skip hidden files
		if(de.name[0]=='.' &&
		  (!S_ISDIR(sb.st_mode) || (S_ISDIR(sb.st_mode) && de.name != "..")))
		{
			continue;
		}
#endif
		if(S_ISDIR(sb.st_mode)) {
			if(de.name == "." || de.name == "..") {
				continue;
			}
			de.is_dir = true;
		} else {
			de.is_dir = false;
			// Check extension matches (case insensitive)
			if(!_ext.empty() && !std::regex_search(de.name, re)) {
				continue;
			}

		}
		FILETIME mtime;
		if(FileSys::get_file_stats(fullpath.c_str(), &de.size, &mtime) == 0) {
			de.mtime = FileSys::filetime_to_time_t(mtime);
		} else {
			PWARNF(LOG_V2, LOG_GUI, "Error accessing '%s': %s\n", fullpath.c_str(), get_error_string().c_str());
			de.mtime = 0;
		}
		if(!de.is_dir && !m_compat_sizes.empty()) {
			if(std::find(m_compat_sizes.begin(), m_compat_sizes.end(), de.size) == std::end(m_compat_sizes)) {
				continue;
			}
		}
		de.id = str_format("de_%u", id);

		try {
			auto p = m_de_map.emplace(std::make_pair(de.id, de));
			if(p.second) {
				m_cur_dir_date.emplace(&(p.first->second));
				m_cur_dir_name.emplace(&(p.first->second));
				id++;
				if(de.name == "..") {
					m_dotdot = &(p.first->second);
				}
			}
		} catch(std::runtime_error &e) {
			PDEBUGF(LOG_V1, LOG_GUI, "  %s\n", e.what());
		}
	}

	closedir(dir);
}

void FileSelect::set_zoom(int _amount)
{
	m_entries_el->SetClass(str_format("zoom-%d", m_zoom), false);
	m_zoom += _amount;
	m_zoom = std::min(4, m_zoom);
	m_zoom = std::max(0, m_zoom);
	m_entries_el->SetClass(str_format("zoom-%d", m_zoom), true);
	m_dirty_scroll = 2;
}

void FileSelect::on_keydown(Rml::Event &_ev)
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