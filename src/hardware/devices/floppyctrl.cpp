/*
 * Copyright (C) 2015-2022  Marco Bortolin
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
#include "filesys.h"
#include "floppyctrl.h"


IODEVICE_PORTS(FloppyCtrl) = {
	{ 0x03F0, 0x03F1, PORT_8BIT|PORT_R_ }, // Status Register A / B
	{ 0x03F2, 0x03F2, PORT_8BIT|PORT_RW }, // DOR
	{ 0x03F4, 0x03F4, PORT_8BIT|PORT_RW }, // MSR R / DSR W
	{ 0x03F5, 0x03F5, PORT_8BIT|PORT_RW }, // FIFO R/W
	{ 0x03F7, 0x03F7, PORT_8BIT|PORT_RW }  // DIR R / CCR W
};

static std::map<std::string, unsigned> drive_str_type = {
	{ "none", FloppyDrive::FDD_NONE  },
	{ "3.5",  FloppyDrive::FDD_350HD },
	{ "5.25", FloppyDrive::FDD_525HD }
};

static std::map<unsigned, std::string> drive_type_str = {
	{ FloppyDrive::FDD_NONE,  "none" },
	{ FloppyDrive::FDD_350HD, "3.5"  },
	{ FloppyDrive::FDD_525HD, "5.25" }
};

std::vector<const char*> FloppyCtrl::get_compatible_file_extensions()
{
	std::vector<const char*> extensions;
	for(auto &f : m_floppy_formats) {
		vector_concat(extensions, f->file_extensions());
	}
	return extensions;
}

void FloppyCtrl::install()
{
	IODevice::install();

	m_installed_fdds = 0;
}

void FloppyCtrl::remove()
{
	IODevice::remove();

	floppy_drive_remove(0);
	floppy_drive_remove(1);
}

void FloppyCtrl::config_changed()
{
	m_mode = static_cast<Mode>(g_program.config().get_enum(DRIVES_SECTION, DRIVES_FDC_MODE, {
		{ "at",       Mode::PC_AT },
		{ "pc",       Mode::PC_AT },
		{ "pc-at",    Mode::PC_AT },
		{ "model30",  Mode::MODEL_30 },
		{ "model-30", Mode::MODEL_30 },
		{ "model 30", Mode::MODEL_30 }
	}, Mode::MODEL_30));

	const char *modestr;
	switch(m_mode) {
		case Mode::PC_AT: modestr = "PC-AT"; break;
		case Mode::MODEL_30: modestr = "Model 30"; break;
		default: assert(false); break;
	}
	PINFOF(LOG_V1, LOG_FDC, "Controller in %s mode\n", modestr);
	g_program.config().set_string(DRIVES_SECTION, DRIVES_FDC_MODE, str_to_lower(modestr));

	m_installed_fdds = 0;

	floppy_drive_setup(0);
	floppy_drive_setup(1);
}

FloppyDrive::Type FloppyCtrl::config_drive_type(unsigned drive)
{
	assert(drive < MAX_DRIVES);

	FloppyDrive::Type devtype;
	if(drive == 0) {
		try {
			devtype = (FloppyDrive::Type)g_program.config().get_enum_quiet(DRIVES_SECTION, DRIVES_FDD_A,
					drive_str_type);
		} catch(std::exception &e) {
			devtype = (FloppyDrive::Type)g_machine.model().floppy_a;
		}
	} else {
		try {
			devtype = (FloppyDrive::Type)g_program.config().get_enum_quiet(DRIVES_SECTION, DRIVES_FDD_B,
					drive_str_type);
		} catch(std::exception &e) {
			devtype = (FloppyDrive::Type)g_machine.model().floppy_b;
		}
	}
	return devtype;
}

FloppyDisk::StdType FloppyCtrl::create_new_floppy_image(std::string _imgpath,
		FloppyDrive::Type _devtype, FloppyDisk::StdType _disktype, std::string _format_name)
{
	if(FileSys::file_exists(_imgpath.c_str())) {
		PERRF(LOG_FDC, "Floppy image file '%s' already exists\n", _imgpath.c_str());
		return FloppyDisk::FD_NONE;
	}

	if(_disktype == FloppyDisk::FD_NONE) {
		// use default floppy types
		switch(_devtype) {
			case FloppyDrive::FDD_525DD: _disktype = FloppyDisk::DD_360K; break;
			case FloppyDrive::FDD_525HD: _disktype = FloppyDisk::HD_1_20; break;
			case FloppyDrive::FDD_350DD: _disktype = FloppyDisk::DD_720K; break;
			case FloppyDrive::FDD_350HD: _disktype = FloppyDisk::HD_1_44; break;
			case FloppyDrive::FDD_350ED: _disktype = FloppyDisk::ED_2_88; break;
			default:
				return FloppyDisk::FD_NONE;
		}
	} else if(_devtype != FloppyDrive::FDD_NONE) {
		if(((_disktype & FloppyDisk::SIZE_MASK) != (_devtype & FloppyDisk::SIZE_MASK)) ||
		  ( (_disktype & FloppyDisk::DENS_MASK) & _devtype )
		) {
			PERRF(LOG_FDC, "Floppy drive incompatible with disk '%s'\n",
					FloppyDisk::std_types.at(_disktype).desc.c_str());
			return FloppyDisk::FD_NONE;
		}
	}

	PINFOF(LOG_V0, LOG_FDC, "Creating new image file '%s'...\n", _imgpath.c_str());

	std::unique_ptr<FloppyFmt> format(FloppyFmt::find_by_name(_format_name));
	if(!format) {
		assert(false);
		throw std::runtime_error("Invalid image format.");
	}

	std::string archive = g_program.config().get_file_path(IMAGES_ARCHIVE, FILE_TYPE_ASSET);
	if(!FileSys::file_exists(archive.c_str())) {
		PERRF(LOG_FDC, "Cannot find the image file archive '%s'\n", IMAGES_ARCHIVE);
		throw std::runtime_error("Missing required file in archive");
	}
	std::string imgname = std::regex_replace(FloppyDisk::std_types.at(_disktype).desc.c_str(),
			std::regex("[\\.\" ]"), "_");
	imgname = std::string("floppy/") + format->name() + "/" + imgname + format->default_file_extension();
	if(!FileSys::extract_file(archive.c_str(), imgname.c_str(), _imgpath.c_str())) {
		PERRF(LOG_FDC, "Cannot extract image file '%s'\n", imgname.c_str());
		throw std::runtime_error("Missing required file in archive");
	}

	return _disktype;
}

void FloppyCtrl::floppy_drive_setup(unsigned drive)
{
	assert(drive < MAX_DRIVES);

	FloppyDrive::Type devtype = config_drive_type(drive);

	g_program.config().set_string(DRIVES_SECTION,
			drive==0?DRIVES_FDD_A:DRIVES_FDD_B, drive_type_str[devtype]);

	floppy_drive_remove(drive);

	if(devtype != FloppyDrive::FDD_NONE) {
		m_installed_fdds++;
		m_fdd[drive] = std::make_unique<FloppyDrive>();
		m_fdd[drive]->install(this, drive, devtype);
		PINFOF(LOG_V0, LOG_FDC, "Installed floppy drive %s as %s\n",
				m_fdd[drive]->name(), m_fdd[drive]->description());
	}
}

void FloppyCtrl::floppy_drive_remove(unsigned _drive)
{
	assert(_drive < MAX_DRIVES);
	if(m_fdd[_drive]) {
		m_fdd[_drive]->remove();
		m_fdd[_drive].reset();
	}
}

bool FloppyCtrl::insert_media(unsigned _drive, FloppyDisk *_floppy)
{
	if(!m_fdd[_drive]) {
		return false;
	}
	return m_fdd[_drive]->insert_floppy(_floppy);
}

FloppyDisk* FloppyCtrl::eject_media(unsigned _drive, bool _remove)
{
	if(!m_fdd[_drive]) {
		return nullptr;
	}
	return m_fdd[_drive]->eject_floppy(_remove);
}

void FloppyCtrl::fdd_index_pulse(uint8_t _drive, int _state)
{
	PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: Index pulse: %d\n", _drive, _state);
}

uint32_t FloppyCtrl::calculate_step_delay_us(uint8_t _drive, int _c0, int _c1)
{
	assert(_drive < 4);

	// returns microseconds
	if(!is_motor_on(_drive)) {
		return 0;
	}
	int steps = 1;
	if(_c0 != _c1) {
		steps = abs(_c1 - _c0);
	}
	int one_step_us = get_one_step_delay_time_us();
	return (one_step_us * steps);
}