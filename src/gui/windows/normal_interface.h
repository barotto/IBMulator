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

#ifndef IBMULATOR_GUI_NORMAL_INTERFACE_H
#define IBMULATOR_GUI_NORMAL_INTERFACE_H

#include "interface.h"
#include <RmlUi/Core/EventListener.h>

class Machine;
class GUI;

class NormalInterface : public Interface
{
public:
	enum class ZoomMode: unsigned {
		NORMAL, COMPACT
	};

private:
	uint m_vga_aspect = 0;
	uint m_vga_scaling = 0;

	Rml::Element *m_sysunit = nullptr, *m_sysbkgd = nullptr;
	Rml::Element *m_btn_pause = nullptr;
	bool m_led_pause = false;
	unsigned m_gui_mode = 0;
	ZoomMode m_cur_zoom = ZoomMode::NORMAL;

public:
	static event_map_t ms_evt_map;

	NormalInterface(Machine *_machine, GUI * _gui, Mixer *_mixer);
	~NormalInterface();

	virtual void create();
	virtual void update();

	void container_size_changed(int _width, int _height);

	void action(int);
	void grab_input(bool _grabbed);
	bool is_system_visible() const;
	void hide_system();
	void show_system();
	ZoomMode zoom_mode() const { return m_cur_zoom; }

	event_map_t & get_event_map() { return NormalInterface::ms_evt_map; }

private:
	void on_pause(Rml::Event &);
	void on_save(Rml::Event &);
	void on_restore(Rml::Event &);
	void on_exit(Rml::Event &);
};

#endif
