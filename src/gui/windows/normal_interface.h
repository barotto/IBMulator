/*
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
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

	RC::Element * m_sysunit;
	RC::Element * m_btn_pause;
	bool m_led_pause;
	uint m_gui_mode;

	void on_pause(RC::Event &);
	void on_save(RC::Event &);
	void on_restore(RC::Event &);
	void on_exit(RC::Event &);

public:

	static event_map_t ms_evt_map;

	NormalInterface(Machine *_machine, GUI * _gui);
	~NormalInterface();

	void update();
	void update_size(uint _width, uint _height);

	event_map_t & get_event_map() { return NormalInterface::ms_evt_map; }
};

#endif
