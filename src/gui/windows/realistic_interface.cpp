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

RealisticInterface::RealisticInterface(Machine *_machine, GUI * _gui)
:
Interface(_machine, _gui, "realistic_interface.rml")
{
	ASSERT(m_wnd);

	m_wnd->AddEventListener("click", this, false);
	m_wnd->AddEventListener("drag", this, false);
	m_wnd->AddEventListener("dragstart", this, false);

	m_system = get_element("system");
	m_floppy_disk = get_element("floppy_disk");
	m_floppy_disk->SetClass("disk", m_floppy_present);
	m_led_power = get_element("power_led");

	m_width = 0;
	m_height = 0;

	m_volume_slider = get_element("volume_slider");
	m_brightness_slider = get_element("brightness_slider");
	m_contrast_slider = get_element("contrast_slider");

	m_volume_left_min = m_volume_slider->GetProperty<float>("left");
	m_brightness_left_min = m_brightness_slider->GetProperty<float>("left");
	m_contrast_left_min = m_contrast_slider->GetProperty<float>("left");

	m_drag_start_x = 0;
	m_drag_start_left = .0f;
}

RealisticInterface::~RealisticInterface()
{
}

void RealisticInterface::update_size(uint _width, uint _height)
{
	Interface::update_size(_width, _height);

	char buf[10];
	snprintf(buf, 10, "%upx", _width);
	m_system->SetProperty("width", buf);
	snprintf(buf, 10, "%upx", _height);
	m_system->SetProperty("height", buf);
	m_width = _width;
	m_height = _height;
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

void RealisticInterface::set_slider_value(RC::Element *_slider, float _xmin, float _value)
{
	_value  = clamp(_value,0.f,1.f);
	float slider_left = _xmin + s_slider_length*_value;
	static char buf[10];
	snprintf(buf, 10, "%.1f%%", slider_left);
	_slider->SetProperty("left", buf);
}

void RealisticInterface::set_audio_volume(float _value)
{
	set_slider_value(m_volume_slider, m_volume_left_min, _value);
}

void RealisticInterface::set_video_brightness(float _value)
{
	set_slider_value(m_brightness_slider, m_brightness_left_min, _value);
}

void RealisticInterface::set_video_contrast(float _value)
{
	set_slider_value(m_contrast_slider, m_contrast_left_min, _value);
}

float RealisticInterface::on_slider_drag(RC::Event &_event, float _xmin)
{
	int x = _event.GetParameter("mouse_x",0);
	float dx = x - m_drag_start_x;
	float dxp = (dx / float(m_width))*100.f;
	PDEBUGF(LOG_V2, LOG_GUI, "slider drag: x=%dpx,dx=%.1fpx,dxp=%.1f%%\n",x,dx,dxp);
	RC::Element * slider = _event.GetTargetElement();
	float slider_left = m_drag_start_left + dxp;
	slider_left = clamp(slider_left, _xmin, _xmin+s_slider_length);
	return ((slider_left-_xmin)/s_slider_length);
}

void RealisticInterface::on_volume_drag(RC::Event &_event)
{
	float value = on_slider_drag(_event, m_volume_left_min);
	m_gui->set_audio_volume(value);
}

void RealisticInterface::on_brightness_drag(RC::Event &_event)
{
	float value = on_slider_drag(_event, m_brightness_left_min);
	m_gui->set_video_brightness(value);
}

void RealisticInterface::on_contrast_drag(RC::Event &_event)
{
	float value = on_slider_drag(_event, m_contrast_left_min);
	m_gui->set_video_contrast(value);
}

void RealisticInterface::on_dragstart(RC::Event &_event)
{
	RC::Element * slider = _event.GetTargetElement();
	m_drag_start_x = _event.GetParameter("mouse_x",0);
	m_drag_start_left = slider->GetProperty<float>("left");
	PDEBUGF(LOG_V2, LOG_GUI, "slider start: x=%d\n",m_drag_start_x);
}

