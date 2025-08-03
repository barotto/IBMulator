/*
 * Copyright (C) 2021-2025  Marco Bortolin
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

#include "items_dialog.h"
#include "state_record.h"
#include <set>
#include <RmlUi/Core/EventListener.h>

class GUI;


class StateDialog : public ItemsDialog
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

		Rml::ElementPtr create_element(Rml::ElementDocument *_doc, unsigned _idx, unsigned _count) const;
		static Rml::ElementPtr create_element(
			Rml::ElementDocument *_doc,
			const std::string &_screen,
			const StateRecord::Info &_info,
			unsigned _idx, unsigned _count
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

	Rml::Element *m_panel_el = nullptr;
	Rml::Element *m_panel_screen_el = nullptr;
	Rml::Element *m_panel_config_el = nullptr;
	Rml::Element *m_buttons_entry_el = nullptr;
	Rml::Element *m_action_button_el = nullptr;
	bool m_shown = false;
	bool m_entries_focus = true;
	bool m_dirty = true;
	int m_dirty_scroll = 0;
	enum class Order {
		BY_DATE, BY_DESC, BY_SLOT
	} m_order = Order::BY_DATE;
	bool m_order_ascending = true;

	std::string m_selected_name;
	std::string m_lazy_select;
	StateRecord::Info m_top_entry {"", "", "", 0, 0};

	std::function<void(StateRecord::Info)> m_action_callbk = nullptr;
	std::function<void(StateRecord::Info)> m_delete_callbk = nullptr;
	std::function<void()> m_cancel_callbk = nullptr;
	
	static constexpr int MIN_ZOOM = 0;
	static constexpr int MAX_ZOOM = 2;
	
public:
	StateDialog(GUI *_gui, const char *_doc, std::string _mode, std::string _order, int _zoom);

	void show() override;
	void update() override;
	bool would_handle(Rml::Input::KeyIdentifier _key, int _mod) override;

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
	
	void set_selection(std::string _slot_name);

	void on_focus(Rml::Event &_ev) override;

	void on_cancel(Rml::Event &_ev) override;
	void on_keydown(Rml::Event &_ev) override;
	void on_keyup(Rml::Event &_ev) override;

	void on_action(Rml::Event &);
	void on_delete(Rml::Event &);
	void on_mode(Rml::Event &_ev);
	void on_order(Rml::Event &_ev);
	void on_asc_desc(Rml::Event &_ev);
	void on_entries(Rml::Event &_ev);
	void on_entries_focus(Rml::Event &_ev);

protected:
	void create() override;

	std::pair<const StateRecord*,Rml::Element*> get_sr_entry(Rml::Element *target_el);
	std::pair<const StateRecord*,Rml::Element*> get_sr_entry(Rml::Event &);

	bool is_empty() const { return ms_rec_map.empty() && m_top_entry.version==0; }

	void entry_select(Rml::Element *_entry) override;
	void entry_select(std::string _name, Rml::Element *_entry, bool _tts_append = true);
	void entry_deselect() override;

	void set_mode(std::string _mode) override;
	void set_zoom(int _amount) override;

	virtual void speak_entries(bool _describe);
	virtual void speak_entry(const StateRecord* _sr, Rml::Element *_entry_el, bool _append);
	virtual void speak_content(bool _append);
	virtual void speak_element(Rml::Element *_el, bool _with_label, bool _describe = false, TTS::Priority _pri = TTS::Priority::Normal) override;
};


#endif
