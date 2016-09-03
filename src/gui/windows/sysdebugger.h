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

#ifndef IBMULATOR_GUI_DEBUGGER_H
#define IBMULATOR_GUI_DEBUGGER_H

class Machine;
class GUI;

class SysDebugger : public Window
{
	friend class SysDebugger286;
	friend class SysDebugger386;

private:
	Machine *m_machine;

	struct s_core {
		RC::Element *eax,*ebx,*ecx,*edx;
		RC::Element *ebp,*esi,*edi,*esp;
		RC::Element *cs,*ds,*ss,*es,*tr;
		RC::Element *eip,*eflags,*cpl;
		RC::Element *cf, *pf, *af, *zf, *sf, *tf,
		            *iff, *df, *of, *pl, *nt;
		RC::Element *csbase,*dsbase,*esbase,*ssbase,*trbase;
		RC::Element *cslimit,*dslimit,*eslimit,*sslimit,*trlimit;
		RC::Element *ldt, *ldtbase, *ldtlimit;
		RC::Element *idtbase, *idtlimit;
		RC::Element *gdtbase, *gdtlimit;

		RC::Element *a20;
	} m_core;

	struct s_memory {
		RC::Element *cs_eip, *cs_eip_str;
		RC::Element *ds_esi, *ds_esi_str;
		RC::Element *es_edi, *es_edi_str;
		RC::Element *ss_esp, *ss_esp_str;
	} m_memory;

	struct s_tools {
		RC::Element *btn_power, *btn_pause, *btn_bp;
		bool led_power, led_pause;
		RCN::ElementFormControl *log_prg_name;
		RC::Element *log_prg_toggle;
		RCN::ElementFormControl *cs_bp,*eip_bp;
	} m_tools;

	struct s_disasm {
		RC::Element *line0;
	} m_disasm;

	RC::Element *m_post;
	RC::Element *m_message;

	void on_cmd_switch_power(RC::Event &);
	void on_cmd_pause(RC::Event &);
	void on_cmd_resume(RC::Event &);
	void on_cmd_save_state(RC::Event &);
	void on_cmd_restore_state(RC::Event &);
	void on_CPU_step(RC::Event &);
	void on_CPU_bp_btn(RC::Event &);
	void on_log_prg_toggle(RC::Event &);
	void on_log_write(RC::Event &);
	void on_mem_dump(RC::Event &);
	void on_cs_dump(RC::Event &);
	void on_ds_dump(RC::Event &);
	void on_ss_dump(RC::Event &);
	void on_es_dump(RC::Event &);
	void on_idt_dump(RC::Event &);
	void on_ldt_dump(RC::Event &);
	void on_gdt_dump(RC::Event &);

	void read_memory(uint32_t _address, uint8_t *_buf, uint _len);

public:

	SysDebugger(GUI * _gui, const char *_rml, Machine *_machine);
	virtual ~SysDebugger();

	virtual void update();
	void show_message(const char* _mex);

private:
	class LogMessage : public Logdev
	{
	private:
		SysDebugger *m_iface;
	public:
		LogMessage(SysDebugger* _iface);
		~LogMessage();

		void log_put(const std::string &_prefix, const std::string &_message);
	};
};

#endif
