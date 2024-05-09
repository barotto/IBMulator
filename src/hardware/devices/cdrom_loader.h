/*
 * Copyright (C) 2024  Marco Bortolin
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

#ifndef IBMULATOR_HW_CDROM_LOADER_H
#define IBMULATOR_HW_CDROM_LOADER_H

#include "shared_queue.h"

class CdRomDrive;
class CdRomDisc;

class CdRomLoader
{
private:
	bool m_quit = false;
	Machine *m_machine;
	shared_queue<std::function<void()>> m_cmd_queue;

public:
	CdRomLoader(Machine *_m) : m_machine(_m) {}

	void thread_start();

	void cmd_quit();
	void cmd_load_cdrom(CdRomDrive *_drive, std::string _path, std::function<void(bool)> _cb, int _config_id);
	void cmd_dispose_cdrom(CdRomDisc *_disc);

	static CdRomDisc * load_cdrom(std::string _path) noexcept;
};

#endif