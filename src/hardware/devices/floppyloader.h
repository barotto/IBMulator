/*
 * Copyright (C) 2022  Marco Bortolin
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

#ifndef IBMULATOR_HW_FLOPPYLOADER_H
#define IBMULATOR_HW_FLOPPYLOADER_H

#include "shared_queue.h"


class Machine;
class FloppyDisk;

/* Conceptually the FloppyLoader is the equivalent of your hand that takes a
 * floppy disk from the box and inserts it into the drive (load), and then
 * removes the floppy ejected from the drive and puts it back into the box
 * (save).
 * Only one "hand" can juggle floppies, so removing with one hand while
 * inserting with the other is not possible. This sequentiality guarantees data
 * consistency.
 */

class FloppyLoader
{
public:
	enum class State {
		IDLE, LOADING, SAVING
	};
	typedef std::function<void(FloppyLoader::State, int drive)> state_cb_t;

private:
	bool m_quit = false;
	Machine *m_machine;
	shared_queue<std::function<void()>> m_cmd_queue;
	state_cb_t m_state_cb = nullptr;

public:
	FloppyLoader(Machine *_machine) : m_machine(_machine) {}

	void thread_start();

	void register_state_cb(state_cb_t _cb) {
		m_state_cb = _cb;
	}

	void cmd_quit();
	void cmd_load_floppy(uint8_t _drive, unsigned _drive_type,
			std::string _path, bool _write_protected, std::function<void(bool)> _cb,
			int _config_id);
	void cmd_save_floppy(FloppyDisk *_floppy, std::string _path, std::shared_ptr<FloppyFmt> _format,
			std::function<void(bool)> _cb);
};

#endif