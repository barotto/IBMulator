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

#ifndef IBMULATOR_GUI_DEBUGGER_386_H
#define IBMULATOR_GUI_DEBUGGER_386_H

#include "sysdebugger.h"

class Machine;
class GUI;

class SysDebugger386 : public SysDebugger
{
private:
	struct s_386core {
		Rml::Element *rf,*vm;
		Rml::Element *pe,*pg,*ts;
		Rml::Element *fs,*gs;
		Rml::Element *fsbase,*gsbase;
		Rml::Element *fslimit,*gslimit;
		Rml::Element *cr2, *cr3;
		Rml::Element *dr03[4], *dr6, *dr7;
	} m_386core = {};

	struct s_tools {
		Rml::ElementFormControl *cs_bp, *eip_bp;
	} m_386tools = {};

	static event_map_t ms_evt_map;

	void on_CPU_skip(Rml::Event &);
	void on_CPU_bp_btn(Rml::Event &);
	void on_fs_dump(Rml::Event &);
	void on_gs_dump(Rml::Event &);

	const std::string & disasm(uint32_t _eip, bool _analyze, uint * _size);

public:

	SysDebugger386(GUI *_gui, Machine *_machine, Rml::Element *_button);
	~SysDebugger386();

	virtual void create();
	virtual void update();

	event_map_t & get_event_map() { return SysDebugger386::ms_evt_map; }
};

#endif
