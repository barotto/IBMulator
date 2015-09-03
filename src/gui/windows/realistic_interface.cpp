/*
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ibmulator.h"
#include "gui.h"
#include "machine.h"
#include "program.h"
#include "realistic_interface.h"
#include "utils.h"
#include <sys/stat.h>

#include <Rocket/Core.h>

event_map_t RealisticInterface::ms_evt_map = {
	GUI_EVT( "power",     "click", Interface::on_power ),
	GUI_EVT( "fdd_select","click", Interface::on_fdd_select ),
	GUI_EVT( "fdd_eject", "click", Interface::on_fdd_eject ),
	GUI_EVT( "fdd_mount", "click", Interface::on_fdd_mount ),
	GUI_EVT( "volume_slider",     "drag",      RealisticInterface::on_volume_drag ),
	GUI_EVT( "volume_slider",     "dragstart", RealisticInterface::on_dragstart ),
	GUI_EVT( "brightness_slider", "drag",      RealisticInterface::on_brightness_drag ),
	GUI_EVT( "brightness_slider", "dragstart", RealisticInterface::on_dragstart ),
	GUI_EVT( "contrast_slider",   "drag",      RealisticInterface::on_contrast_drag ),
	GUI_EVT( "contrast_slider",   "dragstart", RealisticInterface::on_dragstart )
};

RealisticInterface::RealisticInterface(Machine *_machine, GUI * _gui, Mixer *_mixer)
:
Interface(_machine, _gui, _mixer, "realistic_interface.rml")
{
	ASSERT(m_wnd);

	m_wnd->AddEventListener("click", this, false);
	m_wnd->AddEventListener("drag", this, false);
	m_wnd->AddEventListener("dragstart", this, false);

	m_system = get_element("system");
	m_floppy_disk = get_element("floppy_disk");
	m_floppy_disk->SetClass("disk", m_floppy_present);
	m_led_power = get_element("power_led");

	m_volume_slider = get_element("volume_slider");
	m_brightness_slider = get_element("brightness_slider");
	m_contrast_slider = get_element("contrast_slider");

	m_volume_left_min = m_volume_slider->GetProperty<float>("left");
	m_brightness_left_min = m_brightness_slider->GetProperty<float>("left");
	m_contrast_left_min = m_contrast_slider->GetProperty<float>("left");

	m_drag_start_x = 0;
	m_drag_start_left = .0f;

	// initialize with the user supplied values, unless they are expressed as a multiplier
	// they are used by the GUI to resize the main window.
	std::string widths = g_program.config().get_string(GUI_SECTION, GUI_WIDTH);
	if(widths.at(widths.length()-1) == 'x') {
		// realistic interface cannot use scaling factors
		m_size = vec2i(640,640);
	} else {
		//try as a pixel int value
		m_size = vec2i(
			g_program.config().get_int(GUI_SECTION, GUI_WIDTH),
			g_program.config().get_int(GUI_SECTION, GUI_HEIGHT)
		);
	}

	init_display(
		g_program.config().get_enum(DISPLAY_SECTION, DISPLAY_REALISTIC_FILTER, GUI::ms_gui_sampler),
		g_program.config().find_file(DISPLAY_SECTION,DISPLAY_REALISTIC_SHADER)
	);

	set_audio_volume(g_program.config().get_real(MIXER_SECTION, MIXER_VOLUME));
	set_video_brightness(g_program.config().get_real(DISPLAY_SECTION, DISPLAY_BRIGHTNESS));
	set_video_contrast(g_program.config().get_real(DISPLAY_SECTION, DISPLAY_CONTRAST));
}

RealisticInterface::~RealisticInterface()
{
}

void RealisticInterface::container_size_changed(int _width, int _height)
{
	int disp_w, disp_h;
	float xs, ys, xt=0.f, yt;
	const float sys_ratio = ms_width / ms_height;
	const float abs_disp_w = ms_width - ms_vga_left*2;
	const float wdisp_ratio = abs_disp_w / ms_width;

	float system_h = _height;
	float system_w = system_h * sys_ratio;
	if(system_w > _width) {
		system_w = _width;
		system_h = system_w * 1.f/sys_ratio;
		disp_w = round(float(_width) * wdisp_ratio);
		disp_w *= g_program.config().get_real(DISPLAY_SECTION, DISPLAY_REALISTIC_SCALE);
		disp_h = round(float(disp_w) * 0.75f); // aspect ratio 4:3
	} else {
		const float hdisp_ratio = sys_ratio * wdisp_ratio * 0.75f;
		disp_h = round(float(_height) * hdisp_ratio);
		disp_h *= g_program.config().get_real(DISPLAY_SECTION, DISPLAY_REALISTIC_SCALE);
		disp_w = round(float(disp_h) * 1.333333f); // aspect ratio 4:3
	}
	m_size = vec2i(system_w,system_h);

	xs = float(disp_w) / float(_width);  // VGA width (screen ratio)
	ys = float(disp_h) / float(_height); // VGA height (screen ratio)
	float monitor_h = ((ms_monitor_height*system_h)/ms_height) / float(_height);
	system_h /= float(_height);
	float vga_offset = monitor_h - ys;

	if(RealisticInterface::ms_align_top) {
		yt = 1.0 - ys - vga_offset;
	} else {
		yt = -1.0 + (system_h-monitor_h)*2.f + vga_offset + ys;
	}

	m_display.size.x = disp_w;
	m_display.size.y = disp_h;
	m_display.mvmat.load_scale(xs, ys, 1.0);
	m_display.mvmat.load_translation(xt, yt, 0.0);

	char buf[10];
	snprintf(buf, 10, "%upx", m_size.x);
	m_system->SetProperty("width", buf);
	snprintf(buf, 10, "%upx", m_size.y);
	m_system->SetProperty("height", buf);
}

void RealisticInterface::update()
{
	Interface::update();

	if(m_floppy_present) {
		m_floppy_disk->SetClass("present", true);
	} else {
		m_floppy_disk->SetClass("present", false);
	}

	if(m_leds.power) {
		m_led_power->SetClass("active", true);
	} else {
		m_led_power->SetClass("active", false);
	}
}

void RealisticInterface::set_slider_value(RC::Element *_slider, float _xleft, float _value)
{
	_value = clamp(_value,ms_min_slider_val,ms_max_slider_val);
	_value = (_value - ms_min_slider_val) / (ms_max_slider_val - ms_min_slider_val);
	float slider_left = _xleft + ms_slider_length*_value;
	static char buf[10];
	snprintf(buf, 10, "%.1f%%", slider_left);
	_slider->SetProperty("left", buf);
}

void RealisticInterface::set_audio_volume(float _value)
{
	Interface::set_audio_volume(_value);
	set_slider_value(m_volume_slider, m_volume_left_min, _value);
}

void RealisticInterface::set_video_brightness(float _value)
{
	Interface::set_video_brightness(_value);
	set_slider_value(m_brightness_slider, m_brightness_left_min, _value);
}

void RealisticInterface::set_video_contrast(float _value)
{
	Interface::set_video_contrast(_value);
	set_slider_value(m_contrast_slider, m_contrast_left_min, _value);
}

float RealisticInterface::on_slider_drag(RC::Event &_event, float _xmin)
{
	int x = _event.GetParameter("mouse_x",0);
	float dx = x - m_drag_start_x;
	float dxp = (dx / float(m_size.x))*100.f;
	PDEBUGF(LOG_V2, LOG_GUI, "slider drag: x=%dpx,dx=%.1fpx,dxp=%.1f%%\n",x,dx,dxp);
	RC::Element * slider = _event.GetTargetElement();
	float slider_left = m_drag_start_left + dxp;
	slider_left = clamp(slider_left, _xmin, _xmin+ms_slider_length);
	return ((slider_left-_xmin)/ms_slider_length);
}

void RealisticInterface::on_volume_drag(RC::Event &_event)
{
	float value = on_slider_drag(_event, m_volume_left_min);
	value = lerp(ms_min_slider_val, ms_max_slider_val, value);
	set_audio_volume(value);
}

void RealisticInterface::on_brightness_drag(RC::Event &_event)
{
	float value = on_slider_drag(_event, m_brightness_left_min);
	value = lerp(ms_min_slider_val, ms_max_slider_val, value);
	set_video_brightness(value);
}

void RealisticInterface::on_contrast_drag(RC::Event &_event)
{
	float value = on_slider_drag(_event, m_contrast_left_min);
	value = lerp(ms_min_slider_val, ms_max_slider_val, value);
	set_video_contrast(value);
}

void RealisticInterface::on_dragstart(RC::Event &_event)
{
	RC::Element * slider = _event.GetTargetElement();
	m_drag_start_x = _event.GetParameter("mouse_x",0);
	m_drag_start_left = slider->GetProperty<float>("left");
	PDEBUGF(LOG_V2, LOG_GUI, "slider start: x=%d\n",m_drag_start_x);
}

