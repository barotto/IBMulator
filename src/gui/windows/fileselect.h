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

#ifndef IBMULATOR_GUI_FILESELECT_H
#define IBMULATOR_GUI_FILESELECT_H

#include <Rocket/Controls/DataSource.h>
#include <Rocket/Core/EventListener.h>
#include <Rocket/Controls/DataFormatter.h>

class GUI;

class FileSelectTypeFormatter : public RCN::DataFormatter
{
public:
	FileSelectTypeFormatter() : RCN::DataFormatter("file_type") {}
	~FileSelectTypeFormatter() {}

	void FormatData(RC::String& formatted_data, const RC::StringList& raw_data);
};

class FileSelect : public Window, public Rocket::Controls::DataSource
{
private:
	std::function<void(std::string,bool)> m_select_callbk;
	std::function<void()> m_cancel_callbk;

	static event_map_t ms_evt_map;
	std::string m_cwd;
	RC::Element *m_cwd_el;
	RCN::ElementFormControl *m_wprotect;

	FileSelectTypeFormatter m_formatter;

	class DirEntry {
	public:
		std::string name;
		size_t size;
		bool is_dir;
	    bool operator<(const DirEntry& _other) const;
	};
	std::set<DirEntry> m_cur_dir;

	void on_cancel(RC::Event &);
	void on_file(RC::Event &);

	std::set<DirEntry> read_dir(std::string _path, std::string _ext);

public:

	FileSelect(GUI * _gui);
	~FileSelect();

	void set_select_callbk(std::function<void(std::string,bool)> _fn) { m_select_callbk = _fn; }
	void set_cancel_callbk(std::function<void()> _fn) { m_cancel_callbk = _fn; }

	void update();
	event_map_t & get_event_map() { return FileSelect::ms_evt_map; }

	void set_current_dir(const std::string &_path);

	void GetRow(RC::StringList& row, const RC::String& table,
			int row_index, const RC::StringList& columns);
	int GetNumRows(const RC::String& table);
};



#endif
