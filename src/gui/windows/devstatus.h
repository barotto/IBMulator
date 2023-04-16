/*
 * Copyright (C) 2015-2023  Marco Bortolin
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

#ifndef IBMULATOR_GUI_DEVSTATUS_H
#define IBMULATOR_GUI_DEVSTATUS_H

#include "debugtools.h"

class Machine;
class GUI;

class DevStatus : public DebugTools::DebugWindow
{
private:

	Machine *m_machine;
	
	struct {
		bool is_running;
		Rml::Element *btn_update;
		Rml::Element *mode, *screen;
		// CRTC
		Rml::Element *htotal, *hdend, *hblank, *hretr;
		Rml::Element *vtotal, *vdend, *vblank, *vretr;
		Rml::Element *startaddr_hi, *startaddr_lo, *startaddr_latch;
		Rml::Element *scanl, *disp_phase, *hretr_phase, *vretr_phase;
		// Stats
		Rml::Element *frame_cnt;
		Rml::Element *pix_upd, *upd, *saddr_line, *pal_line;
	} m_vga = {};
	
	struct {
		bool is_running;
		Rml::Element *btn_update;
		Rml::Element *irq_e[16], *irr_e[16], *imr_e[16], *isr_e[16];
		uint16_t irq, irr, imr, isr;
	} m_pic = {};

	struct {
		bool is_running;
		Rml::Element *btn_update;
		Rml::Element *mode[3], *cnt[3], *gate[3], *out[3], *in[3];
	} m_pit = {};

	static event_map_t ms_evt_map;

	void on_cmd_vga_update(Rml::Event &);
	void on_cmd_vga_dump_state(Rml::Event &);
	void on_cmd_vga_screenshot(Rml::Event &);
	void on_cmd_pit_update(Rml::Event &);
	void on_cmd_pic_update(Rml::Event &);
	void update_pit();
	void update_pit(unsigned cnt);
	void update_pic();
	void update_pic(uint16_t _irq, uint16_t _irr, uint16_t _imr, uint16_t _isr, uint _irqn);
	void update_vga();

public:

	DevStatus(GUI * _gui, Rml::Element *_button, Machine *_machine);
	~DevStatus();

	virtual void create();
	virtual void update();
	event_map_t & get_event_map() { return DevStatus::ms_evt_map; }
};

#endif
