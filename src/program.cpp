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
#include "filesys.h"
#include "hardware/memory.h"
#include "hardware/cpu.h"
#include "gui/gui_opengl.h"
#include "gui/gui_sdl2d.h"
#include "program.h"
#include "machine.h"
#include "mixer.h"
#include "statebuf.h"
#include <cstdio>
#include <libgen.h>
#include <thread>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
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
	std::string _name,
	std::function<void()> _on_success,
	std::function<void(std::string)> _on_fail)
{
	if(!m_machine->is_on()) {
		PINFOF(LOG_V0, LOG_PROGRAM, "The machine needs to be on\n");
		return;
	}

	if(_name.empty()) {
		_name = "state";
	}
	std::string path = m_config[0].get_file(CAPTURE_SECTION, CAPTURE_DIR, FILE_TYPE_USER)
			+ FS_SEP + _name;

	PINFOF(LOG_V0, LOG_PROGRAM, "Saving current state in '%s'...\n", path.c_str());

	std::string ini = path + ".ini";
	try {
		m_config[1].create_file(ini);
	} catch(std::exception &e) {
		PERRF(LOG_PROGRAM, "Cannot create config file '%s'\n", ini.c_str());
		if(_on_fail != nullptr) {
			_on_fail("Cannot create config file");
		}
		return;
	}

	StateBuf state(path);

	std::unique_lock<std::mutex> lock(ms_lock);
	
	bool paused = m_machine->is_paused();
	if(!paused) {
		m_machine->cmd_pause();
		m_mixer->cmd_pause_and_signal(ms_lock, ms_cv);
		ms_cv.wait(lock);
	}
	
	m_machine->cmd_save_state(state, ms_lock, ms_cv);
	ms_cv.wait(lock);
	
	m_mixer->cmd_save_state(state, ms_lock, ms_cv);
	ms_cv.wait(lock);
	
	if(!paused) {
		m_machine->cmd_resume();
	}

	state.save(path + ".bin");

	m_gui->save_framebuffer(path + ".png", "");

	PINFOF(LOG_V0, LOG_PROGRAM, "Current state saved\n");
	if(_on_success != nullptr) {
		_on_success();
	}
}

void Program::restore_state(
	std::string _name,
	std::function<void()> _on_success,
	std::function<void(std::string)> _on_fail)
{
	m_gui->show_message("Restoring state...");

	/* The actual restore needs to be executed outside libRocket's event manager,
	 * otherwise a deadlock on the libRocket mutex caused by the SysLog will occur.
	 */
	m_restore_fn = [=](){
		std::string path = m_config[0].get_file(CAPTURE_SECTION, CAPTURE_DIR, FILE_TYPE_USER)
				+ FS_SEP + (_name.empty()?"state":_name);
		std::string ini = path + ".ini";
		std::string bin = path + ".bin";

		if(!FileSys::file_exists(ini.c_str())) {
			PERRF(LOG_PROGRAM, "The state ini file '%s' is missing!\n", ini.c_str());
			if(_on_fail != nullptr) {
				_on_fail("The state ini file is missing!");
			}
			return;
		}

		if(!FileSys::file_exists(bin.c_str())) {
			PERRF(LOG_PROGRAM, "The state bin '%s' file is missing!\n", ini.c_str());
			if(_on_fail) {
				_on_fail("The state bin file is missing!");
			}
			return;
		}

		PINFOF(LOG_V0, LOG_PROGRAM, "Loading state from '%s'...\n", path.c_str());

		AppConfig conf;
		try {
			conf.parse(ini);
		} catch(std::exception &e) {
			PERRF(LOG_PROGRAM, "Cannot parse '%s'\n", ini.c_str());
			if(_on_fail != nullptr) {
				_on_fail("Error while parsing the state ini file");
			}
			return;
		}

		StateBuf state(path);

		state.load(bin);

		//from this point, any error in the restore procedure will render the
		//machine inconsistent and it should be terminated
		//TODO the config object needs a mutex!
		//TODO create a revert mechanism?
		m_config[1].copy(m_config[0]);
		m_config[1].merge(conf, MACHINE_CONFIG);

		std::unique_lock<std::mutex> restore_lock(ms_lock);

		m_machine->cmd_pause();
		
		m_mixer->cmd_pause_and_signal(ms_lock, ms_cv);
		ms_cv.wait(restore_lock);
		
		m_machine->sig_config_changed(ms_lock, ms_cv);
		ms_cv.wait(restore_lock);

		m_machine->cmd_restore_state(state, ms_lock, ms_cv);
		ms_cv.wait(restore_lock);
		
		m_mixer->sig_config_changed(ms_lock, ms_cv);
		ms_cv.wait(restore_lock);

		m_mixer->cmd_restore_state(state, ms_lock, ms_cv);
		ms_cv.wait(restore_lock);
		
		// we need to pause the syslog because it'll use the GUI otherwise
		g_syslog.cmd_pause_and_signal(ms_lock, ms_cv);
		ms_cv.wait(restore_lock);
		m_gui->config_changed();
		m_gui->sig_state_restored();
		g_syslog.cmd_resume();

		// mixer resume cmd is issued by the machine
		m_machine->cmd_resume();
		
		if(!state.m_last_restore) {
			PERRF(LOG_PROGRAM, "The restored state is not valid\n");
			if(_on_fail != nullptr) {
				_on_fail("The restored state is not valid");
			}
		} else {
			PINFOF(LOG_V0, LOG_PROGRAM, "State restored\n");
			if(_on_success != nullptr) {
				_on_success();
			}
		}
	};
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
	str = getenv("USERPROFILE");
	if(str) {
		home = str;
	} else {
		str = getenv("HOMEDRIVE");
		char *hpath = getenv("HOMEPATH");
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
		str = getenv("LOCALAPPDATA");
		if(str == nullptr) {
			PERRF(LOG_PROGRAM, "Unable to determine the LOCALAPPDATA directory!\n");
			throw std::exception();
		}
		m_user_dir = str;
#endif
		if(!FileSys::is_directory(m_user_dir.c_str())
		|| access(m_user_dir.c_str(), R_OK | W_OK | X_OK) != 0) {
			PERRF(LOG_PROGRAM, "Unable to access the user directory: %s\n", m_user_dir.c_str());
			throw std::exception();
		}
		m_user_dir += FS_SEP PACKAGE;
	}
	FileSys::create_dir(m_user_dir.c_str());
	PINFO(LOG_V1,"user directory: %s\n", m_user_dir.c_str());
	m_config[0].set_cfg_home(m_user_dir);

	cfgfile = m_user_dir + FS_SEP PACKAGE ".ini";
	if(m_cfg_file.empty()) {
		m_cfg_file = cfgfile;
	}
	PINFO(LOG_V1,"ini file: %s\n", m_cfg_file.c_str());

	if(!FileSys::file_exists(m_cfg_file.c_str())) {
		PWARNF(LOG_V0, LOG_PROGRAM, "The config file '%s' doesn't exists, creating...\n", m_cfg_file.c_str());
		try {
			m_config[0].create_file(m_cfg_file, true);
			std::string message = "The configuration file " PACKAGE ".ini has been created in " +
					m_user_dir + "\n";
			message += "Open it and configure the program as you like.";
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Configuration file",
					message.c_str(),
					nullptr);
			return false;
		} catch(std::exception &e) {
			PWARNF(LOG_V0, LOG_PROGRAM, "Unable to create config file, using default\n", m_cfg_file.c_str());
			std::string message = "A problem occurred trying to create " PACKAGE ".ini in " +
					m_user_dir + "\n";
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Configuration file",
					message.c_str(),
					nullptr);
			m_cfg_file = cfgfile;
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
	PINFO(LOG_V1,"assets directory: %s\n", m_datapath.c_str());

	//Capture dir, create if not exists
	std::string capture_dir_path = m_config[0].get_file(CAPTURE_SECTION, CAPTURE_DIR, FILE_TYPE_USER);
	if(capture_dir_path.empty()) {
		std::string capture_dir = "capture";
		m_config[0].set_string(CAPTURE_SECTION, CAPTURE_DIR, capture_dir);
		capture_dir_path = m_config[0].get_file(CAPTURE_SECTION, CAPTURE_DIR, FILE_TYPE_USER);
	}
	FileSys::create_dir(capture_dir_path.c_str());
	PINFO(LOG_V1,"capture directory: %s\n", capture_dir_path.c_str());

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
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_COM_VERBOSITY),      LOG_COM);
		g_syslog.set_verbosity(m_config[0].get_int(LOG_SECTION, LOG_MIDI_VERBOSITY),     LOG_MIDI);
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
	m_machine->init();
	m_machine->config_changed();

	m_mixer = &g_mixer;
	m_mixer->calibrate(m_pacer);
	m_mixer->init(m_machine);
	m_mixer->config_changed();
	
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
	
	m_gui->init(m_machine, m_mixer);
	m_gui->config_changed();
	
	m_pacer.set_external_sync(m_gui->vsync_enabled());
	
	return true;
}

std::string Program::get_assets_dir(int /*argc*/, char** argv)
{
	/*
	 * DATA dir priorities:
	 * 1. DATA_HOME env variable
	 * 2. dirname(argv[0]) + /../share/PACKAGE
	 * 3. XGD_DATA_HOME env + PACKAGE define
	 * 4. DATA_PATH define
	 */
	char rpbuf[PATH_MAX];
	std::string datapath;

	//1. DATA_HOME env variable
	const char* edatapath = getenv("DATA_HOME");
	if(edatapath != nullptr) {
		return std::string(edatapath);
	}

	//2. dirname(argv[0]) + /../share/PACKAGE
	char *buf = strdup(argv[0]); //dirname modifies the string!
	datapath = dirname(buf);
	free(buf);
	datapath += std::string(FS_SEP) + ".." FS_SEP "share" FS_SEP PACKAGE;
	if(realpath(datapath.c_str(), rpbuf) != nullptr && FileSys::is_directory(rpbuf)) {
		return std::string(rpbuf);
	}

	//3. XGD_DATA_HOME env + PACKAGE define
	edatapath = getenv("XDG_DATA_HOME");
	if(edatapath != nullptr) {
		datapath = std::string(edatapath) + FS_SEP PACKAGE;
		if(FileSys::is_directory(datapath.c_str())) {
			return datapath;
		}
	}

#ifdef DATA_PATH
	//4. DATA_PATH define
	if(FileSys::is_directory(DATA_PATH) && realpath(DATA_PATH, rpbuf) != nullptr) {
		return std::string(rpbuf);
	}
#endif

	PERRF(LOG_PROGRAM, "Cannot find the assets!\n");
	throw std::exception();
}

void Program::parse_arguments(int argc, char** argv)
{
	int c;

	opterr = 0;

	while((c = getopt(argc, argv, "v:c:u:")) != -1) {
		switch(c) {
			case 'c':
				if(!FileSys::file_exists(optarg)) {
					PERRF(LOG_PROGRAM, "The specified config file doesn't exists\n");
				} else {
					m_cfg_file = optarg;
				}
				break;
			case 'u':
				if(!FileSys::is_directory(optarg) || access(optarg, R_OK | W_OK | X_OK) == -1) {
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
	m_quit = true;
}

void Program::set_heartbeat(int64_t _ns)
{
	m_heartbeat = _ns;
	m_pacer.set_heartbeat(_ns);
	m_bench.set_heartbeat(_ns);
}