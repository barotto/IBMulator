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
#include <Rocket/Core.h>
#include <sstream>

#include "hardware/devices/pic.h"
#include "hardware/devices/pit.h"
#include "format.h"

DevStatus::DevStatus(GUI * _gui)
:
Window(_gui, "devstatus.rml")
{
	assert(m_wnd);

	m_pic.irq_e[0] = get_element("pic_irq_0");
	m_pic.irq_e[1] = get_element("pic_irq_1");
	m_pic.irq_e[2] = get_element("pic_irq_2");
	m_pic.irq_e[3] = get_element("pic_irq_3");
	m_pic.irq_e[4] = get_element("pic_irq_4");
	m_pic.irq_e[5] = get_element("pic_irq_5");
	m_pic.irq_e[6] = get_element("pic_irq_6");
	m_pic.irq_e[7] = get_element("pic_irq_7");
	m_pic.irq_e[8] = get_element("pic_irq_8");
	m_pic.irq_e[9] = get_element("pic_irq_9");
	m_pic.irq_e[10] = get_element("pic_irq_10");
	m_pic.irq_e[11] = get_element("pic_irq_11");
	m_pic.irq_e[12] = get_element("pic_irq_12");
	m_pic.irq_e[13] = get_element("pic_irq_13");
	m_pic.irq_e[14] = get_element("pic_irq_14");
	m_pic.irq_e[15] = get_element("pic_irq_15");

	m_pic.irq = 0;

	m_pic.irr_e[0] = get_element("pic_irr_0");
	m_pic.irr_e[1] = get_element("pic_irr_1");
	m_pic.irr_e[2] = get_element("pic_irr_2");
	m_pic.irr_e[3] = get_element("pic_irr_3");
	m_pic.irr_e[4] = get_element("pic_irr_4");
	m_pic.irr_e[5] = get_element("pic_irr_5");
	m_pic.irr_e[6] = get_element("pic_irr_6");
	m_pic.irr_e[7] = get_element("pic_irr_7");
	m_pic.irr_e[8] = get_element("pic_irr_8");
	m_pic.irr_e[9] = get_element("pic_irr_9");
	m_pic.irr_e[10] = get_element("pic_irr_10");
	m_pic.irr_e[11] = get_element("pic_irr_11");
	m_pic.irr_e[12] = get_element("pic_irr_12");
	m_pic.irr_e[13] = get_element("pic_irr_13");
	m_pic.irr_e[14] = get_element("pic_irr_14");
	m_pic.irr_e[15] = get_element("pic_irr_15");

	m_pic.irr = 0;

	m_pic.imr_e[0] = get_element("pic_imr_0");
	m_pic.imr_e[1] = get_element("pic_imr_1");
	m_pic.imr_e[2] = get_element("pic_imr_2");
	m_pic.imr_e[3] = get_element("pic_imr_3");
	m_pic.imr_e[4] = get_element("pic_imr_4");
	m_pic.imr_e[5] = get_element("pic_imr_5");
	m_pic.imr_e[6] = get_element("pic_imr_6");
	m_pic.imr_e[7] = get_element("pic_imr_7");
	m_pic.imr_e[8] = get_element("pic_imr_8");
	m_pic.imr_e[9] = get_element("pic_imr_9");
	m_pic.imr_e[10] = get_element("pic_imr_10");
	m_pic.imr_e[11] = get_element("pic_imr_11");
	m_pic.imr_e[12] = get_element("pic_imr_12");
	m_pic.imr_e[13] = get_element("pic_imr_13");
	m_pic.imr_e[14] = get_element("pic_imr_14");
	m_pic.imr_e[15] = get_element("pic_imr_15");

	m_pic.imr = 0;

	m_pic.isr_e[0] = get_element("pic_isr_0");
	m_pic.isr_e[1] = get_element("pic_isr_1");
	m_pic.isr_e[2] = get_element("pic_isr_2");
	m_pic.isr_e[3] = get_element("pic_isr_3");
	m_pic.isr_e[4] = get_element("pic_isr_4");
	m_pic.isr_e[5] = get_element("pic_isr_5");
	m_pic.isr_e[6] = get_element("pic_isr_6");
	m_pic.isr_e[7] = get_element("pic_isr_7");
	m_pic.isr_e[8] = get_element("pic_isr_8");
	m_pic.isr_e[9] = get_element("pic_isr_9");
	m_pic.isr_e[10] = get_element("pic_isr_10");
	m_pic.isr_e[11] = get_element("pic_isr_11");
	m_pic.isr_e[12] = get_element("pic_isr_12");
	m_pic.isr_e[13] = get_element("pic_isr_13");
	m_pic.isr_e[14] = get_element("pic_isr_14");
	m_pic.isr_e[15] = get_element("pic_isr_15");

	m_pic.isr = 0;

	m_pit.mode[0] = get_element("pit_0_mode");
	m_pit.mode[1] = get_element("pit_1_mode");
	m_pit.mode[2] = get_element("pit_2_mode");
	m_pit.cnt[0] = get_element("pit_0_cnt");
	m_pit.cnt[1] = get_element("pit_1_cnt");
	m_pit.cnt[2] = get_element("pit_2_cnt");
	m_pit.gate[0] = get_element("pit_0_gate");
	m_pit.gate[1] = get_element("pit_1_gate");
	m_pit.gate[2] = get_element("pit_2_gate");
	m_pit.out[0] = get_element("pit_0_out");
	m_pit.out[1] = get_element("pit_1_out");
	m_pit.out[2] = get_element("pit_2_out");
	m_pit.in[0] = get_element("pit_0_in");
	m_pit.in[1] = get_element("pit_1_in");
	m_pit.in[2] = get_element("pit_2_in");
}

DevStatus::~DevStatus()
{
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
