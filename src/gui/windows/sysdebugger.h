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

#ifndef IBMULATOR_GUI_DEBUGGER_H
#define IBMULATOR_GUI_DEBUGGER_H

class Machine;
class GUI;

class SysDebugger : public Window
{
private:

	Machine *m_machine;

	Rocket::Core::ElementDocument * m_debugger_wnd;

	struct s_core {
		Rocket::Core::Element *ax,*bx,*cx,*dx;
		Rocket::Core::Element *bp,*si,*di,*sp;
		Rocket::Core::Element *cs,*ds,*ss,*es,*tr;
		Rocket::Core::Element *ip;
		Rocket::Core::Element *f, *msw;
		Rocket::Core::Element *cf, *pf, *af, *zf, *sf, *tf,
		                      *iff, *df, *of, *pl, *nt;
		Rocket::Core::Element *csbase,*dsbase,*esbase,*ssbase,*trbase;
		Rocket::Core::Element *cslimit,*dslimit,*eslimit,*sslimit,*trlimit;
		Rocket::Core::Element *ldt, *ldtbase, *ldtlimit;
		Rocket::Core::Element *idtbase, *idtlimit;
		Rocket::Core::Element *gdtbase, *gdtlimit;
	} m_core;

	struct s_memory {
		Rocket::Core::Element *cs_ip, *cs_ip_str;
		Rocket::Core::Element *ds_si, *ds_si_str;
		Rocket::Core::Element *es_di, *es_di_str;
		Rocket::Core::Element *ss_sp, *ss_sp_str;
	} m_memory;

	struct s_tools {
		Rocket::Core::Element *step, *skip, *ff;
		Rocket::Controls::ElementFormControl *cs_ff,*ip_ff;
		Rocket::Core::Element *mcmd0, *mcmd1, *mcmd2, *mcmd3, *mcmd4, *mcmd5, *mcmd6, *mcmd7;
	} m_tools;

	struct s_disasm {
		Rocket::Core::Element *line0;
	} m_disasm;

	Rocket::Core::Element *m_post;

	void read_memory(uint32_t _address, uint8_t *_buf, uint _len);
	const Rocket::Core::String & disasm(uint16_t _selector, uint16_t _ip, bool _analyze, uint * _size);

public:

	SysDebugger(Machine *_machine, GUI *_gui);
	~SysDebugger();

	void update();

	void ProcessEvent(Rocket::Core::Event & event);
};

#endif
