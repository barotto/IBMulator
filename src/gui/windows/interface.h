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

#ifndef IBMULATOR_GUI_INTERFACE_H
#define IBMULATOR_GUI_INTERFACE_H

#include "gui/window.h"
#include "gui/guifx.h"
#include "gui/screen_renderer.h"
#include "fileselect.h"
#include "state_save.h"
#include "state_save_info.h"
#include "state_load.h"
#include "hardware/devices/vga.h"
#include "hardware/devices/floppyctrl.h"
#include <RmlUi/Core/EventListener.h>

class Machine;
class GUI;
class Mixer;
class FloppyCtrl;
class StorageCtrl;

class InterfaceFX : public GUIFX
{
public:
	enum SampleType {
		FLOPPY_INSERT,
		FLOPPY_EJECT
	};
	enum FDDType {
		FDD_5_25,
		FDD_3_5
	};

private:
	std::vector<AudioBuffer> m_buffers[2];
	const static SoundFX::samples_t ms_samples[2];
	std::atomic<int> m_event;

public:
	InterfaceFX() : GUIFX() {}
	void init(Mixer *);
	void use_floppy(FDDType _fdd_type, SampleType _how);
	bool create_sound_samples(uint64_t _time_span_us, bool, bool);
};


class InterfaceScreen
{
protected:
	std::unique_ptr<ScreenRenderer> m_renderer;
	GUI *m_gui;
	VGADisplay m_display; // GUI-Machine interface
	
public:
	ScreenRenderer::Params params;
	
public:
	InterfaceScreen(GUI *_gui);
	virtual ~InterfaceScreen();

	void set_brightness(float);
	void set_contrast(float);
	void set_saturation(float);
	void set_ambient(float);
	void set_monochrome(bool);
	
	ScreenRenderer * renderer() { return m_renderer.get(); }
	VGADisplay * display() { return &m_display; }

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

	Machine *m_machine;
	Mixer *m_mixer;
	std::unique_ptr<FileSelect> m_fs;
	std::unique_ptr<StateSave> m_state_save;
	std::unique_ptr<StateSaveInfo> m_state_save_info;
	std::unique_ptr<StateLoad> m_state_load;
	struct {
		FloppyCtrl *ctrl = nullptr;
		unsigned curr_drive = 0;
		InterfaceFX::FDDType curr_drive_type = InterfaceFX::FDD_5_25;
		bool present = false;
		bool changed = false;
		FloppyLoader::State loader[FloppyCtrl::MAX_DRIVES+1] = {FloppyLoader::State::IDLE};
		bool event = false; // would a proper event system be better suited?
		std::mutex mutex;
	} m_floppy;
	StorageCtrl *m_hdd = nullptr;

	bool m_audio_enabled = false;
	InterfaceFX m_audio;

public:
	Interface(Machine *_machine, GUI * _gui, Mixer *_mixer, const char *_rml);
	virtual ~Interface();

	virtual void create();
	virtual void update();
	virtual void close();
	virtual void config_changed(bool _startup);
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
	void on_printer(Rml::Event &);
	void on_dblclick(Rml::Event &);

	VGADisplay * vga_display() { return m_screen->display(); }
	void render_screen();

	ScreenRenderer * screen_renderer() const { return m_screen->renderer(); }

	virtual void action(int) {}
	virtual void switch_power();
	virtual void set_audio_volume(float);
	virtual void set_video_brightness(float);
	virtual void set_video_contrast(float);
	virtual void set_video_saturation(float);
	virtual void set_ambient_light(float);
	
	virtual bool is_system_visible() const { return true; }

	void save_framebuffer(std::string _screenfile, std::string _palfile);
	SDL_Surface * copy_framebuffer();
	
	virtual void sig_state_restored() {}

protected:
	
	void floppy_loader_state_cb(FloppyLoader::State, int);
	
	virtual void set_hdd_active(bool _active);

	virtual void set_floppy_string(std::string _filename);
	virtual void set_floppy_config(bool _b_present);
	virtual void set_floppy_active(bool _active);
	
	void update_floppy_string(unsigned _drive);
	std::vector<unsigned> get_floppy_types(unsigned _drive);
	
	void show_state_dialog(bool _save);
	void show_state_info_dialog(StateRecord::Info _info);
	
	static std::string get_filesel_info(std::string);

	std::string create_new_floppy_image(std::string _dir, std::string _file, 
			FloppyDisk::StdType _type, std::string _format);
	
	void reset_savestate_dialogs(std::string _dir);
};

#endif
