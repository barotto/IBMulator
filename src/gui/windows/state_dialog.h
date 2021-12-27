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
	struct DirEntryOrderDate {
		bool operator()(const DirEntry &_first, const DirEntry &_other) const {
			if(_first.rec->name() == QUICKSAVE_RECORD) {
				return true;
			} else if(_other.rec->name() == QUICKSAVE_RECORD) {
				return false;
			} else if(_first.rec->mtime() == _other.rec->mtime()) {
				return _first.rec->name() < _other.rec->name();
			} else {
				return _first.rec->mtime() > _other.rec->mtime();
			}
		}
	};
	struct DirEntryOrderDesc {
		bool operator()(const DirEntry &_first, const DirEntry &_other) const {
			if(_first.rec->name() == QUICKSAVE_RECORD) {
				return true;
			} else if(_other.rec->name() == QUICKSAVE_RECORD) {
				return false;
			} else if(_first.rec->user_desc() == _other.rec->user_desc()) {
				return _first.rec->name() < _other.rec->name();
			} else {
				return _first.rec->user_desc() < _other.rec->user_desc();
			}
		}
	};
	struct DirEntryOrderSlot {
		bool operator()(const DirEntry &_first, const DirEntry &_other) const {
			if(_first.rec->name() == QUICKSAVE_RECORD) {
				return true;
			} else if(_other.rec->name() == QUICKSAVE_RECORD) {
				return false;
			} else {
				return _first.rec->name() < _other.rec->name();
			}
		}
	};
	inline static std::string ms_cur_path;
	inline static std::set<DirEntry, DirEntryOrderDate> ms_cur_dir_date;
	inline static std::set<DirEntry, DirEntryOrderDesc> ms_cur_dir_desc;
	inline static std::set<DirEntry, DirEntryOrderSlot> ms_cur_dir_slot;
	inline static std::map<std::string, StateRecord> ms_rec_map;

	Rml::Element *m_entries_el = nullptr;
	Rml::Element *m_panel_el = nullptr;
	Rml::Element *m_panel_screen_el = nullptr;
	Rml::Element *m_panel_config_el = nullptr;
	Rml::Element *m_buttons_entry_el = nullptr;
	Rml::Element *m_action_button_el = nullptr;
	bool m_dirty = true;
	int m_dirty_scroll = 0;
	enum class Order {
		BY_DATE, BY_DESC, BY_SLOT
	} m_order = Order::BY_DATE;
	bool m_order_ascending = true;
	Rml::Element *m_selected_entry = nullptr;
	std::string m_selected_id;
	std::string m_lazy_select;

	std::function<void(StateRecord::Info)> m_action_callbk = nullptr;
	std::function<void(StateRecord::Info)> m_delete_callbk = nullptr;
	std::function<void()> m_cancel_callbk = nullptr;
	
	static constexpr int MIN_ZOOM = 0;
	static constexpr int MAX_ZOOM = 2;
	int m_zoom = 1;
	
public:

	StateDialog(GUI *_gui, const char *_doc) : Window(_gui,_doc) {}
	virtual ~StateDialog() {}

	virtual void create(std::string _mode, std::string _order, int _zoom);
	virtual void update();

	void set_callbacks(
		std::function<void(StateRecord::Info)> _on_action,
		std::function<void(StateRecord::Info)> _on_delete,
		std::function<void()> _on_cancel) {
		m_action_callbk = _on_action;
		m_delete_callbk = _on_delete;
		m_cancel_callbk = _on_cancel;
	}
	
	void set_dirty() { m_dirty = true; }

	static void set_current_dir(const std::string &_path);
	static void reload_current_dir() {
		set_current_dir("");
	}
	static const std::string & current_dir() { return ms_cur_path; }

	virtual void action_on_record(std::string _rec_name) = 0;
	virtual void delete_record(std::string _rec_name);

	virtual void on_cancel(Rml::Event &_ev);

	void entry_select(std::string _slot_id);
	void entry_select(std::string _slot_id, Rml::Element *_entry);
	void entry_deselect();
	void on_action(Rml::Event &);
	void on_delete(Rml::Event &);
	void on_mode(Rml::Event &_ev);
	void on_order(Rml::Event &_ev);
	void on_asc_desc(Rml::Event &_ev);
	void on_keydown(Rml::Event &_ev);

protected:

	void set_zoom(int _amount);
};



#endif
