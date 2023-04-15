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

#include "ibmulator.h"
#include "gui/gui_opengl.h"
#include "gui/gui_sdl2d.h"
#include "screen_renderer_opengl.h"
#include "screen_renderer_sdl2d.h"
#include "machine.h"
#include "mixer.h"
#include "program.h"
#include "utils.h"
#include <sys/stat.h>
#include "stb/stb.h"
#include <RmlUi/Core.h>

#include "tinyfiledialogs/tinyfiledialogs.h"

#include "hardware/devices/floppyctrl.h"
#include "hardware/devices/storagectrl.h"
using namespace std::placeholders;


const SoundFX::samples_t InterfaceFX::ms_samples[2] = {
	{
	{"5.25 disk insert", FDD_SAMPLES_DIR "5_25_disk_insert.wav"},
	{"5.25 disk eject",  FDD_SAMPLES_DIR "5_25_disk_eject.wav"}
	},{
	{"3.5 disk insert", FDD_SAMPLES_DIR "3_5_disk_insert.wav"},
	{"3.5 disk eject",  FDD_SAMPLES_DIR "3_5_disk_eject.wav"}
	}
};

void InterfaceFX::init(Mixer *_mixer)
{
	AudioSpec spec({AUDIO_FORMAT_F32, 1, 48000});
	GUIFX::init(_mixer,
		std::bind(&InterfaceFX::create_sound_samples, this, _1, _2, _3),
		"GUI interface", spec);
	m_buffers[FDD_5_25] = SoundFX::load_samples(spec, ms_samples[FDD_5_25]);
	m_buffers[FDD_3_5] = SoundFX::load_samples(spec, ms_samples[FDD_3_5]);
}

void InterfaceFX::use_floppy(FDDType _fdd_type, SampleType _how)
{
	if(m_channel->volume()<=FLT_MIN) {
		return;
	}
	m_event = _fdd_type << 8 | _how;
	m_channel->enable(true);
}

bool InterfaceFX::create_sound_samples(uint64_t, bool, bool)
{
	// Mixer thread
	unsigned evt = m_event & 0xff;
	unsigned sub = (m_event >> 8) & 0xff;
	if(evt != 0xff) {
		assert(sub < 2);
		assert(evt < m_buffers[sub].size());
		m_channel->flush();
		m_channel->play(m_buffers[sub][evt], 0);
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
			dynamic_cast<ScreenRenderer_OpenGL*>(m_renderer.get())->init(m_display);
			break;
		}
		case GUI_RENDERER_SDL2D: {
			m_renderer = std::make_unique<ScreenRenderer_SDL2D>();
			SDL_Renderer *sdlrend = dynamic_cast<GUI_SDL2D*>(_gui)->sdl_renderer();
			dynamic_cast<ScreenRenderer_SDL2D*>(m_renderer.get())->init(m_display, sdlrend);
			break;
		}
		default: {
			// errors should be detected during GUI object creation
			PDEBUGF(LOG_V0, LOG_GUI, "Invalid renderer!\n");
			return;
		}
	}

	params.vga.mvmat.load_identity();
	params.vga.pmat = mat4_ortho<float>(0.0, 1.0, 1.0, 0.0, 0.0, 1.0);
	params.vga.mvpmat = params.vga.pmat;
	
	params.crt.mvmat.load_identity();
	params.crt.pmat = mat4_ortho<float>(0.0, 1.0, 1.0, 0.0, 0.0, 1.0);
	params.crt.mvpmat = params.crt.pmat;
}

InterfaceScreen::~InterfaceScreen()
{
}

void InterfaceScreen::render()
{
	sync_with_device();

	if(params.updated) {
		m_renderer->store_screen_params(params);
		params.updated = false;
	}
	m_renderer->render_begin();
	m_renderer->render_vga();
	m_renderer->render_end();
}

void InterfaceScreen::set_brightness(float _v)
{
	params.brightness = _v;
	params.updated = true;
}

void InterfaceScreen::set_contrast(float _v)
{
	params.contrast = _v;
	params.updated = true;
}

void InterfaceScreen::set_saturation(float _v)
{
	params.saturation = _v;
	params.updated = true;
}

void InterfaceScreen::set_ambient(float _v)
{
	params.ambient = _v;
	params.updated = true;
}

void InterfaceScreen::set_monochrome(bool _v)
{
	m_display.set_monochrome(_v);
	params.monochrome = _v;
	params.updated = true;
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
				m_display.wait_for_device(g_program.heartbeat() * 2);
			} catch(std::exception &) {}

			g_program.pacer().skip();
		}
	}
	else
	{
		g_program.pacer().skip();
	}
	
	if(m_gui->vga_buffering_enabled()) {
		m_display.lock();
		VideoModeInfo vga_mode = m_display.last_mode();
		// this intermediate buffer is to reduce the blocking effect of glTexSubImage2D:
		// when the program runs with the default shaders, the load on the GPU is very low
		// so the drivers lower the GPU's clocks to the minimum value;
		// the result is the GPU's memory controller load goes high and glTexSubImage2D takes
		// a lot of time to complete, bloking the machine emulation thread.
		// PBOs are a possible alternative, but a memcpy is way simpler.
		FrameBuffer vga_buf = m_display.last_framebuffer();
		m_display.unlock();
		// now the Machine thread is free to continue emulation
		// meanwhile we start rendering the last VGA image
		m_renderer->store_vga_framebuffer(vga_buf, vga_mode);
	} else if(m_display.fb_updated() || m_renderer->needs_vga_updates()) {
		m_display.lock();
		FrameBuffer vga_buf = m_display.framebuffer();
		VideoModeInfo vga_mode = m_display.mode();
		m_display.clear_fb_updated();
		m_display.unlock();
		m_renderer->store_vga_framebuffer(vga_buf, vga_mode);
	}
}


Interface::Interface(Machine *_machine, GUI *_gui, Mixer *_mixer, const char *_rml)
:
Window(_gui, _rml),
m_machine(_machine),
m_mixer(_mixer)
{
	_machine->register_floppy_loader_state_cb(
			std::bind(&Interface::floppy_loader_state_cb, this, _1, _2)
	);
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

	auto mode = g_program.config().get_string(DIALOGS_SECTION, DIALOGS_FILE_MODE, "grid");
	auto order = g_program.config().get_string(DIALOGS_SECTION, DIALOGS_FILE_ORDER, "name");
	int zoom = g_program.config().get_int(DIALOGS_SECTION, DIALOGS_FILE_ZOOM, 2);
	m_fs = std::make_unique<FileSelect>(m_gui);
	m_fs->create(mode, order, zoom);
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

	mode = g_program.config().get_string(DIALOGS_SECTION, DIALOGS_SAVE_MODE, "grid");
	order = g_program.config().get_string(DIALOGS_SECTION, DIALOGS_SAVE_ORDER, "date");
	zoom = g_program.config().get_int(DIALOGS_SECTION, DIALOGS_SAVE_ZOOM, 1);

	m_state_save = std::make_unique<StateSave>(m_gui);
	m_state_save->create(mode, order, zoom);
	m_state_save->set_modal(true);

	m_state_save_info = std::make_unique<StateSaveInfo>(m_gui);
	m_state_save_info->create();
	m_state_save_info->set_modal(true);

	m_state_load = std::make_unique<StateLoad>(m_gui);
	m_state_load->create(mode, order, zoom);
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

void Interface::floppy_loader_state_cb(FloppyLoader::State _state, int _drive)
{
	std::lock_guard<std::mutex> lock(m_floppy.mutex);
	if(_drive >= 0) {
		m_floppy.loader[_drive] = _state;
	} else {
		m_floppy.loader[FloppyCtrl::MAX_DRIVES] = _state;
	}
	m_floppy.event = true;
}

void Interface::config_changed(bool _startup)
{
	m_floppy.present = false;
	m_floppy.changed = false;
	m_floppy.curr_drive = 0;
	m_floppy.curr_drive_type = InterfaceFX::FDD_5_25;

	set_floppy_string("");
	set_floppy_active(false);
	set_floppy_config(false);
	m_fs->hide();

	m_floppy.ctrl = m_machine->devices().device<FloppyCtrl>();
	if(m_floppy.ctrl) {
		auto insert_floppy = [this](unsigned _drive, const char *_cfg_section) {
			std::string diskpath = g_program.config().find_media(_cfg_section, DISK_PATH);
			if(!diskpath.empty() && g_program.config().get_bool(_cfg_section, DISK_INSERTED)) {
				g_program.config().set_bool(_cfg_section, DISK_INSERTED, false);
				if(!FileSys::file_exists(diskpath.c_str())) {
					PERRF(LOG_GUI, "The floppy image specified in [%s] doesn't exist\n", _cfg_section);
					return;
				}
				if(FileSys::is_directory(diskpath.c_str())) {
					PERRF(LOG_GUI, "The floppy image specified in [%s] can't be a directory\n", _cfg_section);
					return;
				}
				bool wp = g_program.config().get_bool(_cfg_section, DISK_READONLY);
				// don't play "insert" audio sample
				m_machine->cmd_insert_floppy(_drive, diskpath, wp, nullptr);
			}
		};
		if(_startup) {
			if(m_floppy.ctrl->drive_type(0) != FloppyDrive::FDD_NONE) {
				insert_floppy(0, DISK_A_SECTION);
			}
			if(m_floppy.ctrl->drive_type(1) != FloppyDrive::FDD_NONE) {
				insert_floppy(1, DISK_B_SECTION);
			}
		}
		set_floppy_config(m_floppy.ctrl->drive_type(1) != FloppyDrive::FDD_NONE);
		if((m_floppy.ctrl->drive_type(m_floppy.curr_drive) & FloppyDisk::SIZE_MASK) == FloppyDisk::SIZE_5_25) {
			m_floppy.curr_drive_type = InterfaceFX::FDD_5_25;
		} else {
			m_floppy.curr_drive_type = InterfaceFX::FDD_3_5;
		}

		m_fs->set_compat_types(
				get_floppy_types(m_floppy.curr_drive),
				m_floppy.ctrl->get_compatible_file_extensions(),
				m_floppy.ctrl->get_compatible_formats(),
				!m_floppy.ctrl->can_use_any_floppy());
		if(m_fs->is_current_dir_valid()) {
			m_fs->reload();
		}
	}

	if(!_startup) {
		std::lock_guard<std::mutex> lock(m_floppy.mutex);
		// after a restore any pending FloppyLoader command is ignored
		for(unsigned i=0; i<FloppyCtrl::MAX_DRIVES; i++) {
			m_floppy.loader[i] = FloppyLoader::State::IDLE;
		}
		m_floppy.event = true;
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
	m_screen->set_monochrome(is_mono);
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
	if(!m_floppy.ctrl) {
		return;
	}

	m_fs->hide();

	struct stat sb;
	if((FileSys::stat(_img_path.c_str(), &sb) != 0) || S_ISDIR(sb.st_mode)) {
		PERRF(LOG_GUI, "Unable to read '%s'\n", _img_path.c_str());
		return;
	}

	const char *drive_section = m_floppy.curr_drive ? DISK_B_SECTION : DISK_A_SECTION;
	if(g_program.config().get_bool(drive_section, DISK_INSERTED)) {
		if(FileSys::is_same_file(_img_path.c_str(),
				g_program.config().get_file(drive_section, DISK_PATH, FILE_TYPE_USER).c_str())) {
			PINFOF(LOG_V0, LOG_GUI, "The selected floppy image is already mounted\n");
			return;
		}
	}

	if(m_floppy.ctrl->drive_type(1) != FloppyDrive::FDD_NONE) {
		//check if the same image file is already mounted on drive A
		const char *other_section = m_floppy.curr_drive ? DISK_A_SECTION : DISK_B_SECTION;
		if(g_program.config().get_bool(other_section, DISK_INSERTED)) {
			if(FileSys::is_same_file(_img_path.c_str(),
					g_program.config().get_file(other_section, DISK_PATH, FILE_TYPE_USER).c_str())) {
					PERRF(LOG_GUI, "Can't mount '%s' on drive %s because it's already mounted on drive %s\n",
							_img_path.c_str(), m_floppy.curr_drive?"B":"A", m_floppy.curr_drive?"A":"B");
					return;
			}
		}
	}

	PDEBUGF(LOG_V1, LOG_GUI, "Mounting '%s' on floppy %s %s\n", _img_path.c_str(),
			m_floppy.curr_drive?"B":"A", _write_protect?"(write protected)":"");

	m_machine->cmd_eject_floppy(m_floppy.curr_drive, nullptr);
	m_machine->cmd_insert_floppy(m_floppy.curr_drive, _img_path, _write_protect, [=](bool result) {
		// Machine thread here
		// "insert" audio sample plays only when floppy is confirmed inserted
		if(result && m_audio_enabled) {
			m_audio.use_floppy(m_floppy.curr_drive_type, InterfaceFX::FLOPPY_INSERT);
		}
	});

}

void Interface::update()
{
	if(m_floppy.ctrl) {
		bool motor = m_floppy.ctrl->is_motor_on(m_floppy.curr_drive);
		if(motor && m_leds.fdd==false) {
			set_floppy_active(true);
		} else if(!motor && m_leds.fdd==true) {
			set_floppy_active(false);
		}
		bool present = m_floppy.ctrl->is_media_present(m_floppy.curr_drive);
		bool changed = m_floppy.ctrl->has_disk_changed(m_floppy.curr_drive);
		if(present && (m_floppy.present==false || m_floppy.changed!=changed)) {
			m_floppy.changed = changed;
			m_floppy.present = true;
			update_floppy_string(m_floppy.curr_drive);
		} else if(!present && m_floppy.present==true) {
			m_floppy.present = false;
			update_floppy_string(m_floppy.curr_drive);
		} else {
			std::lock_guard<std::mutex> lock(m_floppy.mutex);
			if(m_floppy.event) {
				update_floppy_string(m_floppy.curr_drive);
				m_floppy.event = false;
			}
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
		m_screen->params.poweron = true;
		m_screen->params.updated = true;
		m_buttons.power->SetClass("active", true);
	} else if(!m_machine->is_on() && m_leds.power==true) {
		m_leds.power = false;
		m_screen->params.poweron = false;
		m_screen->params.updated = true;
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
	if(!m_floppy.ctrl) {
		return;
	}
	m_status.fdd_disk->SetInnerRML("");
	if(m_floppy.curr_drive == 0) {
		m_floppy.curr_drive = 1;
		m_floppy.changed = m_floppy.ctrl->has_disk_changed(1);
		m_buttons.fdd_select->SetClass("a", false);
		m_buttons.fdd_select->SetClass("b", true);
	} else {
		m_floppy.curr_drive = 0;
		m_floppy.changed = m_floppy.ctrl->has_disk_changed(0);
		m_buttons.fdd_select->SetClass("a", true);
		m_buttons.fdd_select->SetClass("b", false);
	}
	if((m_floppy.ctrl->drive_type(m_floppy.curr_drive) & FloppyDisk::SIZE_MASK) == FloppyDisk::SIZE_5_25) {
		m_floppy.curr_drive_type = InterfaceFX::FDD_5_25;
	} else {
		m_floppy.curr_drive_type = InterfaceFX::FDD_3_5;
	}
	m_floppy.event = true;
	m_fs->set_title(str_format("Floppy image for drive %s", m_floppy.curr_drive?"B":"A"));
	m_fs->set_compat_types(
			get_floppy_types(m_floppy.curr_drive),
			m_floppy.ctrl->get_compatible_file_extensions(),
			m_floppy.ctrl->get_compatible_formats(),
			!m_floppy.ctrl->can_use_any_floppy());
	m_fs->reload();
}

void Interface::on_fdd_eject(Rml::Event &)
{
	if(!m_floppy.ctrl) {
		return;
	}
	if(!m_floppy.ctrl->is_media_present(m_floppy.curr_drive)) {
		return;
	}
	m_machine->cmd_eject_floppy(m_floppy.curr_drive, nullptr);
	// "eject" audio sample plays now
	if(m_audio_enabled) {
		m_audio.use_floppy(m_floppy.curr_drive_type, InterfaceFX::FLOPPY_EJECT);
	}
}

void Interface::update_floppy_string(unsigned _drive)
{
	const char *section = _drive ? DISK_B_SECTION : DISK_A_SECTION;

	if(g_program.config().get_bool(section, DISK_INSERTED)) {
		set_floppy_string(g_program.config().get_file(section, DISK_PATH, FILE_TYPE_USER));
	} else {
		switch(m_floppy.loader[_drive]) {
			case FloppyLoader::State::LOADING:
				set_floppy_string("Loading...");
				break;
			case FloppyLoader::State::IDLE:
			default:
				set_floppy_string("");
				break;
		}
	}
}

std::vector<unsigned> Interface::get_floppy_types(unsigned _floppy_drive)
{
	std::vector<unsigned> types;
	FloppyDrive::Type drive_type = m_floppy.ctrl->drive_type(_floppy_drive);
	unsigned dens_drive = (drive_type & FloppyDisk::DENS_MASK) >> FloppyDisk::DENS_SHIFT;
	unsigned dens_mask = FloppyDisk::DENS_MASK >> FloppyDisk::DENS_SHIFT;
	unsigned dens_cur = 1;
	while(dens_cur & dens_mask) {
		if(dens_drive & dens_cur) {
			types.push_back((drive_type & FloppyDisk::SIZE_MASK) | (dens_cur << FloppyDisk::DENS_SHIFT));
		}
		dens_cur <<= 1;
	}
	return types;
}

void Interface::on_fdd_mount(Rml::Event &)
{
	if(!m_floppy.ctrl) {
		show_message("floppy drives not present");
		return;
	}

	std::string floppy_dir;

	if(m_floppy.curr_drive == 0) {
		if(g_program.config().get_bool(DISK_A_SECTION, DISK_INSERTED)) {
			floppy_dir = g_program.config().find_media(DISK_A_SECTION, DISK_PATH);
		}
	} else {
		if(g_program.config().get_bool(DISK_B_SECTION, DISK_INSERTED)) {
			floppy_dir = g_program.config().find_media(DISK_B_SECTION, DISK_PATH);
		}
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
		if(m_fs->is_current_dir_valid()) {
			floppy_dir = m_fs->get_current_dir();
		} else {
			floppy_dir = m_fs->get_home();
		}
	}
	
	if(g_program.config().get_string(DIALOGS_SECTION, DIALOGS_FILE_TYPE, "custom") == "native") {
		floppy_dir += "/";
		auto filter_patterns = m_floppy.ctrl->get_compatible_file_extensions();
		std::vector<std::string> patt;
		for(auto e : filter_patterns) {
			patt.push_back(str_format("*%s", e));
		}
		filter_patterns.clear();
		for(auto &p : patt) {
			filter_patterns.push_back(p.c_str());
		}
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
			str_format("Floppy image for drive %s", m_floppy.curr_drive?"B":"A").c_str(),
			floppy_dir.c_str(),
			filter_patterns.size(),
			&filter_patterns[0],
			std::string("Floppy disk (" + str_implode(filter_patterns) + ")").c_str(),
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
		m_fs->set_title(str_format("Floppy image for drive %s", m_floppy.curr_drive?"B":"A"));
		m_fs->show();
	}
}

void Interface::reset_savestate_dialogs(std::string _dir)
{
	Rml::ReleaseTextures();
	Rml::ReleaseCompiledGeometry();
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
				m_gui->restore_state(_info);
				m_state_save->set_selection(_info.name);
				m_state_load->hide();
				m_gui->grab_input(input_was_grabbed);
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

void Interface::on_printer(Rml::Event &)
{
	m_gui->toggle_printer_control();
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
		m_state_save->set_selection(_info.name);
		m_state_load->set_selection(_info.name);
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
	m_screen->set_brightness(_level);
}

void Interface::set_video_contrast(float _level)
{
	m_screen->set_contrast(_level);
}

void Interface::set_video_saturation(float _level)
{
	m_screen->set_saturation(_level);
}

void Interface::set_ambient_light(float _level)
{
	m_screen->set_ambient(_level);
}

void Interface::save_framebuffer(std::string _screenfile, std::string _palfile)
{
	SDL_Surface * surface = SDL_CreateRGBSurface(
		0,
		m_screen->display()->mode().xres,
		m_screen->display()->mode().yres,
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
	m_screen->display()->lock();
		SDL_LockSurface(surface);
		m_screen->display()->copy_screen((uint8_t*)surface->pixels);
		SDL_UnlockSurface(surface);
		if(palette) {
			SDL_LockSurface(palette);
			for(uint i=0; i<256; i++) {
				((uint32_t*)palette->pixels)[i] = m_screen->display()->get_color(i);
			}
			SDL_UnlockSurface(palette);
		}
	m_screen->display()->unlock();

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
	m_screen->display()->lock();
	SDL_Surface * surface = SDL_CreateRGBSurface(
		0,
		m_screen->display()->mode().xres,
		m_screen->display()->mode().yres,
		32,
		PALETTE_RMASK,
		PALETTE_GMASK,
		PALETTE_BMASK,
		PALETTE_AMASK
	);
	if(surface) {
		SDL_LockSurface(surface);
		m_screen->display()->copy_screen((uint8_t*)surface->pixels);
		SDL_UnlockSurface(surface);
	}
	m_screen->display()->unlock();

	if(!surface) {
		PERRF(LOG_GUI, "Error creating buffer surface\n");
		throw std::exception();
	}
	return surface;
}

std::string Interface::get_filesel_info(std::string _filepath)
{
	std::unique_ptr<FloppyFmt> format(FloppyFmt::find(_filepath));

	if(!format) {
		return "Invalid floppy image format";
	}

	std::string info = std::string("File: ") + str_to_html(FileSys::get_basename(_filepath.c_str())) + "<br />";
	info += format->get_preview_string(_filepath);

	return info;
}

std::string Interface::create_new_floppy_image(std::string _dir, std::string _file, FloppyDisk::StdType _type, std::string _format)
{
	PDEBUGF(LOG_V1, LOG_GUI, "New floppy image: %s, %s, %d, %s\n", _dir.c_str(), _file.c_str(), _type, _format.c_str());

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

	std::unique_ptr<FloppyFmt> format(FloppyFmt::find_by_name(_format));
	if(!format) {
		throw std::runtime_error("Invalid image format.");
	}
	ext = str_to_lower(ext);
	if(!format->has_file_extension(ext)) {
		_file += format->default_file_extension();
	}

	if(_file.size() > 255) {
		throw std::runtime_error("File name too long.");
	}

	std::string path = _dir + FS_SEP + _file;

	if(FileSys::file_exists(path.c_str())) {
		throw std::runtime_error(str_format("The file \"%s\" already exists.", _file.c_str()));
	}

	FloppyCtrl::create_new_floppy_image(path, FloppyDrive::FDD_NONE, _type, _format);

	return _file;
}