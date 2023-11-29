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

#ifndef IBMULATOR_GUI_REALISTIC_INTERFACE_H
#define IBMULATOR_GUI_REALISTIC_INTERFACE_H

#include "interface.h"
#include "gui/shader_preset.h"
#include "mixer.h"
#include "audio/soundfx.h"
#include <RmlUi/Core/EventListener.h>

class Machine;
class GUI;


class RealisticScreen : public InterfaceScreen
{
public:
	RealisticScreen(GUI *_gui);

	void render();
};


class RealisticInterface : public Interface
{
private:

	Rml::Element *m_system = nullptr,
	             *m_floppy_disk = nullptr,
	             *m_led_power = nullptr,
	             *m_led_power_bloom = nullptr,
	             *m_led_fdd_bloom = nullptr,
	             *m_led_hdd_bloom = nullptr;

	Rml::Element *m_volume_slider = nullptr,
	             *m_brightness_slider = nullptr,
	             *m_contrast_slider = nullptr;

	float m_slider_len_p = 0;
	float m_volume_left_min = 0;
	float m_brightness_left_min = 0;
	float m_contrast_left_min = 0;

	int   m_drag_start_x = 0;
	float m_drag_start_left = .0f;
	bool  m_is_dragging = false;

	float m_scale = .0f;
	enum DisplayAlign {
		TOP, TOP_NOBEZEL
	};
	unsigned m_display_align = DisplayAlign::TOP;
	enum ZoomMode {
		WHOLE, MONITOR, BEZEL, SCREEN, CYCLE, MAX_ZOOM=SCREEN 
	};
	unsigned m_cur_zoom = ZoomMode::WHOLE;
	unsigned m_zoom_mode = ZoomMode::CYCLE;
	
	static event_map_t ms_evt_map;

	static constexpr float ms_min_slider_val = 0.0f;
	static constexpr float ms_max_slider_val = 1.3f;

	// scale factors at the available zoom levels:
	static constexpr float ms_zoomin_factors[] = { 1.f, 1.26f, 1.4f, 0.f };

	// the following values depend on the machine texture used (values in pixels)
	static constexpr float ms_width          = 1100.0f; // texture width
	static constexpr float ms_height         = 1200.0f; // texture height
	static constexpr float ms_slider_length  =  100.0f; // vol/brt/cnt slider horizontal movement length

	ShaderPreset::MonitorGeometry m_monitor;
	ShaderPreset::RenderingSize m_rendering_size = ShaderPreset::VGA;

public:

	RealisticInterface(Machine *_machine, GUI * _gui, Mixer *_mixer);
	~RealisticInterface();

	virtual void create();
	virtual void update();

	void container_size_changed(int _width, int _height);

	event_map_t & get_event_map() { return RealisticInterface::ms_evt_map; }

	void action(int);
	void set_audio_volume(float _value);
	void set_video_brightness(float _value);
	void set_video_contrast(float _value);

private:
	vec2f display_size(int _width, int _height, float _xoffset, float _scale, float _aspect);

	void set_slider_value(Rml::Element *_slider, float _xmin, float _value, float _max_value);

	float on_slider_drag(Rml::Event &_event, float _xmin);
	void  on_volume_drag(Rml::Event &);
	void  on_brightness_drag(Rml::Event &);
	void  on_contrast_drag(Rml::Event &);
	void  on_dragstart(Rml::Event &);
	void  on_dragend(Rml::Event &);
	void  on_power(Rml::Event &);

	inline RealisticScreen * screen() { return (RealisticScreen *)m_screen.get(); }
};

#endif
