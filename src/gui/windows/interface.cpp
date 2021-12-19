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
#include "gui/gui_opengl.h"
#include "gui/gui_sdl2d.h"
#include "screen_renderer_opengl.h"
#include "screen_renderer_sdl2d.h"
#include "machine.h"
#include "mixer.h"
#include "program.h"
#include "utils.h"
#include "fatreader.h"
#include <sys/stat.h>
#include "stb/stb.h"
#include <RmlUi/Core.h>

#include "tinyfiledialogs/tinyfiledialogs.h"

#include "hardware/devices/floppy.h"
#include "hardware/devices/storagectrl.h"
using namespace std::placeholders;

const SoundFX::samples_t InterfaceFX::ms_samples = {
	{"Floppy insert", "sounds" FS_SEP "floppy" FS_SEP "disk_insert.wav"},
	{"Floppy eject",  "sounds" FS_SEP "floppy" FS_SEP "disk_eject.wav"}
};

void InterfaceFX::init(Mixer *_mixer)
{
	AudioSpec spec({AUDIO_FORMAT_F32, 1, 48000});
	GUIFX::init(_mixer,
		std::bind(&InterfaceFX::create_sound_samples, this, _1, _2, _3),
		"GUI interface", spec);
	m_buffers = SoundFX::load_samples(spec, ms_samples);
}

void InterfaceFX::use_floppy(bool _insert)
{
	if(m_channel->volume()<=FLT_MIN) {
		return;
	}

	if(_insert) {
		m_event = FLOPPY_INSERT;
	} else {
		m_event = FLOPPY_EJECT;
	}
	m_channel->enable(true);
}

bool InterfaceFX::create_sound_samples(uint64_t, bool, bool)
{
	if(m_event == FLOPPY_INSERT) {
		m_channel->flush();
		m_channel->play(m_buffers[FLOPPY_INSERT], 0);
	} else if(m_event == FLOPPY_EJECT) {
		m_channel->flush();
		m_channel->play(m_buffers[FLOPPY_EJECT], 0);
	}
	// possible event miss, but i don't care, they are very slow anyway
	m_event = -1;
	m_channel->enable(false);
	return false;
}

InterfaceScreen::InterfaceScreen(GUI *_gui)
{
	m_gui = _gui;
	switch(_gui->renderer()) {
		case GUI_RENDERER_OPENGL: {
			m_renderer = std::make_unique<ScreenRenderer_OpenGL>();
			dynamic_cast<ScreenRenderer_OpenGL*>(m_renderer.get())->init(vga.display);
			break;
		}
		case GUI_RENDERER_SDL2D: {
			m_renderer = std::make_unique<ScreenRenderer_SDL2D>();
			SDL_Renderer *sdlrend = dynamic_cast<GUI_SDL2D*>(_gui)->sdl_renderer();
			dynamic_cast<ScreenRenderer_SDL2D*>(m_renderer.get())->init(vga.display, sdlrend);
			break;
		}
		default: {
			// errors should be detected during GUI object creation
			PDEBUGF(LOG_V0, LOG_GUI, "Invalid renderer!\n");
			return;
		}
	}
	
	vga.mvmat.load_identity();
	vga.pmat.load_identity();
	vga.size = 0;
	vga.brightness = 1.f;
	vga.contrast = 1.f;
	vga.saturation = 1.f;
}

InterfaceScreen::~InterfaceScreen()
{
}

void InterfaceScreen::render()
{
	sync_with_device();
	
	m_renderer->render_vga(vga.pmat, vga.mvmat, vga.size,
		vga.brightness, vga.contrast, vga.saturation, vga.display.is_monochrome(),
		0, 0, 0);
}

void InterfaceScreen::sync_with_device()
{
	// TODO The machine is a different thread and these methods are not thread safe.
	// They could return garbage, but the worst that would happen is some sporadic
	// tearing or stuttering. The wait could be skipped (tearing) or could be
	// called without reason (stuttering) but in any case the program should
	// not end in a deadlock.
	if(m_gui->threads_sync_enabled())
	{
		if(
			g_machine.is_on() && 
			!g_machine.is_paused() && 
			g_machine.cycles_factor() == 1.0 &&
			g_machine.get_bench().load < 1.0
		) {
			try {
				// Wait for no more than 2 frames.
				// Using a timeout let us simplify the code at the expense of possible
				// stuttering, which would happen only in specific and non meaningful
				// cases like when the user pauses the machine.
				// I think this is acceptable. 
				vga.display.wait_for_device(g_program.heartbeat() * 2);
			} catch(std::exception &) {}
			
			g_program.pacer().skip();
		}
	}
	else
	{
		g_program.pacer().skip();
	}
	
	if(m_gui->vga_buffering_enabled()) {
		vga.display.lock();
		vec2i vga_res = vec2i(vga.display.last_mode().xres, vga.display.last_mode().yres);
		// this intermediate buffer is to reduce the blocking effect of glTexSubImage2D:
		// when the program runs with the default shaders, the load on the GPU is very low
		// so the drivers lower the GPU's clocks to the minimum value;
		// the result is the GPU's memory controller load goes high and glTexSubImage2D takes
		// a lot of time to complete, bloking the machine emulation thread.
		// PBOs are a possible alternative, but a memcpy is way simpler.
		FrameBuffer vga_buf = vga.display.last_framebuffer();
		vga.display.unlock();
		// now the Machine thread is free to continue emulation
		// meanwhile we start rendering the last VGA image
		m_renderer->store_vga_framebuffer(vga_buf, vga_res);
	} else if(vga.display.fb_updated()) {
		vga.display.lock();
		vec2i vga_res = vec2i(vga.display.mode().xres, vga.display.mode().yres);
		FrameBuffer vga_buf = vga.display.framebuffer();
		vga.display.clear_fb_updated();
		vga.display.unlock();
		m_renderer->store_vga_framebuffer(vga_buf, vga_res);
	}
}


Interface::Interface(Machine *_machine, GUI *_gui, Mixer *_mixer, const char *_rml)
:
Window(_gui, _rml),
m_machine(_machine),
m_mixer(_mixer)
{
}

Interface::~Interface()
{
}

void Interface::close()
{
	if(m_fs) {
		m_fs->close();
		m_fs.reset(nullptr);
	}
	if(m_state_save) {
		m_state_save->close();
		m_state_save.reset(nullptr);
	}
	if(m_state_load) {
		m_state_load->close();
		m_state_load.reset(nullptr);
	}
	if(m_state_save_info) {
		m_state_save_info->close();
		m_state_save_info.reset(nullptr);
	}
	Window::close();
}

void Interface::create()
{
	Window::create();
	
	m_buttons.power = get_element("power");
	m_buttons.fdd_select = get_element("fdd_select");
	m_status.fdd_led = get_element("fdd_led");
	m_status.hdd_led = get_element("hdd_led");
	m_status.fdd_disk = get_element("fdd_disk");
	m_speed = get_element("speed");
	m_speed_value = get_element("speed_value");
	m_message = get_element("message");

	m_leds.power = false;

	m_fs = std::make_unique<FileSelect>(m_gui);
	m_fs->create();
	m_fs->set_select_callbk(std::bind(&Interface::on_floppy_mount, this, _1, _2));
	m_fs->set_cancel_callbk(nullptr);
	m_fs->set_newfloppy_callbk(std::bind(&Interface::create_new_floppy_image, this, _1, _2, _3, _4));
	m_fs->set_inforeq_fn(get_filesel_info);
	std::string home_dir = g_program.config().get_file(PROGRAM_SECTION, PROGRAM_MEDIA_DIR, FILE_TYPE_USER);
	std::string cfg_home = g_program.config().get_cfg_home();
	if(home_dir.empty()) {
		home_dir = cfg_home;
	}
	try {
		m_fs->set_home(home_dir);
	} catch(std::runtime_error &) {
		// if still not valid then give up
		m_fs->set_home(cfg_home);
	}

	m_state_save = std::make_unique<StateSave>(m_gui);
	m_state_save->create();
	m_state_save->set_modal(true);
	m_state_save_info = std::make_unique<StateSaveInfo>(m_gui);
	m_state_save_info->create();
	m_state_save_info->set_modal(true);

	m_state_load = std::make_unique<StateLoad>(m_gui);
	m_state_load->create();
	m_state_load->set_modal(true);

	m_audio_enabled = g_program.config().get_bool(SOUNDFX_SECTION, SOUNDFX_ENABLED);
	if(m_audio_enabled) {
		m_audio.init(m_mixer);
	}
}

void Interface::set_floppy_config(bool _b_present)
{
	m_buttons.fdd_select->SetClass("d-none", !_b_present);

	m_buttons.fdd_select->SetClass("a", true);
	m_buttons.fdd_select->SetClass("b", false);
}

void Interface::set_floppy_active(bool _active)
{
	m_leds.fdd = _active;
	m_status.fdd_led->SetClass("active", _active);
}

void Interface::set_hdd_active(bool _active)
{
	m_leds.hdd = _active;
	m_status.hdd_led->SetClass("active", _active);
}

void Interface::config_changed()
{
	m_floppy_present = false;
	m_floppy_changed = false;
	m_curr_drive = 0;
	
	set_floppy_string("");
	set_floppy_active(false);
	set_floppy_config(false);
	m_fs->hide();
	
	m_floppy = m_machine->devices().device<FloppyCtrl>();
	if(m_floppy) {
		m_floppy_present = g_program.config().get_bool(DISK_A_SECTION, DISK_INSERTED);
		if(m_floppy_present) {
			set_floppy_string(g_program.config().get_file(DISK_A_SECTION, DISK_PATH,
					FILE_TYPE_USER));
		}
		m_floppy_changed = m_floppy->has_disk_changed(0);
		set_floppy_config(m_floppy->drive_type(1) != FDD_NONE);
	}

	m_fs->set_compat_sizes(get_floppy_sizes(m_curr_drive));
	if(m_fs->is_current_dir_valid()) {
		m_fs->reload();
	}
	
	set_hdd_active(false);
	m_hdd = m_machine->devices().device<StorageCtrl>();

	set_audio_volume(g_program.config().get_real(MIXER_SECTION, MIXER_VOLUME));
	set_video_brightness(g_program.config().get_real(DISPLAY_SECTION, DISPLAY_BRIGHTNESS));
	set_video_contrast(g_program.config().get_real(DISPLAY_SECTION, DISPLAY_CONTRAST));
	set_video_saturation(g_program.config().get_real(DISPLAY_SECTION, DISPLAY_SATURATION));
	
	bool is_mono = g_program.config().get_enum(DISPLAY_SECTION, DISPLAY_TYPE, {
			{ "color",     false },
			{ "mono",       true },
			{ "monochrome", true }
	}, false);
	m_screen->vga.display.set_monochrome(is_mono);
	PINFOF(LOG_V0, LOG_GUI, "Installed a %s monitor\n", is_mono?"monochrome":"color");
}

void Interface::set_floppy_string(std::string _filename)
{
	if(!_filename.empty()) {
		size_t pos = _filename.rfind(FS_SEP);
		if(pos!=std::string::npos) {
			_filename = _filename.substr(pos+1);
		}
	}
	m_status.fdd_disk->SetInnerRML(_filename.c_str());
}

void Interface::on_floppy_mount(std::string _img_path, bool _write_protect)
{
	struct stat sb;
	if((FileSys::stat(_img_path.c_str(), &sb) != 0) || S_ISDIR(sb.st_mode)) {
		PERRF(LOG_GUI, "Unable to read '%s'\n", _img_path.c_str());
		m_fs->hide();
		return;
	}

	if(m_floppy->drive_type(1) != FDD_NONE) {
		//check if the same image file is already mounted on drive A
		const char *section = m_curr_drive?DISK_A_SECTION:DISK_B_SECTION;
		if(g_program.config().get_bool(section,DISK_INSERTED)) {
			std::string other = g_program.config().get_file(section,DISK_PATH, FILE_TYPE_USER);
			struct stat other_s;
			if(FileSys::stat(other.c_str(), &other_s) == 0) {
				if(other_s.st_dev == sb.st_dev && other_s.st_ino == sb.st_ino) {
					PERRF(LOG_GUI, "Can't mount '%s' on drive %s because it's already mounted on drive %s\n",
							_img_path.c_str(), m_curr_drive?"B":"A", m_curr_drive?"A":"B");
					m_fs->hide();
					return;
				}
			}
		}
	}

	uint type;
	switch(sb.st_size) {
		case FLOPPY_160K_BYTES: type = FLOPPY_160K; break;
		case FLOPPY_180K_BYTES: type = FLOPPY_180K; break;
		case FLOPPY_320K_BYTES: type = FLOPPY_320K; break;
		case FLOPPY_360K_BYTES: type = FLOPPY_360K; break;
		case FLOPPY_1_2_BYTES:  type = FLOPPY_1_2;  break;
		case FLOPPY_720K_BYTES: type = FLOPPY_720K; break;
		case FLOPPY_1_44_BYTES: type = FLOPPY_1_44; break;
		default:
			PERRF(LOG_GUI, "Unable to determine the type of '%s'\n", _img_path.c_str());
			m_fs->hide();
			return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "mounting '%s' on floppy %s %s\n", _img_path.c_str(),
			m_curr_drive?"B":"A", _write_protect?"(write protected)":"");
	m_machine->cmd_insert_media(m_curr_drive, type, _img_path, _write_protect);
	m_fs->hide();

	if(m_audio_enabled) {
		m_audio.use_floppy(true);
	}
}

void Interface::update()
{
	if(m_floppy) {
		bool motor = m_floppy->is_motor_on(m_curr_drive);
		if(motor && m_leds.fdd==false) {
			set_floppy_active(true);
		} else if(!motor && m_leds.fdd==true) {
			set_floppy_active(false);
		}
		bool present = m_floppy->is_media_present(m_curr_drive);
		bool changed = m_floppy->has_disk_changed(m_curr_drive);
		if(present && (m_floppy_present==false || m_floppy_changed!=changed)) {
			m_floppy_changed = changed;
			m_floppy_present = true;
			const char *section = m_curr_drive?DISK_B_SECTION:DISK_A_SECTION;
			set_floppy_string(g_program.config().get_file(section,DISK_PATH,FILE_TYPE_USER));
		} else if(!present && m_floppy_present==true) {
			m_floppy_present = false;
			set_floppy_string("");
		}
	}

	if(m_hdd) {
		bool hdd_busy = m_hdd->is_busy();
		if(hdd_busy && m_leds.hdd==false) {
			set_hdd_active(true);
		} else if(!hdd_busy && m_leds.hdd==true) {
			set_hdd_active(false);
		}
	}

	if(m_machine->is_on() && m_leds.power==false) {
		m_leds.power = true;
		m_buttons.power->SetClass("active", true);
	} else if(!m_machine->is_on() && m_leds.power==true) {
		m_leds.power = false;
		m_buttons.power->SetClass("active", false);
	}
	
	if(m_machine->is_on()) {
		if(m_machine->is_paused()) {
			m_speed->SetClass("warning", false);
			m_speed->SetClass("slow", false);
			m_speed->SetClass("paused", true);
			m_speed_value->SetInnerRML("paused");
			m_speed->SetProperty("visibility", "visible");
		} else {
			m_speed->SetClass("paused", false);
			int vtime_ratio_1000 = round(m_machine->get_bench().cavg_vtime_ratio * 1000.0);
			static std::string str(10,0);
			m_speed_value->SetInnerRML(str_format(str, "%d%%", vtime_ratio_1000/10));
			if(m_machine->cycles_factor() != 1.0) {
				m_speed->SetClass("warning", false);
				if(m_machine->get_bench().load > 1.0) {
					m_speed->SetClass("slow", true);
				} else {
					m_speed->SetClass("slow", false);
				}
				m_speed->SetProperty("visibility", "visible");
			} else {
				if(m_machine->get_bench().is_stressed()) {
					m_speed->SetClass("warning", true);
					m_speed->SetProperty("visibility", "visible");
				} else {
					m_speed->SetProperty("visibility", "hidden");
				}
			}
		}
	} else {
		m_speed->SetProperty("visibility", "hidden");
	}
	
	m_fs->update();
	m_state_load->update();
	m_state_save->update();
}

void Interface::on_power(Rml::Event &)
{
	switch_power();
}

void Interface::show_message(const char* _mex)
{
	std::string str(_mex);
	str_replace_all(str, "\n", "<br />");

	m_message->SetInnerRML(str);
	if(str.empty()) {
		m_message->SetProperty("visibility", "hidden");
	} else {
		m_message->SetProperty("visibility", "visible");
	}
}

void Interface::on_fdd_select(Rml::Event &)
{
	m_status.fdd_disk->SetInnerRML("");
	if(m_curr_drive == 0) {
		m_curr_drive = 1;
		m_floppy_changed = m_floppy->has_disk_changed(1);
		m_buttons.fdd_select->SetClass("a", false);
		m_buttons.fdd_select->SetClass("b", true);
		if(g_program.config().get_bool(DISK_B_SECTION,DISK_INSERTED)) {
			set_floppy_string(g_program.config().get_file(DISK_B_SECTION,DISK_PATH, FILE_TYPE_USER));
		}
	} else {
		m_curr_drive = 0;
		m_floppy_changed = m_floppy->has_disk_changed(0);
		m_buttons.fdd_select->SetClass("a", true);
		m_buttons.fdd_select->SetClass("b", false);
		if(g_program.config().get_bool(DISK_A_SECTION,DISK_INSERTED)) {
			set_floppy_string(g_program.config().get_file(DISK_A_SECTION,DISK_PATH, FILE_TYPE_USER));
		}
	}
	m_fs->set_compat_sizes(get_floppy_sizes(m_curr_drive));
	m_fs->reload();
}

void Interface::on_fdd_eject(Rml::Event &)
{
	m_machine->cmd_eject_media(m_curr_drive);
	if(m_audio_enabled && m_floppy->is_media_present(m_curr_drive)) {
		m_audio.use_floppy(false);
	}
}

std::vector<uint64_t> Interface::get_floppy_sizes(unsigned _floppy_drive)
{
	switch(m_floppy->drive_type(_floppy_drive)) {
		case FDD_525DD: // 360K  5.25"
			return { FLOPPY_160K_BYTES, FLOPPY_180K_BYTES, FLOPPY_320K_BYTES, FLOPPY_360K_BYTES };
		case FDD_525HD: // 1.2M  5.25"
			return { FLOPPY_160K_BYTES, FLOPPY_180K_BYTES, FLOPPY_320K_BYTES, FLOPPY_360K_BYTES, FLOPPY_1_2_BYTES };
		case FDD_350DD: // 720K  3.5"
			return { FLOPPY_720K_BYTES };
		case FDD_350HD: // 1.44M 3.5"
			return { FLOPPY_720K_BYTES, FLOPPY_1_44_BYTES };
		case FDD_350ED: // 2.88M 3.5"
			return { FLOPPY_720K_BYTES, FLOPPY_1_44_BYTES, FLOPPY_2_88_BYTES };
		default:
			return {};
	}
}

void Interface::on_fdd_mount(Rml::Event &)
{
	if(m_floppy == nullptr) {
		show_message("floppy drives not present");
		return;
	}

	std::string floppy_dir;

	if(m_curr_drive==0) {
		floppy_dir = g_program.config().find_media(DISK_A_SECTION, DISK_PATH);
	} else {
		floppy_dir = g_program.config().find_media(DISK_B_SECTION, DISK_PATH);
	}

	if(!floppy_dir.empty()) {
		size_t pos = floppy_dir.rfind(FS_SEP);
		if(pos == std::string::npos) {
			floppy_dir = "";
		} else {
			floppy_dir = floppy_dir.substr(0,pos);
		}
	}
	if(floppy_dir.empty()) {
		// the file select dialog contains a valid media/home directory
		// the file select dialog always exists, even when native dialogs are set
		floppy_dir = m_fs->get_home();
	}
	
	if(g_program.config().get_string(PROGRAM_SECTION, PROGRAM_FILE_DIALOGS, "custom") == "native") {
		floppy_dir += "/";
		const char *filter_patterns[2] = { "*.img", "*.ima" };
		if(m_gui->is_fullscreen()) {
			// native dialogs don't play well when the application they're called by
			// is rendered fullscreen.
			// the user will have to switch back to fullscreen, can't auto do it.
			m_gui->toggle_fullscreen();
		}
#ifdef _WIN32
		tinyfd_winUtf8 = 1;
#endif
		const char *openfile = tinyfd_openFileDialog(
			"Select floppy image",
			floppy_dir.c_str(),
			2, // aNumOfFilterPatterns
			filter_patterns,
			"Floppy disk (*.img, *.ima)",
			0 // aAllowMultipleSelects
		);
		if(openfile) {
			on_floppy_mount(openfile, false);
		}
	} else {
		try {
			if(m_fs->get_current_dir() != floppy_dir) {
				m_fs->set_current_dir(floppy_dir);
			}
		} catch(...) {
			m_fs->set_current_dir(m_fs->get_home());
		}
		m_fs->show();
	}
}

void Interface::reset_savestate_dialogs(std::string _dir)
{
	Rml::ReleaseTextures();
	StateDialog::set_current_dir(_dir);
	m_state_save->set_dirty();
	m_state_load->set_dirty();
}

void Interface::show_state_dialog(bool _save)
{
	try {
		std::string dir = g_program.config().get_file(CAPTURE_SECTION, CAPTURE_DIR, FILE_TYPE_USER);
		if(dir.empty()) {
			throw std::runtime_error("Capture directory not set!");
		}
		if(dir != StateDialog::current_dir()) {
			reset_savestate_dialogs(dir);
		}
	} catch(std::runtime_error &e) {
		PERRF(LOG_GUI, "%s\n", e.what());
		return;
	}

	bool machine_was_paused = m_machine->is_paused();
	bool input_was_grabbed = m_gui->is_input_grabbed();
	auto dialog_delete = [=](StateRecord::Info _info) {
		m_gui->show_message_box(
			"Delete State",
			str_format("Do you want to delete slot %s?", _info.name.c_str()),
			MessageWnd::Type::MSGW_YES_NO,
			[=]()
			{
				PDEBUGF(LOG_V0, LOG_GUI, "Delete record: %s\n", _info.name.c_str());
				try {
					g_program.delete_state(_info);
				} catch(std::runtime_error &e) {
					PERRF(LOG_GUI, "Error deleting state record: %s\n", e.what());
				} catch(std::out_of_range &) {
					PDEBUGF(LOG_V0, LOG_GUI, "StateDialog: invalid state id!\n");
				}
				reset_savestate_dialogs("");
			}
		);
	};
	auto dialog_end = [=]() {
		m_state_save->hide();
		m_state_load->hide();
		if(!machine_was_paused) {
			m_machine->cmd_resume(false);
		}
		m_gui->grab_input(input_was_grabbed);
	};
	m_machine->cmd_pause(false);
	m_gui->grab_input(false);
	if(_save) {
		m_state_save->update();
		m_state_save->set_callbacks(
			// save
			[=](StateRecord::Info _info)
			{
				if(_info.name == QUICKSAVE_RECORD) {
					save_state({QUICKSAVE_RECORD, QUICKSAVE_DESC, "", 0, 0});
					dialog_end();
				} else {
					m_state_save_info->set_callbacks(
						[=](StateRecord::Info _info)
						{
							save_state(_info);
							m_state_save_info->hide();
							dialog_end();
						}
					);
					m_state_save_info->set_state(_info);
					m_state_save_info->show();
				}
			},
			// delete
			dialog_delete,
			// cancel
			dialog_end
		);
		m_state_save->show();
	} else {
		m_state_load->update();
		m_state_load->set_callbacks(
			[=](StateRecord::Info _info)
			{
				g_program.restore_state(_info, [this]() {
					m_gui->show_message("State restored");
				}, nullptr);
				m_state_save->entry_select(_info.name);
				dialog_end();
			},
			// delete
			dialog_delete,
			// cancel
			dialog_end
		);
		m_state_load->show();
	}
}

void Interface::on_save_state(Rml::Event &)
{
	if(m_machine->is_on()) {
		show_state_dialog(true);
	} else {
		m_gui->show_message("The machine must be on");
	}
}

void Interface::on_load_state(Rml::Event &)
{
	show_state_dialog(false);
}

void Interface::on_dblclick(Rml::Event &)
{
	m_gui->toggle_fullscreen();
}

void Interface::save_state(StateRecord::Info _info)
{
	PDEBUGF(LOG_V0, LOG_GUI, "Saving %s: %s\n", _info.name.c_str(), _info.user_desc.c_str());
	g_program.save_state(_info, [this](StateRecord::Info _info) {
		reset_savestate_dialogs("");
		m_state_save->entry_select(_info.name);
		m_state_load->entry_select(_info.name);
		m_gui->show_message("State saved");
	}, nullptr);
}

void Interface::render_screen()
{
	m_screen->render();
}

void Interface::switch_power()
{
	m_machine->cmd_switch_power();
	m_machine->cmd_resume();
}

void Interface::set_audio_volume(float _volume)
{
	m_mixer->cmd_set_category_volume(MixerChannel::Category::AUDIO, _volume);
}

void Interface::set_video_brightness(float _level)
{
	m_screen->vga.brightness = _level;
}

void Interface::set_video_contrast(float _level)
{
	m_screen->vga.contrast = _level;
}

void Interface::set_video_saturation(float _level)
{
	m_screen->vga.saturation = _level;
}

void Interface::save_framebuffer(std::string _screenfile, std::string _palfile)
{
	SDL_Surface * surface = SDL_CreateRGBSurface(
		0,
		m_screen->vga.display.mode().xres,
		m_screen->vga.display.mode().yres,
		32,
		PALETTE_RMASK,
		PALETTE_GMASK,
		PALETTE_BMASK,
		PALETTE_AMASK
	);
	if(!surface) {
		PERRF(LOG_GUI, "error creating buffer surface\n");
		throw std::exception();
	}
	SDL_Surface * palette = nullptr;
	if(!_palfile.empty()) {
		palette = SDL_CreateRGBSurface(
			0,         //flags (unused)
			16, 16,    //w x h
			32,        //bit depth
			PALETTE_RMASK,
			PALETTE_GMASK,
			PALETTE_BMASK,
			PALETTE_AMASK
		);
		if(!palette) {
			SDL_FreeSurface(surface);
			PERRF(LOG_GUI, "error creating palette surface\n");
			throw std::exception();
		}
	}
	m_screen->vga.display.lock();
		SDL_LockSurface(surface);
		m_screen->vga.display.copy_screen((uint8_t*)surface->pixels);
		SDL_UnlockSurface(surface);
		if(palette) {
			SDL_LockSurface(palette);
			for(uint i=0; i<256; i++) {
				((uint32_t*)palette->pixels)[i] = m_screen->vga.display.get_color(i);
			}
			SDL_UnlockSurface(palette);
		}
	m_screen->vga.display.unlock();

	stbi_write_png_compression_level = 9;
	int result = stbi_write_png(_screenfile.c_str(), surface->w, surface->h,
			surface->format->BytesPerPixel, surface->pixels, surface->pitch);

	SDL_FreeSurface(surface);
	if(result<0) {
		PERRF(LOG_GUI, "error saving surface to PNG\n");
		if(palette) {
			SDL_FreeSurface(palette);
		}
		throw std::exception();
	}
	if(palette) {
		result = stbi_write_png(_palfile.c_str(), palette->w, palette->h,
				palette->format->BytesPerPixel, palette->pixels, palette->pitch);
		SDL_FreeSurface(palette);
		if(result < 0) {
			PERRF(LOG_GUI, "error saving palette to PNG\n");
			throw std::exception();
		}
	}
}

SDL_Surface * Interface::copy_framebuffer()
{
	m_screen->vga.display.lock();
	SDL_Surface * surface = SDL_CreateRGBSurface(
		0,
		m_screen->vga.display.mode().xres,
		m_screen->vga.display.mode().yres,
		32,
		PALETTE_RMASK,
		PALETTE_GMASK,
		PALETTE_BMASK,
		PALETTE_AMASK
	);
	if(surface) {
		SDL_LockSurface(surface);
		m_screen->vga.display.copy_screen((uint8_t*)surface->pixels);
		SDL_UnlockSurface(surface);
	}
	m_screen->vga.display.unlock();

	if(!surface) {
		PERRF(LOG_GUI, "Error creating buffer surface\n");
		throw std::exception();
	}
	return surface;
}

std::string Interface::get_filesel_info(std::string _filepath)
{
	FATReader fat;
	try {
		fat.read(_filepath);
	} catch(std::runtime_error & err) {
		return err.what();
	}
	auto boot_sec = fat.get_boot_sector();
	
	std::string media_desc;
	try {
		media_desc = boot_sec.get_media_str();
	} catch(std::runtime_error & err) {
		return err.what();
	}

	auto to_value = [=](std::string s){
		return std::string("<span class=\"value\">") + str_to_html(s,true) + "</span>";
	};

	std::string info;
	info = std::string("File: ") + str_to_html(FileSys::get_basename(_filepath.c_str())) + "<br />";
	info += "Media: " + str_to_html(media_desc) + "<br />";
	info += "OEM name: " + to_value(boot_sec.get_oem_str());
	if(boot_sec.oem_name[5]=='I' && boot_sec.oem_name[6]=='H' && boot_sec.oem_name[7]=='C') {
		info += " (mod. by Win95+)";
	}
	info += "<br />";
	info += "Disk label: " + to_value(boot_sec.get_vol_label_str()) + "<br />";
	
	auto root = fat.get_root_entries();
	if(root.empty() || root[0].is_empty()) {
		info += "<br />Empty disk";
	} else {
		info += "Volume label: " + to_value(fat.get_volume_id()) + "<br />";
		info += "Directory<br /><br />";
		info += "<table class=\"directory_listing\">";
		for(auto &entry : root) {
			if(entry.is_file() || entry.is_directory()) {
				auto ext = str_to_upper(entry.get_ext_str());
				bool exe = (ext == "BAT" || ext == "COM" || ext == "EXE");
				info += std::string("<tr class=\"") + 
						(entry.is_file()?"file":"dir") +
						(exe?" executable":"") +
						"\">";
				info += "<td class=\"name\">" + str_to_html(entry.get_name_str()) + "</td>";
				info += "<td class=\"extension\">" + str_to_html(entry.get_ext_str()) + "</td>";
				if(entry.is_file()) {
					info += "<td class=\"size\">" + str_format("%u", entry.FileSize) + "</td>";
					time_t wrtime = entry.get_time_t(entry.WrtDate, entry.WrtTime);
					info += "<td class=\"date\">" + str_format_time(wrtime, "%x") + "</td>";
				} else {
					info += "<td class=\"size\">" + str_to_html("<DIR>") + "</td>";
					info += "<td class=\"date\"></td>";
				}
				info += "</tr>";
			}
		}
		info += "</table>";
	}

	return info;
}

std::string Interface::create_new_floppy_image(std::string _dir, std::string _file, FloppyDiskType _type, bool _formatted)
{
	PDEBUGF(LOG_V1, LOG_GUI, "New floppy image: %s, %s, %d, %d\n", _dir.c_str(), _file.c_str(), _type, _formatted);

	if(_file.empty()) {
		throw std::runtime_error("Empty file name.");
	}
	if(!FileSys::is_directory(_dir.c_str())) {
		throw std::runtime_error("Invalid destination directory.");
	}
	if(!FileSys::is_dir_writeable(_dir.c_str())) {
		throw std::runtime_error("You don't have permission to write to the destination directory.");
	}
#ifdef _WIN32
	const std::regex invalid_chars("[<>:\"\\/\\\\|?*]");
#else
	const std::regex invalid_chars("[\\/]");
#endif
	if(std::regex_search(_file, invalid_chars)) {
		throw std::runtime_error("Invalid characters used in the file name.");
	}

	std::string base, ext;
	FileSys::get_file_parts(_file.c_str(), base, ext);
#ifdef _WIN32
	const std::vector<const char*> invalid_names = {
		"CON", "PRN", "AUX", "NUL",
		"COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8","COM9",
		"LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
	};
	if(std::find(invalid_names.begin(), invalid_names.end(), base) != invalid_names.end()) {
		throw std::runtime_error("Invalid file name.");
	}
#endif
	ext = str_to_lower(ext);
	if(ext != ".img" && ext != ".ima") {
		_file += ".img";
	}
	if(_file.size() > 255) {
		throw std::runtime_error("File name too long.");
	}

	std::string path = _dir + FS_SEP + _file;

	if(FileSys::file_exists(path.c_str())) {
		throw std::runtime_error(str_format("The file \"%s\" already exists.", _file.c_str()));
	}

	FloppyCtrl::create_new_floppy_image(path, FDD_NONE, _type, _formatted);
	
	return _file;
}