/*
 * Copyright (C) 2021-2022  Marco Bortolin
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

#ifndef IBMULATOR_GUI_NEWFLOPPY_H
#define IBMULATOR_GUI_NEWFLOPPY_H

#include "../window.h"
#include "hardware/devices/floppydisk.h"
#include "hardware/devices/floppyfmt.h"
#include <RmlUi/Core.h>

class NewFloppy : public Window
{
private:

	static event_map_t ms_evt_map;
	std::function<void(std::string, std::string, FloppyDisk::StdType, bool)> m_create_clbk = nullptr;
	std::function<void()> m_cancel_callbk = nullptr;
	Rml::ElementFormControlInput *m_filename_el = nullptr;
	Rml::ElementFormControlSelect *m_type_el = nullptr;
	Rml::ElementFormControl *m_formatted_el = nullptr;
	Rml::Element *m_create_el = nullptr;
	std::string m_cwd;
	std::string m_media_dir;
	std::string m_dest_dir;

public:

	NewFloppy(GUI *_gui);
	virtual ~NewFloppy();

	void show();

	virtual void create();

	void set_dirs(std::string _cwd, std::string _media) {
		m_cwd = _cwd;
		m_media_dir = _media;
	}
	void set_compat_types(std::vector<unsigned> _types);
	void set_callbacks(
		std::function<void(std::string, std::string, FloppyDisk::StdType, bool)> _create_callback,
		std::function<void()> _cancel_callback = nullptr) {
		m_create_clbk = _create_callback;
		m_cancel_callbk = _cancel_callback;
	}

	event_map_t & get_event_map() { return NewFloppy::ms_evt_map; }

private:
	static const std::map<FloppyDisk::StdType, std::string> std_names;
	static const std::map<std::string, FloppyDisk::StdType> std_enums;
	
	void on_destdir(Rml::Event &);
	void on_create_file(Rml::Event &);
	void on_cancel(Rml::Event &);
	void on_keydown(Rml::Event &_ev);
};



#endif
