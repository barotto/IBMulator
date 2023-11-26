/*
 * Copyright (C) 2023  Marco Bortolin
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

#ifndef IBMULATOR_GUI_MIXERINFO_H
#define IBMULATOR_GUI_MIXERINFO_H

#include "../window.h"
#include <RmlUi/Core.h>


class MixerSaveInfo : public Window
{
private:
	static event_map_t ms_evt_map;
	std::function<void()> m_save_callbk = nullptr;
	std::function<void()> m_cancel_callbk = nullptr;

	struct {
		Rml::ElementFormControl *name = nullptr;
	} m_el;

public:
	struct MixerProfileInfo {
		std::string name;
		std::string directory;
	} values;

public:
	MixerSaveInfo(GUI * _gui);

	void create();
	void show();
	void close();
	void setup_data_bindings();

	void set_callbacks(
		std::function<void()> _save_callback,
		std::function<void()> _cancel_callback = nullptr) {
		m_save_callbk = _save_callback;
		m_cancel_callbk = _cancel_callback;
	}

	event_map_t & get_event_map() { return MixerSaveInfo::ms_evt_map; }
	
private:
	void on_save(Rml::Event &);
	void on_cancel(Rml::Event &);
	void on_keydown(Rml::Event &_ev);
};


#endif
