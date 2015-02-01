/*
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ibmulator.h"
#include "hardware/memory.h"
#include "hardware/cpu.h"
#include "gui/gui.h"
#include "program.h"
#include "machine.h"
#include "mixer.h"
#include "statebuf.h"
#include <cstdio>
#include <libgen.h>
#include <thread>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <sstream>

#ifdef _WIN32
#include "wincompat.h"
#endif

Program g_program;

std::mutex Program::ms_lock;
std::condition_variable Program::ms_cv;

Program::Program()
:
m_machine(NULL),
m_gui(NULL)
{

}

Program::~Program()
{
	SDL_Quit();
}

void Program::save_state(std::string _name)
{
	if(!m_machine->is_on()) {
		PINFOF(LOG_V0, LOG_PROGRAM, "the machine is off\n");
		return;
	}

	if(_name.empty()) {
		_name = "state";
	}
	std::string path = m_config.get_file(PROGRAM_SECTION, PROGRAM_CAPTURE_DIR, false) + FS_SEP + _name;

	PINFOF(LOG_V0, LOG_PROGRAM, "saving current state in '%s'...\n", path.c_str());

	std::string ini = path + ".ini";
	try {
		m_config.create_file(ini);
	} catch(std::exception &e) {
		PERRF(LOG_PROGRAM, "unable to create config file '%s'\n", ini.c_str());
		throw std::exception();
	}

	StateBuf state;

	std::unique_lock<std::mutex> lock(ms_lock);
	m_machine->cmd_save_state(state);
	ms_cv.wait(lock);

	state.save(path + ".bin");

	m_gui->save_framebuffer(path + ".png");

	PINFOF(LOG_V0, LOG_PROGRAM, "current state saved\n");
}

void Program::restore_state(std::string _name)
{
	if(_name.empty()) {
		_name = "state";
	}
	std::string path = m_config.get_file(PROGRAM_SECTION, PROGRAM_CAPTURE_DIR, false) + FS_SEP + _name;
	std::string ini = path + ".ini";
	std::string bin = path + ".bin";

	if(!file_exists(ini.c_str())) {
		PERRF(LOG_PROGRAM, "state ini file missing!\n");
		return;
	}

	if(!file_exists(bin.c_str())) {
		PERRF(LOG_PROGRAM, "state bin file missing!\n");
		return;
	}

	PINFOF(LOG_V0, LOG_PROGRAM, "loading state from '%s'...\n", path.c_str());


	AppConfig conf;
	try {
		conf.parse(ini);
	} catch(std::exception &e) {
		PERRF(LOG_PROGRAM, "unable to parse '%s'\n", ini.c_str());
		throw std::exception();
	}

	StateBuf state;

	state.load(bin);

	//from this point, any error in the restore procedure will render the
	//machine inconsistent and it should be terminated
	m_config.reset(); //TODO the config object needs a mutex!
	m_config.merge(conf);

	std::unique_lock<std::mutex> restore_lock(ms_lock);

	m_mixer->cmd_pause();

	m_machine->sig_config_changed();
	ms_cv.wait(restore_lock);

	m_mixer->sig_config_changed();
	ms_cv.wait(restore_lock);

	m_machine->cmd_restore_state(state);
	ms_cv.wait(restore_lock);

	if(state.m_last_restore == false) {
		PERRF(LOG_PROGRAM, "the restored state is not valid\n");
		throw std::exception();
	}

	m_mixer->cmd_resume();
	m_gui->vga_update();

	PINFOF(LOG_V0, LOG_PROGRAM, "state restored\n");
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

void Program::create_dir(const std::string &_path)
{
	if(!file_exists(_path.c_str())) {
		PDEBUGF(LOG_V0, LOG_PROGRAM, "Creating '%s'\n", _path.c_str());
		if(mkdir(_path.c_str()
#ifndef _WIN32
			, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH
#endif
		) != 0) {
			PERRF(LOG_PROGRAM, "Unable to create '%s'\n", _path.c_str());
			throw std::exception();
		}
	}
}

bool Program::is_directory(const char *_path)
{
	if(_path == NULL) {
		return false;
	}

	struct stat sb;

	if(stat(_path, &sb) != 0) {
		return false;
	}

	return (S_ISDIR(sb.st_mode));
}

bool Program::is_file_readable(const char *_path)
{
	if(_path == NULL) return false;
	return (access(_path, R_OK) == 0);
}

bool Program::file_exists(const char *_path)
{
	if(_path == NULL) return false;
	return (access(_path, F_OK) == 0);
}

size_t Program::get_file_size(const char *_path)
{
	if(_path == NULL) {
		return 0;
	}
	struct stat sb;
	if(stat(_path, &sb) != 0) {
		return 0;
	}
	return sb.st_size;
}

std::string Program::get_next_filename(const std::string &_dir,
		const std::string &_basename, const std::string &_ext)
{
	std::stringstream ss;
	int counter = 0;
	std::string fname;
	do {
		ss.str("");
		ss << _dir << FS_SEP << _basename;
		ss.width(4);
		ss.fill('0');
		ss << counter;
		counter++;
		fname = ss.str() + _ext;
	} while( file_exists(fname.c_str()) && counter<10000);
	if(counter>=10000) {
		return "";
	}
	return ss.str();
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

	if(m_user_dir.empty()) {
#ifndef _WIN32
		str = getenv("XDG_CONFIG_HOME");
		if(str == NULL) {
			m_user_dir = home + FS_SEP ".config";
		} else {
			m_user_dir = str;
		}
#else
		//WINDOWS uses LOCALAPPDATA\{DeveloperName\AppName}
		str = getenv("LOCALAPPDATA");
		if(str == NULL) {
			PERRF_ABORT(LOG_PROGRAM, "Unable to determine the LOCALAPPDATA directory!\n");
		}
		m_user_dir = str;
#endif
		if(!is_directory(m_user_dir.c_str()) || access(m_user_dir.c_str(), R_OK | W_OK | X_OK) != 0) {
			PERRF_ABORT(LOG_PROGRAM, "Unable to access the user directory!\n");
		}
		m_user_dir += FS_SEP PACKAGE;
	}
	create_dir(m_user_dir);
	PINFO(LOG_V1,"user directory: %s\n", m_user_dir.c_str());

	cfgfile = m_user_dir + FS_SEP PACKAGE ".ini";
	if(m_cfg_file.empty()) {
		m_cfg_file = cfgfile;
	}
	PINFO(LOG_V1,"ini file: %s\n", m_cfg_file.c_str());

	if(!file_exists(m_cfg_file.c_str())) {
		PWARNF(LOG_PROGRAM, "The config file '%s' doesn't exists, creating...\n", m_cfg_file.c_str());
		try {
			m_config.create_file(m_cfg_file,true);
			std::string message = "The configuration file " PACKAGE ".ini has been created in " +
					m_user_dir + "\n";
			message += "Open it and configure the program as you like.";
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Configuration file",
					message.c_str(),
			        NULL);
			return false;
		} catch(std::exception &e) {
			PWARNF(LOG_PROGRAM, "Unable to create config file, using default\n", m_cfg_file.c_str());
			std::string message = "A problem occurred trying to create " PACKAGE ".ini in " +
					m_user_dir + "\n";
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Configuration file",
					message.c_str(),
			        NULL);
			m_cfg_file = cfgfile;
		}
	}

	if(CONFIG_PARSE) {
		try {
			m_config.parse(m_cfg_file);
		} catch(std::exception &e) {
			int error = m_config.get_error();
			if(error < 0) {
				PERRF(LOG_PROGRAM, "Unable to open '%s'\n", m_cfg_file.c_str());
				throw;
			}
			PERRF(LOG_PROGRAM, "Parse error on line %d in '%s'\n", error, m_cfg_file.c_str());
			throw;
		}
	}
	m_config.set_user_home(home);
	m_config.set_cfg_home(m_user_dir);

	m_datapath = get_assets_dir(argc,argv);
	m_config.set_assets_home(m_datapath);
	PINFO(LOG_V1,"assets directory: %s\n", m_datapath.c_str());

	//Capture dir, create if not exists
	std::string capture_dir = m_config.get_file(PROGRAM_SECTION, PROGRAM_CAPTURE_DIR, false);
	if(capture_dir.empty()) {
		capture_dir = m_user_dir + FS_SEP "capture";
	}
	create_dir(capture_dir);
	m_config.set_string(PROGRAM_SECTION, PROGRAM_CAPTURE_DIR, capture_dir);
	PINFO(LOG_V1,"capture directory: %s\n", capture_dir.c_str());

	std::string dumplog = m_config.get_file_path("log.txt", false);
	g_syslog.add_device(LOG_ALL_PRIORITIES, LOG_ALL_FACILITIES, new LogStream(dumplog.c_str()));

	init_SDL();

	PINFO(LOG_V0, "Calibrating...");
	m_main_chrono.calibrate();
	if(CHRONO_RDTSC) {
		PINFO(LOG_V0, " %llu Hz\n", m_main_chrono.get_freq());
	} else {
		PINFO(LOG_V0, " done\n");
	}

	m_fpscap = 16666;
	m_heartbeat = GUI_HEARTBEAT;
	m_frameskip = 5;
	m_quit = false;
	m_next_beat = m_main_chrono.get_usec();
	m_bench.init(&m_main_chrono, 1000, 1000);

	//g_syslog.set_verbosity(LOG_V2, LOG_FDC);
	//g_syslog.set_verbosity(LOG_V2, LOG_DMA);
	//g_syslog.set_verbosity(LOG_V2, LOG_MACHINE);
	//g_syslog.set_verbosity(LOG_V2, LOG_LPT);

	return true;
}

std::string Program::get_assets_dir(int argc, char** argv)
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
	if(edatapath != NULL) {
		return std::string(edatapath);
	}

	//2. dirname(argv[0]) + /../share/PACKAGE
	char *buf = strdup(argv[0]); //dirname modifies the string!
	datapath = dirname(buf);
	free(buf);
	datapath += std::string(FS_SEP) + ".." FS_SEP "share" FS_SEP PACKAGE;
	if(realpath(datapath.c_str(), rpbuf) != NULL && is_directory(rpbuf)) {
		return std::string(rpbuf);
	}

	//3. XGD_DATA_HOME env + PACKAGE define
	edatapath = getenv("XDG_DATA_HOME");
	if(edatapath != NULL) {
		datapath = std::string(edatapath) + FS_SEP PACKAGE;
		if(is_directory(datapath.c_str())) {
			return datapath;
		}
	}

#ifdef DATA_PATH
	//4. DATA_PATH define
	if(is_directory(DATA_PATH) && realpath(DATA_PATH, rpbuf) != NULL) {
		return std::string(rpbuf);
	}
#endif

	PERRF(LOG_PROGRAM, "unable to find the assets!\n");
	throw std::exception();
}

void Program::parse_arguments(int argc, char** argv)
{
	int c;

	opterr = 0;

	while((c = getopt(argc, argv, "v:c:u:")) != -1) {
		switch(c) {
			case 'c':
				if(!file_exists(optarg)) {
					PERRF(LOG_PROGRAM, "The specified config file doesn't exists\n");
				} else {
					m_cfg_file = optarg;
				}
				break;
			case 'u':
				if(!is_directory(optarg) || access(optarg, R_OK | W_OK | X_OK) == -1) {
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

void Program::set_gui(GUI *_gui)
{
	if(!m_machine) {
		PERR("Initialize the machine first\n");
		throw std::exception();
	}
	if(!m_mixer) {
		PERR("Initialize the mixer first\n");
		throw std::exception();
	}
	m_gui = _gui;
	m_gui->init(m_machine, m_mixer);
}

void Program::set_machine(Machine *_machine)
{
	m_machine = _machine;
	m_machine->calibrate(m_main_chrono);
	m_machine->init();
}

void Program::set_mixer(Mixer *_mixer)
{
	if(!m_machine) {
		PERR("Initialize the machine first\n");
		throw std::exception();
	}
	m_mixer = _mixer;
	m_mixer->calibrate(m_main_chrono);
	m_mixer->init(m_machine);
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
	/* http://www.flipcode.com/archives/Main_Loop_with_Fixed_Time_Steps.shtml
	 * http://www.bulletphysics.org/mediawiki-1.5.8/index.php?title=Canonical_Game_Loop
	 */
	int64_t next_time_diff = 0;
	while(!m_quit) {
		uint loops = 0;

		uint64_t time = m_main_chrono.elapsed_usec();
		if(time < m_fpscap) {
			uint64_t sleep = (m_fpscap - time) + next_time_diff;
			uint64_t t0 = m_main_chrono.get_usec();
			std::this_thread::sleep_for( std::chrono::microseconds(sleep) );
			uint64_t t1 = m_main_chrono.get_usec();
			next_time_diff = sleep - (t1 - t0);
		}
		time = m_main_chrono.get_usec(m_main_chrono.start());

		m_bench.frame_start();

		while(!m_quit && ((time - m_next_beat) >= m_heartbeat && loops <= m_frameskip))
		{
			m_bench.beat_start();
			m_bench.frameskip = loops;

			process_evts();

			if(!MULTITHREADED) {
				if(!m_machine->main_loop()) {
					m_quit = true;
				} else {
					m_mixer->main_loop();
				}
			}
			m_gui->update();

			m_next_beat += m_heartbeat;
			loops++;

			m_bench.beat_end();
		}

		// Account for loops overflow causing percent > 1.
		//float percentWithinTick = std::min(1.f, float(time - g_next_beat)/float(g_heartbeat));

		m_gui->render();

		m_bench.frame_end();
	}
}

void Program::start()
{
	if(MULTITHREADED) {
		PDEBUGF(LOG_V2, LOG_PROGRAM, "Program thread started\n");
		std::thread machine(&Machine::start,m_machine);
		std::thread mixer(&Mixer::start,m_mixer);

		main_loop();

		machine.join();
		mixer.join();

	} else {

		m_machine->start();
		m_mixer->start();

		main_loop();
	}

	m_gui->shutdown();
}

void Program::stop()
{
	m_quit = true;
	m_machine->cmd_power_off();
	m_machine->cmd_quit();
	m_mixer->cmd_quit();
}

