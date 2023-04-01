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
#include "filesys.h"
#include "hardware/memory.h"
#include "hardware/cpu.h"
#include "hardware/devices/sblaster.h"
#include "hardware/devices/ps1audio.h"
#include "hardware/devices/adlib.h"
#include "hardware/devices/mpu401.h"
#include "hardware/devices/storagectrl.h"
#include "gui/gui_opengl.h"
#include "gui/gui_sdl2d.h"
#include "program.h"
#include "machine.h"
#include "mixer.h"
#include <cstdio>
#include <libgen.h>
#include <state_record.h>
#include <thread>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <optional>
#ifdef _WIN32
#include "wincompat.h"
#endif

Program g_program;

std::mutex Program::ms_lock;
std::condition_variable Program::ms_cv;

Program::Program()
:
m_quit(false),
m_machine(nullptr),
m_mixer(nullptr),
m_gui(nullptr),
m_restore_fn(nullptr)
{

}

Program::~Program()
{
	SDL_Quit();
}

void Program::save_state(
	StateRecord::Info _info,
	std::function<void(StateRecord::Info)> _on_success,
	std::function<void(std::string)> _on_fail)
{
	if(!m_machine->is_on()) {
		PINFOF(LOG_V0, LOG_PROGRAM, "The machine must be on\n");
		return;
	}
	
	std::string capture_path = m_config[0].get_file(CAPTURE_SECTION, CAPTURE_DIR, FILE_TYPE_USER);
	if(capture_path.empty()) {
		PERRF(LOG_PROGRAM, "The capture directory is not set\n");
		if(_on_fail) {
			_on_fail("The capture directory is not set");
		}
		return;
	}

	if(_info.name.empty()) {
		try {
			_info.name = FileSys::get_next_dirname(capture_path, STATE_RECORD_BASE);
		} catch(std::runtime_error &) {
			PERRF(LOG_PROGRAM, "Too many savestates!\n");
			if(_on_fail) {
				_on_fail("Too many savestates!");
			}
			return;
		}
	}

	std::optional<StateRecord> sstate;
	try {
		sstate.emplace(capture_path, _info.name, false);
	} catch(std::runtime_error &e) {
		PERRF(LOG_PROGRAM, "%s\n", e.what());
		if(_on_fail) {
			_on_fail(e.what());
		}
		return;
	}

	PINFOF(LOG_V0, LOG_PROGRAM, "Saving current state in '%s'...\n", sstate->path());

	sstate->info().user_desc = _info.user_desc;
	sstate->config().copy(m_config[1]);

	bool paused = m_machine->is_paused();
	{
		std::unique_lock<std::mutex> lock(ms_lock);

		if(!paused) {
			m_machine->cmd_pause(false);
			m_mixer->cmd_pause_and_signal(ms_lock, ms_cv);
			ms_cv.wait(lock);
		}

		m_machine->cmd_save_state(sstate->state(), ms_lock, ms_cv);
		ms_cv.wait(lock);
		if(!sstate->state().m_last_save) {
			// keep the machine paused
			if(_on_fail) {
				_on_fail("Error saving the state. See logfile.");
			}
			return;
		}

		m_mixer->cmd_save_state(sstate->state(), ms_lock, ms_cv);
		ms_cv.wait(lock);
	}

	try {
		sstate->set_framebuffer(m_gui->copy_framebuffer());
	} catch(...) {
		if(_on_fail) {
			_on_fail("Error copying the framebuffer");
		}
		return;
	}

	// prepare the configuration description
	const ModelConfig & mcfg = m_machine->configured_model();
	sstate->info().config_desc = "Machine: " + mcfg.machine_name + "\n";
	sstate->info().config_desc += "ROM: " + FileSys::get_basename(m_machine->sys_rom().romset().c_str()) + "\n";
	sstate->info().config_desc += "CPU: " + str_format("%s @ %u MHz", mcfg.cpu_model.c_str(), mcfg.cpu_freq) + "\n";
	sstate->info().config_desc += "RAM: " + str_format("%u KiB + %u KiB", mcfg.board_ram, mcfg.exp_ram) + "\n";

	// TODO classify expansion cards so that we can ask for all audio cards etc...
	sstate->info().config_desc  += "Audio: ";
	std::vector<std::string> audiocards;
	if(m_machine->devices().device<PS1Audio>()) {
		audiocards.emplace_back("PS/1");
	}
	SBlaster *sb = m_machine->devices().device<SBlaster>();
	if(sb) {
		audiocards.emplace_back(sb->short_name());
	}
	if(m_machine->devices().device<AdLib>()) {
		audiocards.emplace_back("AdLib");
	}
	if(m_machine->devices().device<MPU401>()) {
		audiocards.emplace_back("MPU-401");
	}
	if(audiocards.empty()) {
		sstate->info().config_desc  += "none";
	} else {
		sstate->info().config_desc  += str_implode(audiocards, ", ");
	}
	sstate->info().config_desc  += "\n";

	FloppyCtrl *fdc = m_machine->devices().device<FloppyCtrl>();
	if(fdc) {
		for(int i=0; i<2; i++) {
			if(fdc->is_media_present(i)) {
				sstate->info().config_desc += str_format("Drive %s: %s", 
						i?"B":"A", FileSys::get_basename(fdc->get_media_path(i).c_str()).c_str()) 
						+ "\n";
			}
		}
	}

	// TODO consider more than 1 controller
	StorageCtrl *hddctrl = m_machine->devices().device<StorageCtrl>();
	if(hddctrl) {
		for(int i=0; i<hddctrl->installed_devices(); i++) {
			const StorageDev *dev = hddctrl->get_device(i);
			if(dev) {
				sstate->info().config_desc += str_format("%s: %s", 
						dev->name(), FileSys::get_basename(dev->path()).c_str()) 
						+ "\n";
			}
		}
	}

	try {
		sstate->save();
	} catch(std::runtime_error &e) {
		PERRF(LOG_PROGRAM, "%s\n", e.what());
		if(_on_fail) {
			_on_fail(e.what());
		}
		return;
	}

	if(!paused) {
		m_machine->cmd_resume(false);
	}

	PINFOF(LOG_V0, LOG_PROGRAM, "State saved\n");
	if(_on_success != nullptr) {
		_on_success(_info);
	}
}

void Program::restore_state(
	StateRecord::Info _info,
	std::function<void()> _on_success,
	std::function<void(std::string)> _on_fail)
{
	m_gui->show_message("Restoring state...");

	/* The actual restore needs to be executed outside RmlUi's event manager,
	 * otherwise a deadlock on the RmlUi mutex caused by the SysLog will occur.
	 */
	m_restore_fn = [=](){
		std::string capture_path = m_config[0].get_file(CAPTURE_SECTION, CAPTURE_DIR, FILE_TYPE_USER);
		if(capture_path.empty()) {
			PERRF(LOG_PROGRAM, "The capture directory is not set\n");
			if(_on_fail) {
				_on_fail("The capture directory is not set");
			}
			return;
		}
		if(_info.name.empty()) {
			assert(false);
			return;
		}

		std::string statepath = capture_path + FS_SEP + _info.name;
		if(!FileSys::is_directory(statepath.c_str())) {
			// the only case this is true should be the quicksave directory
			// any other case is a bug
			PERRF(LOG_PROGRAM, "Save state not present\n");
			if(_on_fail) {
				_on_fail("Save state not present");
			}
			return;
		}

		std::optional<StateRecord> sstate;
		try {
			sstate.emplace(capture_path, _info.name);
			if(sstate->info().version != STATE_RECORD_VERSION) {
				throw std::runtime_error("Invalid savestate version");
			}
			sstate->load();
		} catch(std::runtime_error &e) {
			PERRF(LOG_PROGRAM, "%s\n", e.what());
			if(_on_fail) {
				_on_fail(e.what());
			}
			return;
		}

		PINFOF(LOG_V0, LOG_PROGRAM, "Loading state from '%s'...\n", sstate->path());

		//from this point, any error in the restore procedure will render the
		//machine inconsistent and it should be terminated
		//TODO the config object needs a mutex!
		//TODO create a revert mechanism?
		m_config[1].copy(m_config[0]);
		m_config[1].merge(sstate->config(), MACHINE_CONFIG);

		std::unique_lock<std::mutex> restore_lock(ms_lock);

		m_machine->cmd_pause();

		m_mixer->cmd_pause_and_signal(ms_lock, ms_cv);
		ms_cv.wait(restore_lock);

		m_machine->sig_config_changed(ms_lock, ms_cv);
		ms_cv.wait(restore_lock);

		m_machine->cmd_restore_state(sstate->state(), ms_lock, ms_cv);
		ms_cv.wait(restore_lock);

		// we need to pause the syslog because it'll use the GUI otherwise
		g_syslog.cmd_pause_and_signal(ms_lock, ms_cv);
		ms_cv.wait(restore_lock);
		m_gui->config_changed(false);
		m_gui->sig_state_restored();
		g_syslog.cmd_resume();

		if(sstate->state().m_last_restore) {
			m_mixer->sig_config_changed(ms_lock, ms_cv);
			ms_cv.wait(restore_lock);

			m_mixer->cmd_restore_state(sstate->state(), ms_lock, ms_cv);
			ms_cv.wait(restore_lock);

			// mixer resume cmd is issued by the machine
			m_machine->cmd_resume(false);

			PINFOF(LOG_V0, LOG_PROGRAM, "State restored\n");
			if(_on_success != nullptr) {
				_on_success();
			}
		} else {
			PERRF(LOG_PROGRAM, "The restored state is not valid, please restart the program\n");
			if(_on_fail != nullptr) {
				_on_fail("The restored state is not valid, please restart the program");
			}
		}
	};
}

void Program::delete_state(StateRecord::Info _info)
{
	std::string capture_path = m_config[0].get_file(CAPTURE_SECTION, CAPTURE_DIR, FILE_TYPE_USER);
	if(capture_path.empty()) {
		throw std::runtime_error("The capture directory is not set");
	}
	if(_info.name.empty()) {
		assert(false);
		return;
	}
	// check the path before constructing the state record, otherwise it'll
	// create a new directory if it doesn't exist
	std::string statepath = capture_path + FS_SEP + _info.name;
	if(!FileSys::is_directory(statepath.c_str())) {
		throw std::runtime_error("Invalid state record path");
	}

	StateRecord sstate(capture_path, _info.name);
	sstate.remove();
}

void Program::init_SDL()
{
	SDL_version compiled;
	SDL_version linked;

	SDL_VERSION(&compiled);
	SDL_GetVersion(&linked);
	PINFOF(LOG_V1, LOG_PROGRAM, "Compiled against SDL version %d.%d.%d\n",
	       compiled.major, compiled.minor, compiled.patch);
	PINFOF(LOG_V1, LOG_PROGRAM, "Linking against SDL version %d.%d.%d\n",
	       linked.major, linked.minor, linked.patch);

	if(SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
		PERR("unable to initialize SDL\n");
		throw std::exception();
	}
}

bool Program::initialize(int argc, char** argv)
{
	std::string home, cfgfile, datapath;
	char *str;
	parse_arguments(argc, argv);

#ifndef _WIN32
	str = getenv("HOME");
	if(str) {
		home = str;
	}
#else
	str = utf8::getenv("USERPROFILE");
	if(str) {
		home = str;
	} else {
		str = utf8::getenv("HOMEDRIVE");
		char *hpath = utf8::getenv("HOMEPATH");
		if(str && hpath) {
			home = str;
			home += hpath;
		}
	}
#endif
	if(home.empty()) {
		PERRF(LOG_PROGRAM, "Unable to determine the home directory!\n");
		throw std::exception();
	}
	m_config[0].set_user_home(home);

	if(m_user_dir.empty()) {
#ifndef _WIN32
		str = getenv("XDG_CONFIG_HOME");
		if(str == nullptr) {
			m_user_dir = home + FS_SEP ".config";
		} else {
			m_user_dir = str;
		}
#else
		//WINDOWS uses LOCALAPPDATA\{DeveloperName\AppName}
		str = utf8::getenv("LOCALAPPDATA");
		if(str == nullptr) {
			PERRF(LOG_PROGRAM, "Unable to determine the LOCALAPPDATA directory!\n");
			throw std::exception();
		}
		m_user_dir = str;
#endif
		if(!FileSys::is_directory(m_user_dir.c_str())
		|| FileSys::access(m_user_dir.c_str(), R_OK | W_OK | X_OK) != 0) {
			PERRF(LOG_PROGRAM, "Unable to access the user directory: %s\n", m_user_dir.c_str());
			throw std::exception();
		}
		m_user_dir += FS_SEP PACKAGE;
	}
	FileSys::create_dir(m_user_dir.c_str());
	PINFO(LOG_V1, "User directory: %s\n", FileSys::to_utf8(m_user_dir).c_str());
	m_config[0].set_cfg_home(m_user_dir);

	cfgfile = m_user_dir + FS_SEP PACKAGE ".ini";
	if(m_cfg_file.empty()) {
		m_cfg_file = cfgfile;
	} else if(!FileSys::is_absolute(m_cfg_file.c_str(), m_cfg_file.length())) {
		m_cfg_file = m_user_dir + FS_SEP + m_cfg_file;
	}

	PINFO(LOG_V0,"INI file: %s\n", m_cfg_file.c_str());

	if(!FileSys::file_exists(m_cfg_file.c_str())) {
		PWARNF(LOG_V0, LOG_PROGRAM, "The config file '%s' doesn't exists, creating...\n", m_cfg_file.c_str());
		std::string inidir, ininame;
		FileSys::get_path_parts(m_cfg_file.c_str(), inidir, ininame);
		try {
			m_config[0].create_file(m_cfg_file, false);
			std::string message = "The configuration file " + ininame + " has been created in " +
					inidir + "\n";
			message += "Open it and configure the program as you like.";
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Configuration file",
					message.c_str(),
					nullptr);
			return false;
		} catch(std::exception &e) {
			PERRF(LOG_PROGRAM, "Cannot create the INI file.\n", m_cfg_file.c_str());
			std::string message = "A problem occurred while trying to create " + ininame + " in " +
					inidir + "\n";
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Configuration file",
					message.c_str(),
					nullptr);
			return false;
		}
	}

	if(CONFIG_PARSE) {
		try {
			m_config[0].parse(m_cfg_file);
		} catch(std::exception &e) {
			int error = m_config[0].get_error();
			if(error < 0) {
				PERRF(LOG_PROGRAM, "Cannot open '%s'\n", m_cfg_file.c_str());
				throw;
			}
			PERRF(LOG_PROGRAM, "Parsing error on line %d in '%s'\n", error, m_cfg_file.c_str());
			throw;
		}
	}

	m_datapath = get_assets_dir(argc,argv);
	m_config[0].set_assets_home(m_datapath);
	PINFO(LOG_V1,"Assets directory: %s\n", m_datapath.c_str());

	// User's shaders dir
	auto user_shaders = m_config[0].get_users_shaders_path();
	FileSys::create_dir(user_shaders.c_str());
	PINFO(LOG_V1, "Shaders directory: %s\n", user_shaders.c_str());

	//Capture dir, create if not exists
	std::string capture_dir_path = m_config[0].get_file(CAPTURE_SECTION, CAPTURE_DIR, FILE_TYPE_USER);
	if(capture_dir_path.empty()) {
		std::string capture_dir = "capture";
		m_config[0].set_string(CAPTURE_SECTION, CAPTURE_DIR, capture_dir);
		capture_dir_path = m_config[0].get_file(CAPTURE_SECTION, CAPTURE_DIR, FILE_TYPE_USER);
	}
	FileSys::create_dir(capture_dir_path.c_str());
	PINFO(LOG_V1,"Capture directory: %s\n", capture_dir_path.c_str());

	std::string dumplog = m_config[0].get_file(PROGRAM_SECTION, PROGRAM_LOG_FILE, FILE_TYPE_USER);
	g_syslog.add_device(LOG_ALL_PRIORITIES, LOG_ALL_FACILITIES, new LogStream(dumplog.c_str()));

	m_config[1].copy(m_config[0]);

	init_SDL();

	m_quit = false;

	if(m_config[0].get_bool(LOG_SECTION, LOG_OVERRIDE_VERBOSITY)) {
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_PROGRAM_VERBOSITY),  LOG_PROGRAM);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_FS_VERBOSITY),       LOG_FS);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_GFX_VERBOSITY),      LOG_GFX);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_INPUT_VERBOSITY),    LOG_INPUT);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_GUI_VERBOSITY),      LOG_GUI);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_OGL_VERBOSITY),      LOG_OGL);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_MACHINE_VERBOSITY),  LOG_MACHINE);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_MIXER_VERBOSITY),    LOG_MIXER);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_MEM_VERBOSITY),      LOG_MEM);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_CPU_VERBOSITY),      LOG_CPU);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_MMU_VERBOSITY),      LOG_MMU);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_PIT_VERBOSITY),      LOG_PIT);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_PIC_VERBOSITY),      LOG_PIC);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_DMA_VERBOSITY),      LOG_DMA);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_KEYB_VERBOSITY),     LOG_KEYB);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_VGA_VERBOSITY),      LOG_VGA);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_CMOS_VERBOSITY),     LOG_CMOS);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_FDC_VERBOSITY),      LOG_FDC);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_HDD_VERBOSITY),      LOG_HDD);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_AUDIO_VERBOSITY),    LOG_AUDIO);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_GAMEPORT_VERBOSITY), LOG_GAMEPORT);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_LPT_VERBOSITY),      LOG_LPT);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_PRN_VERBOSITY),      LOG_PRN);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_COM_VERBOSITY),      LOG_COM);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_MIDI_VERBOSITY),     LOG_MIDI);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_NET_VERBOSITY),      LOG_NET);
	}
	
	static std::map<std::string, unsigned> waitmethods = {
		{ "",      PACER_WAIT_AUTO },
		{ "auto",  PACER_WAIT_AUTO },
		{ "sleep", PACER_WAIT_SLEEP },
		{ "loop",  PACER_WAIT_BUSYLOOP }
	};
	PacerWaitMethod waitm = (PacerWaitMethod)m_config[0].get_enum(
			PROGRAM_SECTION, PROGRAM_WAIT_METHOD, waitmethods);
	m_pacer.calibrate(waitm);
	m_bench.init(m_pacer.chrono(), 1000);
	set_heartbeat(DEFAULT_HEARTBEAT);
	m_pacer.start();
	
	m_machine = &g_machine;
	m_machine->calibrate(m_pacer);
	try {
		m_machine->init();
		m_machine->config_changed(true);
	} catch(...) {
		m_machine->shutdown();
		throw;
	}

	m_mixer = &g_mixer;
	m_mixer->calibrate(m_pacer);
	try {
		m_mixer->init(m_machine);
		m_mixer->config_changed();
	} catch(...) {
		// the Machine and Mixer threads are not started yet, but both manage threads that are already working
		m_machine->shutdown();
		m_mixer->shutdown();
		throw;
	}

	static std::map<std::string, unsigned> renderers = {
		{ "", GUI_RENDERER_OPENGL },
		{ "opengl", GUI_RENDERER_OPENGL },
		{ "accelerated", GUI_RENDERER_SDL2D },
		{ "software", GUI_RENDERER_SDL2D },
	};
	unsigned renderer = m_config[0].get_enum(GUI_SECTION, GUI_RENDERER, renderers);
	switch(renderer) {
		case GUI_RENDERER_OPENGL:
			m_gui = std::make_unique<GUI_OpenGL>();
			break;
		case GUI_RENDERER_SDL2D: {
			std::string flavor = m_config[0].get_string(GUI_SECTION, GUI_RENDERER);
			if(flavor == "accelerated") {
				m_gui = std::make_unique<GUI_SDL2D>(SDL_RENDERER_ACCELERATED);
			} else {
				m_gui = std::make_unique<GUI_SDL2D>(SDL_RENDERER_SOFTWARE);
			}
			break;
		}
		default:
			assert(false);
			break;
	}

	try {
		m_gui->init(m_machine, m_mixer);
	} catch(...) {
		m_machine->shutdown();
		m_mixer->shutdown();
		throw;
	}
	m_gui->config_changed(true);

	m_pacer.set_external_sync(m_gui->vsync_enabled());
	
	return true;
}

std::string Program::get_assets_dir(int /*argc*/, char** argv)
{
	/*
	 * DATA dir priorities:
	 * 1. IBMULATOR_DATA_PATH env variable
	 * 2. dirname(argv[0]) + /../share/PACKAGE
	 * 3. XDG_DATA_HOME env + PACKAGE define
	 * 4. $HOME/.local/share + PACKAGE define
	 * 5. DATA_PATH define
	 */
	char rpbuf[PATH_MAX];
	std::vector<std::string> paths;

	//1. DATA_PATH env variable
#ifdef _WIN32
	const char* envstr = utf8::getenv("IBMULATOR_DATA_PATH");
#else
	const char* envstr = getenv("IBMULATOR_DATA_PATH");
#endif
	if(envstr) {
		if(FileSys::realpath(envstr, rpbuf)) {
			paths.emplace_back(FileSys::to_utf8(rpbuf));
		} else {
			PERRF(LOG_PROGRAM, "IBMULATOR_DATA_PATH is set, but '%s' cannot be resolved.\n", envstr);
			throw std::exception();
		}
	}

	//2. dirname(argv[0]) + /../share/PACKAGE
	if(FileSys::realpath(argv[0], rpbuf)) {
		std::string datapath = FileSys::to_utf8(dirname(rpbuf)) + FS_SEP ".." FS_SEP "share" FS_SEP PACKAGE;
		if(FileSys::realpath(datapath.c_str(), rpbuf)) {
			paths.emplace_back(FileSys::to_utf8(rpbuf));
		} else {
			PWARNF(LOG_V0, LOG_PROGRAM, "The 'share" FS_SEP PACKAGE"' directory cannot be found!\n");
		}
	} else {
		PWARNF(LOG_V0, LOG_PROGRAM, "Cannot resolve the executable path: %s\n", argv[0]);
	}

#ifndef _WIN32
	//3. XDG_DATA_HOME env + PACKAGE define
	envstr = getenv("XDG_DATA_HOME");
	if(envstr) {
		paths.emplace_back(std::string(envstr) + FS_SEP PACKAGE);
	}

	//4. $HOME/.local/share + PACKAGE define
	envstr = getenv("HOME");
	if(envstr) {
		paths.emplace_back(std::string(envstr) + FS_SEP ".local" FS_SEP "share" FS_SEP PACKAGE);
	}
#endif

#if !defined(NDEBUG) && defined(DATA_PATH)
	//5. DATA_PATH define
	if(FileSys::realpath(DATA_PATH, rpbuf)) {
		paths.emplace_back(FileSys::to_utf8(rpbuf));
	}
#endif

	for(auto & path : paths) {
		PINFOF(LOG_V2, LOG_PROGRAM, "Searching assets in '%s'...", path.c_str());
		if(FileSys::is_directory(path.c_str())) {
			if(!FileSys::is_file_readable(path.c_str())) {
				PINFOF(LOG_V2, LOG_PROGRAM, " the directory is not readable!\n");
			} else {
				PINFOF(LOG_V2, LOG_PROGRAM, " directory found.\n");
				return path;
			}
		} else {
			PINFOF(LOG_V2, LOG_PROGRAM, " directory not found.\n");
		}
	}

	PERRF(LOG_PROGRAM, "Cannot find the assets directory!\n");
	PERRF(LOG_PROGRAM, "Please verify that the 'share" FS_SEP PACKAGE "' directory exists\n");

	throw std::exception();
}

void Program::parse_arguments(int argc, char** argv)
{
	int c;

	opterr = 0;

	while((c = getopt(argc, argv, "v:c:u:")) != -1) {
		switch(c) {
			case 'c': {
				m_cfg_file = "";
				PINFOF(LOG_V0, LOG_PROGRAM, "INI file specified from the command line: '%s'\n", optarg);
				std::string dir, base, ext;
				FileSys::get_path_parts(optarg, dir, base, ext);
				if(str_to_lower(ext) != ".ini") {
					PERRF(LOG_PROGRAM, "The configuration file must be an INI file, '%s' is not a valid extension.\n",
							str_to_lower(ext).c_str());
					throw std::exception();
				}
				std::string resolved_dir;
				if(!dir.empty()) {
					try {
						resolved_dir = FileSys::realpath(dir.c_str());
					} catch(std::exception &) {
						PERRF(LOG_PROGRAM, "The INI file's directory '%s' doesn't exist.\n", dir.c_str());
						throw;
					}
					m_cfg_file = resolved_dir + FS_SEP;
				}
				m_cfg_file += base + ext;
				break;
			}
			case 'u':
				if(!FileSys::is_directory(optarg) || FileSys::access(optarg, R_OK | W_OK | X_OK) == -1) {
					PERRF(LOG_PROGRAM, "Can't access the specified user directory\n");
				} else {
					m_user_dir = optarg;
				}
				break;
			case 'v': {
				int level = atoi(optarg);
				level = std::min(level,LOG_VERBOSITY_MAX-1);
				level = std::max(level,0);
				g_syslog.set_verbosity(level);
				break;
			}
			case '?':
				if(optopt == 'c')
					PERRF(LOG_PROGRAM, "Option -%c requires an argument\n", optopt);
				else if(isprint(optopt))
					PERRF(LOG_PROGRAM, "Unknown option `-%c'\n", optopt);
				else
					PERRF(LOG_PROGRAM, "Unknown option character `\\x%x'.\n", optopt);
				return;
			default:
				return;
		}
	}
	for(int index = optind; index < argc; index++) {
		PINFOF(LOG_V0,LOG_PROGRAM,"Non-option argument %s\n", argv[index]);
	}
}

void Program::process_evts()
{
	SDL_Event event;
	while (SDL_PollEvent(&event)) {

		m_gui->dispatch_event(event);

		switch(event.type)
		{
		case SDL_QUIT:
			stop();
			break;
		default:
			break;
		}
	}
}

void Program::main_loop()
{
	m_bench.start();

	while(!m_quit) {
		m_bench.frame_start();

		process_evts();
		m_gui->update(m_pacer.chrono().get_nsec());
		// in the following function, this thread will wait for the Machine 
		// which will notify on VGA's vertical retrace.
		// see InterfaceScreen::sync_with_device()
		m_gui->render();

		if(m_restore_fn != nullptr) {
			m_restore_fn();
			m_restore_fn = nullptr;
		}
		
		m_bench.load_end();
		
		m_pacer.wait(m_bench.load_time, m_bench.frame_time);
		
		m_bench.frame_end();
	}
}

void Program::start()
{
	PDEBUGF(LOG_V0, LOG_PROGRAM, "Program thread started\n");
	std::thread machine(&Machine::start,m_machine);
	std::thread mixer(&Mixer::start,m_mixer);

	main_loop();

	std::unique_lock<std::mutex> lock(ms_lock);
	
	m_machine->cmd_power_off();

	// Capture thread needs Mixer and Machine to be alive when stopping
	m_gui->cmd_stop_capture_and_signal(ms_lock, ms_cv);
	ms_cv.wait(lock);
	
	// Mixer needs Machine to be alive when stopping capture
	m_mixer->cmd_stop_capture();
	// Wait for the Mixer to stop accesing its channels
	m_mixer->cmd_pause_and_signal(ms_lock, ms_cv);
	ms_cv.wait(lock);
	
	// Now it's safe to destroy the Machine and all its devices
	m_machine->cmd_quit();
	machine.join();
	PDEBUGF(LOG_V0, LOG_PROGRAM, "Machine thread stopped\n");

	m_mixer->cmd_quit();
	mixer.join();
	PDEBUGF(LOG_V0, LOG_PROGRAM, "Mixer thread stopped\n");

	m_gui->shutdown();
}

void Program::stop()
{
	static bool quitting = false;
	if(!quitting) {
		quitting = true;
		m_machine->cmd_pause(false);
		m_machine->cmd_commit_media([=](){
			m_quit = true;
		});
	}
}

void Program::set_heartbeat(int64_t _ns)
{
	m_heartbeat = _ns;
	m_pacer.set_heartbeat(_ns);
	m_bench.set_heartbeat(_ns);
}