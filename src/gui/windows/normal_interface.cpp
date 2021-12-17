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
#include "machine.h"
#include "program.h"
#include "normal_interface.h"
#include "utils.h"
#include <sys/stat.h>

#include <RmlUi/Core.h>

event_map_t NormalInterface::ms_evt_map = {
	GUI_EVT( "power",    "click", Interface::on_power ),
	GUI_EVT( "pause",    "click", NormalInterface::on_pause ),
	GUI_EVT( "save",     "click", Interface::on_save_state ),
	GUI_EVT( "restore",  "click", Interface::on_load_state ),
	GUI_EVT( "exit",     "click", NormalInterface::on_exit ),
	GUI_EVT( "visibility","click", NormalInterface::on_visibility ),
	GUI_EVT( "fdd_select","click", NormalInterface::on_fdd_select ),
	GUI_EVT( "fdd_eject", "click", Interface::on_fdd_eject ),
	GUI_EVT( "fdd_mount", "click", Interface::on_fdd_mount ),
	GUI_EVT( "fdd_select_c","click", NormalInterface::on_fdd_select ),
	GUI_EVT( "fdd_eject_c", "click", Interface::on_fdd_eject ),
	GUI_EVT( "fdd_mount_c", "click", Interface::on_fdd_mount ),
	GUI_EVT( "move_target", "mousemove", NormalInterface::on_mouse_move ),
	GUI_EVT_T( "move_target", "dblclick", Interface::on_dblclick ),
	GUI_EVT( "main_interface", "mousemove", NormalInterface::on_mouse_move ),
	GUI_EVT_T( "main_interface", "dblclick", Interface::on_dblclick )
};

NormalInterface::NormalInterface(Machine *_machine, GUI * _gui, Mixer *_mixer, EventTimers *_timers)
:
Interface(_machine, _gui, _mixer, "normal_interface.rml"),
m_timers(_timers)
{
}

NormalInterface::~NormalInterface()
{
}

void NormalInterface::create()
{
	Interface::create();

	m_main_interface = get_element("main_interface");
	m_main_interface->SetClass("with_disk", m_floppy_present);
	m_sysunit = get_element("system_unit");
	m_sysbar = get_element("system_bar");
	m_sysctrl = get_element("system_control");
	m_btn_pause = get_element("pause");
	m_btn_visibility = get_element("visibility");
	m_fdd_mount_c = get_element("fdd_mount_c");
	m_fdd_select_c = get_element("fdd_select_c");
	m_cur_zoom = ZoomMode::NORMAL;

	m_compact_ifc_timeout = g_program.config().get_real(GUI_SECTION, GUI_COMPACT_TIMEOUT, 0.0)
			* NSEC_PER_SECOND;

	if(m_compact_ifc_timeout) {
		m_compact_ifc_timer = m_timers->register_timer(
				[this](uint64_t) { hide_system(); },
				"Compact Interface");
	}
	
	m_led_pause = false;
	ZoomMode zoom = static_cast<ZoomMode>(g_program.config().get_enum(GUI_SECTION, GUI_MODE, {
			{ "normal", ec_to_i(ZoomMode::NORMAL) },
			{ "compact", ec_to_i(ZoomMode::COMPACT) }
	}, ec_to_i(ZoomMode::NORMAL)));
	
	std::string aspect_s = g_program.config().get_string(DISPLAY_SECTION, DISPLAY_NORMAL_ASPECT);
	unsigned wr, hr;
	if(sscanf(aspect_s.c_str(), "%u:%u", &wr, &hr) == 2) {
		if(hr != 0) {
			m_aspect_ratio = double(wr) / double(hr);
			m_aspect_mode = DISPLAY_ASPECT_FIXED;
		} else {
			PERRF(LOG_GUI, "Invalid H parameter value for [" DISPLAY_SECTION "]:" DISPLAY_NORMAL_ASPECT "\n");
			throw std::exception();
		}
		PDEBUGF(LOG_V0, LOG_GUI, "Fixed display ratio: %u:%u (%.6f)\n", wr, hr, m_aspect_ratio);
	} else {
		m_aspect_ratio = .0;
		m_aspect_mode = g_program.config().get_enum(DISPLAY_SECTION, DISPLAY_NORMAL_ASPECT,
			GUI::ms_display_aspect);
		if(m_aspect_mode == DISPLAY_ASPECT_ORIGINAL) {
			m_aspect_ratio = ORIGINAL_MONITOR_RATIO;
			m_aspect_mode = DISPLAY_ASPECT_FIXED;
		}
	}
	m_scale_mode = g_program.config().get_enum(DISPLAY_SECTION, DISPLAY_NORMAL_SCALE,
		GUI::ms_display_scale);
	if(m_scale_mode == DISPLAY_SCALE_1X || m_scale_mode == DISPLAY_SCALE_INTEGER) {
		m_scale_integer = true;
	}
	int w,h;
	// try to parse the width as a scaling factor
	// window auto-resizing mode (undocumented and incomplete)
	std::string widths = g_program.config().get_string(GUI_SECTION, GUI_WIDTH);
	if(widths.at(widths.length()-1) == 'x') {
		int scan = sscanf(widths.c_str(), "%ux", &m_window_scaling);
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
		m_window_scaling = 0;
	}
	if(zoom == ZoomMode::NORMAL) {
		h += std::min(256, w/4); //the sysunit proportions are 4:1
	}
	m_size = vec2i(w,h);
	set_zoom(zoom);

	m_screen = std::make_unique<InterfaceScreen>(m_gui);

	std::string frag_sh = g_program.config().find_file(DISPLAY_SECTION, DISPLAY_NORMAL_SHADER);
	m_screen->renderer()->load_vga_program(
		GUI::shaders_dir() + "fb-normal.vs", frag_sh,
		g_program.config().get_enum(DISPLAY_SECTION, DISPLAY_NORMAL_FILTER, GUI::ms_gui_sampler)
	);
	if(!m_scale_integer) {
		m_screen->vga.pmat = mat4_ortho<float>(0, 1.0, 1.0, 0, 0, 1);
	}
}

void NormalInterface::container_size_changed(int _width, int _height)
{
	float xs = 1.f, ys = 1.f;
	float xt = 0.f, yt = 0.f;

	unsigned sysunit_w, sysunit_h;
	sysunit_h = std::min(unsigned(256.0 * m_gui->scaling_factor()), unsigned(_height / 4));
	if(m_cur_zoom == ZoomMode::COMPACT) {
		sysunit_w = std::min(
				std::max(unsigned(640.0 * m_gui->scaling_factor()), sysunit_h * 4),
				unsigned(_width));
	} else {
		sysunit_w = std::min(sysunit_h * 4, unsigned(_width));
	}
	sysunit_h = sysunit_w / 4;

	float disp_w, disp_h;
	int disp_area_w = _width, disp_area_h = _height;
	if(m_cur_zoom == ZoomMode::NORMAL) {
		disp_area_h -= sysunit_h;
	}
	if(m_window_scaling > 0) {
		disp_w = m_screen->vga.display.mode().xres * m_window_scaling;
		disp_h = m_screen->vga.display.mode().yres * m_window_scaling;
	} else {
		disp_w = disp_area_w;
		disp_h = disp_area_h;
	}

	float ratio;
	switch(m_aspect_mode) {
		case DISPLAY_ASPECT_FIXED:
			ratio = m_aspect_ratio;
			break;
		case DISPLAY_ASPECT_VGA:
			if(m_scale_integer) {
				ratio = float(m_screen->vga.display.mode().imgw) / float(m_screen->vga.display.mode().imgh);
			} else {
				ratio = float(m_screen->vga.display.mode().xres) / float(m_screen->vga.display.mode().yres);
			}
			break;
		case DISPLAY_ASPECT_AREA:
			ratio = float(disp_area_w) / float(disp_area_h);
			break;
		default:
			assert(false);
			break;
	}
	if(m_scale_mode == DISPLAY_SCALE_1X) {
		disp_w = m_screen->vga.display.mode().imgw;
		disp_h = m_screen->vga.display.mode().imgh;
	} else {
		disp_w = disp_h * ratio;
		xs = disp_w / float(_width);
		if(xs > 1.0f) {
			disp_w = disp_area_w;
			xs = 1.0f;
			disp_h = disp_w / ratio;
		}
		ys = disp_h / float(_height);
		if(ys > 1.0f) {
			disp_h = disp_area_h;
			ys = disp_h / float(_height);
			disp_w = disp_h * ratio;
			xs = disp_w / float(_width);
		}
	}
	if(m_scale_integer) {
		int multw = disp_w / m_screen->vga.display.mode().imgw;
		int multh = disp_h / m_screen->vga.display.mode().imgh;
		disp_w = m_screen->vga.display.mode().imgw;
		disp_h = m_screen->vga.display.mode().imgh;
		if(multw > 0) {
			disp_w *= multw;
		}
		if(multh > 0) {
			disp_h *= multh;
		}
		xs = disp_w;
		ys = disp_h;
		xt = int((_width - disp_w) / 2);
		if(m_cur_zoom == ZoomMode::COMPACT) {
			yt = int((_height - disp_h) / 2);
		}
		m_screen->vga.pmat = mat4_ortho<float>(0, _width, _height, 0, 0, 1);
		PINFOF(LOG_V2, LOG_GUI, "VGA resized to: %dx%d (x:%dx,y:%dx,ratio:%.3f)\n",
				int(disp_w),int(disp_h), multw, multh, xs/ys);
	} else {
		xt = (1.0 - xs) / 2.0;
		if(m_cur_zoom == ZoomMode::COMPACT) {
			yt = (1.0 - ys) / 2.0;
		}
	}

	m_screen->vga.size.x = disp_w;
	m_screen->vga.size.y = disp_h;

	m_screen->vga.mvmat.load_scale(xs, ys, 1.0);
	m_screen->vga.mvmat.load_translation(xt, yt, 0.0);

	m_size = m_screen->vga.size;
	m_main_interface->SetProperty("width", str_format("%upx", sysunit_w));
	m_main_interface->SetProperty("height", str_format("%upx", sysunit_h));

	unsigned fontsize = sysunit_w / 40;
	m_status.fdd_disk->SetProperty("font-size", str_format("%upx", fontsize));
	m_fdd_mount_c->GetChild(0)->SetProperty("font-size", str_format("%upx", fontsize));
}

void NormalInterface::update()
{
	Interface::update();

	if(m_scale_integer || m_window_scaling > 0)
	{
		m_screen->vga.display.lock();
		if(m_screen->vga.display.dimension_updated()) {
			uint32_t wflags = m_gui->window_flags();
			if(!(wflags & SDL_WINDOW_FULLSCREEN)&&
			   !(wflags & SDL_WINDOW_MAXIMIZED) &&
			   m_window_scaling)
			{
				// TODO incomplete, will not resize properly when ratio is fixed
				int w = m_screen->vga.display.mode().xres * m_window_scaling;
				int h = m_screen->vga.display.mode().yres * m_window_scaling;
				if(m_cur_zoom == ZoomMode::NORMAL) {
					h += std::min(256, w/4); //the sysunit proportions are 4:1
				}
				// gui lock already acquired in GUI::update()
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

	if(is_system_visible()) {
		if(m_floppy_present) {
			m_main_interface->SetClass("with_disk", true);
		} else {
			m_main_interface->SetClass("with_disk", false);
		}
	}
	if(m_machine->is_paused() && m_led_pause==false) {
		m_led_pause = true;
		m_btn_pause->SetClass("resume", true);
	} else if(!m_machine->is_paused() && m_led_pause==true){
		m_led_pause = false;
		m_btn_pause->SetClass("resume", false);
	}
}

void NormalInterface::action(int _action)
{
	switch(m_cur_zoom) {
		case ZoomMode::COMPACT: {
			if(_action == ACTION_ZOOM) {
				set_zoom(ZoomMode::NORMAL);
				m_gui->show_message("Normal interface mode");
			} else if(_action == ACTION_SHOW_HIDE) {
				if(is_system_visible()) {
					hide_system();
				} else {
					show_system();
				}
			}
			break;
		}
		case ZoomMode::NORMAL:
			if(_action == ACTION_ZOOM) {
				set_zoom(ZoomMode::COMPACT);
				m_gui->show_message("Compact interface mode");
			}
			break;
	}
}

void NormalInterface::set_zoom(ZoomMode _zoom)
{
	m_cur_zoom = _zoom;
	switch(_zoom) {
		case ZoomMode::COMPACT:
			collapse_sysunit(true);
			if(m_gui->is_input_grabbed()) {
				hide_system();
			} else {
				show_system();
				if(m_compact_ifc_timer != NULL_TIMER_HANDLE) {
					m_timers->activate_timer(m_compact_ifc_timer, m_compact_ifc_timeout, false);
				}
			}
			m_main_interface->SetClass("normal", false);
			break;
		case ZoomMode::NORMAL:
			collapse_sysunit(false);
			show_system();
			m_main_interface->SetClass("normal", true);
			break;
	}
}

void NormalInterface::grab_input(bool _grabbed)
{
	if(m_cur_zoom == ZoomMode::COMPACT) {
		if(_grabbed) {
			hide_system();
		} else {
			show_system();
		}
	}
}

bool NormalInterface::is_system_visible() const
{
	return (!m_main_interface->IsClassSet("hidden"));
}

void NormalInterface::hide_system()
{
	m_main_interface->SetClass("hidden", true);
	if(!m_gui->are_windows_visible()) {
		SDL_ShowCursor(false);
	}
}

void NormalInterface::show_system()
{
	if(m_compact_ifc_timer != NULL_TIMER_HANDLE) {
		m_timers->deactivate_timer(m_compact_ifc_timer);
	}
	SDL_ShowCursor(true);
	m_main_interface->SetClass("hidden", false);
}

void NormalInterface::on_pause(Rml::Event &)
{
	if(m_machine->is_paused()) {
		m_machine->cmd_resume();
	} else {
		m_machine->cmd_pause();
	}
}

void NormalInterface::on_exit(Rml::Event &)
{
	g_program.stop();
}

void NormalInterface::collapse_sysunit(bool _collapse)
{
	if(is_sysunit_collapsed() == _collapse) {
		return;
	}
	if(_collapse) {
		m_main_interface->SetClass("collapsed", true);
		m_main_interface->SetClass("compact", false);
		m_main_interface->SetClass("normal", false);
		m_sysbar->AppendChild(m_sysunit->RemoveChild(m_sysctrl));
	} else {
		m_main_interface->SetClass("collapsed", false);
		switch(m_cur_zoom) {
			case ZoomMode::COMPACT: m_main_interface->SetClass("compact", true); break;
			case ZoomMode::NORMAL: m_main_interface->SetClass("normal", true); break;
		}
		m_sysunit->AppendChild(m_sysbar->RemoveChild(m_sysctrl));
	}
}

bool NormalInterface::is_sysunit_collapsed()
{
	return m_main_interface->IsClassSet("collapsed");
}

void NormalInterface::on_visibility(Rml::Event &)
{
	if(is_sysunit_collapsed()) {
		collapse_sysunit(false);
	} else {
		collapse_sysunit(true);
	}
}

void NormalInterface::on_fdd_select(Rml::Event &_e)
{
	m_fdd_mount_c->GetChild(0)->SetInnerRML("");
	if(m_curr_drive == 0) {
		m_fdd_select_c->SetClass("a", false);
		m_fdd_select_c->SetClass("b", true);
	} else {
		m_fdd_select_c->SetClass("a", true);
		m_fdd_select_c->SetClass("b", false);
	}
	Interface::on_fdd_select(_e);
}

void NormalInterface::set_floppy_string(std::string _filename)
{
	Interface::set_floppy_string(_filename);
	auto name = m_status.fdd_disk->GetInnerRML();
	m_fdd_mount_c->GetChild(0)->SetInnerRML(name);
}

void NormalInterface::set_floppy_config(bool _b_present)
{
	Interface::set_floppy_config(_b_present);
	m_fdd_select_c->SetClass("d-none", !_b_present);
	m_fdd_select_c->SetClass("a", true);
	m_fdd_select_c->SetClass("b", false);
}

void NormalInterface::set_floppy_active(bool _active)
{
	Interface::set_floppy_active(_active);
	m_fdd_mount_c->SetClass("active", _active);
}

void NormalInterface::on_mouse_move(Rml::Event &_ev)
{
	if(m_compact_ifc_timer != NULL_TIMER_HANDLE && m_cur_zoom == ZoomMode::COMPACT) {
		show_system();
		if(_ev.GetTargetElement()->GetId() == "move_target" || _ev.GetTargetElement()->GetId() == "main_interface") {
			m_timers->activate_timer(m_compact_ifc_timer, m_compact_ifc_timeout, false);
		}
	}
}