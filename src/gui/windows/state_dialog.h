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

#ifndef IBMULATOR_GUI_STATEDIALOG_H
#define IBMULATOR_GUI_STATEDIALOG_H

#include "../window.h"
#include "state_record.h"
#include <set>
#include <RmlUi/Core/EventListener.h>

class GUI;

class StateDialog : public Window
{
protected:
	class DirEntry {
	public:
		const StateRecord *rec;

		DirEntry(const StateRecord *_rec)
		: rec(_rec) {}

		bool operator<(const DirEntry &_other) const {
			if(rec->name() == QUICKSAVE_RECORD) {
				return true;
			} else if(_other.rec->name() == QUICKSAVE_RECORD) {
				return false;
			} else if(rec->mtime() == _other.rec->mtime()) {
				return rec->name() < _other.rec->name();
			} else {
				return rec->mtime() > _other.rec->mtime();
			}
		}

		Rml::ElementPtr create_element(Rml::ElementDocument *_doc) const;
		static Rml::ElementPtr create_element(
			Rml::ElementDocument *_doc,
			const std::string &_screen,
			const StateRecord::Info &_info
		);
	};
	inline static std::string ms_cur_path;
	inline static std::set<DirEntry> ms_cur_dir;
	inline static std::map<std::string, StateRecord> ms_rec_map;

	Rml::Element *m_list_el = nullptr;
	inline static bool ms_dirty = true;
	bool m_dirty = true;

public:

	StateDialog(GUI *_gui, const char *_doc) : Window(_gui,_doc) {}
	virtual ~StateDialog() {}

	virtual void create();
	virtual void update();

	void set_dirty() {
		ms_dirty = true;
		m_dirty = true;
	}

	static void set_current_dir(const std::string &_path);
	static void reload_current_dir() {
		set_current_dir("");
	}
	static const std::string & current_dir() { return ms_cur_path; }
};



#endif
