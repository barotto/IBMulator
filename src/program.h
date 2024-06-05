/*
 * Copyright (C) 2015-2024  Marco Bortolin
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

#include "pacer.h"
#include "bench.h"
#include "appconfig.h"
#include "state_record.h"
#include <condition_variable>

class GUI;
class Machine;
class Mixer;
class Program;
extern Program g_program;

class Program
{
	std::string m_datapath;

	std::atomic<int64_t> m_heartbeat;
	std::atomic<bool> m_quit;
	Pacer m_pacer;
	Bench m_bench;

	Machine *m_machine;
	Mixer *m_mixer;
	std::unique_ptr<GUI> m_gui;

	std::string m_user_dir; // the directory where the user keeps files like ibmulator.ini
	std::string m_cfg_file; // the full path of ibmulator.ini
	AppConfig m_config[2];  // 0: the start up program config, 1: the current config

	bool m_start_machine;
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

	bool initialize(int argc, char** argv);
	void start();
	void stop();

	int64_t heartbeat() const { return m_heartbeat; }
	void set_heartbeat(int64_t _ns);
	Pacer & pacer() { return m_pacer; }

	inline AppConfig & config() {
		return m_config[1];
	}
	inline AppConfig & initial_config() {
		return m_config[0];
	}

	Bench & get_bench() { return m_bench; }

	void save_state(StateRecord::Info _info, std::function<void(StateRecord::Info)> _on_success, std::function<void(std::string)> _on_fail);
	void restore_state(StateRecord::Info _info, std::function<void()> _on_success, std::function<void(std::string)> _on_fail);
	void delete_state(StateRecord::Info _info);

	GUI * gui_instance() const {
		return m_gui.get();
	}
	
	static void check_state_bufsize(uint8_t *_buf, size_t _exp_size);
};

#endif
