/*
 * Copyright (C) 2016-2021  Marco Bortolin
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
#include "machine.h"
#include "md5.h"
#include "systemrom.h"
#include "memory.h"
#include "cpu.h"
#include "filesys.h"
#include "utils.h"
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#if HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif
#include <algorithm>
#include <SDL.h>

#define MAX_ROM_SIZE   0x80000
#define SYS_ROM_ADDR   0xF80000
#define BIOS_OFFSET    0x70000
#define BIOS_SIZE      0x10000


SystemROM::SystemROM()
: m_data(nullptr),
  m_low_mapping(0), m_high_mapping(0)
{
}

SystemROM::~SystemROM()
{
	delete[] m_data;
}

void SystemROM::init()
{
	m_data = new uint8_t[MAX_ROM_SIZE];
	memset(m_data, 0, MAX_ROM_SIZE);

	m_low_mapping = g_memory.add_mapping(0xE0000, 0x20000, MEM_MAPPING_EXTERNAL,
		SystemROM::s_read<uint8_t>, SystemROM::s_read<uint16_t>, SystemROM::s_read<uint32_t>, this);
	m_high_mapping = g_memory.add_mapping(0xF80000, 0x80000, MEM_MAPPING_EXTERNAL,
		SystemROM::s_read<uint8_t>, SystemROM::s_read<uint16_t>, SystemROM::s_read<uint32_t>, this);
}

void SystemROM::config_changed()
{
	int c = 1 + ceil(g_machine.model().rom_speed / g_cpu.cycle_time_ns());
	int d = (g_machine.model().rom_bit==32)?c:c*2;
	g_memory.set_mapping_cycles(m_low_mapping, c,c,d);
	g_memory.set_mapping_cycles(m_high_mapping, c,c,d);

	PINFOF(LOG_V2, LOG_MACHINE, "ROM speed: %u ns, %d/%d/%d cycles\n",
		g_machine.model().rom_speed, c,c,d);
}

void SystemROM::load(const std::string _romset)
{
	assert(m_data != nullptr);
	m_bios = g_bios_db.at("unknown");
	memset(m_data, 0, MAX_ROM_SIZE);
	m_romset.clear();

	if(FileSys::is_directory(_romset.c_str())) {
		PINFOF(LOG_V0, LOG_MACHINE, "Loading ROM directory '%s'\n", _romset.c_str());
		load_dir(_romset);
	} else {
		if(!FileSys::file_exists(_romset.c_str())) {
			PERRF(LOG_MACHINE, "Unable to find ROM set '%s'\n", _romset.c_str());
			throw std::exception();
		}
		std::string dir,base,ext;
		FileSys::get_path_parts(_romset.c_str(), dir, base, ext);
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
		if(ext == ".bin" || ext == "") {
			PINFOF(LOG_V0, LOG_MACHINE, "Loading ROM file '%s'\n", _romset.c_str());
			load_file(_romset, ~0);
		} else {
			PINFOF(LOG_V0, LOG_MACHINE, "Loading ROM set '%s'\n", _romset.c_str());
			load_archive(_romset);
		}
	}
	m_romset = _romset;

	MD5 md5;
	md5.update(&m_data[BIOS_OFFSET], BIOS_SIZE);
	md5.finalize();
	std::string bios_md5 = md5.hexdigest();

	PINFOF(LOG_V1, LOG_MACHINE, "BIOS md5sum: %s\n", bios_md5.c_str());

	auto biostype = g_bios_db.find(bios_md5);
	if(biostype != g_bios_db.end()) {
		m_bios = biostype->second;
	} else {
		m_bios = g_bios_db.at("unknown");
		for(int i=0; i<69; ++i) {
			uint8_t c = m_data[BIOS_OFFSET+i];
			if(c>=0x20 && c<=0x7E) {
				m_bios.version += c;
			}
		}
	}
	PINFOF(LOG_V0, LOG_MACHINE, "BIOS version: %s\n", m_bios.version.c_str());
	PINFOF(LOG_V0, LOG_MACHINE, "BIOS type: %s\n", m_bios.type.c_str());
	if(m_bios.machine_model == MDL_UNKNOWN) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Unknown BIOS",
				"You are using an unsupported system ROM.\n"
				"Please consider sending a copy to the " PACKAGE_NAME "'s author. "
				"Thank you! :)",
				nullptr);
	}

	/* Model  Submdl  Rev     BIOS date       System
	 * F8h    30h     00h       ???           PS/1 Model 2121 (16 MHz 386SX)
	 * FCh    0Bh     00h     12/01/89        PS/1 (LW-Type 44)
	 * FCh    0Bh     00h     02/16/90        PS/1 Model 2011 (10 MHz 286)
	 */
	uint8_t modelID = m_data[BIOS_OFFSET+0xFFFE];
	PINFOF(LOG_V0, LOG_MACHINE, "BIOS system model ID: 0x%02X\n", modelID);
	if(modelID != 0xFC && modelID != 0xF8) {
		PWARNF(LOG_V0, LOG_MACHINE, "Unsupported system model ID!\n");
	} else {
		std::string biosdate;
		for(int i=0; i<8; i++) {
			biosdate += m_data[BIOS_OFFSET+0xFFF5+i];
		}
		PINFOF(LOG_V0, LOG_MACHINE, "BIOS date: %s\n", biosdate.c_str());
	}
	PINFOF(LOG_V1, LOG_MACHINE, "BIOS checksum: 0x%02X\n", m_data[BIOS_OFFSET+0xFFFF]);
}

void SystemROM::load_bios_patch(std::string _patch_file, unsigned _patch_offset)
{
	if(_patch_offset >= BIOS_SIZE) {
		PERRF(LOG_MACHINE, "BIOS patch offset value exceeds 0x%x limit\n", BIOS_SIZE);
		throw std::exception();
	}

	uint64_t patch_size = FileSys::get_file_size(_patch_file.c_str());
	if(_patch_offset + patch_size > BIOS_SIZE) {
		PERRF(LOG_MACHINE, "BIOS patch is too big\n");
		throw std::exception();
	}

	auto file = FileSys::make_file(_patch_file.c_str(), "rb");
	if(file == nullptr) {
		PERRF(LOG_MACHINE, "Error opening file '%s'\n", _patch_file.c_str());
		throw std::exception();
	}

	if(fread(static_cast<void*>(&m_data[BIOS_OFFSET + _patch_offset]), patch_size, 1, file.get()) != 1) {
		PERRF(LOG_MACHINE, "Error reading BIOS patch file '%s'\n", _patch_file.c_str());
		throw std::exception();
	}
}

void SystemROM::inject_custom_hdd_params(int _table_entry_id, HDDParams _params)
{
	if(_table_entry_id<=0 || _table_entry_id>47) {
		PERRF(LOG_MACHINE, "Invalid HDD parameters table entry id: %d\n", _table_entry_id);
		throw std::exception();
	}

	if(m_bios.hdd_ptable_off == 0xFFFF) {
		PERRF(LOG_MACHINE, "The HDD parameters table offset for the current BIOS is unknown\n");
		throw std::exception();
	}

	uint32_t off = BIOS_OFFSET + m_bios.hdd_ptable_off + _table_entry_id*16;
	PDEBUGF(LOG_V1, LOG_MACHINE, "Custom HDD table_entry_id=%d, addr=%x\n", _table_entry_id, off);

	// update the parameters table
	size_check<HDDParams, 16>();
	memcpy(&m_data[off], &_params, 16);

	// update the BIOS checksum
	update_bios_checksum();
}

void SystemROM::update_bios_checksum()
{
	uint8_t old_sum = m_data[BIOS_OFFSET + BIOS_SIZE - 1];
	uint8_t sum = 0;
	for(int i=0; i<BIOS_SIZE-1; i++) {
		sum += m_data[BIOS_OFFSET + i];
	}
	sum = (~sum) + 1;
	m_data[BIOS_OFFSET + BIOS_SIZE - 1] = sum;

	if(old_sum != sum) {
		PINFOF(LOG_V1, LOG_MACHINE, "New BIOS checksum: 0x%02X\n", sum);
	}
}

int SystemROM::load_file(const std::string &_filename, uint32_t _phyaddr)
{
	uint64_t size = FileSys::get_file_size(_filename.c_str());

	if(_phyaddr == uint32_t(~0)) {
		if(size > MAX_ROM_SIZE) {
			PERRF(LOG_MACHINE, "ROM file '%s' is of wrong size\n", _filename.c_str());
			throw std::exception();
		}
		// a 64KB ROM will be loaded at physical addr 0xF0000
		_phyaddr = MAX_ROM_SIZE - size;
	} else {
		// _destaddr is the absolute physical memory address
		assert(_phyaddr >= SYS_ROM_ADDR);
		_phyaddr -= SYS_ROM_ADDR;
		if(_phyaddr + size > MAX_ROM_SIZE) {
			PERRF(LOG_MACHINE, "ROM file '%s' is of wrong size\n", _filename.c_str());
			throw std::exception();
		}
	}
	auto file = FileSys::make_file(_filename.c_str(), "rb");
	if(file == nullptr) {
		PERRF(LOG_MACHINE, "Error opening ROM file '%s'\n", _filename.c_str());
		throw std::exception();
	}
	PINFOF(LOG_V1, LOG_MACHINE, "Loading '%s' ...\n", _filename.c_str());
	size = fread((void*)(m_data + _phyaddr), size, 1, file.get());
	if(size != 1) {
		PERRF(LOG_MACHINE, "Error reading ROM file '%s'\n", _filename.c_str());
		throw std::exception();
	}
	return size;
}

void SystemROM::load_dir(const std::string &_dirname)
{
	DIR *dir;
	struct dirent *ent;

	if((dir = opendir(_dirname.c_str())) == nullptr) {
		PERRF(LOG_MACHINE, "Unable to open directory %s\n", _dirname.c_str());
		throw std::exception();
	}
	std::string dirname = _dirname + FS_SEP;
	bool f80000found = false;
	bool fc0000found = false;
	while((ent = readdir(dir)) != nullptr) {
		struct stat sb;
		std::string name = ent->d_name;
		std::string fullpath = dirname + name;
		if(stat(fullpath.c_str(), &sb) != 0) {
			continue;
		}
		if(S_ISDIR(sb.st_mode)) {
			continue;
		} else {
			std::transform(name.begin(), name.end(), name.begin(), ::tolower);
			if(!fc0000found && name.compare("fc0000.bin")==0) {
				fc0000found = true;
				load_file(fullpath, 0xFC0000);
			} else if(!f80000found && name.compare("f80000.bin")==0) {
				f80000found = true;
				int size = load_file(fullpath, 0xF80000);
				if(size == 512*1024) {
					fc0000found = true;
					break;
				}
			}
			if(fc0000found && f80000found) {
				break;
			}
		}
	}
	closedir(dir);
	if(!fc0000found) {
		PERRF(LOG_MACHINE, "Required file FC0000.BIN missing in '%s'\n", _dirname.c_str());
		throw std::exception();
	}
}

void SystemROM::load_archive(const std::string &_filename)
{
	//TODO add support for splitted 128K EPROMs
#if HAVE_LIBARCHIVE
	struct archive *ar;
	struct archive_entry *entry;
	int res;

	ar = archive_read_new();
	archive_read_support_filter_all(ar);
	archive_read_support_format_all(ar);
	res = archive_read_open_filename(ar, _filename.c_str(), 10240);
	if(res != ARCHIVE_OK) {
		PERRF(LOG_MACHINE, "Error opening ROM set '%s'\n", _filename.c_str());
		throw std::exception();
	}
	bool f80000found = false;
	bool singlerom = false;
	bool fc0000found = false;
	int64_t size;
	uint8_t *dest;
	while(archive_read_next_header(ar, &entry) == ARCHIVE_OK) {
		std::string name = archive_entry_pathname(entry);
		std::transform(name.begin(), name.end(), name.begin(), ::tolower);
		if(!fc0000found && name.compare("fc0000.bin")==0) {
			fc0000found = true;
			size = archive_entry_size(entry);
			if(size != 256*1024) {
				PERRF(LOG_MACHINE, "ROM file '%s' is of wrong size\n", archive_entry_pathname(entry));
				throw std::exception();
			}
			if(f80000found && singlerom) {
				PWARNF(LOG_V0, LOG_MACHINE, "Single ROM file F80000.BIN already loaded\n");
				break;
			}
			//read the rom
			dest = m_data + (0xFC0000 - SYS_ROM_ADDR);
			PINFOF(LOG_MACHINE, LOG_V1, "Loading %s ...\n", archive_entry_pathname(entry));
			size = archive_read_data(ar, dest, size);
			if(size <= 0) {
				PERRF(LOG_MACHINE, "Error reading ROM file '%s'\n", archive_entry_pathname(entry));
				throw std::exception();
			}
		} else if(!f80000found && name.compare("f80000.bin")==0) {
			f80000found = true;
			size = archive_entry_size(entry);
			if(size != 512*1024 && size != 256*1024) {
				PERRF(LOG_MACHINE, "ROM file '%s' is of wrong size\n", archive_entry_pathname(entry));
				throw std::exception();
			}
			if(size == 512*1024) {
				if(fc0000found) {
					PERRF(LOG_MACHINE, "ROM file FC0000.BIN already loaded\n");
					throw std::exception();
				}
				fc0000found = true;
				singlerom = true;
			}
			//read the rom
			dest = m_data + (0xF80000 - SYS_ROM_ADDR);
			PINFOF(LOG_MACHINE, LOG_V1, "Loading %s ...\n", archive_entry_pathname(entry));
			size = archive_read_data(ar, dest, size);
			if(size <= 0) {
				PERRF(LOG_MACHINE, "Error reading ROM file '%s'\n", archive_entry_pathname(entry));
				throw std::exception();
			}
		}
	}
	archive_read_free(ar);
	if(!fc0000found) {
		PERRF(LOG_MACHINE, "Required file FC0000.BIN missing in the ROM set '%s'\n", _filename.c_str());
		throw std::exception();
	}
#else
	PERRF(LOG_MACHINE, "To use a zip archive you need to enable libarchive support.\n");
	throw std::exception();
#endif
}
