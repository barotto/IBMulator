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

#ifndef IBMULATOR_GUI_INTERFACE_H
#define IBMULATOR_GUI_INTERFACE_H

#include "gui/window.h"
#include "fileselect.h"
#include "hardware/devices/vga.h"
#include "hardware/devices/vgadisplay.h"
#include <Rocket/Core/EventListener.h>

class Machine;
class GUI;
class Mixer;

class Interface : public Window
{
protected:

	class Display {
	public:
		VGADisplay vga;
		vec2i vga_res;
		std::atomic<bool> vga_updated;
		GLuint tex;
		std::vector<uint32_t> tex_buf;
		GLuint sampler;
		GLint  glintf;
		GLenum glf;
		GLenum gltype;
		GLuint prog;
		mat4f mvmat;
		float brightness;
		float contrast;
		float saturation;
		vec2i size; // size in pixel of the destination quad

		struct {
			GLint vgamap;
			GLint brightness;
			GLint contrast;
			GLint saturation;
			GLint mvmat;
			GLint size;
		} uniforms;

		Display();

	} m_display;

	GLuint  m_vertex_buffer;
	GLfloat m_quad_data[18];

	vec2i m_size; // current size of the interface

	struct {
		RC::Element *power;
		RC::Element *fdd_select;
	} m_buttons;

	struct {
		RC::Element *fdd_led, *hdd_led;
		RC::Element *fdd_disk;
	} m_status;

	struct {
		bool power, fdd, hdd;
	} m_leds;

	RC::Element * m_warning;
	RC::Element * m_message;

	bool m_drive_b;
	uint m_curr_drive;
	bool m_floppy_present;
	bool m_floppy_changed;

	Machine *m_machine;
	Mixer *m_mixer;
	FileSelect *m_fs;

public:
	Interface(Machine *_machine, GUI * _gui, Mixer *_mixer, const char *_rml);
	virtual ~Interface();

	virtual void update();
	virtual void container_size_changed(int /*_width*/, int /*_height*/) {}
	vec2i get_size() { return m_size; }

	void show_warning(bool _show);
	void show_message(const char* _mex);

	void on_power(RC::Event &);
	void on_fdd_select(RC::Event &);
	void on_fdd_eject(RC::Event &);
	void on_fdd_mount(RC::Event &);
	void on_floppy_mount(std::string _img_path, bool _write_protect);

	void vga_update() { m_display.vga_updated.store(true); }
	virtual void render(RC::Context *_rcontext);

	virtual void set_audio_volume(float);
	virtual void set_video_brightness(float);
	virtual void set_video_contrast(float);
	virtual void set_video_saturation(float);

	void save_framebuffer(std::string _screenfile, std::string _palfile);
	void print_VGA_text(std::vector<uint16_t> &_text);

	virtual void sig_state_restored() {}

protected:
	void init_gl(uint _vga_sampler, std::string _vga_vshader, std::string _vga_fshader);
	virtual void render_monitor();
	virtual void render_vga();
	void render_quad();

private:
	void update_floppy_disk(std::string _filename);
};

class LogMessage : public Logdev
{
private:
	Interface *m_iface;
public:
	LogMessage(Interface* _iface);
	~LogMessage();

	void log_put(const std::string &_prefix, const std::string &_message);
};

#endif
