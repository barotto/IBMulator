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

#include "ibmulator.h"
#include "program.h"
#include "machine.h"
#include "filesys.h"
#include "cdrom_loader.h"
#include "cdrom_disc.h"
#include <chrono>

void CdRomLoader::thread_start()
{
	PDEBUGF(LOG_V0, LOG_MACHINE, "CdRomLoader: thread started\n");

	while(true) {
		PDEBUGF(LOG_V1, LOG_MACHINE, "CdRomLoader: waiting for commands\n");
		std::function<void()> fn;
		m_cmd_queue.wait_and_pop(fn);
		fn();
		if(m_quit) {
			break;
		}
	}

	PDEBUGF(LOG_V0, LOG_MACHINE, "CdRomLoader: thread stopped\n");
}

void CdRomLoader::cmd_quit()
{
	m_cmd_queue.push([this] () {
		m_quit = true;
	});
}

CdRomDisc * CdRomLoader::load_cdrom(std::string _path) noexcept
{
	PINFOF(LOG_V0, LOG_HDD, "CD-ROM: loading image '%s' ...\n", _path.c_str());

	std::unique_ptr<CdRomDisc> image = std::make_unique<CdRomDisc>();

	auto path = g_program.config().find_media(_path);
	if(FileSys::file_exists(path.c_str())) {
		try {
			image->load(path);

			auto geometry = image->geometry();
			PINFOF(LOG_V1, LOG_HDD,  "CD-ROM:   total tracks: %u, sectors: %u\n",
					image->tracks_count(), image->sectors());
			PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM:   C/S: %u/%u, radius: %.1f mm\n",
					geometry.cylinders, geometry.spt, image->radius());
		} catch(std::runtime_error &e) {
			PERRF(LOG_HDD, "CD-ROM: %s.\n", e.what());
			image.reset();
		}
	} else {
		PERRF(LOG_HDD, "CD-ROM: cannot find the image file!\n");
		image.reset();
	}
	// test: std::this_thread::sleep_for(std::chrono::milliseconds(2000));
	return image.release();
}

void CdRomLoader::cmd_load_cdrom(CdRomDrive *_drive, std::string _path, std::function<void(bool)> _cb, int _config_id)
{
	m_cmd_queue.push([=] () {
		m_machine->cmd_insert_cdrom(_drive, load_cdrom(_path), _path, _cb, _config_id);
	});
}

void CdRomLoader::cmd_dispose_cdrom(CdRomDisc *_disc)
{
	m_cmd_queue.push([=] () {
		std::unique_ptr<CdRomDisc> disc(_disc);
		PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: disposing of disc ...\n");
		disc->dispose(); // blocking
	});
}