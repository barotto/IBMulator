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

#include "ibmulator.h"
#include "gui/gui.h"
#include "printer_control.h"
#include "program.h"


event_map_t PrinterControl::ms_evt_map = {
	GUI_EVT( "close",     "click",   Window::on_cancel  ),
	GUI_EVT( "on_line",   "click",   PrinterControl::on_online ),
	GUI_EVT( "form_feed", "click",   PrinterControl::on_form_feed ),
	GUI_EVT( "line_feed", "click",   PrinterControl::on_line_feed ),
	GUI_EVT( "*",         "keydown", Window::on_keydown )
};

PrinterControl::PrinterControl(GUI *_gui, std::shared_ptr<MpsPrinter> _printer)
:
Window(_gui, "printer_control.rml"),
m_printer(_printer)
{
}

PrinterControl::~PrinterControl()
{
	if(m_preview) {
		SDL_FreeSurface(m_preview);
	}
}

void PrinterControl::show()
{
	Window::show();
	get_element("on_line")->Focus();
}

void PrinterControl::create()
{
	Window::create();

	m_ready_el = get_element("ready");

	m_head_el = get_element("head");
	m_line_el = get_element("head_y");
	
	m_preview_cnt_el = get_element("preview_cnt");
	m_preview_img_el = get_element("preview_img");
	
	bool headpos = g_program.config().get_bool(PRN_SECTION, PRN_SHOW_HEAD, true);
	if(!headpos) {
		m_preview_cnt_el->SetClass("no_head", true);
	}
	
	m_ready_timer = m_gui->timers().register_timer([this](uint64_t){
		m_ready_el->SetClass("not_ready", !m_ready_el->IsClassSet("not_ready"));
		static unsigned blink = 0;
		if((blink++ % 4) == 0) {
			if(!m_printer->is_active()) {
				m_gui->timers().deactivate_timer(m_ready_timer);
				m_ready_el->SetClass("not_ready", false);
			}
		}
	}, "Printer ready led");
	
	switch(m_printer->get_interpreter()) {
		case MPS_PRINTER_INTERPRETER_EPSON:
			set_title(str_format("Epson %s", m_printer->is_color_mode()?"JX-80":"FX-80"));
			break;
		case MPS_PRINTER_INTERPRETER_IBMPP:
			set_title("IBM Proprinter");
			break;
		case MPS_PRINTER_INTERPRETER_IBMGP:
			set_title("IBM Graphics Printer");
			break;
	}
}

void PrinterControl::update()
{
	if(!m_printer->is_online()) {
		m_ready_el->SetClass("not_ready", true);
		m_gui->timers().deactivate_timer(m_ready_timer);
	} else {
		if(m_printer->is_active() && !m_gui->timers().is_timer_active(m_ready_timer)) {
			m_ready_el->SetClass("not_ready", true);
			m_gui->timers().activate_timer(m_ready_timer, 250_ms, true);
		}
	}
	if(m_printer->is_paper_loaded()) {
		static float cnt_w = .0f, cnt_h = .0f;
		static float img_w = .0f, img_h = .0f;
		static float head_w = 2.f, head_h = 4.f;
		static std::pair<int,int> head_pos = {-1,-1};
		float cw = m_preview_cnt_el->GetClientWidth() / m_gui->scaling_factor();
		float ch = m_preview_cnt_el->GetClientHeight() / m_gui->scaling_factor();
		if(cw != cnt_w || ch != cnt_h) {
			cnt_w = cw;
			cnt_h = ch;
			PrinterPaper paper = m_printer->get_paper();
			img_w = cnt_w;
			img_h = img_w * (paper.height_inch / paper.width_inch);
			if(img_h > cnt_h) {
				img_h = cnt_h;
				img_w = img_h * (paper.width_inch / paper.height_inch);
			}
			m_preview_img_el->SetProperty("width", str_format("%ddp", int(img_w)));
			m_preview_img_el->SetProperty("height", str_format("%ddp", int(img_h)));

			m_line_el->SetProperty("width", str_format("%ddp", int(img_w)));

			auto page_px = m_printer->get_page_size_px();
			head_h = (float(MPS_PRINTER_HEAD_HEIGHT) / float(page_px.second)) * img_h;
			head_w = 2.f;
			if(head_h <= 1.f) {
				head_h = 1.f;
				head_w = 1.f;
			}
			m_head_el->SetProperty("height", str_format("%ddp", int(round(head_h))));
			m_head_el->SetProperty("width", str_format("%ddp", int(round(head_w))));
			head_pos = {-1,-1};
		}
		auto hpos = m_printer->get_head_pos();
		if(hpos != head_pos) {
			auto page_px = m_printer->get_page_size_px();
			int x = (float(hpos.first) / float(page_px.first)) * img_w;
			int y = (float(hpos.second) / float(page_px.second)) * img_h;
			m_head_el->SetProperty("top", str_format("%ddp",y));
			m_head_el->SetProperty("left", str_format("%ddp",x));
			m_line_el->SetProperty("top", str_format("%ddp",y+int(round(head_h))));
			head_pos = hpos;
		}
		if(m_preview && m_printer->is_preview_updated()) {
			m_printer->copy_preview(*m_preview);
			m_gui->update_surface("gui:printer_preview", m_preview);
		}
	} else {
		// TODO remove preview?
	}

	Window::update();
}

SDL_Surface * PrinterControl::get_preview_surface()
{
	if(!m_preview) {
		auto [w,h] = m_printer->get_preview_max_size();
		m_preview = SDL_CreateRGBSurface(0,
				w, h,
				32,
				0x000000ff,
				0x0000ff00,
				0x00ff0000,
				0xff000000
		);
		SDL_FillRect(m_preview, NULL, 0xffffffff);
	}
	return m_preview;
}

void PrinterControl::on_online(Rml::Event &_evt)
{
	if(_evt.GetTargetElement()->IsClassSet("active")) {
		m_printer->cmd_set_offline();
		get_element("form_feed")->SetClass("disabled", false);
		get_element("line_feed")->SetClass("disabled", false);
		_evt.GetTargetElement()->SetClass("active", false);
	} else {
		m_printer->cmd_set_online();
		get_element("form_feed")->SetClass("disabled", true);
		get_element("line_feed")->SetClass("disabled", true);
		_evt.GetTargetElement()->SetClass("active", true);
		m_ready_el->SetClass("not_ready", false);
	}
}

void PrinterControl::on_form_feed(Rml::Event &_evt)
{
	if(_evt.GetTargetElement()->IsClassSet("disabled")) {
		return;
	}
	m_printer->cmd_form_feed();
}

void PrinterControl::on_line_feed(Rml::Event &_evt)
{
	if(_evt.GetTargetElement()->IsClassSet("disabled")) {
		return;
	}
	m_printer->cmd_line_feed();
}
