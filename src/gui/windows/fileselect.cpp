/*
 * Copyright (C) 2015-2024  Marco Bortolin
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
	GUI_EVT( "entries", "keydown", FileSelect::on_entries ),
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
	GUI_EVT( "new_floppy","click", FileSelect::on_new_floppy),
	GUI_EVT( "*",       "keydown", FileSelect::on_keydown )
};

FileSelect::FileSelect(GUI * _gui)
:
ItemsDialog(_gui, "fileselect.rml")
{
}

FileSelect::~FileSelect()
{
}

void FileSelect::create(std::string _mode, std::string _order, int _zoom)
{
	Window::create();

	m_panel_el = get_element("info_panel");
	m_buttons_entry_el = get_element("buttons_entry");
	m_wprotect = dynamic_cast<Rml::ElementFormControl*>(get_element("wprotect"));
	m_home_btn_el = get_element("home");
	
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

	m_max_zoom = MAX_ZOOM;
	m_min_zoom = MIN_ZOOM;
	ItemsDialog::create(_mode, _zoom, "entries", "entries_container");

	if(_order == "date") {
		m_order = Order::BY_DATE;
		get_element("order_date")->SetAttribute("checked", true);
	} else if(_order == "title" || _order == "desc") {
		m_order = Order::BY_NAME;
		get_element("order_name")->SetAttribute("checked", true);
	} else {
		m_order = Order::BY_NAME;
		get_element("order_name")->SetAttribute("checked", true);
	}

	m_new_floppy = std::make_unique<NewFloppy>(m_gui);
	m_new_floppy->create();
	m_new_floppy->set_modal(true);
	m_new_btn = get_element("new_floppy");
	m_new_btn->SetClass("invisible", true);

	m_inforeq_btn = get_element("show_panel");
}

void FileSelect::set_features(NewMediumCb _new_medium_cb, MediumInfoCb _medium_info_cb, bool _wp_option)
{
	m_inforeq_fn = _medium_info_cb;
	if(!m_inforeq_fn) {
		m_inforeq_btn->SetClass("invisible", true);
		m_wnd->SetClass("wpanel", false);
	} else {
		m_inforeq_btn->SetClass("invisible", false);
		m_wnd->SetClass("wpanel", is_active(m_inforeq_btn));
		m_dirty_scroll = 2;
	}

	m_newfloppy_callbk = _new_medium_cb;
	if(!m_newfloppy_callbk) {
		m_new_btn->SetClass("invisible", true);
	}

	m_wprotect->SetClass("invisible", !_wp_option);
}

void FileSelect::update()
{
	Window::update();

	if(m_dirty) {
		set_disabled(m_path_el.up, get_up_path().empty());
		const DirEntry *prev_selected = m_selected_de;
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
		if(prev_selected) {
			auto entry_el = m_entries_el->GetElementById(prev_selected->id);
			if(entry_el) {
				entry_select(prev_selected, entry_el);
			}
		}
	}
	if(m_lazy_select) {
		auto entry_el = m_entries_el->GetElementById(m_lazy_select->id);
		if(entry_el) {
			entry_select(m_lazy_select, entry_el);
			m_buttons_entry_el->GetChild(1)->Focus();
		}
		m_lazy_select = nullptr;
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

	if(m_lazy_reload) {
		reload();
		m_lazy_reload = false;
	}

	if(m_selected_entry) {
		m_entries_el->Focus();
	}
}

void FileSelect::close()
{
	m_new_floppy->close();
	m_new_floppy.reset(nullptr);

	Window::close();
}

std::pair<FileSelect::DirEntry*,Rml::Element*> FileSelect::get_de_entry(Rml::Element *target_el)
{
	Rml::Element *entry_el = ItemsDialog::get_entry(target_el);
	if(!entry_el) {
		return std::make_pair(nullptr,nullptr);
	}

	auto pair = m_de_map.find(entry_el->GetId());
	if(pair == m_de_map.end()) {
		return std::make_pair(nullptr,nullptr);
	}

	return std::make_pair(&pair->second, entry_el);
}

std::pair<FileSelect::DirEntry*,Rml::Element*> FileSelect::get_de_entry(Rml::Event &_ev)
{
	return FileSelect::get_de_entry(_ev.GetTargetElement());
}

void FileSelect::on_entry(Rml::Event &_ev)
{
	auto [de, entry_el] = FileSelect::get_de_entry(_ev);
	if(!de) {
		entry_deselect();
		return;
	}

	if(de->is_dir) {
		enter_dir(de);
	} else {
		entry_select(de, entry_el);
	}
}

void FileSelect::on_insert(Rml::Event &_ev)
{
	const DirEntry *de = nullptr;
	Rml::Element *entry_el = nullptr;

	if(_ev.GetType() != "dblclick") {
		de = m_selected_de;
		entry_el = m_selected_entry;
	} else {
		std::tie(de, entry_el) = FileSelect::get_de_entry(_ev);
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

void FileSelect::on_entries(Rml::Event &_ev)
{
	auto id = get_key_identifier(_ev);
	switch(id) {
		case Rml::Input::KeyIdentifier::KI_RETURN:
		case Rml::Input::KeyIdentifier::KI_NUMPADENTER:
		{
			if(m_selected_de) {
				if(m_selected_de->is_dir) {
					enter_dir(m_selected_de);
				} else {
					on_insert(_ev);
				}
			}
			break;
		}
		default:
			on_keydown(_ev);
			return;
	}
	_ev.StopImmediatePropagation();
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
	bool active = !is_active(m_inforeq_btn);
	m_inforeq_btn->SetClass("active", active);
	m_wnd->SetClass("wpanel", active);
	m_dirty_scroll = 2;
}

void FileSelect::on_new_floppy(Rml::Event &)
{
	if(m_new_btn->IsClassSet("invisible")) {
		return;
	}
	m_new_floppy->set_callbacks(
		[=](std::string _dir, std::string _file, FloppyDisk::StdType _type, std::string _format)
		{
			if(!m_newfloppy_callbk) {
				return;
			}
			// in case of error should throw
			_file = m_newfloppy_callbk(_dir, _file, _type, _format);
			if(_dir != m_cwd) {
				set_history();
				try {
					set_current_dir(_dir);
				} catch(...) { return; }
			} else {
				reload();
			}
			auto direntry = std::find_if(m_cur_dir_name.begin(), m_cur_dir_name.end(),
				[&](const DirEntry *_de)->bool {
					return !_de->is_dir && _de->name == _file;
				});
			if(direntry != m_cur_dir_name.end()) {
				m_lazy_select = *direntry;
			}
		}
	);
	auto cwd = m_cwd;
	auto home = m_home;
	if(!m_valid_cwd || !m_writable_cwd) {
		cwd = "";
	}
	if(!m_writable_home) {
		home = "";
	}
	if(cwd.empty() && home.empty()) {
		return;
	}
	m_new_floppy->set_dirs(cwd, home);
	m_new_floppy->show();
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

void FileSelect::enter_dir(const DirEntry *_de)
{
	if(_de->name == "..") {
		Rml::Event ev;
		on_up(ev);
	} else {
		set_history();
		try {
			set_current_dir(m_cwd + FS_SEP + _de->name);
		} catch(...) { }
	}
}

void FileSelect::entry_select(Rml::Element *_entry_el)
{
	entry_deselect();
	auto pair = FileSelect::get_de_entry(_entry_el);
	if(!pair.first) {
		return;
	}
	entry_select(pair.first, pair.second);
}

void FileSelect::entry_select(const DirEntry *_de, Rml::Element *_entry_el)
{
	ItemsDialog::entry_select(_entry_el);

	m_selected_de = _de;

	m_panel_el->SetInnerRML("");
	if(m_valid_cwd && m_inforeq_fn) {
		if(!_de->is_dir) {
			m_panel_el->SetInnerRML(m_inforeq_fn(m_cwd + FS_SEP + _de->name));
		}
		m_panel_el->SetScrollTop(0);
	}

	if(!_de->is_dir) {
		m_buttons_entry_el->SetClass("invisible", false);
	}
}

void FileSelect::entry_deselect()
{
	ItemsDialog::entry_deselect();

	m_selected_de = nullptr;

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

void FileSelect::set_mode(std::string _mode)
{
	ItemsDialog::set_mode(_mode);

	if(m_selected_de) {
		m_dirty_scroll = 2;
	}
}

void FileSelect::on_mode(Rml::Event &_ev)
{
	set_mode(Window::get_form_input_value(_ev));
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
		if(type & FILE_FLOPPY_DISK) {
			if(type & FloppyDisk::SIZE_3_5) {
				inner += "floppy_3_5";
			} else if(type & FloppyDisk::SIZE_5_25) {
				inner += "floppy_5_25";
			} else {
				inner += "hdd";
			}
		} else if(type & FILE_OPTICAL_DISC) {
			inner += "cdrom";
		} else {
			inner += "hdd";
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
	m_home_btn_el->SetClass("active", (m_cwd == m_home));
}

void FileSelect::set_home(const std::string &_path)
{
	char buf[PATH_MAX];
	if(FileSys::realpath(_path.c_str(), buf) == nullptr) {
		throw std::runtime_error("Unresolvable path");
	}
	DIR *dir;
	if((dir = FileSys::opendir(_path.c_str())) == nullptr) {
		throw std::runtime_error("Directory unreadable");
	}
	closedir(dir);

	m_home = _path;
	m_writable_home = FileSys::is_dir_writeable(m_home.c_str());
	if(m_newfloppy_callbk) {
		m_new_btn->SetClass("invisible", !m_writable_home);
	}
}

void FileSelect::set_current_dir(const std::string &_path)
{
	if(_path.empty()) {
		return;
	}
	PDEBUGF(LOG_V0, LOG_GUI, "Opening %s\n", _path.c_str());
	m_writable_cwd = false;
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

	if(m_compat_types.empty()) {
		return;
	}

	// throws:
	read_dir(new_cwd, m_compat_regexp.c_str());
	m_valid_cwd = true;
	m_writable_cwd = FileSys::is_dir_writeable(m_cwd.c_str());
	if(m_newfloppy_callbk) {
		if(!m_writable_home && !m_writable_cwd) {
			m_new_btn->SetClass("invisible", true);
		} else {
			m_new_btn->SetClass("invisible", false);
		}
	}
}

void FileSelect::set_compat_types(std::vector<unsigned> _types,
		const std::vector<const char*> &_extensions,
		const std::vector<std::unique_ptr<FloppyFmt>> &_file_formats,
		bool _dos_formats_only)
{
	set_compat_types(_types, _extensions);

	m_compat_dos_formats_only = _dos_formats_only;
	if(m_newfloppy_callbk) {
		m_new_floppy->set_compat_types(m_compat_types, _file_formats);
		m_new_btn->SetClass("invisible", false);
	}
}

void FileSelect::set_compat_types(std::vector<unsigned> _types,
		const std::vector<const char*> &_extensions)
{
	if(_types.empty()) {
		PDEBUGF(LOG_V0, LOG_GUI, "FileSelect::set_compat_types(): no valid types.\n");
		m_compat_types = {FILE_NONE};
	} else {
		m_compat_types = _types;
	}
	std::vector<std::string> ext;
	for(auto e : _extensions) {
		ext.push_back(std::string("\\") + e);
	}
	m_compat_regexp = "(" + str_implode(ext,"|") + ")$";

	m_new_btn->SetClass("invisible", true);
}

void FileSelect::reload()
{
	if(!is_current_dir_valid()) {
		return;
	}
	if(!is_visible()) {
		m_lazy_reload = true;
		return;
	}
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
	std::unique_ptr<FloppyFmt> diskfmt;
	while((ent = readdir(dir)) != nullptr) {
		struct stat sb;
		DirEntry de;
		de.name = FileSys::to_utf8(ent->d_name);
		FileSys::get_file_parts(de.name.c_str(), de.base, de.ext);
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
		if(de.is_dir) {
			de.type = FILE_NONE;
		} else {
			if(m_compat_types[0] & FILE_FLOPPY_DISK) {
				bool compatible = false;
				// this assumes all compat types are floppy disks of the same size
				FloppyDisk::Size dsksize = FloppyDisk::Size(m_compat_types[0] & FloppyDisk::SIZE_MASK);

				diskfmt.reset(FloppyFmt::find(fullpath));
				if(diskfmt == nullptr) {
					continue;
				}

				auto ident = diskfmt->identify(fullpath, de.size, dsksize);
				if(ident.type == FloppyDisk::FD_NONE) {
					continue;
				} 
				if(m_compat_dos_formats_only && !(ident.type & FloppyDisk::DOS_FMT)) {
					continue;
				}
				// check density
				for(auto ctype : m_compat_types ) {
					if(
						((ctype & FloppyDisk::SIZE_MASK) & (ident.type & FloppyDisk::SIZE_MASK)) &&
						(ctype & FloppyDisk::DENS_MASK) == (ident.type & FloppyDisk::DENS_MASK)
					)
					{
						compatible = true;
					}
				}
				if(!compatible) {
					PDEBUGF(LOG_V2, LOG_GUI, "Incompatible floppy image (type:%u): '%s'\n",
							ident.type, fullpath.c_str());
					continue;
				}
				de.type = FILE_FLOPPY_DISK | ident.type;
			} else if(m_compat_types[0] & FILE_OPTICAL_DISC) {
				// TODO distinguish between different types of optical media?
				de.type = FILE_OPTICAL_DISC;
			} else {
				PDEBUGF(LOG_V0, LOG_GUI, "invalid file type requested: %X.\n", m_compat_types[0]);
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
	ItemsDialog::set_zoom(_amount);

	m_dirty_scroll = 2;
}

void FileSelect::on_keydown(Rml::Event &_ev)
{
	auto id = get_key_identifier(_ev);
	bool handled = true;
	switch(id) {
		case Rml::Input::KeyIdentifier::KI_S:
			if(_ev.GetParameter<bool>("ctrl_key", false) &&
			    m_selected_de && !m_selected_de->is_dir)
			{
				on_insert(_ev);
			} else {handled=false;}
			break;
		case Rml::Input::KeyIdentifier::KI_W:
			if(_ev.GetParameter<bool>("ctrl_key", false) &&
			    m_selected_de && !m_selected_de->is_dir)
			{
				bool wp = m_wprotect->GetAttribute("checked") != nullptr;
				if(wp) {
					m_wprotect->RemoveAttribute("checked");
				} else {
					m_wprotect->SetAttribute("checked", true);
				}
			} else {handled=false;}
			break;
		case Rml::Input::KeyIdentifier::KI_UP:
			if(_ev.GetParameter<bool>("alt_key", false)) {
				on_up(_ev);
			} else {handled=false;}
			break;
		case Rml::Input::KeyIdentifier::KI_LEFT:
			if(_ev.GetParameter<bool>("alt_key", false)) {
				on_prev(_ev);
			} else {handled=false;}
			break;
		case Rml::Input::KeyIdentifier::KI_RIGHT:
			if(_ev.GetParameter<bool>("alt_key", false)) {
				on_next(_ev);
			} else {handled=false;}
			break;
		case Rml::Input::KeyIdentifier::KI_HOME:
			if(_ev.GetParameter<bool>("alt_key", false)) {
				on_home(_ev);
			} else {handled=false;}
			break;
		case Rml::Input::KeyIdentifier::KI_BACK:
			on_up(_ev);
			break;
		case Rml::Input::KeyIdentifier::KI_F5:
			on_reload(_ev);
			break;
		case Rml::Input::KeyIdentifier::KI_F9:
			if(!m_inforeq_btn->IsClassSet("invisible")) {
				on_show_panel(_ev);
			} else {handled=false;}
			break;
		case Rml::Input::KeyIdentifier::KI_N:
			if(!m_new_btn->IsClassSet("invisible") && _ev.GetParameter<bool>("ctrl_key", false)) {
				on_new_floppy(_ev);
			} else {handled=false;}
			break;
		default: handled=false; break;
	}
	if(handled) {
		_ev.StopImmediatePropagation();
	} else {
		ItemsDialog::on_keydown(_ev);
	}
}
