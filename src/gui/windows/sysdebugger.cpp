/*
 * Copyright (C) 2015-2021  Marco Bortolin
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

#include "ibmulator.h"
#include "gui.h"
#include "program.h"
#include "hardware/cpu.h"
#include "hardware/cpu/core.h"
#include "hardware/memory.h"
#include "machine.h"
#include "hardware/cpu/debugger.h"
#include "sysdebugger.h"
#include <cstdio>
#include <RmlUi/Core.h>

#include "format.h"


SysDebugger::SysDebugger(GUI * _gui, const char *_rml, Machine *_machine,
		Rml::Element *_button)
:
DebugWindow(_gui, _rml, _button),
m_machine(_machine)
{
}

SysDebugger::~SysDebugger()
{
}

void SysDebugger::create()
{
	DebugTools::DebugWindow::create();

	m_core.eax = get_element("EAX");
	m_core.ebx = get_element("EBX");
	m_core.ecx = get_element("ECX");
	m_core.edx = get_element("EDX");

	m_core.ebp = get_element("EBP");
	m_core.esi = get_element("ESI");
	m_core.edi = get_element("EDI");
	m_core.esp = get_element("ESP");

	m_core.cs = get_element("CS");
	m_core.ds = get_element("DS");
	m_core.ss = get_element("SS");
	m_core.es = get_element("ES");
	m_core.tr = get_element("TR");

	m_core.eip = get_element("EIP");
	m_core.eflags = get_element("EFLAGS");
	m_core.cpl = get_element("CPL");

	m_core.cf = get_element("CF");
	m_core.pf = get_element("PF");
	m_core.af = get_element("AF");
	m_core.zf = get_element("ZF");
	m_core.sf = get_element("SF");
	m_core.tf = get_element("TF");
	m_core.iff = get_element("IF");
	m_core.df = get_element("DF");
	m_core.of = get_element("OF");
	m_core.pl = get_element("PL");
	m_core.nt = get_element("NT");

	m_core.csbase = get_element("CSbase");
	m_core.dsbase = get_element("DSbase");
	m_core.esbase = get_element("ESbase");
	m_core.ssbase = get_element("SSbase");
	m_core.trbase = get_element("TRbase");

	m_core.cslimit = get_element("CSlimit");
	m_core.dslimit = get_element("DSlimit");
	m_core.eslimit = get_element("ESlimit");
	m_core.sslimit = get_element("SSlimit");
	m_core.trlimit = get_element("TRlimit");

	m_core.ldt = get_element("LDT");
	m_core.ldtbase = get_element("LDTbase");
	m_core.ldtlimit = get_element("LDTlimit");
	m_core.idtbase = get_element("IDTbase");
	m_core.idtlimit = get_element("IDTlimit");
	m_core.gdtbase = get_element("GDTbase");
	m_core.gdtlimit = get_element("GDTlimit");

	m_core.a20 = get_element("A20");

	m_memory.cs_eip = get_element("CS_EIP");
	m_memory.ds_esi = get_element("DS_ESI");
	m_memory.es_edi = get_element("ES_EDI");
	m_memory.ss_esp = get_element("SS_ESP");

	m_memory.cs_eip_str = get_element("CS_EIP_str");
	m_memory.ds_esi_str = get_element("DS_ESI_str");
	m_memory.es_edi_str = get_element("ES_EDI_str");
	m_memory.ss_esp_str = get_element("SS_ESP_str");

	m_tools.btn_power = get_element("cmd_switch_power");
	m_tools.led_power = false;
	m_tools.btn_pause = get_element("cmd_pause");
	m_tools.led_pause = false;
	m_tools.btn_bp = get_element("CPU_bp_btn");
	m_tools.log_prg_name =
		dynamic_cast<Rml::ElementFormControlInput*>(get_element("log_prg_name"));
	m_tools.log_prg_toggle = get_element("log_prg_toggle");
	m_tools.cs_bp = dynamic_cast<Rml::ElementFormControlInput*>(get_element("CS_bp"));
	m_tools.eip_bp = dynamic_cast<Rml::ElementFormControlInput*>(get_element("EIP_bp"));
	m_tools.cs_bp->SetValue(format_hex16(0));

	m_disasm.line0 = get_element("disasm");

	m_post = get_element("POST");
	m_message = get_element("message");
}

void SysDebugger::read_memory(uint32_t _address, uint8_t *_buf, uint _len)
{
	assert(_buf);

	while(_len--) {
		*(_buf++) = g_memory.dbg_read_byte(_address++);
	}
}

void SysDebugger::update()
{
	m_core.cs->SetInnerRML(format_hex16(REG_CS.sel.value));
	m_core.ds->SetInnerRML(format_hex16(REG_DS.sel.value));
	m_core.ss->SetInnerRML(format_hex16(REG_SS.sel.value));
	m_core.es->SetInnerRML(format_hex16(REG_ES.sel.value));
	m_core.tr->SetInnerRML(format_hex16(REG_TR.sel.value));

	m_core.cpl->SetInnerRML(format_uint16(CPL));

	m_core.cf->SetInnerRML(format_bit(FLAG_CF));
	m_core.pf->SetInnerRML(format_bit(FLAG_PF));
	m_core.af->SetInnerRML(format_bit(FLAG_AF));
	m_core.zf->SetInnerRML(format_bit(FLAG_ZF));
	m_core.sf->SetInnerRML(format_bit(FLAG_SF));
	m_core.tf->SetInnerRML(format_bit(FLAG_TF));
	m_core.iff->SetInnerRML(format_bit(FLAG_IF));
	m_core.df->SetInnerRML(format_bit(FLAG_DF));
	m_core.of->SetInnerRML(format_bit(FLAG_OF));
	m_core.pl->SetInnerRML(format_uint16(FLAG_IOPL));
	m_core.nt->SetInnerRML(format_bit(FLAG_NT));

	m_core.ldt->SetInnerRML(format_hex16(REG_LDTR.sel.value));
	m_core.ldtlimit->SetInnerRML(format_hex16(GET_LIMIT(LDTR)));
	m_core.idtlimit->SetInnerRML(format_hex16(GET_LIMIT(IDTR)));
	m_core.gdtlimit->SetInnerRML(format_hex16(GET_LIMIT(GDTR)));

	m_core.a20->SetInnerRML(format_bit(g_memory.get_A20_line()));

	static std::string str(10,0);
	m_post->SetInnerRML(str_format(str, "%02X", m_machine->get_POST_code()));

	if(m_machine->is_paused() && m_tools.led_pause==false) {
		m_tools.led_pause = true;
		m_tools.btn_pause->SetClass("on", true);
	} else if(!m_machine->is_paused() && m_tools.led_pause==true){
		m_tools.led_pause = false;
		m_tools.btn_pause->SetClass("on", false);
	}
	if(m_machine->is_on() && m_tools.led_power==false) {
		m_tools.led_power = true;
		m_tools.btn_power->SetClass("on", true);
	} else if(!m_machine->is_on() && m_tools.led_power==true) {
		m_tools.led_power = false;
		m_tools.btn_power->SetClass("on", false);
	}
}

void SysDebugger::show_message(const char* _mex)
{
	std::string str(_mex);
	str_replace_all(str, "\n", "<br />");
	if(str.empty()) {
		str = "&nbsp;";
	}
	m_message->SetInnerRML(str);
}

void SysDebugger::on_cmd_switch_power(Rml::Event &)
{
	m_machine->cmd_switch_power();
}

void SysDebugger::on_cmd_pause(Rml::Event &)
{
	if(m_machine->is_paused()) {
		m_machine->cmd_resume();
	} else {
		m_machine->cmd_pause();
	}
}

void SysDebugger::on_mem_dump(Rml::Event &)
{
	m_machine->cmd_memdump(0, 0);
}

void SysDebugger::on_cs_dump(Rml::Event &)
{
	m_machine->cmd_memdump(REG_CS.desc.base, REG_CS.desc.limit);
}

void SysDebugger::on_ds_dump(Rml::Event &)
{
	m_machine->cmd_memdump(REG_DS.desc.base, REG_DS.desc.limit);
}

void SysDebugger::on_ss_dump(Rml::Event &)
{
	m_machine->cmd_memdump(REG_SS.desc.base, REG_SS.desc.limit);
}

void SysDebugger::on_es_dump(Rml::Event &)
{
	m_machine->cmd_memdump(REG_ES.desc.base, REG_ES.desc.limit);
}

void SysDebugger::on_cmd_save_state(Rml::Event &)
{
	g_program.save_state({QUICKSAVE_RECORD, QUICKSAVE_DESC, "",0}, nullptr, nullptr);
}

void SysDebugger::on_cmd_restore_state(Rml::Event &)
{
	g_program.restore_state({QUICKSAVE_RECORD, QUICKSAVE_DESC, "",0}, nullptr, nullptr);
}

void SysDebugger::on_CPU_step(Rml::Event &)
{
	if(m_machine->is_paused()) {
		m_machine->cmd_cpu_step();
	}
}

void SysDebugger::on_CPU_bp_btn(Rml::Event &)
{
	// this works only in real address mode
	if(!m_tools.btn_bp->IsClassSet("on")) {
		Rml::String cs_str = m_tools.cs_bp->GetValue();
		uint32_t cs,eip;
		if(!sscanf(cs_str.c_str(), "%x", &cs)) {
			m_gui->show_dbg_message("invalid breakpoint Code Segment");
			return;
		}
		Rml::String eip_str = m_tools.eip_bp->GetValue();
		if(!sscanf(eip_str.c_str(), "%x", &eip)) {
			m_gui->show_dbg_message("invalid breakpoint Offset");
			return;
		}

		if(cs > 0) {
			m_machine->cmd_cpu_breakpoint(cs, eip, [&](){
				//TODO this function is called by a different thread!
				m_tools.btn_bp->SetClass("on", false);
			});
			std::stringstream ss;
			ss << "breakpoint set to " << cs_str.c_str() << ":" << eip_str.c_str();
			m_gui->show_dbg_message(ss.str().c_str());
			m_tools.btn_bp->SetClass("on", true);
		}
	} else {
		m_machine->cmd_cpu_breakpoint(0, 0, [](){});
		m_tools.btn_bp->SetClass("on", false);
	}
}

void SysDebugger::on_log_prg_toggle(Rml::Event &)
{
	if(CPULOG) {
		if(m_tools.log_prg_toggle->IsClassSet("on")) {
			m_tools.log_prg_toggle->SetClass("on", false);
			m_machine->cmd_prg_cpulog("");
			m_gui->show_dbg_message("program CPU logging deactivated");
		} else {
			std::string str = m_tools.log_prg_name->GetValue();
			if(str != "") {
				m_tools.log_prg_toggle->SetClass("on", true);
				m_machine->cmd_prg_cpulog(str.c_str());
				m_gui->show_dbg_message("program CPU logging activated");
			} else {
				m_gui->show_dbg_message("specify a program name");
			}
		}
	} else {
		m_gui->show_dbg_message("recompile with CPULOG defined as true in cpu/logger.h");
	}
}

void SysDebugger::on_log_write(Rml::Event &)
{
	if(CPULOG) {
		m_machine->cmd_cpulog();
		m_gui->show_dbg_message("writing CPU log...");
	} else {
		m_gui->show_dbg_message("recompile with CPULOG defined as true in cpu/logger.h");
	}
}

void SysDebugger::on_idt_dump(Rml::Event &)
{
	m_machine->cmd_dtdump("IDT");
}

void SysDebugger::on_ldt_dump(Rml::Event &)
{
	m_machine->cmd_dtdump("LDT");
}

void SysDebugger::on_gdt_dump(Rml::Event &)
{
	m_machine->cmd_dtdump("GDT");
}
