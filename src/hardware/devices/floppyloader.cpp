/*
 * Copyright (C) 2022-2025  Marco Bortolin
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
#include "floppyctrl.h"
#include "floppyfmt.h"


void FloppyLoader::thread_start()
{
	PDEBUGF(LOG_V0, LOG_MACHINE, "FloppyLoader: thread started\n");

	while(true) {
		PDEBUGF(LOG_V1, LOG_MACHINE, "FloppyLoader: waiting for commands\n");
		std::function<void()> fn;
		m_cmd_queue.wait_and_pop(fn);
		fn();
		if(m_quit) {
			break;
		}
	}

	PDEBUGF(LOG_V0, LOG_MACHINE, "FloppyLoader: thread stopped\n");
}

void FloppyLoader::cmd_quit()
{
	m_cmd_queue.push([this] () {
		m_quit = true;
	});
}

void FloppyLoader::cmd_load_floppy(uint8_t _drive_idx, unsigned _drive_type,
		std::string _path, bool _write_protected, std::function<void(bool)> _cb,
		int _config_id)
{
	assert(_drive_idx < 4);

	m_cmd_queue.push([=] () {
		PINFOF(LOG_V0, LOG_MACHINE, "Loading '%s'...\n", _path.c_str());

		auto fdc = m_machine->devices().device<FloppyCtrl>();
		if(!fdc) {
			PERRF(LOG_MACHINE, "Cannot create a floppy disk without a floppy controller!\n");
			m_machine->cmd_insert_floppy(_drive_idx, nullptr, _cb, _config_id);
			return;
		}

		std::shared_ptr<FloppyFmt> fmt(FloppyFmt::find(_path));
		if(!fmt) {
			PERRF(LOG_MACHINE, "Cannot find a valid format to read '%s'\n", _path.c_str());
			m_machine->cmd_insert_floppy(_drive_idx, nullptr, _cb, _config_id);
			return;
		}

		auto props = fmt->identify(
			_path,
			FileSys::get_file_size(_path.c_str()),
			FloppyDisk::Size(_drive_type & FloppyDisk::SIZE_MASK)
		);
		if(props.type == FloppyDisk::FD_NONE) {
			PERRF(LOG_MACHINE, "Medium not valid for this floppy drive: '%s'\n", _path.c_str());
			m_machine->cmd_insert_floppy(_drive_idx, nullptr, _cb, _config_id);
			return;
		} 

		std::unique_ptr<FloppyDisk> image(fdc->create_floppy_disk(props));

		if(m_activity_cb) {
			m_activity_cb(FloppyEvents::EVENT_DISK_LOADING, _drive_idx);
		}
		bool result = image->load(_path, fmt);
		if(m_activity_cb) {
			m_activity_cb(FloppyEvents::EVENT_MEDIUM, _drive_idx);
		}
		if(!result) {
			PERRF(LOG_MACHINE, "Cannot load image file '%s'\n", _path.c_str());
			m_machine->cmd_insert_floppy(_drive_idx, nullptr, _cb, _config_id);
			return;
		}

		if(_write_protected) {
			image->set_write_protected(_write_protected);
		}

		if(LOG_DEBUG_MESSAGES && g_syslog.get_verbosity(LOG_FDC) >= LOG_V5) {
			dump_image_tracks(image.get(), _path);
		}

		m_machine->cmd_insert_floppy(_drive_idx, image.release(), _cb, _config_id);
	});
}

void FloppyLoader::cmd_save_floppy(FloppyDisk *_floppy, std::string _path, std::shared_ptr<FloppyFmt> _format,
		uint8_t _drive_idx, std::function<void(bool)> _cb)
{
	m_cmd_queue.push([=] () {
		PINFOF(LOG_V0, LOG_MACHINE, "Saving '%s'...\n", _path.c_str());

		if(m_activity_cb) {
			m_activity_cb(FloppyEvents::EVENT_DISK_SAVING, _drive_idx);
		}
		bool result = _floppy->save(_path, _format);
		if(m_activity_cb) {
			m_activity_cb(FloppyEvents::EVENT_MEDIUM, _drive_idx);
		}
		if(_cb) {
			_cb(result);
		}
	});
}

void FloppyLoader::dump_image_tracks(FloppyDisk *_floppy, std::string _floppy_path)
{
	int cell_size;
	if(_floppy->props().type & FloppyDisk::DENS_SD) {
		cell_size = 4000;
	} if(_floppy->props().type & FloppyDisk::DENS_DD) {
		cell_size = 2000;
	} else if(_floppy->props().type & FloppyDisk::DENS_HD) {
		if(_floppy->props().type & FloppyDisk::SIZE_5_25) {
			cell_size = 1200;
		} else {
			cell_size = 1000;
		}
	} else if(_floppy->props().type & FloppyDisk::DENS_ED) {
		cell_size = 500;
	} else {
		return;
	}

	std::string dir, filename;
	if(!FileSys::get_path_parts(_floppy_path.c_str(), dir, filename)) {
		PDEBUGF(LOG_V0, LOG_MACHINE, "FloppyLoader: invalid filename.\n");
		return;
	}
	std::string basename, ext;
	FileSys::get_file_parts(filename.c_str(), basename, ext);
	std::string dest_dir = dir + "/" + basename + "_TRACKS";
	try {
		FileSys::create_dir(dest_dir.c_str());
	} catch(std::exception &e) {
		PDEBUGF(LOG_V0, LOG_MACHINE, "FloppyLoader: cannot create '%s'\n", dest_dir.c_str());
		return;
	}

	PDEBUGF(LOG_V0, LOG_MACHINE,"FloppyLoader: dumping floppy data into '%s'\n", dest_dir.c_str());

	std::vector<uint8_t> track_data;
	for(unsigned cyl = 0; cyl < _floppy->props().tracks; cyl++) {
		for(unsigned head = 0; head < _floppy->props().sides; head++) {
			track_data.clear();

			auto track_name = str_format("c%uh%u", cyl, head);
			PDEBUGF(LOG_V0, LOG_MACHINE,"FloppyLoader:   track %s\n", track_name.c_str());

			auto bitstream = FloppyFmt::generate_bitstream_from_track(cyl, head, cell_size, *_floppy);

			uint32_t pos = 0;
			do {
				track_data.push_back(FloppyFmt::sbyte_mfm_r(bitstream, pos));
			} while(pos);

			std::string track_file_path = dest_dir + "/" + track_name + ".data";

			auto track_file = FileSys::make_ofstream(track_file_path.c_str(), std::ofstream::binary);
			if(!track_file.is_open()) {
				PDEBUGF(LOG_V0, LOG_MACHINE,"FloppyLoader: cannot open file.\n", track_file_path.c_str());
				return;
			}
			track_file.write(reinterpret_cast<char*>(&track_data[0]), track_data.size());
			if(track_file.bad()) {
				PDEBUGF(LOG_V0, LOG_MACHINE,"FloppyLoader: cannot write to file.\n");
				return;
			}
			track_file.close();
		}
	}
}
