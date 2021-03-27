/*
 * Copyright (C) 2015-2020  Marco Bortolin
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
#include "machine.h"
#include "program.h"
#include "normal_interface.h"
#include "utils.h"
#include <sys/stat.h>

#include <Rocket/Core.h>


event_map_t NormalInterface::ms_evt_map = {
	GUI_EVT( "power",     "click", Interface::on_power ),
	GUI_EVT( "pause",     "click", NormalInterface::on_pause ),
	GUI_EVT( "save",      "click", NormalInterface::on_save ),
	GUI_EVT( "restore",   "click", NormalInterface::on_restore ),
	GUI_EVT( "exit",      "click", NormalInterface::on_exit ),
	GUI_EVT( "fdd_select","click", Interface::on_fdd_select ),
	GUI_EVT( "fdd_eject", "click", Interface::on_fdd_eject ),
	GUI_EVT( "fdd_mount", "click", Interface::on_fdd_mount )
};

NormalInterface::NormalInterface(Machine *_machine, GUI * _gui, Mixer *_mixer)
:
Interface(_machine, _gui, _mixer, "normal_interface.rml")
{
	assert(m_wnd);

	m_sysunit = get_element("sysunit");
	m_sysbkgd = get_element("sysbkgd");
	m_sysbkgd->SetClass("disk", m_floppy_present);
	m_btn_pause = get_element("pause");
	m_led_pause = false;
	m_gui_mode = g_program.config().get_enum(GUI_SECTION, GUI_MODE, GUI::ms_gui_modes);
	m_vga_aspect = g_program.config().get_enum(DISPLAY_SECTION,DISPLAY_NORMAL_ASPECT,
		GUI::ms_display_aspect);

	int w,h;
	//try to parse the width as a scaling factor
	std::string widths = g_program.config().get_string(GUI_SECTION, GUI_WIDTH);
	if(widths.at(widths.length()-1) == 'x') {
		int scan = sscanf(widths.c_str(), "%ux", &m_vga_scaling);
		if(scan == 1) {
			//sensible defaults
			w = 640;
			h = 480;
		} else {
			PERRF(LOG_GUI, "invalid scaling factor: '%s'\n", widths.c_str());
			throw std::exception();
		}
	} else {
		//try as a pixel int value
		w = g_program.config().get_int(GUI_SECTION, GUI_WIDTH);
		h = g_program.config().get_int(GUI_SECTION, GUI_HEIGHT);
		m_vga_scaling = 0;
	}
	if(m_gui_mode == GUI_MODE_NORMAL) {
		h += std::min(256, w/4); //the sysunit proportions are 4:1
	}
	m_size = vec2i(w,h);

	m_screen = std::make_unique<InterfaceScreen>(_gui);
	
	std::string frag_sh = g_program.config().find_file(DISPLAY_SECTION, DISPLAY_NORMAL_SHADER);
	
	m_screen->renderer()->load_vga_program(
		GUI::shaders_dir() + "fb-normal.vs", frag_sh,
		g_program.config().get_enum(DISPLAY_SECTION, DISPLAY_NORMAL_FILTER, GUI::ms_gui_sampler)
	);
}

NormalInterface::~NormalInterface()
{
}

void NormalInterface::container_size_changed(int _width, int _height)
{
	float xs = 1.f, ys = 1.f;
	float xt = 0.f, yt = 0.f;
	uint sysunit_h = _height/4;
	sysunit_h = std::min(256u, sysunit_h);
	uint sysunit_w = sysunit_h*4;
	sysunit_w = std::min(sysunit_w, (uint)_width);
	sysunit_h = sysunit_w/4; //the sysunit proportions are 4:1

	int disp_w, disp_h;
	int disp_area_w = _width, disp_area_h = _height;
	if(m_vga_scaling>0) {
		disp_w = m_screen->vga.display.mode().xres * m_vga_scaling;
		disp_h = m_screen->vga.display.mode().yres * m_vga_scaling;
	} else {
		disp_w = disp_area_w;
		disp_h = disp_area_h;
	}

	if(m_gui_mode == GUI_MODE_NORMAL) {
		disp_area_h = _height - sysunit_h;
	}

	disp_w = std::min(disp_w,disp_area_w);
	disp_h = std::min(disp_h,disp_area_h);

	float ratio;
	if(m_vga_aspect == DISPLAY_ASPECT_ORIGINAL) {
		ratio = 1.333333f; //4:3
	} else if(m_vga_aspect == DISPLAY_ASPECT_ADAPTIVE) {
		ratio = float(m_screen->vga.display.mode().xres) / float(m_screen->vga.display.mode().yres);
	} else {
		//SCALED
		ratio = float(disp_w) / float(disp_h);
	}
	disp_w = round(float(disp_h) * ratio);
	xs = float(disp_w)/float(_width);
	if(xs>1.0f) {
		disp_w = disp_area_w;
		xs = 1.0f;
		disp_h = round(float(disp_w) / ratio);
	}
	if(m_vga_aspect == DISPLAY_ASPECT_SCALED) {
		ratio = float(disp_w) / float(disp_h);
	}
	ys = float(disp_h)/float(_height);
	if(m_gui_mode == GUI_MODE_NORMAL) {
		yt = 1.f - ys; //aligned to top
	}
	if(ys>1.f) {
		disp_h = disp_area_h;
		ys = float(disp_h)/float(_height);
		yt = 0.f;
		if(m_vga_aspect == DISPLAY_ASPECT_SCALED) {
			ratio = float(disp_w) / float(disp_h);
		}
		disp_w = round(float(disp_h) * ratio);
		xs = float(disp_w)/float(_width);
	}

	m_screen->vga.size.x = disp_w;
	m_screen->vga.size.y = disp_h;
	m_screen->vga.mvmat.load_scale(xs, ys, 1.0);
	m_screen->vga.mvmat.load_translation(xt, yt, 0.0);

	m_size = m_screen->vga.size;
	if(m_gui_mode == GUI_MODE_NORMAL) {
		m_size.y += sysunit_h;
	}
	char buf[10];
	snprintf(buf, 10, "%upx", sysunit_w);
	m_sysunit->SetProperty("width", buf);
	snprintf(buf, 10, "%upx", sysunit_h);
	m_sysunit->SetProperty("height", buf);
}

void NormalInterface::update()
{
	Interface::update();

	if(m_vga_aspect==DISPLAY_ASPECT_ADAPTIVE || m_vga_scaling>0) {
		m_screen->vga.display.lock();
		if(m_screen->vga.display.dimension_updated()) {
			uint32_t wflags = m_gui->window_flags();
			//WARNING in order for the MAXIMIZED case to work under X11 you need
			//SDL 2.0.4 with this patch:
			//https://bugzilla.libsdl.org/show_bug.cgi?id=2793
			if(!(wflags & SDL_WINDOW_FULLSCREEN)&&
			   !(wflags & SDL_WINDOW_MAXIMIZED) &&
			   m_vga_scaling)
			{
				int w = m_screen->vga.display.mode().xres * m_vga_scaling;
				int h = m_screen->vga.display.mode().yres * m_vga_scaling;
				if(m_gui_mode == GUI_MODE_NORMAL) {
					h += std::min(256, w/4); //the sysunit proportions are 4:1
				}
				vec2i size = m_gui->resize_window(w,h);
				if(size.x!=w || size.y!=h) {
					//not enough space?
					//what TODO ?
				}
			} else {
				container_size_changed(
					m_gui->window_width(),
					m_gui->window_height()
				);
			}
			m_screen->vga.display.clear_dimension_updated();
		}
		m_screen->vga.display.unlock();
	}

	if(is_visible()) {
		if(m_floppy_present) {
			m_sysbkgd->SetClass("disk", true);
		} else {
			m_sysbkgd->SetClass("disk", false);
		}

		if(m_machine->is_paused() && m_led_pause==false) {
			m_led_pause = true;
			m_btn_pause->SetClass("resume", true);
		} else if(!m_machine->is_paused() && m_led_pause==true){
			m_led_pause = false;
			m_btn_pause->SetClass("resume", false);
		}
	}
}

void NormalInterface::action(int _action)
{
	if((m_gui_mode == GUI_MODE_COMPACT) && (_action == 0)) {
		if(is_system_visible()) {
			hide_system();
		} else {
			show_system();
		}
	}
}

bool NormalInterface::is_system_visible()
{
	return (m_sysunit->GetProperty("visibility")->ToString() != "hidden");
}

void NormalInterface::hide_system()
{
	m_sysunit->SetProperty("visibility", "hidden");
}

void NormalInterface::show_system()
{
	m_sysunit->SetProperty("visibility", "visible");
}

void NormalInterface::on_pause(RC::Event &)
{
	if(m_machine->is_paused()) {
		m_machine->cmd_resume();
	} else {
		m_machine->cmd_pause();
	}
}

void NormalInterface::on_save(RC::Event &)
{
	//TODO file select window to choose the destination
	g_program.save_state("", [this]() {
		m_gui->show_message("State saved");
	}, nullptr);
}

void NormalInterface::on_restore(RC::Event &)
{
	//TODO file select window to choose the source
	g_program.restore_state("", [this]() {
		m_gui->show_message("State restored");
	}, nullptr);
}

void NormalInterface::on_exit(RC::Event &)
{
	g_program.stop();
}


