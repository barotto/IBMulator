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

#ifndef IBMULATOR_GUI_REALISTIC_INTERFACE_H
#define IBMULATOR_GUI_REALISTIC_INTERFACE_H

#include "interface.h"
#include <Rocket/Core/EventListener.h>

class Machine;
class GUI;

class RealisticInterface : public Interface
{
private:

	RC::Element *m_system,
	            *m_floppy_disk,
	            *m_led_power;

	RC::Element *m_volume_slider,
	            *m_brightness_slider,
	            *m_contrast_slider;

	uint  m_width, m_height;
	float m_volume_left_min;
	float m_brightness_left_min;
	float m_contrast_left_min;

	int   m_drag_start_x;
	float m_drag_start_left;

	static event_map_t ms_evt_map;

	void set_slider_value(RC::Element *_slider, float _xmin, float _value);

	float on_slider_drag(RC::Event &_event, float _xmin);
	void  on_volume_drag(RC::Event &);
	void  on_brightness_drag(RC::Event &);
	void  on_contrast_drag(RC::Event &);
	void  on_dragstart(RC::Event &);

	static constexpr float s_min_slider_val = 0.0f;
	static constexpr float s_max_slider_val = 1.3f;

public:

	// the following values depend on the machine texture used:
	static constexpr float s_width          = 2057.0f; // texture width (pixels)
	static constexpr float s_height         = 2237.0f; // texture height (pixels)
	static constexpr float s_monitor_height = 1600.0f; // monitor height bezel included (pixels)
	static constexpr float s_vga_left       =  358.0f; // offset of the VGA image from the left border (pixels)
	static constexpr float s_slider_length  =    7.0f; // slider horizontal movement length (%)

	// the alignment is specified in the rml file:
	static constexpr bool s_align_top = false;

public:

	RealisticInterface(Machine *_machine, GUI * _gui);
	~RealisticInterface();

	void update();
	void update_size(uint _width, uint _height);

	event_map_t & get_event_map() { return RealisticInterface::ms_evt_map; }

	void set_audio_volume(float _value);
	void set_video_brightness(float _value);
	void set_video_contrast(float _value);
};

#endif
