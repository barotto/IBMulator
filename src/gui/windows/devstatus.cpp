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
#include "program.h"
#include "gui.h"
#include "machine.h"
#include "devstatus.h"
#include "filesys.h"
#include <Rocket/Core.h>
#include <sstream>

#include "hardware/devices/pic.h"
#include "hardware/devices/pit.h"
#include "hardware/devices/vga.h"
#include "format.h"

event_map_t DevStatus::ms_evt_map = {
	GUI_EVT( "cmd_dump_vga_state", "click", DevStatus::on_cmd_dump_vga_state ),
	GUI_EVT( "close", "click", DebugTools::DebugWindow::on_close )
};

DevStatus::DevStatus(GUI * _gui, RC::Element *_button)
:
DebugTools::DebugWindow(_gui, "devstatus.rml", _button)
{
	assert(m_wnd);

	char buf[3];
	for(int i=0; i<16; i++) {
		snprintf(buf, 3, "%d", i);
		m_pic.irq_e[i] = get_element(RC::String("pic_irq_")+buf);
		m_pic.irr_e[i] = get_element(RC::String("pic_irr_")+buf);
		m_pic.imr_e[i] = get_element(RC::String("pic_imr_")+buf);
		m_pic.isr_e[i] = get_element(RC::String("pic_isr_")+buf);
		if(i<3) {
			m_pit.mode[i] = get_element(RC::String("pit_")+buf+"_mode");
			m_pit.cnt[i] = get_element(RC::String("pit_")+buf+"_cnt");
			m_pit.gate[i] = get_element(RC::String("pit_")+buf+"_gate");
			m_pit.out[i] = get_element(RC::String("pit_")+buf+"_out");
			m_pit.in[i] = get_element(RC::String("pit_")+buf+"_in");
		}
	}
	m_pic.irq = 0;
	m_pic.irr = 0;
	m_pic.imr = 0;
	m_pic.isr = 0;
}

DevStatus::~DevStatus()
{
}

void DevStatus::on_cmd_dump_vga_state(RC::Event &)
{
	try {
		std::string filepath = FileSys::get_next_filename(g_program.config().get_cfg_home(), "vga_state_", ".txt");
		m_gui->machine()->devices().vga()->state_to_textfile(filepath);
		std::string mex = "VGA state dumped to " + filepath;
		PINFOF(LOG_V0, LOG_GUI, "%s\n", mex.c_str());
		m_gui->show_message(mex.c_str());
	} catch(std::exception &) {}
}

void DevStatus::update_pic(uint16_t _irq, uint16_t _irr, uint16_t _imr, uint16_t _isr, uint _irqn)
{
	assert(_irqn<16);
	bool mybit, picbit;

	picbit = (_irq>>_irqn) & 1;
	mybit = (m_pic.irq>>_irqn) & 1;

	if(mybit && !picbit) {
		m_pic.irq_e[_irqn]->SetClass("led_active", false);
	} else if(!mybit && picbit) {
		m_pic.irq_e[_irqn]->SetClass("led_active", true);
	}

	picbit = (_irr>>_irqn) & 1;
	mybit = (m_pic.irr>>_irqn) & 1;

	if(mybit && !picbit) {
		m_pic.irr_e[_irqn]->SetClass("led_active", false);
	} else if(!mybit && picbit) {
		m_pic.irr_e[_irqn]->SetClass("led_active", true);
	}

	picbit = (_imr>>_irqn) & 1;
	mybit = (m_pic.imr>>_irqn) & 1;

	if(mybit && !picbit) {
		m_pic.imr_e[_irqn]->SetClass("led_active", false);
	} else if(!mybit && picbit) {
		m_pic.imr_e[_irqn]->SetClass("led_active", true);
	}

	picbit = (_isr >>_irqn) & 1;
	mybit = (m_pic.isr>>_irqn) & 1;

	if(mybit && !picbit) {
		m_pic.isr_e[_irqn]->SetClass("led_active", false);
	} else if(!mybit && picbit) {
		m_pic.isr_e[_irqn]->SetClass("led_active", true);
	}

}

void DevStatus::update_pit(uint cnt)
{
	const PIT_82C54 & timer = m_gui->machine()->devices().pit()->get_timer();

	m_pit.mode[cnt]->SetInnerRML(format_uint16(timer.read_mode(cnt)));
	m_pit.cnt[cnt]->SetInnerRML(format_hex32(timer.read_CNT(cnt)));
	bool gate = timer.read_GATE(cnt);
	bool out = timer.read_OUT(cnt);
	m_pit.gate[cnt]->SetInnerRML(format_bit(gate));
	m_pit.out[cnt]->SetInnerRML(format_bit(out));
	if(gate && out) {
		m_pit.out[cnt]->SetClass("led_active", true);
	} else {
		m_pit.out[cnt]->SetClass("led_active", false);
	}
	m_pit.in[cnt]->SetInnerRML(format_hex16(timer.read_inlatch(cnt)));
}

void DevStatus::update()
{
	if(!m_enabled) {
		return;
	}

	PIC *pic = m_gui->machine()->devices().pic();
	uint16_t pic_irq = pic->get_irq();
	uint16_t pic_irr = pic->get_irr();
	uint16_t pic_imr = pic->get_imr();
	uint16_t pic_isr = pic->get_isr();
	for(uint i=0; i<16; i++) {
		update_pic(pic_irq, pic_irr, pic_imr, pic_isr, i);
	}
	m_pic.irq = pic_irq;
	m_pic.irr = pic_irr;
	m_pic.imr = pic_imr;
	m_pic.isr = pic_isr;

	for(uint i=0; i<3; i++) {
		update_pit(i);
	}
}
