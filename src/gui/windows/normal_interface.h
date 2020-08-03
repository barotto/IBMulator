/*
 * Copyright (C) 2015, 2016  Marco Bortolin
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
#include <Rocket/Core/EventListener.h>

class Machine;
class GUI;

class NormalInterface : public Interface
{
private:
	uint m_vga_aspect;
	uint m_vga_scaling;

	RC::Element *m_sysunit, *m_sysbkgd;
	RC::Element *m_btn_pause;
	bool m_led_pause;
	uint m_gui_mode;

public:
	static event_map_t ms_evt_map;

	NormalInterface(Machine *_machine, GUI * _gui, Mixer *_mixer);
	~NormalInterface();

	void update();
	void container_size_changed(int _width, int _height);

	void action(int);
	
	event_map_t & get_event_map() { return NormalInterface::ms_evt_map; }

private:
	void on_pause(RC::Event &);
	void on_save(RC::Event &);
	void on_restore(RC::Event &);
	void on_exit(RC::Event &);
};

#endif
