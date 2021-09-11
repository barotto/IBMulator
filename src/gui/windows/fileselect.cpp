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
	GUI_EVT( "cancel", "click", FileSelect::on_cancel ),
	GUI_EVT( "close",  "click", FileSelect::on_cancel ),
	GUI_EVT( "files",  "click", FileSelect::on_file ),
	GUI_EVT( "*",    "keydown", Window::on_keydown )
};

FileSelect::FileSelect(GUI * _gui)
:
Rml::DataSource("file_select"),
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
	m_wprotect = dynamic_cast<Rml::ElementFormControl*>(get_element("wprotect"));
}

void FileSelect::update()
{
}

void FileSelect::on_file(Rml::Event &_event)
{
	Rml::Element * el = _event.GetTargetElement();
	Rml::ElementDataGridCell *cell = dynamic_cast<Rml::ElementDataGridCell *>(el);
	if(cell == nullptr) {
		//try the parent?
		cell = dynamic_cast<Rml::ElementDataGridCell *>(el->GetParentNode());
		if(cell == nullptr) {
			//give up
			return;
		}
	}
	Rml::ElementDataGridRow *row =
			dynamic_cast<Rml::ElementDataGridRow *>(cell->GetParentNode());
	if(row == nullptr) {
		return;
	}
	int row_index = row->GetTableRelativeIndex();
	std::vector<std::string> sl;

	GetRow(sl, "files", row_index, {"name"});
	PDEBUGF(LOG_V1, LOG_GUI, "clicked on row %u=%s\n", row_index, sl[0].c_str());

	auto it = m_cur_dir.begin();
	advance(it,row_index);
	std::string path = m_cwd;
	if(it->is_dir) {
		if(it->name == "..") {
			size_t pos = path.rfind(FS_SEP);
			if(pos == std::string::npos) {
				return;
			}
			if(pos == 0) {
				// the root on unix
				pos = 1;
			}
			path = path.substr(0, pos);
		} else {
			path += FS_SEP;
			path += it->name;
		}
		try {
			set_current_dir(path);
		} catch(...) { }
		return;
	}
	if(m_select_callbk != nullptr) {
		path += FS_SEP;
		path += it->name;
		bool wp = m_wprotect->GetAttribute("checked") != nullptr;
		m_select_callbk(path,wp);
	} else {
		hide();
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

void FileSelect::GetRow(std::vector<std::string> &row, const std::string &table, int row_index,
		const std::vector<std::string> &columns)
{
	if(table != "files") {
		return;
	}
	auto it = m_cur_dir.begin();
	advance(it,row_index);
	for(size_t i = 0; i < columns.size(); i++)
	{
		if (columns[i] == "name")
		{
			row.push_back(str_format("<div class=\"name\">%s</div>", it->name.c_str()));
		}
		else if (columns[i] == "label")
		{
			//to read the volume label the file must be opened. i don't think
			//it would be wise
			row.push_back("");
		}
		else if (columns[i] == "type")
		{
			std::string formatted = "<div class=\"";
			if(it->is_dir) {
				formatted += "DIR";
			} else {
				switch(it->size) {
				case 160*1024:
					formatted += "floppy_160";
					break;
				case 180*1024:
					formatted += "floppy_180";
					break;
				case 320*1024:
					formatted += "floppy_320";
					break;
				case 360*1024:
					formatted += "floppy_360";
					break;
				case 1200*1024:
					formatted += "floppy_1_20";
					break;
				case 720*1024:
					formatted += "floppy_720";
					break;
				case 1440*1024:
					formatted += "floppy_1_44";
					break;
				default:
					formatted += "hdd";
					break;
				}
			}
			formatted += "\"></div>";
			row.push_back(formatted);
		}
	}
}

int FileSelect::GetNumRows(const Rml::String &_table)
{
	if(_table != "files") {
		return 0;
	}
	return m_cur_dir.size();
}

bool FileSelect::DirEntry::operator<(const FileSelect::DirEntry &_other) const
{
	if(is_dir) {
		if(_other.is_dir) {
			return name < _other.name;
		}
		return true;
	} else {
		if(_other.is_dir) {
			return false;
		}
		return name < _other.name;
	}
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
		m_cur_dir = read_dir(new_cwd, "(\\.img|\\.ima|\\.flp)$");
	} catch(std::exception &e) {
		return;
	}
	m_cwd = new_cwd;
	m_cwd_el->SetInnerRML(m_cwd.c_str());
	NotifyRowChange("files");
}

std::set<FileSelect::DirEntry> FileSelect::read_dir(std::string _path, std::string _ext)
{
	DIR *dir;
	struct dirent *ent;

	if((dir = opendir(_path.c_str())) == nullptr) {
		PERRF(LOG_GUI, "Cannot open directory '%s' for reading\n", _path.c_str());
		throw std::exception();
	}

	std::set<DirEntry> cur_dir;

	std::regex re(_ext, std::regex::ECMAScript|std::regex::icase);
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
		de.size = sb.st_size;
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
		cur_dir.insert(de);
	}

	if(closedir(dir) != 0) {
		PWARNF(LOG_V1, LOG_GUI, "Cannot close directory '%s'\n", _path.c_str());
	}

	return cur_dir;
}


void FileSelectTypeFormatter::FormatData(std::string &formatted_,
		const std::vector<std::string> &_raw)
{
	formatted_ = _raw[0];
}
