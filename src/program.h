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

#ifndef IBMULATOR_PROGRAM_H
#define IBMULATOR_PROGRAM_H

#include "chrono.h"
#include "bench.h"
#include "appconfig.h"
#include <condition_variable>

class GUI;
class Machine;
class Mixer;
class Program;
extern Program g_program;

class Program
{
	std::string m_datapath;

	double m_freq;
	uint64_t m_fpscap;
	uint m_heartbeat;
	uint64_t m_next_beat;
	uint m_frameskip;
	bool m_quit;
	Chrono m_main_chrono;
	Bench m_bench;

	Machine *m_machine;
	GUI *m_gui;
	Mixer *m_mixer;

	std::string m_user_dir; //the directory where the user keeps files like ibmulator.ini
	std::string m_cfg_file; //the full path of ibmulator.ini
	AppConfig m_config;

	void init_SDL();
	void process_evts();
	void main_loop();

	std::string get_assets_dir(int argc, char** argv);
	void parse_arguments(int argc, char** argv);
	void create_dir(const std::string &_path);

public:

	//this is for synchronization between program and machine threads
	//(see state save/restore)
	static std::mutex ms_lock;
	static std::condition_variable ms_cv;

public:

	Program();
	~Program();

	void set_machine(Machine*);
	void set_gui(GUI*);
	void set_mixer(Mixer*);

	bool initialize(int argc, char** argv);
	void start();
	void stop();

	uint get_beat_time_usec() { return m_heartbeat; }

	inline AppConfig & config() { return m_config; }
	Bench & get_bench() { return m_bench; }

	void save_state(std::string _path);
	void restore_state(std::string _path);

	static void check_state_bufsize(uint8_t *_buf, size_t _exp_size);

	static std::string get_next_filename(const std::string &_dir,
			const std::string &_basename, const std::string &_ext);

	static bool is_directory(const char *_path);
	static bool file_exists(const char *_path);
	static bool is_file_readable(const char *_path);
	static size_t get_file_size(const char *_path);
};

#endif
