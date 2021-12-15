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

#ifndef IBMULATOR_GUI_INTERFACE_H
#define IBMULATOR_GUI_INTERFACE_H

#include "gui/window.h"
#include "gui/guifx.h"
#include "screen_renderer.h"
#include "fileselect.h"
#include "state_save.h"
#include "state_save_info.h"
#include "state_load.h"
#include "hardware/devices/vga.h"
#include "hardware/devices/floppy.h"
#include <RmlUi/Core/EventListener.h>

class Machine;
class GUI;
class Mixer;
class FloppyCtrl;
class StorageCtrl;

class InterfaceFX : public GUIFX
{
private:
	enum SampleType {
		FLOPPY_INSERT,
		FLOPPY_EJECT
	};
	std::vector<AudioBuffer> m_buffers;
	const static SoundFX::samples_t ms_samples;
	std::atomic<int> m_event;

public:
	InterfaceFX() : GUIFX() {}
	void init(Mixer *);
	void use_floppy(bool _insert);
	bool create_sound_samples(uint64_t _time_span_us, bool, bool);
};


class InterfaceScreen
{
protected:
	std::unique_ptr<ScreenRenderer> m_renderer;
	GUI *m_gui;
	
public:
	struct {
		VGADisplay display; // GUI-Machine interface
		
		vec2i size;         // size in pixels of the vga image inside the interface
		mat4f mvmat;        // position and size of the vga image inside the interface
		mat4f pmat;         // projection matrix

		float brightness;
		float contrast;
		float saturation;
	} vga;
	
public:
	InterfaceScreen(GUI *_gui);
	virtual ~InterfaceScreen();
	
	ScreenRenderer * renderer() { return m_renderer.get(); }
	virtual void render();
	
protected:
	void sync_with_device();
};


class Interface : public Window
{
protected:
	std::unique_ptr<InterfaceScreen> m_screen;

	vec2i m_size = 0; // current size of the interface

	struct {
		Rml::Element *power;
		Rml::Element *fdd_select;
	} m_buttons;

	struct {
		Rml::Element *fdd_led, *hdd_led;
		Rml::Element *fdd_disk;
	} m_status;

	struct {
		bool power, fdd, hdd;
	} m_leds;

	Rml::Element *m_speed = nullptr, *m_speed_value = nullptr;
	Rml::Element *m_message = nullptr;

	uint m_curr_drive = 0;
	bool m_floppy_present = false;
	bool m_floppy_changed = false;

	Machine *m_machine;
	Mixer *m_mixer;
	std::unique_ptr<FileSelect> m_fs;
	std::unique_ptr<StateSave> m_state_save;
	std::unique_ptr<StateSaveInfo> m_state_save_info;
	std::unique_ptr<StateLoad> m_state_load;
	FloppyCtrl *m_floppy = nullptr;
	StorageCtrl *m_hdd = nullptr;

	bool m_audio_enabled = false;
	InterfaceFX m_audio;

public:
	Interface(Machine *_machine, GUI * _gui, Mixer *_mixer, const char *_rml);
	virtual ~Interface();

	virtual void create();
	virtual void update();
	virtual void close();
	virtual void config_changed();
	virtual void container_size_changed(int /*_width*/, int /*_height*/) {}
	vec2i get_size() { return m_size; }

	void show_message(const char* _mex);

	void save_state(StateRecord::Info _info);
	
	void on_power(Rml::Event &);
	void on_fdd_select(Rml::Event &);
	void on_fdd_eject(Rml::Event &);
	void on_fdd_mount(Rml::Event &);
	void on_floppy_mount(std::string _img_path, bool _write_protect);
	void on_save_state(Rml::Event &);
	void on_load_state(Rml::Event &);
	void on_dblclick(Rml::Event &);

	VGADisplay * vga_display() { return & m_screen->vga.display; }
	void render_screen();

	virtual void action(int) {}
	virtual void switch_power();
	virtual void set_audio_volume(float);
	virtual void set_video_brightness(float);
	virtual void set_video_contrast(float);
	virtual void set_video_saturation(float);
	
	virtual bool is_system_visible() const { return true; }

	void save_framebuffer(std::string _screenfile, std::string _palfile);
	SDL_Surface * copy_framebuffer();
	
	virtual void sig_state_restored() {}

protected:
	virtual void set_floppy_string(std::string _filename);
	virtual void set_floppy_config(bool _b_present);
	virtual void set_floppy_active(bool _active);
	
	std::vector<uint64_t> get_floppy_sizes(unsigned _floppy_drive);
	
	void show_state_dialog(bool _save);
	void show_state_info_dialog(StateRecord::Info _info);
	
	static std::string get_filesel_info(std::string);

	std::string create_new_floppy_image(std::string _dir, std::string _file, 
			FloppyDiskType _type, bool _formatted);
	
	void reset_savestate_dialogs(std::string _dir);
};

#endif
