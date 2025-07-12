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

	enum Actions {
		ACTION_SHOW_HIDE,
		ACTION_ZOOM
	};

private:
	unsigned m_aspect_mode = 0;
	double   m_aspect_ratio = .0;
	unsigned m_window_scaling = 0;
	unsigned m_scale_mode = 0;
	bool     m_scale_integer = false;

	Rml::Element *m_main_interface = nullptr;
	Rml::Element *m_sysunit = nullptr;
	Rml::Element *m_sysbar = nullptr;
	Rml::Element *m_sysctrl = nullptr;
	Rml::Element *m_btn_pause = nullptr;
	Rml::Element *m_btn_visibility = nullptr;
	Rml::Element *m_hdd_led_c = nullptr;
	bool m_led_pause = false;
	ZoomMode m_cur_zoom = ZoomMode::NORMAL;

	EventTimers *m_timers;
	TimerID m_compact_ifc_timer = NULL_TIMER_ID;
	uint64_t m_compact_ifc_timeout = 1_s;
	
public:
	static event_map_t ms_evt_map;

	NormalInterface(Machine *_machine, GUI * _gui, Mixer *_mixer, EventTimers *_timers);
	~NormalInterface();

	virtual void create();
	virtual void update();
	virtual void config_changed(bool);
	virtual bool would_handle(Rml::Input::KeyIdentifier, int) { return false; }

	void container_size_changed(int _width, int _height);

	void action(int);
	void grab_input(bool _grabbed);
	bool is_system_visible() const;
	void hide_system();
	void show_system();
	ZoomMode zoom_mode() const { return m_cur_zoom; }

	event_map_t & get_event_map() { return NormalInterface::ms_evt_map; }

private:
	void set_hdd_active(bool _active);

	void set_zoom(ZoomMode _zoom);
	void on_pause(Rml::Event &);
	void on_save(Rml::Event &);
	void on_restore(Rml::Event &);
	void on_exit(Rml::Event &);
	void on_visibility(Rml::Event &);
	void on_mouse_move(Rml::Event &);

	void collapse_sysunit(bool _collapsed);
	bool is_sysunit_collapsed();
};

#endif
