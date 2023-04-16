/*
 * Copyright (C) 2016-2023  Marco Bortolin
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

#ifndef IBMULATOR_GUI_DEBUGGER_286_H
#define IBMULATOR_GUI_DEBUGGER_286_H

#include "sysdebugger.h"

class Machine;
class GUI;

class SysDebugger286 : public SysDebugger
{
private:
	struct s_286core {
		Rml::Element *msw;
	} m_286core = {};

	struct s_tools {

	} m_286tools;

	static event_map_t ms_evt_map;

	void on_CPU_skip(Rml::Event &);
	void on_CPU_bp_btn(Rml::Event &);

	const std::string & disasm(uint16_t _ip, bool _analyze, uint * _size);

public:

	SysDebugger286(GUI *_gui, Machine *_machine, Rml::Element *_button);
	~SysDebugger286();

	virtual void create();
	virtual void update();

	event_map_t & get_event_map() { return SysDebugger286::ms_evt_map; }
};

#endif
