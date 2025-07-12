/*
 * Copyright (C) 2015-2025  Marco Bortolin
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

#include "items_dialog.h"
#include "new_floppy.h"
#include "gui/medium_info.h"

class GUI;

class FileSelect : public ItemsDialog
{
public:
	enum FileType {
		FILE_TYPE_MASK = 0xF0000000,
		FILE_NONE = 0,
		FILE_FLOPPY_DISK = 0x80000000,
		FILE_OPTICAL_DISC = 0x40000000
	};
	using FileSelectCb = std::function<void(std::string,bool)>;
	using CancelCb = std::function<void()>;
	using NewMediumCb = std::function<std::string(std::string, std::string, FloppyDisk::StdType, std::string)>;

private:
	std::function<void(std::string,bool)> m_select_callbk = nullptr;
	std::function<void()> m_cancel_callbk = nullptr;
	MediumInfoCb m_inforeq_fn = nullptr;
	std::function<std::string(std::string, std::string, FloppyDisk::StdType, std::string)>
		m_newfloppy_callbk = nullptr;
	
	static event_map_t ms_evt_map;
	
	std::string m_home;
	bool m_writable_home = false;
	std::string m_cwd;
	bool m_valid_cwd = false;
	bool m_writable_cwd = false;

	Rml::Element *m_panel_el = nullptr;
	Rml::Element *m_buttons_entry_el = nullptr;
	Rml::ElementFormControl *m_wprotect = nullptr;
	Rml::Element *m_home_btn_el = nullptr;
	struct {
		Rml::Element *cwd;
		Rml::Element *prev, *next, *up;
	} m_path_el = {};
	unsigned m_drives_mask = 0;

	class DirEntry {
	public:
		std::string id;
		std::string name;
		std::string base;
		std::string ext;
		unsigned type;
		uint64_t size;
		time_t mtime;
		bool is_dir;

		Rml::ElementPtr create_element(Rml::ElementDocument *_doc, unsigned _idx, unsigned _count) const;
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
	unsigned m_de_folders = 0;
	unsigned m_de_files = 0;

	const DirEntry *m_selected_de = nullptr;
	MediumInfoData m_selected_de_info;

	bool m_shown = false;
	bool m_dirty = true;
	int m_dirty_scroll = 0;
	enum class Order {
		BY_DATE, BY_NAME
	} m_order = Order::BY_NAME;
	bool m_order_ascending = true;
	std::vector<unsigned> m_compat_types = {FILE_NONE};
	std::string m_compat_regexp;
	bool m_compat_dos_formats_only = false;
	std::vector<std::string> m_history;
	unsigned m_history_idx = 0;

	static constexpr int MIN_ZOOM = 0;
	static constexpr int MAX_ZOOM = 4;

	Rml::Element *m_inforeq_btn = nullptr;
	Rml::Element *m_new_btn = nullptr;
	std::unique_ptr<NewFloppy> m_new_floppy;
	const DirEntry *m_lazy_select = nullptr;
	bool m_lazy_reload = false;
	bool m_lazy_tts = true;
	bool m_entries_focus = true;
	
public:

	FileSelect(GUI * _gui);
	~FileSelect();

	void set_select_callbk(FileSelectCb _fn) { m_select_callbk = _fn; }
	void set_cancel_callbk(CancelCb _fn) { m_cancel_callbk = _fn; }
	void set_features(NewMediumCb _new_medium_cb, MediumInfoCb _medium_info_cb, bool _wp_option);

	virtual void create(std::string _mode, std::string _order, int _zoom);
	virtual void update();
	virtual void show(const std::string &_curr_file);
	virtual void close();
	virtual bool would_handle(Rml::Input::KeyIdentifier _key, int _mod);

	event_map_t & get_event_map() { return FileSelect::ms_evt_map; }

	void set_home(const std::string &_path);
	std::string get_home() const { return m_home; }
	void set_current_dir(const std::string &_path);
	std::string get_current_dir() const { return m_cwd; }
	bool is_current_dir_valid() const { return m_valid_cwd; }
	void set_compat_types(std::vector<unsigned> _disk_types,
			const std::vector<const char*> &_extensions,
			const std::vector<std::unique_ptr<FloppyFmt>> &_file_formats,
			bool _dos_formats_only);
	void set_compat_types(std::vector<unsigned> _disk_types,
			const std::vector<const char*> &_extensions);
	void reload();

	void speak_element(Rml::Element *_el, bool _with_label, bool _describe = false, TTS::Priority _pri = TTS::Priority::Normal);

protected:
	
	void on_cancel(Rml::Event &);
	void on_entry(Rml::Event &);
	void on_drive(Rml::Event &);
	void on_prev_drive(Rml::Event &);
	void on_next_drive(Rml::Event &);
	void on_mode(Rml::Event &);
	void on_order(Rml::Event &);
	void on_asc_desc(Rml::Event &);
	void on_up(Rml::Event &);
	void on_prev(Rml::Event &);
	void on_next(Rml::Event &);
	void on_insert(Rml::Event &);
	void on_entries(Rml::Event &);
	void on_entries_focus(Rml::Event &);
	void on_home(Rml::Event &);
	void on_reload(Rml::Event &);
	void on_show_panel(Rml::Event &);
	void on_new_floppy(Rml::Event &);
	void on_keydown(Rml::Event &_ev);
	void on_keyup(Rml::Event &_ev);
	void on_focus(Rml::Event &_ev);

	void clear();
	void set_cwd(const std::string &_path);
	void set_history();
	void set_mode(std::string _mode);
	void set_zoom(int _amount);

	void enter_drive(char _letter);
	void read_dir(std::string _path, std::string _ext);
	void enter_dir(const std::string &_path, bool _tts_selection = true, bool _tts_speak_path = false);
	void enter_dir(const DirEntry *_de, bool _tts_selection = true, bool _tts_speak_path = false);
	void entry_select(Rml::Element *_entry);
	void entry_select(const DirEntry *_de, Rml::Element *_entry, bool _tts = true, bool _tts_append = true);
	void entry_deselect();
	std::pair<std::string,std::string> get_path_parts(const std::string &_path);
	std::pair<std::string,std::string> get_up_path();
	const DirEntry * find_de(const std::string _name);

	std::pair<DirEntry*,Rml::Element*> get_de_entry(Rml::Element *target_el);
	std::pair<DirEntry*,Rml::Element*> get_de_entry(Rml::Event &);

	bool is_empty() const;
	void speak_path(const std::string &_path);
	void speak_entries(bool _describe);
	void speak_entry(const DirEntry *_de, const MediumInfoData &_de_info, Rml::Element *_entry_el, bool _append);
	void speak_content(bool _append);
};



#endif
