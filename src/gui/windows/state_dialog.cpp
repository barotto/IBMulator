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

	m_list_el = get_element("list");
}

void StateDialog::update()
{
	Window::update();
	if(ms_dirty) {
		Rml::ReleaseTextures();
		ms_dirty = false;
	}
}

void StateDialog::set_current_dir(const std::string &_path)
{
	ms_cur_dir.clear();
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
				ms_cur_dir.emplace(&(p.first->second));
			}
		} catch(std::runtime_error &e) {
			PDEBUGF(LOG_V1, LOG_GUI, "  %s\n", e.what());
		}
	}

	closedir(dir);
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

