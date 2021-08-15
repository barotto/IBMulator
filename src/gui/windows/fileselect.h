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

#include <RmlUi/Core/Elements/DataSource.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Elements/DataFormatter.h>

class GUI;

class FileSelectTypeFormatter : public Rml::DataFormatter
{
public:
	FileSelectTypeFormatter() : Rml::DataFormatter("file_type") {}
	~FileSelectTypeFormatter() {}

	void FormatData(std::string &formatted_data, const std::vector<std::string> &raw_data);
};

class FileSelect : public Rml::DataSource, public Window
{
private:
	std::function<void(std::string,bool)> m_select_callbk = nullptr;
	std::function<void()> m_cancel_callbk = nullptr;

	static event_map_t ms_evt_map;
	std::string m_cwd;
	Rml::Element *m_cwd_el = nullptr;
	Rml::ElementFormControl *m_wprotect = nullptr;

	FileSelectTypeFormatter m_formatter;

	class DirEntry {
	public:
		std::string name;
		size_t size;
		bool is_dir;
		bool operator<(const DirEntry& _other) const;
	};
	std::set<DirEntry> m_cur_dir;

	void on_cancel(Rml::Event &);
	void on_file(Rml::Event &);

	std::set<DirEntry> read_dir(std::string _path, std::string _ext);

public:

	FileSelect(GUI * _gui);
	~FileSelect();

	void set_select_callbk(std::function<void(std::string,bool)> _fn) { m_select_callbk = _fn; }
	void set_cancel_callbk(std::function<void()> _fn) { m_cancel_callbk = _fn; }

	virtual void create();
	virtual void update();
	event_map_t & get_event_map() { return FileSelect::ms_evt_map; }

	void set_current_dir(const std::string &_path);

	virtual void GetRow(std::vector<std::string> &row, const std::string &table,
			int row_index, const std::vector<std::string> &columns);
	virtual int GetNumRows(const Rml::String &table);
};



#endif
