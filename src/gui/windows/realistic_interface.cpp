/*
 * Copyright (C) 2015, 2016  Marco Bortolin
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
#include "realistic_interface.h"
#include "utils.h"
#include <sys/stat.h>

#include <Rocket/Core.h>

#define REALISTIC_MONITOR_VS      "fb-normal.vs"
#define REALISTIC_MONITOR_FS      "monitor.fs"
#define REALISTIC_VGA_VS          "fb-realistic.vs"
#define REALISTIC_REFLECTION_MAP  "realistic_reflection.png"
#define REALISTIC_VGA_SCALE       1.05f // determines the VGA border size

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
	GUI_EVT( "power",     "click", RealisticInterface::on_power ),
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
	if((_power_on || _change_state) && !m_channel->is_enabled()) {
		m_channel->enable(true);
	}
	m_power_on = _power_on;
	m_change_state = _change_state;
}

//this method is called by the Mixer thread
bool RealisticFX::create_sound_samples(uint64_t _time_span_us, bool, bool)
{
	bool power_on = m_power_on;
	bool change_state = m_change_state;
	m_change_state = false;

	return SoundFX::play_motor(_time_span_us, *m_channel, power_on, change_state,
			m_buffers[POWER_UP], m_buffers[POWER_ON], m_buffers[POWER_DOWN]);
}


RealisticInterface::RealisticInterface(Machine *_machine, GUI * _gui, Mixer *_mixer)
:
Interface(_machine, _gui, _mixer, "realistic_interface.rml")
{
	assert(m_wnd);

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
	std::string shadersdir = GUI::get_shaders_dir();
	init_gl(
		g_program.config().get_enum(DISPLAY_SECTION, DISPLAY_REALISTIC_FILTER, GUI::ms_gui_sampler),
		shadersdir + REALISTIC_VGA_VS,
		g_program.config().find_file(DISPLAY_SECTION,DISPLAY_REALISTIC_SHADER)
	);
	GLCALL( m_rdisplay.uniforms.ambient = glGetUniformLocation(m_display.prog, "iAmbientLight") );
	GLCALL( m_rdisplay.uniforms.reflection_map = glGetUniformLocation(m_display.prog, "iReflectionMap") );
	GLCALL( m_rdisplay.uniforms.reflection_scale = glGetUniformLocation(m_display.prog, "iReflectionScale") );
	GLCALL( m_rdisplay.uniforms.vga_scale = glGetUniformLocation(m_display.prog, "iVGAScale") );

	m_monitor.mvmat.load_identity();
	std::vector<std::string> vs,fs;
	vs.push_back(shadersdir + REALISTIC_MONITOR_VS);
	fs.push_back(shadersdir + REALISTIC_MONITOR_FS);
	m_monitor.prog = GUI::load_GLSL_program(vs,fs);
	m_monitor.ambient = clamp(g_program.config().get_real(DISPLAY_SECTION, DISPLAY_REALISTIC_AMBIENT), 0.0, 1.0);
	GLCALL( m_monitor.uniforms.mvmat = glGetUniformLocation(m_monitor.prog, "iModelView") );
	GLCALL( m_monitor.uniforms.ambient = glGetUniformLocation(m_display.prog, "iAmbientLight") );
	GLCALL( m_monitor.uniforms.reflection_map = glGetUniformLocation(m_monitor.prog, "iReflectionMap") );

	GLCALL( glGenSamplers(1, &m_monitor.reflection_sampler) );
	GLCALL( glSamplerParameteri(m_monitor.reflection_sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER) );
	GLCALL( glSamplerParameteri(m_monitor.reflection_sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER) );
	GLCALL( glSamplerParameteri(m_monitor.reflection_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR) );
	GLCALL( glSamplerParameteri(m_monitor.reflection_sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR) );

	m_monitor.reflection_map = GUI::load_texture(GUI::get_images_dir() + REALISTIC_REFLECTION_MAP);
	m_rdisplay.reflection_scale = vec2f(1.0,1.0);

	set_audio_volume(g_program.config().get_real(MIXER_SECTION, MIXER_VOLUME));
	set_video_brightness(g_program.config().get_real(DISPLAY_SECTION, DISPLAY_BRIGHTNESS));
	set_video_contrast(g_program.config().get_real(DISPLAY_SECTION, DISPLAY_CONTRAST));

	m_real_audio.init(_mixer);
}

RealisticInterface::~RealisticInterface()
{
}

vec2f RealisticInterface::display_size(int _width, int _height, float _sys_w, float _xoffset, float _scale, float _aspect)
{
	vec2f disp;

	const float sys_ratio = ms_width / ms_height;
	const float abs_disp_w = ms_width - _xoffset*2.f;
	const float wdisp_ratio = abs_disp_w / ms_width;

	if(_sys_w > _width) {
		disp.x = float(_width) * wdisp_ratio;
		disp.x *= _scale;
		disp.y = disp.x * 1.f/_aspect;
	} else {
		const float hdisp_ratio = sys_ratio * wdisp_ratio * 1.f/_aspect;
		disp.y = float(_height) * hdisp_ratio;
		disp.y *= _scale;
		disp.x = disp.y * _aspect;
	}
	return disp;
}

void RealisticInterface::display_transform(int _width, int _height,
		const vec2f &_disp, const vec2f &_system, mat4f &_mvmat)
{
	vec2f scale, trans;

	scale.x = _disp.x / float(_width);  // VGA width (screen ratio)
	scale.y = _disp.y / float(_height); // VGA height (screen ratio)
	float monitor_h = (((ms_monitor_height+ms_monitor_bezelh*2.f)*_system.y)/ms_height) / float(_height);
	float sysy = _system.y / float(_height);
	float disp_offset = monitor_h - scale.y;
	trans.x = 0.f;
	if(RealisticInterface::ms_align_top) {
		trans.y = 1.0 - scale.y - disp_offset;
	} else {
		trans.y = -1.0 + (sysy-monitor_h)*2.f + disp_offset + scale.y;
	}
	_mvmat.load_scale(scale.x, scale.y, 1.0);
	_mvmat.load_translation(trans.x, trans.y, 0.0);
}

void RealisticInterface::container_size_changed(int _width, int _height)
{
	vec2f system, disp, mdisp;
	float disp_scale = g_program.config().get_real(DISPLAY_SECTION, DISPLAY_REALISTIC_SCALE);
	const float sys_ratio = ms_width / ms_height;

	system.y = _height;
	system.x = system.y * sys_ratio;
	disp  = display_size(_width, _height, system.x, ms_monitor_bezelw+ms_vga_left, disp_scale*REALISTIC_VGA_SCALE, 4.f/3.f);
	mdisp = display_size(_width, _height, system.x, ms_monitor_bezelw-1.f, 1.f, ms_monitor_width/ms_monitor_height);
	m_rdisplay.reflection_scale = disp / mdisp;
	if(system.x > _width) {
		system.x = _width;
		system.y = system.x * 1.f/sys_ratio;
	}
	m_size = system;
	m_display.size.x = round(disp.x);
	m_display.size.y = round(disp.y);
	display_transform(_width, _height, disp, system, m_display.mvmat);
	display_transform(_width, _height, mdisp, system, m_monitor.mvmat);

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

void RealisticInterface::render_monitor()
{
	GLCALL( glUseProgram(m_monitor.prog) );
	GLCALL( glUniformMatrix4fv(m_monitor.uniforms.mvmat, 1, GL_FALSE, m_monitor.mvmat.data()) );
	GLCALL( glActiveTexture(GL_TEXTURE0) );
	GLCALL( glBindTexture(GL_TEXTURE_2D, m_monitor.reflection_map) );
	GLCALL( glBindSampler(0, m_monitor.reflection_sampler) );
	GLCALL( glUniform1i(m_monitor.uniforms.reflection_map, 0) );
	GLCALL( glUniform1f(m_monitor.uniforms.ambient, m_monitor.ambient) );
	GLCALL( glDisable(GL_BLEND) );
	render_quad();

	Interface::render_monitor();
}

void RealisticInterface::render_vga()
{
	//m_display.prog is active
	// texunit0 is the VGA image
	GLCALL( glActiveTexture(GL_TEXTURE1) );
	GLCALL( glBindTexture(GL_TEXTURE_2D, m_monitor.reflection_map) );
	GLCALL( glBindSampler(1, m_monitor.reflection_sampler) );
	GLCALL( glUniform1i(m_rdisplay.uniforms.reflection_map, 1) );
	GLCALL( glUniform2f(m_rdisplay.uniforms.reflection_scale, m_rdisplay.reflection_scale.x, m_rdisplay.reflection_scale.y) );
	GLCALL( glUniform2f(m_rdisplay.uniforms.vga_scale, REALISTIC_VGA_SCALE, REALISTIC_VGA_SCALE) );
	GLCALL( glUniform1f(m_rdisplay.uniforms.ambient, m_monitor.ambient) );
	render_quad();
}

void RealisticInterface::set_slider_value(RC::Element *_slider, float _xleft, float _value)
{
	_value = clamp(_value,ms_min_slider_val,ms_max_slider_val);
	_value = (_value - ms_min_slider_val) / (ms_max_slider_val - ms_min_slider_val);
	float slider_left = _xleft + m_slider_len_p*_value;
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

void RealisticInterface::sig_state_restored()
{
	m_real_audio.update(true, false);
}

float RealisticInterface::on_slider_drag(RC::Event &_event, float _xmin)
{
	int x = _event.GetParameter("mouse_x",0);
	float dx = x - m_drag_start_x;
	float dxp = (dx / float(m_size.x))*100.f;
	PDEBUGF(LOG_V2, LOG_GUI, "slider drag: x=%dpx,dx=%.1fpx,dxp=%.1f%%\n",x,dx,dxp);
	float slider_left = m_drag_start_left + dxp;
	slider_left = clamp(slider_left, _xmin, _xmin+m_slider_len_p);
	return ((slider_left-_xmin)/m_slider_len_p);
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

void RealisticInterface::on_power(RC::Event &_evt)
{
	bool on = m_gui->machine()->is_on();
	Interface::on_power(_evt);
	m_real_audio.update(!on, true);
}

