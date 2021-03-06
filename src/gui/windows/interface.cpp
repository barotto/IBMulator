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
#include <SDL_image.h>
#include <Rocket/Core.h>

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
	
	m_renderer->render_vga(vga.mvmat, vga.size,
		vga.brightness, vga.contrast, vga.saturation,
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
m_size(0),
m_curr_drive(0),
m_floppy_present(false),
m_floppy_changed(false),
m_machine(_machine),
m_mixer(_mixer),
m_floppy(nullptr),
m_hdd(nullptr)
{
	assert(m_wnd);
	
	m_buttons.power = get_element("power");
	m_buttons.fdd_select = get_element("fdd_select");
	m_status.fdd_led = get_element("fdd_led");
	m_status.hdd_led = get_element("hdd_led");
	m_status.fdd_disk = get_element("fdd_disk");
	m_speed = get_element("speed");
	m_speed_value = get_element("speed_value");
	m_message = get_element("message");

	m_leds.power = false;

	m_fs = new FileSelect(_gui);
	m_fs->set_select_callbk(std::bind(&Interface::on_floppy_mount, this, _1, _2));
	m_fs->set_cancel_callbk(nullptr);

	m_audio_enabled = g_program.config().get_bool(SOUNDFX_SECTION, SOUNDFX_ENABLED);
	if(m_audio_enabled) {
		m_audio.init(_mixer);
	}
}

Interface::~Interface()
{
	if(m_fs) {
		m_fs->close();
		delete m_fs;
	}
}

void Interface::config_changed()
{
	m_leds.fdd = false;
	m_status.fdd_led->SetClass("active", false);
	m_status.fdd_disk->SetInnerRML("");
	m_floppy_present = false;
	m_floppy_changed = false;
	m_curr_drive = 0;
	m_buttons.fdd_select->SetProperty("visibility", "hidden");
	m_floppy = m_machine->devices().device<FloppyCtrl>();
	if(m_floppy) {
		m_floppy_present = g_program.config().get_bool(DISK_A_SECTION, DISK_INSERTED);
		if(m_floppy_present) {
			update_floppy_disk(g_program.config().get_file(DISK_A_SECTION, DISK_PATH,
					FILE_TYPE_USER));
		}
		m_floppy_changed = m_floppy->has_disk_changed(0);

		if(m_floppy->drive_type(1) != FDD_NONE) {
			m_buttons.fdd_select->SetProperty("visibility", "visible");
			m_buttons.fdd_select->SetClass("a", true);
			m_buttons.fdd_select->SetClass("b", false);
		}
	}

	m_leds.hdd = false;
	m_status.hdd_led->SetClass("active", false);
	m_hdd = m_machine->devices().device<StorageCtrl>();

	set_audio_volume(g_program.config().get_real(MIXER_SECTION, MIXER_VOLUME));
	set_video_brightness(g_program.config().get_real(DISPLAY_SECTION, DISPLAY_BRIGHTNESS));
	set_video_contrast(g_program.config().get_real(DISPLAY_SECTION, DISPLAY_CONTRAST));
	set_video_saturation(g_program.config().get_real(DISPLAY_SECTION, DISPLAY_SATURATION));
}

void Interface::update_floppy_disk(std::string _filename)
{
	size_t pos = _filename.rfind(FS_SEP);
	if(pos!=std::string::npos) {
		_filename = _filename.substr(pos+1);
	}
	m_status.fdd_disk->SetInnerRML(_filename.c_str());
}

void Interface::on_floppy_mount(std::string _img_path, bool _write_protect)
{
	struct stat sb;
	if((stat(_img_path.c_str(), &sb) != 0) || S_ISDIR(sb.st_mode)) {
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
			if(stat(other.c_str(), &other_s) == 0) {
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
		case 160*1024:
			type = FLOPPY_160K;
			break;
		case 180*1024:
			type = FLOPPY_180K;
			break;
		case 320*1024:
			type = FLOPPY_320K;
			break;
		case 360*1024:
			type = FLOPPY_360K;
			break;
		case 1200*1024:
			type = FLOPPY_1_2;
			break;
		case 720*1024:
			type = FLOPPY_720K;
			break;
		case 1440*1024:
			type = FLOPPY_1_44;
			break;
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
	if(is_visible()) {
		if(m_floppy) {
			bool motor = m_floppy->is_motor_on(m_curr_drive);
			if(motor && m_leds.fdd==false) {
				m_leds.fdd = true;
				m_status.fdd_led->SetClass("active", true);
			} else if(!motor && m_leds.fdd==true) {
				m_leds.fdd = false;
				m_status.fdd_led->SetClass("active", false);
			}
			bool present = m_floppy->is_media_present(m_curr_drive);
			bool changed = m_floppy->has_disk_changed(m_curr_drive);
			if(present && (m_floppy_present==false || m_floppy_changed!=changed)) {
				m_floppy_changed = changed;
				m_floppy_present = true;
				const char *section = m_curr_drive?DISK_B_SECTION:DISK_A_SECTION;
				update_floppy_disk(g_program.config().get_file(section,DISK_PATH,FILE_TYPE_USER));
			} else if(!present && m_floppy_present==true) {
				m_floppy_present = false;
				m_status.fdd_disk->SetInnerRML("");
			}
		}
		if(m_hdd) {
			bool hdd_busy = m_hdd->is_busy();
			if(hdd_busy && m_leds.hdd==false) {
				m_leds.hdd = true;
				m_status.hdd_led->SetClass("active", true);
			} else if(!hdd_busy && m_leds.hdd==true) {
				m_leds.hdd = false;
				m_status.hdd_led->SetClass("active", false);
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
				m_speed_value->SetInnerRML(RC::String(10, "%d%%", vtime_ratio_1000/10));
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
	}
}

void Interface::on_power(RC::Event &)
{
	switch_power();
}

void Interface::show_message(const char* _mex)
{
	Rocket::Core::String str(_mex);
	str.Replace("\n", "<br />");

	m_message->SetInnerRML(str);
	if(str.Empty()) {
		m_message->SetProperty("visibility", "hidden");
	} else {
		m_message->SetProperty("visibility", "visible");
	}
}

void Interface::on_fdd_select(RC::Event &)
{
	m_status.fdd_disk->SetInnerRML("");
	if(m_curr_drive == 0) {
		m_curr_drive = 1;
		m_floppy_changed = m_floppy->has_disk_changed(1);
		m_buttons.fdd_select->SetClass("a", false);
		m_buttons.fdd_select->SetClass("b", true);
		if(g_program.config().get_bool(DISK_B_SECTION,DISK_INSERTED)) {
			update_floppy_disk(g_program.config().get_file(DISK_B_SECTION,DISK_PATH, FILE_TYPE_USER));
		}
	} else {
		m_curr_drive = 0;
		m_floppy_changed = m_floppy->has_disk_changed(0);
		m_buttons.fdd_select->SetClass("a", true);
		m_buttons.fdd_select->SetClass("b", false);
		if(g_program.config().get_bool(DISK_A_SECTION,DISK_INSERTED)) {
			update_floppy_disk(g_program.config().get_file(DISK_A_SECTION,DISK_PATH, FILE_TYPE_USER));
		}
	}
}

void Interface::on_fdd_eject(RC::Event &)
{
	m_machine->cmd_eject_media(m_curr_drive);
	if(m_audio_enabled && m_floppy->is_media_present(m_curr_drive)) {
		m_audio.use_floppy(false);
	}
}

void Interface::on_fdd_mount(RC::Event &)
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
		floppy_dir = g_program.config().get_file(PROGRAM_SECTION, PROGRAM_MEDIA_DIR, FILE_TYPE_USER);
		if(floppy_dir.empty()) {
			floppy_dir = g_program.config().get_cfg_home();
		}
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
			m_fs->set_current_dir(floppy_dir);
		} catch(std::exception &e) {
			return;
		}
		m_fs->show();
	}
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

	int result = IMG_SavePNG(surface, _screenfile.c_str());
	SDL_FreeSurface(surface);
	if(result<0) {
		PERRF(LOG_GUI, "error saving surface to PNG\n");
		if(palette) {
			SDL_FreeSurface(palette);
		}
		throw std::exception();
	}
	if(palette) {
		result = IMG_SavePNG(palette, _palfile.c_str());
		SDL_FreeSurface(palette);
		if(result < 0) {
			PERRF(LOG_GUI, "error saving palette to PNG\n");
			throw std::exception();
		}
	}
}

