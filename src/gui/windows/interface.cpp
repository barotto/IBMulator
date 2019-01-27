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
#include "mixer.h"
#include "program.h"
#include "utils.h"
#include <sys/stat.h>
#include <SDL2/SDL_image.h>
#include <Rocket/Core.h>
#include "hardware/devices/vga.h"
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


Interface::Display::Display()
{}

Interface::Interface(Machine *_machine, GUI * _gui, Mixer *_mixer, const char *_rml)
:
Window(_gui, _rml),
m_quad_data{
	-1.0f, -1.0f, 0.0f,
	 1.0f, -1.0f, 0.0f,
	-1.0f,  1.0f, 0.0f,
	-1.0f,  1.0f, 0.0f,
	 1.0f, -1.0f, 0.0f,
	 1.0f,  1.0f, 0.0f
}
{
	assert(m_wnd);
	m_machine = _machine;
	m_mixer = _mixer;

	m_buttons.power = get_element("power");
	m_buttons.fdd_select = get_element("fdd_select");
	m_warning = get_element("warning");
	m_message = get_element("message");
	m_status.fdd_led = get_element("fdd_led");
	m_status.hdd_led = get_element("hdd_led");
	m_status.fdd_disk = get_element("fdd_disk");

	m_leds.power = false;

	m_fs = new FileSelect(_gui);
	m_fs->set_select_callbk(std::bind(&Interface::on_floppy_mount, this, _1, _2));
	m_fs->set_cancel_callbk(nullptr);

	m_size = 0;

	m_audio.init(_mixer);
}

Interface::~Interface()
{
	if(m_fs) {
		m_fs->close();
		delete m_fs;
	}
}

void Interface::init_gl(uint _sampler, std::string _vshader, std::string _fshader)
{
	std::vector<std::string> vs,fs;
	std::string shadersdir = GUI::get_shaders_dir();

	m_display.mvmat.load_identity();

	if(_sampler == DISPLAY_SAMPLER_NEAREST || _sampler == DISPLAY_SAMPLER_BILINEAR) {
		fs.push_back(shadersdir + "filter_bilinear.fs");
	} else if(_sampler == DISPLAY_SAMPLER_BICUBIC) {
		fs.push_back(shadersdir + "filter_bicubic.fs");
	} else {
		PERRF(LOG_GUI, "Invalid sampler interpolation method\n");
		throw std::exception();
	}

	vs.push_back(_vshader);
	fs.push_back(shadersdir + "color_functions.glsl");
	fs.push_back(_fshader);

	try {
		m_display.prog = GUI::load_GLSL_program(vs,fs);
	} catch(std::exception &e) {
		PERRF(LOG_GUI, "Unable to create the shader program!\n");
		throw std::exception();
	}

	//find the uniforms
	GLCALL( m_display.uniforms.vgamap = glGetUniformLocation(m_display.prog, "iVGAMap") );
	if(m_display.uniforms.vgamap == -1) {
		PWARNF(LOG_GUI, "iVGAMap not found in shader program\n");
	}
	GLCALL( m_display.uniforms.brightness = glGetUniformLocation(m_display.prog, "iBrightness") );
	if(m_display.uniforms.brightness == -1) {
		PWARNF(LOG_GUI, "iBrightness not found in shader program\n");
	}
	GLCALL( m_display.uniforms.contrast = glGetUniformLocation(m_display.prog, "iContrast") );
	if(m_display.uniforms.contrast == -1) {
		PWARNF(LOG_GUI, "iContrast not found in shader program\n");
	}
	GLCALL( m_display.uniforms.saturation = glGetUniformLocation(m_display.prog, "iSaturation") );
	if(m_display.uniforms.saturation == -1) {
		PWARNF(LOG_GUI, "iSaturation not found in shader program\n");
	}
	GLCALL( m_display.uniforms.mvmat = glGetUniformLocation(m_display.prog, "iModelView") );
	if(m_display.uniforms.mvmat == -1) {
		PWARNF(LOG_GUI, "iModelView not found in shader program\n");
	}
	GLCALL( m_display.uniforms.size = glGetUniformLocation(m_display.prog, "iDisplaySize") );
	if(m_display.uniforms.size == -1) {
		PWARNF(LOG_GUI, "iDisplaySize not found in shader program\n");
	}

	m_display.glintf = GL_RGBA;
	m_display.glf = GL_RGBA;
	m_display.gltype = GL_UNSIGNED_INT_8_8_8_8_REV;
	GLCALL( glGenTextures(1, &m_display.tex) );
	GLCALL( glBindTexture(GL_TEXTURE_2D, m_display.tex) );
	GLCALL( glTexImage2D(GL_TEXTURE_2D, 0, m_display.glintf,
			m_display.vga.get_screen_xres(), m_display.vga.get_screen_yres(),
			0, m_display.glf, m_display.gltype, nullptr) );

	m_display.tex_buf.resize(m_display.vga.get_fb_size());

	GLCALL( glGenSamplers(1, &m_display.sampler) );
	GLCALL( glSamplerParameteri(m_display.sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER) );
	GLCALL( glSamplerParameteri(m_display.sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER) );
	if(_sampler == DISPLAY_SAMPLER_NEAREST) {
		GLCALL( glSamplerParameteri(m_display.sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST) );
		GLCALL( glSamplerParameteri(m_display.sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST) );
	} else {
		GLCALL( glSamplerParameteri(m_display.sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR) );
		GLCALL( glSamplerParameteri(m_display.sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR) );
	}

	GLCALL( glBindTexture(GL_TEXTURE_2D, 0) );

	m_display.vga.set_fb_updated();

	GLCALL( glGenBuffers(1, &m_vertex_buffer) );
	GLCALL( glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer) );
	GLCALL( glBufferData(GL_ARRAY_BUFFER, sizeof(m_quad_data), m_quad_data, GL_DYNAMIC_DRAW) );
	GLCALL( glBindBuffer(GL_ARRAY_BUFFER, 0) );
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
		PERRF(LOG_GUI, "unable to read '%s'\n", _img_path.c_str());
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
					PERRF(LOG_GUI, "can't mount '%s' on drive %s because is already mounted on drive %s\n",
							_img_path.c_str(), m_curr_drive?"A":"B", m_curr_drive?"B":"A");
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
			PERRF(LOG_GUI, "unable to determine the type of '%s'\n", _img_path.c_str());
			return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "mounting '%s' on floppy %s %s\n", _img_path.c_str(),
			m_curr_drive?"B":"A", _write_protect?"(write protected)":"");
	m_machine->cmd_insert_media(m_curr_drive, type, _img_path, _write_protect);
	m_fs->hide();
	m_audio.use_floppy(true);
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
	}
}

void Interface::on_power(RC::Event &)
{
	m_machine->cmd_switch_power();
	m_machine->cmd_resume();
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
	if(m_floppy->is_media_present(m_curr_drive)) {
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
	try {
		m_fs->set_current_dir(floppy_dir);
	} catch(std::exception &e) {
		return;
	}
	m_fs->show();
}

void Interface::show_warning(bool _show)
{
	if(_show) {
		m_warning->SetProperty("visibility", "visible");
	} else {
		m_warning->SetProperty("visibility", "hidden");
	}
}

void Interface::render()
{
	render_monitor();
}

void Interface::render_quad()
{
	GLCALL( glEnableVertexAttribArray(0) );
	GLCALL( glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer) );
	GLCALL( glVertexAttribPointer(
            0,        // attribute 0. must match the layout in the shader.
            3,        // size
            GL_FLOAT, // type
            GL_FALSE, // normalized?
            0,        // stride
            (void*)0  // array buffer offset
    ) );
	GLCALL( glDrawArrays(GL_TRIANGLES, 0, 6) ); // 2*3 indices starting at 0 -> 2 triangles
	GLCALL( glDisableVertexAttribArray(0) );
	GLCALL( glBindBuffer(GL_ARRAY_BUFFER, 0) );
}

void Interface::render_monitor()
{
	GLCALL( glActiveTexture(GL_TEXTURE0) );
	GLCALL( glBindTexture(GL_TEXTURE_2D, m_display.tex) );
	if(m_display.vga.fb_updated()) {
		m_display.vga.lock();
		vec2i vga_res = vec2i(m_display.vga.get_screen_xres(),m_display.vga.get_screen_yres());
		//this intermediate buffer is to reduce the blocking effect of glTexSubImage2D:
		//when the program runs with the default shaders, the load on the GPU is very low
		//so the drivers lower the clock of the GPU to the minimum value;
		//the result is the GPU memory controller load goes high and glTexSubImage2D takes
		//a lot of time to complete, bloking the machine emulation thread.
		//PBOs are a possible alternative, but a memcpy is way simpler.
		m_display.tex_buf = m_display.vga.get_fb();
		m_display.vga.clear_fb_updated();
		if(THREADS_WAIT) {
			m_display.vga.notify_all();
		}
		m_display.vga.unlock();

		GLCALL( glPixelStorei(GL_UNPACK_ROW_LENGTH, m_display.vga.get_fb_width()) );
		if(!(m_display.vga_res == vga_res)) {
			GLCALL( glTexImage2D(GL_TEXTURE_2D, 0, m_display.glintf,
					vga_res.x, vga_res.y,
					0, m_display.glf, m_display.gltype,
					&m_display.tex_buf[0]) );
			m_display.vga_res = vga_res;
		} else {
			GLCALL( glTexSubImage2D(GL_TEXTURE_2D, 0,
					0, 0,
					vga_res.x, vga_res.y,
					m_display.glf, m_display.gltype,
					&m_display.tex_buf[0]) );
		}
		GLCALL( glPixelStorei(GL_UNPACK_ROW_LENGTH, 0) );
	}
	GLCALL( glBindSampler(0, m_display.sampler) );
	GLCALL( glUseProgram(m_display.prog) );
	GLCALL( glUniform1i(m_display.uniforms.vgamap, 0) );
	if(m_machine->is_on()) {
		GLCALL( glUniform1f(m_display.uniforms.brightness, m_display.brightness) );
		GLCALL( glUniform1f(m_display.uniforms.contrast, m_display.contrast) );
		GLCALL( glUniform1f(m_display.uniforms.saturation, m_display.saturation) );
	} else {
		GLCALL( glUniform1f(m_display.uniforms.brightness, 1.f) );
		GLCALL( glUniform1f(m_display.uniforms.contrast, 1.f) );
		GLCALL( glUniform1f(m_display.uniforms.saturation, 1.f) );
	}
	GLCALL( glUniform1f(m_display.uniforms.saturation, m_display.saturation) );
	GLCALL( glUniformMatrix4fv(m_display.uniforms.mvmat, 1, GL_FALSE, m_display.mvmat.data()) );
	GLCALL( glUniform2iv(m_display.uniforms.size, 1, m_display.size) );
	render_vga();
}

void Interface::render_vga()
{
	GLCALL( glDisable(GL_BLEND) );
	render_quad();
}

void Interface::set_audio_volume(float _volume)
{
	m_mixer->cmd_set_category_volume(MixerChannelCategory::AUDIO, _volume);
}

void Interface::set_video_brightness(float _level)
{
	m_display.brightness = _level;
}

void Interface::set_video_contrast(float _level)
{
	m_display.contrast = _level;
}

void Interface::set_video_saturation(float _level)
{
	m_display.saturation = _level;
}

void Interface::save_framebuffer(std::string _screenfile, std::string _palfile)
{
	SDL_Surface * surface = SDL_CreateRGBSurface(
		0,
		m_display.vga.get_screen_xres(),
		m_display.vga.get_screen_yres(),
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
			16,	16,    //w x h
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
	m_display.vga.lock();
		SDL_LockSurface(surface);
		m_display.vga.copy_screen((uint8_t*)surface->pixels);
		SDL_UnlockSurface(surface);
		if(palette) {
			SDL_LockSurface(palette);
			for(uint i=0; i<256; i++) {
				((uint32_t*)palette->pixels)[i] = m_display.vga.get_color(i);
			}
			SDL_UnlockSurface(palette);
		}
	m_display.vga.unlock();

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

void Interface::print_VGA_text(std::vector<uint16_t> &_text)
{
	TextModeInfo tminfo;
	tminfo.start_address = 0;
	tminfo.cs_start = 1;
	tminfo.cs_end = 0;
	tminfo.line_offset = 80*2;
	tminfo.line_compare = 1023;
	tminfo.h_panning = 0;
	tminfo.v_panning = 0;
	tminfo.line_graphics = false;
	tminfo.split_hpanning = false;
	tminfo.double_scanning = false;
	tminfo.blink_flags = 0;
	for(int i=0; i<16; i++) {
		tminfo.actl_palette[i] = i;
	}
	std::vector<uint16_t> oldtxt(80*25,0);
	m_display.vga.text_update(
			(uint8_t*)(oldtxt.data()),
			(uint8_t*)(_text.data()),
			0, 0, &tminfo);
}
