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
#include "program.h"
#include "gui.h"
#include "machine.h"
#include "devstatus.h"
#include "filesys.h"
#include <RmlUi/Core.h>
#include <sstream>

#include "hardware/devices/pic.h"
#include "hardware/devices/pit.h"
#include "hardware/devices/vga.h"
#include "format.h"

event_map_t DevStatus::ms_evt_map = {
	GUI_EVT( "cmd_vga_dump_state", "click", DevStatus::on_cmd_vga_dump_state ),
	GUI_EVT( "cmd_vga_screenshot", "click", DevStatus::on_cmd_vga_screenshot ),
	GUI_EVT( "cmd_vga_update",     "click", DevStatus::on_cmd_vga_update ),
	GUI_EVT( "cmd_pit_update",     "click", DevStatus::on_cmd_pit_update ),
	GUI_EVT( "cmd_pic_update",     "click", DevStatus::on_cmd_pic_update ),
	GUI_EVT( "close",              "click", DebugTools::DebugWindow::on_cancel ),
	GUI_EVT( "*",                "keydown", Window::on_keydown )
};

DevStatus::DevStatus(GUI * _gui, Rml::Element *_button, Machine *_machine)
:
DebugTools::DebugWindow(_gui, "devstatus.rml", _button),
m_machine(_machine)
{
}

DevStatus::~DevStatus()
{
}

void DevStatus::create()
{
	DebugTools::DebugWindow::create();

	m_vga.is_running = false;
	m_vga.btn_update = get_element("cmd_vga_update");
	
	m_vga.mode   = get_element("vga_mode");
	m_vga.screen = get_element("vga_screen");
	
	m_vga.htotal = get_element("vga_htotal");
	m_vga.hdend  = get_element("vga_hdend");
	m_vga.hblank = get_element("vga_hblank");
	m_vga.hretr  = get_element("vga_hretr");
	m_vga.vtotal = get_element("vga_vtotal");
	m_vga.vdend  = get_element("vga_vdend");
	m_vga.vblank = get_element("vga_vblank");
	m_vga.vretr  = get_element("vga_vretr");
	
	m_vga.startaddr_hi = get_element("vga_startaddr_hi");
	m_vga.startaddr_lo = get_element("vga_startaddr_lo");
	m_vga.startaddr_latch = get_element("vga_startaddr_latch");
	
	m_vga.scanl = get_element("vga_scanl");
	m_vga.disp_phase  = get_element("vga_disp_phase");
	m_vga.hretr_phase = get_element("vga_hretr_phase");
	m_vga.vretr_phase = get_element("vga_vretr_phase");
	
	m_pit.is_running = false;
	m_pit.btn_update = get_element("cmd_pit_update");
	m_pic.is_running = false;
	m_pic.btn_update = get_element("cmd_pic_update");
	
	for(int i=0; i<16; i++) {
		m_pic.irq_e[i] = get_element(str_format("pic_irq_%d", i));
		m_pic.irr_e[i] = get_element(str_format("pic_irr_%d", i));
		m_pic.imr_e[i] = get_element(str_format("pic_imr_%d", i));
		m_pic.isr_e[i] = get_element(str_format("pic_isr_%d", i));
		if(i<3) {
			m_pit.mode[i] = get_element(str_format("pit_%d_mode", i));
			m_pit.cnt[i]  = get_element(str_format("pit_%d_cnt", i));
			m_pit.gate[i] = get_element(str_format("pit_%d_gate", i));
			m_pit.out[i]  = get_element(str_format("pit_%d_out", i));
			m_pit.in[i]   = get_element(str_format("pit_%d_in", i));
		}
	}
	m_pic.irq = 0;
	m_pic.irr = 0;
	m_pic.imr = 0;
	m_pic.isr = 0;
	
	m_vga.frame_cnt  = get_element("vga_frame_cnt");
	m_vga.pix_upd    = get_element("vga_pix_upd");
	m_vga.upd        = get_element("vga_upd");
	m_vga.saddr_line = get_element("vga_saddr_line");
	m_vga.pal_line   = get_element("vga_pal_line");
}

void DevStatus::on_cmd_vga_update(Rml::Event &)
{
	m_vga.is_running = !m_vga.is_running;
	m_vga.btn_update->SetClass("on", m_vga.is_running);
}

void DevStatus::on_cmd_vga_dump_state(Rml::Event &)
{
	try {
		std::string captpath = g_program.config().find_file(CAPTURE_SECTION, CAPTURE_DIR);
		std::string statefile = FileSys::get_next_filename(captpath, "vga_state_", ".txt");
		if(!statefile.empty()) {
			m_gui->save_framebuffer(statefile + ".png", statefile + ".pal.png");
			m_machine->devices().vga()->state_to_textfile(statefile);
			std::string mex = "VGA state dumped to " + statefile;
			PINFOF(LOG_V0, LOG_GUI, "%s\n", mex.c_str());
			m_gui->show_message(mex.c_str());
		}
	} catch(std::exception &) {}
}

void DevStatus::on_cmd_vga_screenshot(Rml::Event &)
{
	m_gui->take_screenshot(true);
}

void DevStatus::on_cmd_pit_update(Rml::Event &)
{
	m_pit.is_running = !m_pit.is_running;
	m_pit.btn_update->SetClass("on", m_pit.is_running);
}

void DevStatus::on_cmd_pic_update(Rml::Event &)
{
	m_pic.is_running = !m_pic.is_running;
	m_pic.btn_update->SetClass("on", m_pic.is_running);
}

void DevStatus::update_pic()
{
	PIC *pic = m_machine->devices().pic();
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

void DevStatus::update_pit()
{
	for(unsigned i=0; i<3; i++) {
		update_pit(i);
	}
}

void DevStatus::update_pit(unsigned cnt)
{
	const PIT *pit = m_machine->devices().pit();
	if(!pit) {
		assert(false);
		return;
	}

	m_pit.mode[cnt]->SetInnerRML(format_uint16(pit->read_mode(cnt)));
	m_pit.cnt[cnt]->SetInnerRML(format_hex32(pit->read_CNT(cnt)));
	bool gate = pit->read_GATE(cnt);
	bool out = pit->read_OUT(cnt);
	m_pit.gate[cnt]->SetInnerRML(format_bit(gate));
	m_pit.out[cnt]->SetInnerRML(format_bit(out));
	if(gate && out) {
		m_pit.out[cnt]->SetClass("led_active", true);
	} else {
		m_pit.out[cnt]->SetClass("led_active", false);
	}
	m_pit.in[cnt]->SetInnerRML(format_hex16(pit->read_inlatch(cnt)));
}

void DevStatus::update_vga()
{
	VGA *vga = m_machine->devices().vga();
	const VideoModeInfo & vm = vga->video_mode();
	static std::string str(100,'0');

	if(vm.mode == VGA_M_TEXT) {
		str_format(str, "%ux%u %s %ux%u %ux%u",
			vm.imgw, vm.imgh, vga->current_mode_string(),
			vm.textcols, vm.textrows,
			vm.cwidth, vm.cheight);
	} else {
		str_format(str, "%ux%u %s",
			vm.imgw, vm.imgh, vga->current_mode_string());
	}
	m_vga.mode->SetInnerRML(str);

	const VideoTimings & vt = vga->timings();
	m_vga.screen->SetInnerRML(str_format(str, "%ux%u/%u-%u:%u-%u %.2fkHz %.2fHz",
		vm.xres, vm.yres,
		vm.borders.top, vm.borders.bottom, vm.borders.left, vm.borders.right,
		vt.hfreq, vt.vfreq));

	m_vga.htotal->SetInnerRML(format_uint16(vt.htotal));
	m_vga.hdend->SetInnerRML(format_uint16(vt.hdend));
	m_vga.hblank->SetInnerRML(str_format(str, "%d-%d", vt.hbstart, vt.hbend));
	m_vga.hretr->SetInnerRML(str_format(str, "%d-%d", vt.hrstart, vt.hrend));
	
	m_vga.vtotal->SetInnerRML(format_uint16(vt.vtotal));
	m_vga.vdend->SetInnerRML(format_uint16(vt.vdend));
	m_vga.vblank->SetInnerRML(str_format(str, "%d-%d", vt.vbstart, vt.vbend));
	m_vga.vretr->SetInnerRML(str_format(str, "%d-%d", vt.vrstart, vt.vrend));
	
	m_vga.startaddr_hi->SetInnerRML(format_hex8(vga->crtc().startaddr_hi));
	m_vga.startaddr_lo->SetInnerRML(format_hex8(vga->crtc().startaddr_lo));
	m_vga.startaddr_latch->SetInnerRML(format_hex16(vga->crtc().latches.start_address));
	
	bool disp=false, vret=false, hret=false;
	double scanline = vga->current_scanline(disp, hret, vret);
	m_vga.scanl->SetInnerRML(str_format(str, "%.2f", scanline));
	if(disp) {
		m_vga.disp_phase->SetClass("led_active", true);
	} else {
		m_vga.disp_phase->SetClass("led_active", false);
	}
	if(hret) {
		m_vga.hretr_phase->SetClass("led_active", true);
	} else {
		m_vga.hretr_phase->SetClass("led_active", false);
	}
	if(vret) {
		m_vga.vretr_phase->SetClass("led_active", true);
	} else {
		m_vga.vretr_phase->SetClass("led_active", false);
	}
	
	const VideoStats & stats = vga->stats();
	m_vga.frame_cnt->SetInnerRML(str_format(str, "%d", stats.frame_cnt));
	if(stats.updated_pix > 0) {
		m_vga.upd->SetClass("led_active", true);
		m_vga.pix_upd->SetInnerRML(str_format(str, "%d", stats.updated_pix));
	} else {
		m_vga.upd->SetClass("led_active", false);
	}
	m_vga.saddr_line->SetInnerRML(str_format(str, "%d", stats.last_saddr_line));
	m_vga.pal_line->SetInnerRML(str_format(str, "%d", stats.last_pal_line));
}

void DevStatus::update()
{
	if(!m_enabled) {
		return;
	}

	static bool updated = false;
	if(m_machine->is_paused() && !updated) {
		update_vga();
		update_pic();
		update_pit();
	} else {
		if(m_vga.is_running) {
			update_vga();
		}
		if(m_pic.is_running) {
			update_pic();
		}
		if(m_pit.is_running) {
			update_pit();
		}
	}
	
	if(m_machine->is_paused()) {
		updated = true;
	} else {
		updated = false;
	}
}
