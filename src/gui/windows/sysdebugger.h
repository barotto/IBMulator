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
		Rocket::Core::Element *f, *msw, *cpl;
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
		Rocket::Controls::ElementFormControl *cs_ff,*ip_ff;
		Rocket::Controls::ElementFormControl *log_prg_name;
		Rocket::Core::Element *log_prg_toggle;
	} m_tools;

	struct s_disasm {
		Rocket::Core::Element *line0;
	} m_disasm;

	Rocket::Core::Element *m_post;

	static event_map_t ms_evt_map;
	void on_cmd_switch_power(RC::Event &);
	void on_cmd_pause(RC::Event &);
	void on_cmd_resume(RC::Event &);
	void on_cmd_memdump(RC::Event &);
	void on_cmd_csdump(RC::Event &);
	void on_cmd_save_state(RC::Event &);
	void on_cmd_restore_state(RC::Event &);
	void on_CPU_step(RC::Event &);
	void on_CPU_skip(RC::Event &);
	void on_CPU_ff_btn(RC::Event &);
	void on_log_prg_toggle(RC::Event &);
	void on_log_write(RC::Event &);
	void on_idt_dump(RC::Event &);
	void on_ldt_dump(RC::Event &);
	void on_gdt_dump(RC::Event &);

	void read_memory(uint32_t _address, uint8_t *_buf, uint _len);
	const Rocket::Core::String & disasm(uint16_t _selector, uint16_t _ip, bool _analyze, uint * _size);

public:

	SysDebugger(Machine *_machine, GUI *_gui);
	~SysDebugger();

	void update();
	event_map_t & get_event_map() { return SysDebugger::ms_evt_map; }
};

#endif
