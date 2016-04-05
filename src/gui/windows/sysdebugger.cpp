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
#include <Rocket/Core.h>
#include <Rocket/Controls.h>

#include "format.h"

event_map_t SysDebugger::ms_evt_map = {
	GUI_EVT( "cmd_switch_power", "click", SysDebugger::on_cmd_switch_power ),
	GUI_EVT( "cmd_pause",        "click", SysDebugger::on_cmd_pause ),
	GUI_EVT( "cmd_resume",       "click", SysDebugger::on_cmd_resume ),
	GUI_EVT( "cmd_memdump",      "click", SysDebugger::on_cmd_memdump ),
	GUI_EVT( "cmd_csdump",       "click", SysDebugger::on_cmd_csdump ),
	GUI_EVT( "cmd_save_state",   "click", SysDebugger::on_cmd_save_state ),
	GUI_EVT( "cmd_restore_state","click", SysDebugger::on_cmd_restore_state ),
	GUI_EVT( "CPU_step",         "click", SysDebugger::on_CPU_step ),
	GUI_EVT( "CPU_skip",         "click", SysDebugger::on_CPU_skip ),
	GUI_EVT( "CPU_ff_btn",       "click", SysDebugger::on_CPU_ff_btn ),
	GUI_EVT( "log_prg_toggle",   "click", SysDebugger::on_log_prg_toggle ),
	GUI_EVT( "log_write",        "click", SysDebugger::on_log_write ),
	GUI_EVT( "idt_dump",         "click", SysDebugger::on_idt_dump ),
	GUI_EVT( "ldt_dump",         "click", SysDebugger::on_ldt_dump ),
	GUI_EVT( "gdt_dump",         "click", SysDebugger::on_gdt_dump ),
};

SysDebugger::SysDebugger(Machine *_machine, GUI *_gui)
:
Window(_gui, "debugger.rml")
{
	assert(m_wnd);

	m_wnd->AddEventListener("click", this, false);

	m_machine = _machine;

	m_core.ax = get_element("AX");
	m_core.bx = get_element("BX");
	m_core.cx = get_element("CX");
	m_core.dx = get_element("DX");

	m_core.bp = get_element("BP");
	m_core.si = get_element("SI");
	m_core.di = get_element("DI");
	m_core.sp = get_element("SP");

	m_core.cs = get_element("CS");
	m_core.ds = get_element("DS");
	m_core.ss = get_element("SS");
	m_core.es = get_element("ES");
	m_core.tr = get_element("TR");

	m_core.ip = get_element("IP");
	m_core.f = get_element("F");
	m_core.msw = get_element("MSW");
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

	m_memory.cs_ip = get_element("CS_IP");
	m_memory.ds_si = get_element("DS_SI");
	m_memory.es_di = get_element("ES_DI");
	m_memory.ss_sp = get_element("SS_SP");

	m_memory.cs_ip_str = get_element("CS_IP_str");
	m_memory.ds_si_str = get_element("DS_SI_str");
	m_memory.es_di_str = get_element("ES_DI_str");
	m_memory.ss_sp_str = get_element("SS_SP_str");

	m_tools.cs_ff = dynamic_cast<Rocket::Controls::ElementFormControlInput*>(get_element("CS_ff"));
	m_tools.ip_ff = dynamic_cast<Rocket::Controls::ElementFormControlInput*>(get_element("IP_ff"));
	m_tools.cs_ff->SetValue(format_hex16(REG_CS.sel.value));
	m_tools.ip_ff->SetValue(format_hex16(REG_IP));
	m_tools.log_prg_name =
		dynamic_cast<Rocket::Controls::ElementFormControlInput*>(get_element("log_prg_name"));
	m_tools.log_prg_toggle = get_element("log_prg_toggle");

	m_disasm.line0 = get_element("disasm");

	m_post = get_element("POST");
}

SysDebugger::~SysDebugger()
{
}

void SysDebugger::read_memory(uint32_t _address, uint8_t *_buf, uint _len)
{
	assert(_buf);

	while(_len--) {
		*(_buf++) = g_memory.read_byte_notraps(_address++);
	}
}

const Rocket::Core::String & SysDebugger::disasm(uint16_t _selector, uint16_t _ip, bool _analyze, uint * _size)
{
	CPUDebugger debugger;

	static Rocket::Core::String str;

	str = "";

	static char empty = 0;

	uint32_t start = debugger.get_address(_selector, _ip, &g_cpucore);
	char dline[200];
	uint size = debugger.disasm(dline, 200, start, _ip, &g_memory);
	if(_size!=nullptr) {
		*_size = size;
	}

	char *res = &empty;

	if(_analyze) {
		res = debugger.analyze_instruction(dline, true, &g_cpucore, &g_memory);
		if(!res || !(*res))
			res = &empty;
	}

	dline[30] = 0;

	str.FormatString(100, "%04X:%04X &nbsp; %s &nbsp; %s",_selector,_ip, dline, res);

	return str;
};

void SysDebugger::update()
{
	m_core.ax->SetInnerRML(format_hex16(REG_AX));
	m_core.bx->SetInnerRML(format_hex16(REG_BX));
	m_core.cx->SetInnerRML(format_hex16(REG_CX));
	m_core.dx->SetInnerRML(format_hex16(REG_DX));

	m_core.bp->SetInnerRML(format_hex16(REG_BP));
	m_core.si->SetInnerRML(format_hex16(REG_SI));
	m_core.di->SetInnerRML(format_hex16(REG_DI));
	m_core.sp->SetInnerRML(format_hex16(REG_SP));

	m_core.cs->SetInnerRML(format_hex16(REG_CS.sel.value));
	m_core.ds->SetInnerRML(format_hex16(REG_DS.sel.value));
	m_core.ss->SetInnerRML(format_hex16(REG_SS.sel.value));
	m_core.es->SetInnerRML(format_hex16(REG_ES.sel.value));
	m_core.tr->SetInnerRML(format_hex16(REG_TR.sel.value));

	m_core.ip->SetInnerRML(format_hex16(REG_IP));
	m_core.f->SetInnerRML(format_bin16(GET_FLAG(ALL)));
	m_core.msw->SetInnerRML(format_bin4(GET_MSW(MSW_ALL)));
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

	m_core.csbase->SetInnerRML(format_hex24(GET_BASE(CS)));
	m_core.dsbase->SetInnerRML(format_hex24(GET_BASE(DS)));
	m_core.esbase->SetInnerRML(format_hex24(GET_BASE(ES)));
	m_core.ssbase->SetInnerRML(format_hex24(GET_BASE(SS)));
	m_core.trbase->SetInnerRML(format_hex24(GET_BASE(TR)));

	m_core.cslimit->SetInnerRML(format_hex16(GET_LIMIT(CS)));
	m_core.dslimit->SetInnerRML(format_hex16(GET_LIMIT(DS)));
	m_core.eslimit->SetInnerRML(format_hex16(GET_LIMIT(ES)));
	m_core.sslimit->SetInnerRML(format_hex16(GET_LIMIT(SS)));
	m_core.trlimit->SetInnerRML(format_hex16(GET_LIMIT(TR)));

	m_core.ldt->SetInnerRML(format_hex16(REG_LDTR.sel.value));
	m_core.ldtbase->SetInnerRML(format_hex24(GET_BASE(LDTR)));
	m_core.ldtlimit->SetInnerRML(format_hex16(GET_LIMIT(LDTR)));
	m_core.idtbase->SetInnerRML(format_hex24(GET_BASE(IDTR)));
	m_core.idtlimit->SetInnerRML(format_hex16(GET_LIMIT(IDTR)));
	m_core.gdtbase->SetInnerRML(format_hex24(GET_BASE(GDTR)));
	m_core.gdtlimit->SetInnerRML(format_hex16(GET_LIMIT(GDTR)));

	m_core.a20->SetInnerRML(format_bit(g_memory.get_A20_line()));

	const uint len = 12;
	uint8_t buf[len];

	read_memory(GET_PHYADDR(CS, REG_IP), buf, len);
	m_memory.cs_ip->SetInnerRML(format_words(buf, len));
	m_memory.cs_ip_str->SetInnerRML(format_words_string(buf, len));

	read_memory(GET_PHYADDR(DS, REG_SI), buf, len);
	m_memory.ds_si->SetInnerRML(format_words(buf, len));
	m_memory.ds_si_str->SetInnerRML(format_words_string(buf, len));

	read_memory(GET_PHYADDR(ES, REG_DI), buf, len);
	m_memory.es_di->SetInnerRML(format_words(buf, len));
	m_memory.es_di_str->SetInnerRML(format_words_string(buf, len));

	read_memory(GET_PHYADDR(SS, REG_SP), buf, len);
	m_memory.ss_sp->SetInnerRML(format_words(buf, len));
	m_memory.ss_sp_str->SetInnerRML(format_words_string(buf, len));

	Rocket::Core::String str;
	uint size;
	uint16_t nextip = REG_IP;
	str = disasm(REG_CS.sel.value, nextip, true, &size) + "<br />";
	nextip += size;
	str += disasm(REG_CS.sel.value, nextip, false, &size) + "<br />";
	nextip += size;
	str += disasm(REG_CS.sel.value, nextip, false, nullptr);
	m_disasm.line0->SetInnerRML(str);

	str.FormatString(3, "%02X", m_gui->machine()->get_POST_code());
	m_post->SetInnerRML(str);
}

void SysDebugger::on_cmd_switch_power(RC::Event &)
{
	m_machine->cmd_switch_power();
}

void SysDebugger::on_cmd_pause(RC::Event &)
{
	m_machine->cmd_pause();
}

void SysDebugger::on_cmd_resume(RC::Event &)
{
	m_machine->cmd_resume();
}

void SysDebugger::on_cmd_memdump(RC::Event &)
{
	m_machine->cmd_memdump(0, 0);
}

void SysDebugger::on_cmd_csdump(RC::Event &)
{
	m_machine->cmd_memdump(REG_CS.desc.base, REG_CS.desc.limit);
}

void SysDebugger::on_cmd_save_state(RC::Event &)
{
	g_program.save_state("", nullptr, nullptr);
}

void SysDebugger::on_cmd_restore_state(RC::Event &)
{
	g_program.restore_state("", nullptr, nullptr);
}

void SysDebugger::on_CPU_step(RC::Event &)
{
	m_machine->cmd_cpu_step();
}

void SysDebugger::on_CPU_skip(RC::Event &)
{
	uint size;
	disasm(REG_CS.sel.value, REG_IP, false, &size);
	uint32_t curphy = GET_PHYADDR(CS, REG_IP);
	m_machine->cmd_cpu_step_to(curphy+size);
}

void SysDebugger::on_CPU_ff_btn(RC::Event &)
{
	Rocket::Core::String str = m_tools.cs_ff->GetValue();
	uint cs,ip;
	if(!sscanf(str.CString(), "%x", &cs))
		return;
	str = m_tools.ip_ff->GetValue();
	if(!sscanf(str.CString(), "%x", &ip))
		return;

	uint32_t phy = (cs<<4) + ip;

	m_machine->cmd_cpu_step_to(phy);
}

void SysDebugger::on_log_prg_toggle(RC::Event &)
{
	if(m_tools.log_prg_toggle->IsClassSet("on")) {
		m_tools.log_prg_toggle->SetClass("on", false);
		m_machine->cmd_prg_cpulog("");
	} else {
		Rocket::Core::String str = m_tools.log_prg_name->GetValue();
		if(str != "") {
			m_tools.log_prg_toggle->SetClass("on", true);
			m_machine->cmd_prg_cpulog(str.CString());
		}
	}
}

void SysDebugger::on_log_write(RC::Event &)
{
	m_machine->cmd_cpulog();
}

void SysDebugger::on_idt_dump(RC::Event &)
{
	m_machine->cmd_dtdump("IDT");
}

void SysDebugger::on_ldt_dump(RC::Event &)
{
	m_machine->cmd_dtdump("LDT");
}

void SysDebugger::on_gdt_dump(RC::Event &)
{
	m_machine->cmd_dtdump("GDT");
}
