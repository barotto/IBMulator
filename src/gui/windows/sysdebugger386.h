/*
 * Copyright (C) 2016  Marco Bortolin
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

#ifndef IBMULATOR_GUI_DEBUGGER_386_H
#define IBMULATOR_GUI_DEBUGGER_386_H

#include "sysdebugger.h"

class Machine;
class GUI;

class SysDebugger386 : public SysDebugger
{
private:
	struct s_386core {
		RC::Element *rf,*vm;
		RC::Element *pe,*pg,*ts;
		RC::Element *fs,*gs;
		RC::Element *fsbase,*gsbase;
		RC::Element *fslimit,*gslimit;
	} m_386core;

	static event_map_t ms_evt_map;

	void on_CPU_skip(RC::Event &);
	void on_CPU_bp_btn(RC::Event &);

	const RC::String & disasm(uint16_t _selector, uint32_t _eip, bool _analyze, uint * _size);

public:

	SysDebugger386(GUI *_gui, Machine *_machine);
	~SysDebugger386();

	void update();
	event_map_t & get_event_map() { return SysDebugger386::ms_evt_map; }
};

#endif
