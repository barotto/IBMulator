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
#include "sysdebugger286.h"
#include <cstdio>
#include <RmlUi/Core.h>

#include "format.h"


event_map_t SysDebugger286::ms_evt_map = {
	GUI_EVT( "cmd_switch_power", "click", SysDebugger::on_cmd_switch_power ),
	GUI_EVT( "cmd_pause",        "click", SysDebugger::on_cmd_pause ),
	GUI_EVT( "cmd_save_state",   "click", SysDebugger::on_cmd_save_state ),
	GUI_EVT( "cmd_restore_state","click", SysDebugger::on_cmd_restore_state ),
	GUI_EVT( "CPU_step",         "click", SysDebugger::on_CPU_step ),
	GUI_EVT( "CPU_skip",         "click", SysDebugger286::on_CPU_skip ),
	GUI_EVT( "CPU_bp_btn",       "click", SysDebugger::on_CPU_bp_btn ),
	GUI_EVT( "log_prg_toggle",   "click", SysDebugger::on_log_prg_toggle ),
	GUI_EVT( "log_write",        "click", SysDebugger::on_log_write ),
	GUI_EVT( "mem_dump",         "click", SysDebugger::on_mem_dump ),
	GUI_EVT( "cs_dump",          "click", SysDebugger::on_cs_dump ),
	GUI_EVT( "ds_dump",          "click", SysDebugger::on_ds_dump ),
	GUI_EVT( "ss_dump",          "click", SysDebugger::on_ss_dump ),
	GUI_EVT( "es_dump",          "click", SysDebugger::on_es_dump ),
	GUI_EVT( "idt_dump",         "click", SysDebugger::on_idt_dump ),
	GUI_EVT( "ldt_dump",         "click", SysDebugger::on_ldt_dump ),
	GUI_EVT( "gdt_dump",         "click", SysDebugger::on_gdt_dump ),
	GUI_EVT( "close",            "click", DebugTools::DebugWindow::on_cancel ),
	GUI_EVT( "*",              "keydown", Window::on_keydown )
};

SysDebugger286::SysDebugger286(GUI *_gui, Machine *_machine, Rml::Element *_button)
:
SysDebugger(_gui, "debugger286.rml", _machine,  _button)
{
}

SysDebugger286::~SysDebugger286()
{
}

void SysDebugger286::create()
{
	SysDebugger::create();

	m_tools.eip_bp->SetValue(format_hex16(0));
	m_286core.msw = get_element("MSW");
}

const std::string & SysDebugger286::disasm(uint16_t _ip, bool _analyze, uint * _size)
{
	CPUDebugger debugger;
	static char empty = 0;

	char dline[200];
	unsigned size = debugger.disasm(dline, 200, REG_CS.desc.base, _ip, nullptr, &g_memory, nullptr, 0, false);
	if(_size!=nullptr) {
		*_size = size;
	}

	char *res = &empty;

	if(_analyze) {
		res = debugger.analyze_instruction(dline, &g_cpucore, &g_memory, 16);
		if(!res || !(*res))
			res = &empty;
	}

	dline[30] = 0;

	static std::string str(100,0);
	str_format(str, "%04X:%04X &nbsp; %s &nbsp; %s", REG_CS.sel.value, _ip, dline, res);

	return str;
}

void SysDebugger286::update()
{
	if(!m_enabled) {
		return;
	}

	SysDebugger::update();

	m_core.eax->SetInnerRML(format_hex16(REG_AX));
	m_core.ebx->SetInnerRML(format_hex16(REG_BX));
	m_core.ecx->SetInnerRML(format_hex16(REG_CX));
	m_core.edx->SetInnerRML(format_hex16(REG_DX));

	m_core.ebp->SetInnerRML(format_hex16(REG_BP));
	m_core.esi->SetInnerRML(format_hex16(REG_SI));
	m_core.edi->SetInnerRML(format_hex16(REG_DI));
	m_core.esp->SetInnerRML(format_hex16(REG_SP));

	m_286core.msw->SetInnerRML(format_bin4(GET_MSW()));

	m_core.eip->SetInnerRML(format_hex16(REG_IP));
	m_core.eflags->SetInnerRML(format_hex16(GET_FLAGS()));

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

	m_core.ldtbase->SetInnerRML(format_hex24(GET_BASE(LDTR)));
	m_core.idtbase->SetInnerRML(format_hex24(GET_BASE(IDTR)));
	m_core.gdtbase->SetInnerRML(format_hex24(GET_BASE(GDTR)));

	const uint len = 12;
	uint8_t buf[len];

	// no #PF in 286 mode
	read_memory(DBG_GET_PHYADDR(CS, REG_IP), buf, len);
	m_memory.cs_eip->SetInnerRML(format_words(buf, len));
	m_memory.cs_eip_str->SetInnerRML(format_words_string(buf, len));

	read_memory(DBG_GET_PHYADDR(DS, REG_SI), buf, len);
	m_memory.ds_esi->SetInnerRML(format_words(buf, len));
	m_memory.ds_esi_str->SetInnerRML(format_words_string(buf, len));

	read_memory(DBG_GET_PHYADDR(ES, REG_DI), buf, len);
	m_memory.es_edi->SetInnerRML(format_words(buf, len));
	m_memory.es_edi_str->SetInnerRML(format_words_string(buf, len));

	read_memory(DBG_GET_PHYADDR(SS, REG_SP), buf, len);
	m_memory.ss_esp->SetInnerRML(format_words(buf, len));
	m_memory.ss_esp_str->SetInnerRML(format_words_string(buf, len));

	std::string str;
	uint size;
	uint16_t nextip = REG_IP;
	str = disasm(nextip, true, &size) + "<br />";
	nextip += size;
	str += disasm(nextip, false, &size) + "<br />";
	nextip += size;
	str += disasm(nextip, false, nullptr);
	m_disasm.line0->SetInnerRML(str);
}

void SysDebugger286::on_CPU_skip(Rml::Event &)
{
	if(m_machine->is_paused()) {
		uint size;
		disasm(REG_IP, false, &size);
		m_machine->cmd_cpu_breakpoint(REG_CS.sel.value, REG_IP+size, [](){});
		m_tools.btn_bp->SetClass("on", false);
		m_machine->cmd_resume();
	}
}

