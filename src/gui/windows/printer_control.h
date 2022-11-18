/*
 * Copyright (C) 2022  Marco Bortolin
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

#ifndef IBMULATOR_GUI_PRINTER_CONTROL_H
#define IBMULATOR_GUI_PRINTER_CONTROL_H

#include "../window.h"
#include "hardware/printer/mps_printer.h"
#include <RmlUi/Core.h>

class PrinterControl : public Window
{
private:
	static event_map_t ms_evt_map;
	std::shared_ptr<MpsPrinter> m_printer;
	Rml::Element *m_ready_el = nullptr;
	Rml::Element *m_head_el = nullptr;
	
	Rml::Element *m_line_el = nullptr;
	TimerID m_ready_timer = NULL_TIMER_ID;
	SDL_Surface * m_preview = nullptr;
	Rml::Element *m_preview_cnt_el = nullptr;
	Rml::Element *m_preview_img_el = nullptr;
	
public:
	PrinterControl(GUI *_gui, std::shared_ptr<MpsPrinter> _printer);
	virtual ~PrinterControl();

	void show();
	void create();
	void update();

	SDL_Surface * get_preview_surface();
	event_map_t & get_event_map() { return PrinterControl::ms_evt_map; }

private:
	void on_online(Rml::Event &);
	void on_form_feed(Rml::Event &);
	void on_line_feed(Rml::Event &);
};



#endif
