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

class GUI;

class FileSelect : public Window
{
private:
	std::function<void(std::string,bool)> m_select_callbk = nullptr;
	std::function<void()> m_cancel_callbk = nullptr;

	static event_map_t ms_evt_map;
	
	std::string m_home;
	std::string m_cwd;
	Rml::Element *m_cwd_el = nullptr;
	Rml::Element *m_entries_el = nullptr;
	Rml::Element *m_panel_el = nullptr;
	Rml::Element *m_buttons_entry_el = nullptr;
	Rml::ElementFormControl *m_wprotect = nullptr;

	class DirEntry {
	public:
		std::string id;
		std::string name;
		uint64_t size;
		time_t mtime;
		bool is_dir;

		Rml::ElementPtr create_element(Rml::ElementDocument *_doc) const;
	};
	struct DirEntryOrderDate {
		bool operator()(const DirEntry *_first, const DirEntry *_other) const {
			if(_first->is_dir) {
				if(_other->is_dir) {
					if(_first->mtime == _other->mtime) {
						return _first->name < _other->name;
					} else {
						return _first->mtime > _other->mtime;
					}
				}
				return true;
			} else {
				if(_other->is_dir) {
					return false;
				}
				if(_first->mtime == _other->mtime) {
					return _first->name < _other->name;
				} else {
					return _first->mtime > _other->mtime;
				}
			}
		}
	};
	struct DirEntryOrderName {
		bool operator()(const DirEntry *_first, const DirEntry *_other) const {
			if(_first->is_dir) {
				if(_other->is_dir) {
					return _first->name < _other->name;
				}
				return true;
			} else {
				if(_other->is_dir) {
					return false;
				}
				return _first->name < _other->name;
			}
		}
	};
	std::set<const DirEntry*, DirEntryOrderDate> m_cur_dir_date;
	std::set<const DirEntry*, DirEntryOrderName> m_cur_dir_name;
	std::map<std::string, DirEntry> m_de_map;
	const DirEntry *m_dotdot = nullptr;
	Rml::Element *m_selected_entry = nullptr;
	std::string m_selected_id;
	bool m_dirty = true;
	enum class Order {
		BY_DATE, BY_NAME
	} m_order = Order::BY_NAME;
	bool m_order_ascending = true;
	std::vector<uint64_t> m_compat_sizes;

public:

	FileSelect(GUI * _gui);
	~FileSelect();

	void set_select_callbk(std::function<void(std::string,bool)> _fn) { m_select_callbk = _fn; }
	void set_cancel_callbk(std::function<void()> _fn) { m_cancel_callbk = _fn; }

	virtual void create();
	virtual void update();

	event_map_t & get_event_map() { return FileSelect::ms_evt_map; }

	void set_home(const std::string &_path) { m_home = _path; }
	void set_current_dir(const std::string &_path);
	void set_compat_sizes(std::vector<uint64_t> _sizes) { m_compat_sizes = _sizes; }
	void reload();

protected:
	
	void on_cancel(Rml::Event &);
	void on_entry(Rml::Event &);
	void on_mode(Rml::Event &);
	void on_order(Rml::Event &);
	void on_asc_desc(Rml::Event &);
	void on_up(Rml::Event &);
	void on_insert(Rml::Event &);
	void on_home(Rml::Event &);
	void on_reload(Rml::Event &);
	
	void set_dirty() { m_dirty = true; }
	
	void read_dir(std::string _path, std::string _ext);
	void entry_select(const DirEntry *_de, Rml::Element *_entry);
	void entry_deselect();
	
	std::pair<DirEntry*,Rml::Element*> get_entry(Rml::Event &);
};



#endif
