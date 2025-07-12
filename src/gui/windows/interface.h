/*
 * Copyright (C) 2015-2025  Marco Bortolin
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
#include "gui/screen_renderer.h"
#include "interface_fx.h"
#include "interface_screen.h"
#include "fileselect.h"
#include "state_save.h"
#include "state_save_info.h"
#include "state_load.h"
#include "hardware/devices/vga.h"
#include "hardware/devices/floppyevents.h"
#include "hardware/devices/floppyctrl.h"
#include "hardware/devices/cdrom_drive.h"
#include <RmlUi/Core/EventListener.h>

class Machine;
class GUI;
class Mixer;
class FloppyCtrl;
class StorageCtrl;
class Keymap;

class Interface : public Window
{
protected:
	std::unique_ptr<InterfaceScreen> m_screen;

	vec2i m_size = 0; // current size of the interface

	struct {
		Rml::Element *power;
	} m_buttons = {};

	struct {
		Rml::Element *hdd_led;
	} m_status = {};

	struct {
		bool power, hdd;
	} m_leds = {};

	Rml::Element *m_speed = nullptr;
	Rml::Element *m_speed_value = nullptr;
	Rml::Element *m_message = nullptr;

	Machine *m_machine;
	Mixer *m_mixer;
	std::unique_ptr<FileSelect> m_fs;
	std::unique_ptr<StateSave> m_state_save;
	std::unique_ptr<StateSaveInfo> m_state_save_info;
	std::unique_ptr<StateLoad> m_state_load;

	struct UIDrive;
	struct UIDriveBlock;
	struct MachineDrive {
		// a MachineDrive is the interface between the Machine and the UI
		// it can be assigned to multiple UIDrive's
		std::vector<UIDrive*> uidrives;
		GUIDrivesFX::DriveType drive_type = GUIDrivesFX::NONE;
		int drive_id = -1; // the id of the drive relative to the controller
		FloppyCtrl *floppy_ctrl = nullptr;
		StorageCtrl *storage_ctrl = nullptr;
		CdRomDrive *cdrom = nullptr;

		bool event = true;
		FloppyEvents::EventType floppy_event_type = FloppyEvents::EVENT_MEDIUM;
		CdRomEvents::EventType cd_event_type = CdRomEvents::MEDIUM;
		int64_t led_activity = 0; // tells how long the led should be on; set by Mch thread, reset by GUI thread.
		bool led_on = false;
		TimerID led_on_timer = NULL_TIMER_ID;

		std::string label;

		bool is_floppy_drive() {
			return floppy_ctrl && (drive_type == GUIDrivesFX::FDD_3_5 || drive_type == GUIDrivesFX::FDD_5_25);
		}
		bool is_cdrom_drive() {
			return storage_ctrl && (drive_type == GUIDrivesFX::CDROM);
		}
		bool is_active();
		std::vector<unsigned> compatible_file_types();
		std::vector<const char*> compatible_file_extensions();
		void add_uidrive(UIDrive *);
	};
	std::list<MachineDrive> m_drives;
	std::list<MachineDrive>::iterator m_curr_drive;

	struct UIDrive {
		// it belongs to 1 UIDriveBlock
		UIDriveBlock *uiblock = nullptr;
		// a UIDrive visually represents a MachineDrive 
		MachineDrive *drive = nullptr;

		Rml::Element *uidrive_el = nullptr;
		Rml::Element *activity_led = nullptr;
		Rml::Element *activity_led_bloom = nullptr;
		Rml::Element *medium_string = nullptr;
		Rml::Element *drive_select = nullptr;

		bool event = true;
		bool led_active = false;

		UIDrive(UIDriveBlock *_uiblock, MachineDrive *_drive, Rml::Element *_uidrive_el,
				Rml::Element *_activity_led, Rml::Element *_activity_led_bloom,
				Rml::Element *_medium_string, Rml::Element *_drive_select) :
					uiblock(_uiblock),
					drive(_drive),
					uidrive_el(_uidrive_el),
					activity_led(_activity_led),
					activity_led_bloom(_activity_led_bloom),
					medium_string(_medium_string),
					drive_select(_drive_select) {}

		void hide();
		void show();

		void set_led(bool);
		void set_medium_string(std::string _string);
		void update_medium_status();

		void update();
	};

	Rml::ElementPtr create_uidrive_el(MachineDrive *_drive, UIDriveBlock *_block);

	struct UIDriveBlock {
		// a UIDriveBlock contains multiple UIDrive's 
		Rml::Element *block_el = nullptr;
		std::list<UIDrive> uidrives;
		std::list<UIDrive>::iterator curr_drive;

		UIDriveBlock(Rml::Element *_block_el) : block_el(_block_el) {
			curr_drive = uidrives.end();
		}

		UIDrive * create_uidrive(MachineDrive *_drive, Rml::Element *uidrive_el,
				Rml::Element *activity_led, Rml::Element *activity_led_bloom,
				Rml::Element *medium_string, Rml::Element *drive_select);
		void change_drive();
		void change_drive(std::list<UIDrive>::iterator _drive);
		void change_drive(std::list<MachineDrive>::iterator _drive);
	};
	// there can be multiple UIDriveBlock's
	std::list<UIDriveBlock> m_drive_blocks;

	UIDriveBlock * create_uidrive_block(Rml::Element *_uidrive_block_el);

	void remove_drives();
	MachineDrive * config_floppy(int _id);
	MachineDrive * config_cdrom(int _id);

	void floppy_activity_cb(FloppyEvents::EventType _what, uint8_t _drive_id, MachineDrive *_drive);
	void cdrom_activity_cb(CdRomEvents::EventType _what, uint64_t _duration, MachineDrive *_drive);
	void cdrom_led_timer(uint64_t, MachineDrive *_drive);

	std::mutex m_drives_mutex; // to be acquired for every access to UIDrive

	std::vector<StorageCtrl*> m_storage_ctrls;

	bool m_audio_enabled = false;
	GUIDrivesFX m_drives_audio;
	GUISystemFX m_system_audio;

	std::string m_welcome_string;

public:
	Interface(Machine *_machine, GUI * _gui, Mixer *_mixer, const char *_rml);
	virtual ~Interface();

	virtual void create();
	virtual void update();
	virtual void close();
	virtual void config_changing();
	virtual void config_changed(bool _startup);
	virtual bool would_handle(Rml::Input::KeyIdentifier, int) { return false; }
	virtual void container_size_changed(int /*_width*/, int /*_height*/) {}
	vec2i get_size() { return m_size; }

	void show_message(const char* _mex);

	void save_state(StateRecord::Info _info);
	
	void on_power(Rml::Event &);
	bool on_drive_select(Rml::Event &, UIDriveBlock *_block);
	bool on_medium_button(Rml::Event &, MachineDrive *_drive);
	bool on_medium_mount(Rml::Event &, MachineDrive *_drive);
	void on_medium_select(std::string _img_path, bool _write_protect, MachineDrive *_drive);
	void on_save_state(Rml::Event &);
	void on_load_state(Rml::Event &);
	void on_sound(Rml::Event &);
	void on_printer(Rml::Event &);
	void on_dblclick(Rml::Event &);

	void drive_select();
	void drive_medium_mount();
	void drive_medium_eject();

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
	
	virtual void sig_state_restored();
	
	void tts_describe();
	void show_welcome_screen(const Keymap *_keymap, unsigned _mode);

protected:
	virtual void set_hdd_active(bool _active);

	std::vector<unsigned> get_floppy_types(unsigned _drive);
	
	void show_state_dialog(bool _save);
	void show_state_info_dialog(StateRecord::Info _info);
	
	static MediumInfoData get_filesel_info(std::string);

	std::string create_new_floppy_image(std::string _dir, std::string _file, 
			FloppyDisk::StdType _type, std::string _format);
	
	void reset_savestate_dialogs(std::string _dir);
};

#endif
