/*
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ibmulator.h"
#include "gui.h"
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <regex>
#include <limits.h>
#include <stdlib.h>

#include <Rocket/Core.h>
#include <Rocket/Controls.h>
#include <Rocket/Core/TypeConverter.h>


event_map_t FileSelect::ms_evt_map = {
	GUI_EVT( "cancel", "click", FileSelect::on_cancel ),
	GUI_EVT( "files", "click", FileSelect::on_file )
};

FileSelect::FileSelect(GUI * _gui)
:
Window(_gui, "fileselect.rml"),
Rocket::Controls::DataSource("file_select")
{
	ASSERT(m_wnd);
	m_select_callbk = NULL;
	m_cancel_callbk = NULL;
	m_cwd_el = get_element("cwd");
	m_wprotect = dynamic_cast<RCN::ElementFormControl*>(get_element("wprotect"));
	init_events();
}

FileSelect::~FileSelect()
{
}


void FileSelect::update()
{

}

void FileSelect::on_file(RC::Event &_event)
{
	RC::Element * el = _event.GetTargetElement();
	RCN::ElementDataGridCell *cell = dynamic_cast<RCN::ElementDataGridCell *>(el);
	if(cell == nullptr) {
		//try the parent?
		cell = dynamic_cast<RCN::ElementDataGridCell *>(el->GetParentNode());
		if(cell == nullptr) {
			//give up
			return;
		}
	}
	RCN::ElementDataGridRow *row =
			dynamic_cast<RCN::ElementDataGridRow *>(cell->GetParentNode());
	if(row == nullptr) {
		return;
	}
	int row_index = row->GetTableRelativeIndex();
	RC::StringList sl;

	GetRow(sl, "files", row_index, {"name"});
	PDEBUGF(LOG_V1, LOG_GUI, "clicked on row %u=%s\n", row_index, sl[0].CString());

	auto it = m_cur_dir.begin();
	advance(it,row_index);
	std::string path = m_cwd;
	if(it->is_dir) {
		if(it->name == "..") {
			size_t pos = path.rfind(FS_SEP);
			if(pos==string::npos) {
				return;
			}
			path = path.substr(0,pos);
		} else {
			path += FS_SEP;
			path += it->name;
		}
		set_current_dir(path);
		return;
	}
	if(m_select_callbk != NULL) {
		path += FS_SEP;
		path += it->name;
		bool wp = m_wprotect->GetAttribute("checked") != NULL;
		m_select_callbk(path,wp);
	} else {
		hide();
	}
}

void FileSelect::on_cancel(RC::Event &)
{
	if(m_cancel_callbk != NULL) {
		m_cancel_callbk();
	} else {
		hide();
	}
}

void FileSelect::GetRow(RC::StringList& row, const RC::String& table, int row_index,
		const RC::StringList& columns)
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
			std::string name = it->name;
			//std::transform(name.begin(), name.end(), name.begin(), ::toupper);
			row.push_back(RC::String(name.c_str()));
		}
		else if (columns[i] == "label")
		{
			//to read the volume label the file must be opened. i don't think
			//it would be wise
			row.push_back("");
		}
		else if (columns[i] == "type")
		{
			RC::String formatted = "<div class=\"";
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
					row.push_back("???");
					return;
				}
			}
			formatted += "\"></div>";
			row.push_back(formatted);
		}
	}
}

int FileSelect::GetNumRows(const RC::String &_table)
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
	if(realpath(_path.c_str(), buf) == NULL) {
		PERRF(LOG_GUI, "unable to set the current path to '%s'\n", _path.c_str());
		throw std::exception();
	}
	m_cwd = buf;

	if(m_cwd.rfind(FS_SEP) == m_cwd.size()-1) {
		m_cwd.pop_back();
	}

	read_dir(m_cwd, "(\\.img|\\.ima|\\.flp)$");
	//read_dir(m_cwd, "");
	m_cwd_el->SetInnerRML(m_cwd.c_str());
	NotifyRowChange("files");
}

void FileSelect::read_dir(std::string _path, std::string _ext)
{
	m_cur_dir.clear();

	DIR *dir;
	struct dirent *ent;

	if((dir = opendir(_path.c_str())) == NULL) {
		PERRF(LOG_FS, "Unable to open directory %s\n", _path.c_str());
		throw std::exception();
	}
	_path += FS_SEP;
	std::regex re(_ext, std::regex::ECMAScript|std::regex::icase);
	while((ent = readdir(dir)) != NULL) {
		struct stat sb;
		DirEntry de;
		de.name = ent->d_name;
		std::string fullpath = _path + de.name;
		if(stat(fullpath.c_str(), &sb) != 0) {
			continue;
		}
		de.size = sb.st_size;
		if(S_ISDIR(sb.st_mode)) {
			if(de.name == ".") {
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
		m_cur_dir.insert(de);
	}

	if(closedir(dir) != 0) {
		PERRF(LOG_FS, "Unable to close directory %s\n", _path.c_str());
		throw std::exception();
	}
}


void FileSelectTypeFormatter::FormatData(RC::String& formatted_,
		const RC::StringList& _raw)
{
	formatted_ = "<div class=\"" + _raw[0] + "\"></div>";
}
