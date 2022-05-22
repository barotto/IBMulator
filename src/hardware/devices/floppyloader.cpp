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
			PERRF(LOG_MACHINE, "Media not valid for this floppy drive: '%s'\n", _path.c_str());
			m_machine->cmd_insert_floppy(_drive_idx, nullptr, _cb, _config_id);
			return;
		} 

		std::unique_ptr<FloppyDisk> image(fdc->create_floppy_disk(props));

		if(m_state_cb) {
			m_state_cb(State::LOADING, _drive_idx);
		}
		bool result = image->load(_path, fmt);
		if(m_state_cb) {
			m_state_cb(State::IDLE, _drive_idx);
		}
		if(!result) {
			PERRF(LOG_MACHINE, "Cannot load media file '%s'\n", _path.c_str());
			m_machine->cmd_insert_floppy(_drive_idx, nullptr, _cb, _config_id);
			return;
		}

		image->set_write_protected(_write_protected);

		m_machine->cmd_insert_floppy(_drive_idx, image.release(), _cb, _config_id);
	});
}

void FloppyLoader::cmd_save_floppy(FloppyDisk *_floppy, std::string _path, std::shared_ptr<FloppyFmt> _format,
		std::function<void(bool)> _cb)
{
	m_cmd_queue.push([=] () {
		PINFOF(LOG_V0, LOG_MACHINE, "Saving '%s'...\n", _path.c_str());

		if(m_state_cb) {
			m_state_cb(State::SAVING, -1);
		}
		bool result = _floppy->save(_path, _format);
		if(m_state_cb) {
			m_state_cb(State::IDLE, -1);
		}
		if(_cb) {
			_cb(result);
		}
	});
}