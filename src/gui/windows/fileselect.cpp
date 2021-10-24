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
	GUI_EVT( "cancel", "click",  FileSelect::on_cancel ),
	GUI_EVT( "close",  "click",  FileSelect::on_cancel ),
	GUI_EVT( "entries","click",  FileSelect::on_entry ),
	GUI_EVT( "insert", "click",  FileSelect::on_insert ),
	GUI_EVT( "mode",    "click", FileSelect::on_mode ),
	GUI_EVT( "order",   "click", FileSelect::on_order ),
	GUI_EVT( "asc_desc","click", FileSelect::on_asc_desc),
	GUI_EVT( "reload",  "click", FileSelect::on_reload),
	GUI_EVT( "home",    "click", FileSelect::on_home),
	GUI_EVT( "*",    "keydown", Window::on_keydown )
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

	m_cwd_el = get_element("cwd");
	m_entries_el = get_element("entries");
	//m_panel_el = get_element("panel");
	m_buttons_entry_el = get_element("buttons_entry");
	m_wprotect = dynamic_cast<Rml::ElementFormControl*>(get_element("wprotect"));
}

void FileSelect::update()
{
	Window::update();

	if(m_dirty) {
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
	}
}

void FileSelect::on_entry(Rml::Event &_ev)
{
	Rml::Element *entry_el;
	Rml::Element *target_el = _ev.GetTargetElement();
	entry_el = target_el;
	while(entry_el && entry_el->GetId().empty()) {
		entry_el = entry_el->GetParentNode();
	}
	if(!entry_el) {
		return;
	}

	auto pair = m_de_map.find(entry_el->GetId());
	if(pair == m_de_map.end()) {
		return;
	}
	auto de = &pair->second;
	
	std::string path = m_cwd;
	if(de->is_dir) {
		if(de->name == "..") {
			on_up(_ev);
		} else {
			path += FS_SEP;
			path += de->name;
			try {
				set_current_dir(path);
			} catch(...) { }
		}
		return;
	}
	
	if(target_el->IsClassSet("action")) {
		if(m_select_callbk != nullptr) {
			path += FS_SEP;
			path += de->name;
			bool wp = m_wprotect->GetAttribute("checked") != nullptr;
			m_select_callbk(path, wp);
		} else {
			hide();
		}
		return;
	}

	entry_select(de, entry_el);

}

void FileSelect::on_insert(Rml::Event &)
{
	if(m_selected_id.empty()) {
		return;
	}
	auto pair = m_de_map.find(m_selected_id);
	if(pair == m_de_map.end()) {
		return;
	}
	auto de = &pair->second;
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
	try {
		set_current_dir(m_home);
	} catch(...) {
		PERRF(LOG_GUI, "Cannot open directory\n");
	}
}

void FileSelect::on_reload(Rml::Event &)
{
	reload();
}

void FileSelect::on_up(Rml::Event &)
{
	std::string path = m_cwd;
	size_t pos = path.rfind(FS_SEP);
	if(pos == std::string::npos) {
		return;
	}
	if(pos == 0) {
		// the root on unix
		pos = 1;
	}
	path = path.substr(0, pos);
	try {
		set_current_dir(path);
	} catch(...) { }
}

void FileSelect::entry_select(const DirEntry *_de, Rml::Element *_entry_el)
{
	try {
		entry_deselect();

		m_selected_entry = _entry_el;
		m_selected_entry->SetClass("selected", true);
		m_selected_id = _de->id;

		// TODO extract the list of files

		m_buttons_entry_el->SetClass("invisible", false);
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
}

void FileSelect::on_mode(Rml::Event &_ev)
{
	std::string value = Window::get_form_input_value(_ev);
	if(!value.empty()) {
		m_entries_el->SetClassNames(value);
		//m_panel_el->SetClassNames(value);
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
		set_dirty();
		update();
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
		set_dirty();
		update();
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
	if(mtime) {
		inner += "<div class=\"date\">" + str_format_time(mtime, "%x %R") + "</div>";
	}
	child->SetInnerRML(inner);

	return child;
}

void FileSelect::set_current_dir(const std::string &_path)
{
	char buf[PATH_MAX];
	if(realpath(_path.c_str(), buf) == nullptr) {
		PERRF(LOG_GUI, "The path to '%s' cannot be resolved\n", _path.c_str());
		throw std::exception();
	}
	std::string new_cwd = buf;
	if(new_cwd.size() > FS_PATH_MIN && new_cwd.rfind(FS_SEP) == new_cwd.size()-1) {
		new_cwd.pop_back();
	}

	try {
		read_dir(new_cwd, "(\\.img|\\.ima|\\.flp)$");
	} catch(std::exception &e) {
		return;
	}
	m_cwd = new_cwd;
	m_cwd_el->SetInnerRML(m_cwd.c_str());
	set_dirty();
	update();
}

void FileSelect::reload()
{
	try {
		set_current_dir(m_cwd);
	} catch(...) {
		PERRF(LOG_GUI, "Cannot open directory\n");
	}
}

void FileSelect::read_dir(std::string _path, std::string _ext)
{
	DIR *dir;
	struct dirent *ent;

	if((dir = opendir(_path.c_str())) == nullptr) {
		PERRF(LOG_GUI, "Cannot open directory '%s' for reading\n", _path.c_str());
		throw std::exception();
	}

	m_cur_dir_date.clear();
	m_cur_dir_name.clear();
	m_de_map.clear();
	m_dotdot = nullptr;

	std::regex re(_ext, std::regex::ECMAScript|std::regex::icase);
	unsigned id = 0;
	while((ent = readdir(dir)) != nullptr) {
		struct stat sb;
		DirEntry de;
		de.name = ent->d_name;
		std::string fullpath = _path + FS_SEP + de.name;
		if(stat(fullpath.c_str(), &sb) != 0) {
			continue;
		}
#ifndef _WIN32
		//skip hidden files
		if(ent->d_name[0]=='.' &&
		  (!S_ISDIR(sb.st_mode) || (S_ISDIR(sb.st_mode) && de.name != "..")))
		{
			continue;
		}
#endif
		if(S_ISDIR(sb.st_mode)) {
			if(de.name == ".") {
				continue;
			}
			if(de.name == ".." && _path.length() <= FS_PATH_MIN) {
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

