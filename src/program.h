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

	bool m_threads_sync;
	bool m_framecap;
	std::atomic<int64_t> m_heartbeat;
	bool m_quit;
	Chrono m_main_chrono;
	Bench m_bench;

	Machine *m_machine;
	GUI *m_gui;
	Mixer *m_mixer;

	std::string m_user_dir; // the directory where the user keeps files like ibmulator.ini
	std::string m_cfg_file; // the full path of ibmulator.ini
	AppConfig m_config[2];  // 0: the start up program config, 1: the current config

	std::function<void()> m_restore_fn;

	void init_SDL();
	void process_evts();
	void main_loop();

	std::string get_assets_dir(int argc, char** argv);
	void parse_arguments(int argc, char** argv);

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

	bool threads_sync() const { return m_threads_sync; }
	int64_t heartbeat() const { return m_heartbeat; }
	void set_heartbeat(int64_t _ns);

	inline AppConfig & config() {
		return m_config[1];
	}
	Bench & get_bench() { return m_bench; }

	void save_state(std::string _path, std::function<void()> _on_success, std::function<void(std::string)> _on_fail);
	void restore_state(std::string _path, std::function<void()> _on_success, std::function<void(std::string)> _on_fail);

	static void check_state_bufsize(uint8_t *_buf, size_t _exp_size);
};

#endif
