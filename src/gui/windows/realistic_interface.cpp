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
#include "gui_opengl.h"
#include "machine.h"
#include "program.h"
#include "realistic_interface.h"
#include "utils.h"
#include <sys/stat.h>

#include <RmlUi/Core.h>

#define REALISTIC_MONITOR_VS      "fb-normal.vs"
#define REALISTIC_MONITOR_FS      "monitor.fs"
#define REALISTIC_VGA_VS          "fb-realistic.vs"
#define REALISTIC_REFLECTION_MAP  "realistic_reflection.png"
#define REALISTIC_VGA_BORDER      1.0f

constexpr float RealisticInterface::ms_zoomin_factors[];

/* Anatomy of the "realistic" monitor:
 ___________________
/   _____________   \
|  /             \  |
|  |  ---------  |  |
|  |  |       |  |  |
|  |  |  VGA  |  |  |
|  |  | image |  |  |
|  |  ---------  |  |
|  \_VGA border__/  |
\____VGA frame______/
   Monitor bezel
*/

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
	GUI_EVT( "contrast_slider",   "dragstart", RealisticInterface::on_dragstart ),
	GUI_EVT_T( "sysbkgd",    "dblclick", Interface::on_dblclick ),
	GUI_EVT_T( "background", "dblclick", Interface::on_dblclick )
};

const SoundFX::samples_t RealisticFX::ms_samples = {
	{"System power up",   "sounds" FS_SEP "system" FS_SEP "power_up.wav"},
	{"System power down", "sounds" FS_SEP "system" FS_SEP "power_down.wav"},
	{"System power on",   "sounds" FS_SEP "system" FS_SEP "power_on.wav"}
};

void RealisticFX::init(Mixer *_mixer)
{
	AudioSpec spec({AUDIO_FORMAT_F32, 1, 48000});
	GUIFX::init(_mixer,
		std::bind(&RealisticFX::create_sound_samples, this,
				std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
		"GUI system", spec);
	m_buffers = SoundFX::load_samples(spec, ms_samples);
}

void RealisticFX::update(bool _power_on, bool _change_state)
{
	if(m_channel->volume()<=FLT_MIN) {
		return;
	}
	if((_power_on || _change_state)) {
		m_channel->enable(true);
	}
	m_power_on = _power_on;
	m_change_state = _change_state;
}

//this method is called by the Mixer thread
bool RealisticFX::create_sound_samples(uint64_t _time_span_ns, bool, bool)
{
	bool power_on = m_power_on;
	bool change_state = m_change_state;
	m_change_state = false;

	return SoundFX::play_motor(_time_span_ns, *m_channel, power_on, change_state,
			m_buffers[POWER_UP], m_buffers[POWER_ON], m_buffers[POWER_DOWN]);
}

RealisticScreen::RealisticScreen(GUI *_gui)
: InterfaceScreen(_gui),
vga_image_scale(REALISTIC_VGA_BORDER)
{
}

RealisticScreen::~RealisticScreen()
{
}

void RealisticScreen::render()
{
	m_renderer->render_monitor(monitor.pmat, monitor.mvmat, monitor.ambient);
	
	sync_with_device();
	m_renderer->render_vga(
		vga.pmat, vga.mvmat, vga.size,
		vga.brightness, vga.contrast, vga.saturation, vga.display.is_monochrome(),
		monitor.ambient, vga_image_scale, vga_reflection_scale);
}

RealisticInterface::RealisticInterface(Machine *_machine, GUI *_gui, Mixer *_mixer)
:
Interface(_machine, _gui, _mixer, "realistic_interface.rml")
{
}

RealisticInterface::~RealisticInterface()
{
}

void RealisticInterface::create()
{
	Interface::create();

	m_system = get_element("system");
	m_floppy_disk = get_element("floppy_disk");
	m_floppy_disk->SetClass("disk", m_floppy_present);
	m_led_power = get_element("power_led");
	m_led_power_bloom = get_element("power_led_bloom");
	m_led_fdd_bloom = get_element("fdd_led_bloom");
	m_led_hdd_bloom = get_element("hdd_led_bloom");

	m_volume_slider = get_element("volume_slider");
	m_brightness_slider = get_element("brightness_slider");
	m_contrast_slider = get_element("contrast_slider");

	m_scale = 1.f;
	m_display_align = DisplayAlign::TOP;
	m_cur_zoom = ZoomMode::WHOLE;
	
	static std::map<std::string, unsigned> modes = {
		{ "",        ZoomMode::CYCLE   },
		{ "cycle",   ZoomMode::CYCLE   },
		{ "monitor", ZoomMode::MONITOR },
		{ "bezel",   ZoomMode::BEZEL   },
		{ "screen",  ZoomMode::SCREEN  }
	};
	m_zoom_mode = g_program.config().get_enum(GUI_SECTION, GUI_REALISTIC_ZOOM, modes, ZoomMode::CYCLE);
	
	
	static std::map<std::string, unsigned> dark_val = {
		{ "",        false },
		{ "bright",  false },
		{ "dark",    true  }
	};
	bool dark_style = g_program.config().get_enum(GUI_SECTION, GUI_REALISTIC_STYLE, dark_val, false);
	m_system->SetClass("dark", dark_style);
	
	float slider_width = m_volume_slider->GetProperty<float>("width");
	m_slider_len_p = ms_slider_length/ms_width * 100.f - slider_width;
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
	
	m_screen = std::make_unique<RealisticScreen>(m_gui);
	
	std::string frag_sh = g_program.config().find_file(DISPLAY_SECTION, DISPLAY_REALISTIC_SHADER);
	
	screen()->renderer()->load_vga_program(
		GUI::shaders_dir() + REALISTIC_VGA_VS, frag_sh,
		g_program.config().get_enum(DISPLAY_SECTION, DISPLAY_REALISTIC_FILTER, GUI::ms_gui_sampler)
	);
	
	screen()->renderer()->load_monitor_program(
		GUI::shaders_dir() + REALISTIC_MONITOR_VS,
		GUI::shaders_dir() + REALISTIC_MONITOR_FS,
		GUI::images_dir() + REALISTIC_REFLECTION_MAP
	);
	screen()->vga.pmat = mat4_ortho<float>(0, 1.0, 1.0, 0, 0, 1);

	screen()->monitor.pmat = mat4_ortho<float>(0.0, 1.0, 1.0, 0.0, 0.0, 1.0);
	screen()->monitor.mvmat.load_identity();
	if(dark_style) {
		screen()->monitor.ambient = 0.0;
	} else {
		screen()->monitor.ambient = g_program.config().get_real(DISPLAY_SECTION, DISPLAY_REALISTIC_AMBIENT);
	}
	screen()->vga_reflection_scale = vec2f(1.0,1.0);
	
	RealisticInterface::set_audio_volume(g_program.config().get_real(MIXER_SECTION, MIXER_VOLUME));
	RealisticInterface::set_video_brightness(g_program.config().get_real(DISPLAY_SECTION, DISPLAY_BRIGHTNESS));
	RealisticInterface::set_video_contrast(g_program.config().get_real(DISPLAY_SECTION, DISPLAY_CONTRAST));

	m_real_audio_enabled = g_program.config().get_bool(SOUNDFX_SECTION, SOUNDFX_ENABLED);
	if(m_real_audio_enabled) {
		m_real_audio.init(m_mixer);
	}
}

vec2f RealisticInterface::display_size(int _width, int _height, float _xoffset, float _scale, float _aspect)
{
	vec2f size;

	const float abs_disp_w = ms_width - _xoffset*2.f;
	const float wdisp_ratio = abs_disp_w / ms_width;
	
	if(m_cur_zoom == ZoomMode::SCREEN) {
		float h = float(_width) / _aspect;
		if(h > _height) {
			size.y = float(_height) * wdisp_ratio * _scale;
			size.x = size.y * _aspect;
		} else {
			size.x = float(_width) * wdisp_ratio * _scale;
			size.y = size.x / _aspect;
		}
	} else {
		const float sys_ratio = ms_width / ms_height;
		const float hdisp_ratio = (sys_ratio * wdisp_ratio) / _aspect;
		size.y = float(_height) * hdisp_ratio;
		size.y *= _scale;
		size.x = size.y * _aspect;
	}
	return size;
}

void RealisticInterface::container_size_changed(int _width, int _height)
{
	// sizes_in pixels
	vec2f sys_size, vga_size, mon_size;
	const float sys_ratio = ms_width / ms_height;
	int system_top = 0;
	
	if(m_cur_zoom == ZoomMode::SCREEN) {
		vga_size  = display_size(_width, _height,
				ms_vga_left, // x offset
				1.f, // scale
				ORIGINAL_MONITOR_RATIO // aspect
				);
		mon_size = display_size(_width, _height,
				0.f, // x offset
				1.f, // scale
				ms_monitor_width / ms_monitor_height // aspect
				);
		sys_size.x = ms_width * (mon_size.x / ms_monitor_width);
		sys_size.y = sys_size.x / sys_ratio;
		system_top = -(ms_monitor_bezelh * (mon_size.y / ms_monitor_height));
	} else {
		sys_size.y = _height * m_scale;
		sys_size.x = sys_size.y * sys_ratio;
		if(sys_size.x > _width) {
			sys_size.x = _width;
			sys_size.y = sys_size.x / sys_ratio;
		}
		system_top = 0;
		vga_size = display_size(sys_size.x, sys_size.y,
					ms_monitor_bezelw + ms_vga_left, // x offset
					1.f, // scale
					ORIGINAL_MONITOR_RATIO // aspect
					);
		mon_size = display_size(sys_size.x, sys_size.y,
					ms_monitor_bezelw - 1.f, // x offset
					1.f, // scale
					ms_monitor_width / ms_monitor_height // aspect
					);
	}
	screen()->vga_reflection_scale = vga_size / mon_size;

	m_size = sys_size;
	screen()->vga.size.x = round(vga_size.x);
	screen()->vga.size.y = round(vga_size.y);

	vec2f scale, trans;
	scale.x = mon_size.x / _width;
	scale.y = mon_size.y / _height;
	trans.x = (1.f - scale.x) / 2.f;
	if(m_display_align == DisplayAlign::TOP_NOBEZEL) {
		trans.y = 0;
	} else {
		trans.y = (ms_monitor_bezelh / ms_height * sys_size.y) / float(_height);
	}
	screen()->monitor.mvmat.load_scale(scale.x, scale.y, 1.0);
	screen()->monitor.mvmat.load_translation(trans.x, trans.y, 0.0);
	
	float vga_scale_y = vga_size.y / _height;
	trans.y = trans.y + scale.y/2.f - vga_scale_y/2.f;
	scale.x = vga_size.x / _width;
	scale.y = vga_scale_y;
	trans.x = (1.f - scale.x) / 2.f;
	screen()->vga.mvmat.load_scale(scale.x, scale.y, 1.0);
	screen()->vga.mvmat.load_translation(trans.x, trans.y, 0.0);

	m_system->SetProperty("width",  str_format("%upx", m_size.x));
	m_system->SetProperty("height", str_format("%upx", m_size.y));
	m_system->SetProperty("top",    str_format("%dpx", system_top));

	unsigned fontsize = m_size.x / 40;
	m_status.fdd_disk->SetProperty("font-size", str_format("%upx", fontsize));
}

void RealisticInterface::update()
{
	Interface::update();
	if(m_leds.fdd) {
		m_led_fdd_bloom->SetClass("active", true);
	} else {
		m_led_fdd_bloom->SetClass("active", false);
	}
	if(m_floppy_present) {
		m_floppy_disk->SetClass("present", true);
	} else {
		m_floppy_disk->SetClass("present", false);
	}
	if(m_leds.hdd) {
		m_led_hdd_bloom->SetClass("active", true);
	} else {
		m_led_hdd_bloom->SetClass("active", false);
	}
	if(m_leds.power) {
		m_led_power->SetClass("active", true);
		m_led_power_bloom->SetClass("active", true);
	} else {
		m_led_power->SetClass("active", false);
		m_led_power_bloom->SetClass("active", false);
	}
}

void RealisticInterface::set_slider_value(Rml::Element *_slider, float _xleft, float _value)
{
	_value = clamp(_value,ms_min_slider_val,ms_max_slider_val);
	_value = (_value - ms_min_slider_val) / (ms_max_slider_val - ms_min_slider_val);
	float slider_left = _xleft + m_slider_len_p*_value;
	_slider->SetProperty("left", str_format("%.1f%%", slider_left));
}

void RealisticInterface::action(int _action)
{
	if(_action == 0) {
		if(m_zoom_mode == ZoomMode::CYCLE) {
			m_cur_zoom = (m_cur_zoom + 1) % (ZoomMode::MAX_ZOOM+1);
		} else {
			if(m_cur_zoom == ZoomMode::WHOLE) {
				m_cur_zoom = m_zoom_mode;
			} else {
				m_cur_zoom = ZoomMode::WHOLE;
			}
		}
		m_scale = ms_zoomin_factors[m_cur_zoom];
		switch(m_cur_zoom) {
			case ZoomMode::WHOLE:
			case ZoomMode::MONITOR:
			case ZoomMode::BEZEL:
				m_display_align = DisplayAlign::TOP;
				break;
			case ZoomMode::SCREEN:
				m_display_align = DisplayAlign::TOP_NOBEZEL;
				break;
			default:
				break;
		}
	} else if(_action == 1) {
		if(m_system->IsClassSet("dark")) {
			m_system->SetClass("dark", false);
			screen()->monitor.ambient =
				g_program.config().get_real(DISPLAY_SECTION, DISPLAY_REALISTIC_AMBIENT);
		} else {
			m_system->SetClass("dark", true);
			screen()->monitor.ambient = 0.0;
		}
	}
}

void RealisticInterface::switch_power()
{
	bool on = m_machine->is_on();
	Interface::switch_power();
	if(m_real_audio_enabled) {
		m_real_audio.update(!on, true);
	}
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

void RealisticInterface::sig_state_restored()
{
	if(m_real_audio_enabled) {
		m_real_audio.update(true, false);
	}
}

float RealisticInterface::on_slider_drag(Rml::Event &_event, float _xmin)
{
	int x = _event.GetParameter("mouse_x",0);
	float dx = x - m_drag_start_x;
	float dxp = (dx / float(m_size.x))*100.f;
	PDEBUGF(LOG_V2, LOG_GUI, "slider drag: x=%dpx,dx=%.1fpx,dxp=%.1f%%\n",x,dx,dxp);
	float slider_left = m_drag_start_left + dxp;
	slider_left = clamp(slider_left, _xmin, _xmin+m_slider_len_p);
	return ((slider_left-_xmin)/m_slider_len_p);
}

void RealisticInterface::on_volume_drag(Rml::Event &_event)
{
	float value = on_slider_drag(_event, m_volume_left_min);
	value = lerp(ms_min_slider_val, ms_max_slider_val, value);
	set_audio_volume(value);
}

void RealisticInterface::on_brightness_drag(Rml::Event &_event)
{
	float value = on_slider_drag(_event, m_brightness_left_min);
	value = lerp(ms_min_slider_val, ms_max_slider_val, value);
	set_video_brightness(value);
}

void RealisticInterface::on_contrast_drag(Rml::Event &_event)
{
	float value = on_slider_drag(_event, m_contrast_left_min);
	value = lerp(ms_min_slider_val, ms_max_slider_val, value);
	set_video_contrast(value);
}

void RealisticInterface::on_dragstart(Rml::Event &_event)
{
	Rml::Element * slider = _event.GetTargetElement();
	m_drag_start_x = _event.GetParameter("mouse_x",0);
	m_drag_start_left = slider->GetProperty<float>("left");
	PDEBUGF(LOG_V2, LOG_GUI, "slider start: x=%d\n",m_drag_start_x);
}

