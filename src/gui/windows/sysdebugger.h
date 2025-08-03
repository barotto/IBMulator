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

#ifndef IBMULATOR_GUI_DEBUGGER_H
#define IBMULATOR_GUI_DEBUGGER_H

#include "debugtools.h"

class Machine;
class GUI;


class SysDebugger : public DebugTools::DebugWindow
{
	friend class SysDebugger286;
	friend class SysDebugger386;

private:
	Machine *m_machine;

	struct s_core {
		Rml::Element *eax,*ebx,*ecx,*edx,
		             *ebp,*esi,*edi,*esp,
		             *cs,*ds,*ss,*es,*tr,
		             *eip,*eflags,*cpl,
		             *cf, *pf, *af, *zf, *sf, *tf,
		             *iff, *df, *of, *pl, *nt,
		             *csbase,*dsbase,*esbase,*ssbase,*trbase,
		             *cslimit,*dslimit,*eslimit,*sslimit,*trlimit,
		             *ldt, *ldtbase, *ldtlimit,
		             *idtbase, *idtlimit,
		             *gdtbase, *gdtlimit,
		             *a20;
	} m_core = {};

	struct s_memory {
		Rml::Element *cs_eip, *cs_eip_str,
		             *ds_esi, *ds_esi_str,
		             *es_edi, *es_edi_str,
		             *ss_esp, *ss_esp_str;
	} m_memory = {};

	struct s_tools {
		Rml::Element *btn_power, *btn_pause, *btn_bp;
		bool led_power, led_pause;
		Rml::ElementFormControl *log_prg_name;
		Rml::Element *log_prg_toggle;
		Rml::ElementFormControl *cs_bp,*eip_bp;
	} m_tools = {};

	struct s_disasm {
		Rml::Element *line0;
	} m_disasm = {};

	Rml::Element *m_post = nullptr;
	Rml::Element *m_message = nullptr;

public:
	SysDebugger(GUI * _gui, const char *_rml, Machine *_machine, Rml::Element *_button);

	void update() override;

	void show_message(const char* _mex);

protected:
	void create() override;

private:
	void on_cmd_switch_power(Rml::Event &);
	void on_cmd_pause(Rml::Event &);
	void on_cmd_resume(Rml::Event &);
	void on_cmd_save_state(Rml::Event &);
	void on_cmd_restore_state(Rml::Event &);
	void on_CPU_step(Rml::Event &);
	void on_CPU_bp_btn(Rml::Event &);
	void on_log_prg_toggle(Rml::Event &);
	void on_log_write(Rml::Event &);
	void on_mem_dump(Rml::Event &);
	void on_cs_dump(Rml::Event &);
	void on_ds_dump(Rml::Event &);
	void on_ss_dump(Rml::Event &);
	void on_es_dump(Rml::Event &);
	void on_idt_dump(Rml::Event &);
	void on_ldt_dump(Rml::Event &);
	void on_gdt_dump(Rml::Event &);
	void on_close(Rml::Event &);

	void read_memory(uint32_t _address, uint8_t *_buf, uint _len);
};


#endif
