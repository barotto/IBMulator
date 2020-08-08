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

#ifndef IBMULATOR_GUI_REALISTIC_INTERFACE_H
#define IBMULATOR_GUI_REALISTIC_INTERFACE_H

#include "interface.h"
#include "mixer.h"
#include "audio/soundfx.h"
#include <Rocket/Core/EventListener.h>

class Machine;
class GUI;

class RealisticFX : public GUIFX
{
private:
	std::atomic<bool> m_power_on, m_change_state;
	enum SampleType {
		POWER_UP = 0,
		POWER_DOWN,
		POWER_ON
	};
	std::vector<AudioBuffer> m_buffers;
	const static SoundFX::samples_t ms_samples;

public:
	RealisticFX() : GUIFX(), m_power_on(false), m_change_state(false) {}
	void init(Mixer *_mixer);
	void update(bool _power_on, bool _change_state);
	bool create_sound_samples(uint64_t _time_span_us, bool, bool);
};


class RealisticScreen : public InterfaceScreen
{
public:
	struct {
		mat4f mvmat;
		float ambient;
	} monitor;

	vec2f vga_image_scale;
	vec2f vga_reflection_scale;
	
	RealisticScreen(GUI *_gui);
	~RealisticScreen();
	
	void render();
};


class RealisticInterface : public Interface
{
private:

	RC::Element *m_system,
	            *m_floppy_disk,
	            *m_led_power;

	RC::Element *m_volume_slider,
	            *m_brightness_slider,
	            *m_contrast_slider;

	float m_slider_len_p;
	float m_volume_left_min;
	float m_brightness_left_min;
	float m_contrast_left_min;

	int   m_drag_start_x;
	float m_drag_start_left;

	float m_scale;
	enum DisplayAlign {
		TOP, TOP_NOBEZEL, BOTTOM
	};
	unsigned m_display_align;
	enum ZoomMode {
		WHOLE, MONITOR, BEZEL, SCREEN, CYCLE, MAX_ZOOM=SCREEN 
	};
	unsigned m_cur_zoom;
	unsigned m_zoom_mode;
	
	static event_map_t ms_evt_map;

	bool m_real_audio_enabled;
	RealisticFX m_real_audio;

	static constexpr float ms_min_slider_val = 0.0f;
	static constexpr float ms_max_slider_val = 1.3f;

	// the following values depend on the machine texture used.
	// all values are expressed in pixels.
	static constexpr float ms_width          = 1100.0f; // texture width
	static constexpr float ms_height         = 1200.0f; // texture height
	static constexpr float ms_monitor_width  =  858.0f; // monitor width, bezel excluded
	static constexpr float ms_monitor_height =  648.0f; // monitor height, bezel excluded
	static constexpr float ms_monitor_bezelw =  121.0f; // monitor bezel width (should be ms_width-ms_monitor_width/2)
	static constexpr float ms_monitor_bezelh =  106.0f; // monitor bezel height
	static constexpr float ms_vga_left       =   86.0f; // offset of the VGA image from the left bezel
	static constexpr float ms_slider_length  =  100.0f; // slider horizontal movement length

	// scale factors at the available zoom levels:
	static constexpr float ms_zoomin_factors[] = { 1.f, 1.26f, 1.4f, 0.f };

public:

	RealisticInterface(Machine *_machine, GUI * _gui, Mixer *_mixer);
	~RealisticInterface();

	void update();
	void container_size_changed(int _width, int _height);

	event_map_t & get_event_map() { return RealisticInterface::ms_evt_map; }

	void action(int);
	
	void set_audio_volume(float _value);
	void set_video_brightness(float _value);
	void set_video_contrast(float _value);
	
	void sig_state_restored();

private:
	vec2f display_size(int _width, int _height, float _xoffset, float _scale, float _aspect);
	void  display_transform(int _width, int _height, const vec2f &_disp, const vec2f &_system, mat4f &_mvmat);

	void set_slider_value(RC::Element *_slider, float _xmin, float _value);

	float on_slider_drag(RC::Event &_event, float _xmin);
	void  on_volume_drag(RC::Event &);
	void  on_brightness_drag(RC::Event &);
	void  on_contrast_drag(RC::Event &);
	void  on_dragstart(RC::Event &);
	void  on_power(RC::Event &);

	inline RealisticScreen * screen() { return (RealisticScreen *)m_screen.get(); }
};

#endif
