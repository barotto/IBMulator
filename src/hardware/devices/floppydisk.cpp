/*
 * Copyright (C) 2002-2014  The Bochs Project
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
#include "floppydisk.h"
#include "filesys.h"
#include "utils.h"

#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>
#ifdef _WIN32
#include "wincompat.h"
#endif


const std::map<std::string, unsigned> FloppyDisk::disk_names_350 = {
	{ "720K",  FLOPPY_720K },
	{ "1.44M", FLOPPY_1_44 },
	{ "2.88M", FLOPPY_2_88 }
};

const std::map<std::string, unsigned> FloppyDisk::disk_names_525 = {
	{ "160K", FLOPPY_160K },
	{ "180K", FLOPPY_180K },
	{ "320K", FLOPPY_320K },
	{ "360K", FLOPPY_360K },
	{ "1.2M", FLOPPY_1_2  }
};

#ifdef O_BINARY
#define RDONLY O_RDONLY | O_BINARY
#define RDWR O_RDWR | O_BINARY
#else
#define RDONLY O_RDONLY
#define RDWR O_RDWR
#endif

bool FloppyDisk::open(unsigned _type, const char *_path, bool _write_prot)
{
	path = _path;

	if(_type == FLOPPY_NONE) {
		return false;
	}

	wprot = _write_prot;

	// open media file
	if(wprot) {
		fd = FileSys::open(_path, RDONLY);
	} else {
		fd = FileSys::open(_path, RDWR);
	}

	if(!wprot && (fd < 0)) {
		PINFOF(LOG_V1, LOG_FDC, "tried to open '%s' read/write: %s\n", _path, strerror(errno));
		// try opening the file read-only
		wprot = true;
		fd = FileSys::open(_path, RDONLY);
		if(fd < 0) {
			// failed to open read-only too
			PINFOF(LOG_V1, LOG_FDC, "tried to open '%s' read only: %s\n", _path, strerror(errno));
			type = _type;
			return false;
		}
	}

	struct stat stat_buf;
	int ret = ::fstat(fd, &stat_buf);

	if(ret) {
		PERRF(LOG_FDC, "fstat() floppy image file returns error: %s\n", strerror(errno));
		return false;
	}

	if(S_ISREG(stat_buf.st_mode)) {
		// regular file
		switch(_type) {
			case FLOPPY_160K: // 160K 5.25"
			case FLOPPY_180K: // 180K 5.25"
			case FLOPPY_320K: // 320K 5.25"
			case FLOPPY_360K: // 360K 5.25"
			case FLOPPY_720K: // 720K 3.5"
			case FLOPPY_1_2:  // 1.2M 5.25"
			case FLOPPY_2_88: // 2.88M 3.5"
				type    = _type;
				tracks  = std_types[_type].trk;
				heads   = std_types[_type].hd;
				spt     = std_types[_type].spt;
				sectors = std_types[_type].sectors;
				if(stat_buf.st_size > (int)(sectors * 512)) {
					PDEBUGF(LOG_V0, LOG_FDC, "size of file '%s' (%lu) too large for selected type\n",
							_path, (unsigned long) stat_buf.st_size);
					return false;
				}
				break;
			default: // 1.44M 3.5"
				type = _type;
				if(stat_buf.st_size <= 1474560) {
					tracks = std_types[_type].trk;
					heads  = std_types[_type].hd;
					spt    = std_types[_type].spt;
				} else if(stat_buf.st_size == 1720320) {
					spt    = 21;
					tracks = 80;
					heads  = 2;
				} else if(stat_buf.st_size == 1763328) {
					spt    = 21;
					tracks = 82;
					heads  = 2;
				} else if(stat_buf.st_size == 1884160) {
					spt    = 23;
					tracks = 80;
					heads  = 2;
				} else {
					PDEBUGF(LOG_V0, LOG_FDC, "file '%s' of unknown size %lu\n",
							_path, (unsigned long) stat_buf.st_size);
					return false;
				}
				sectors = heads * tracks * spt;
				break;
		}
		return (sectors > 0); // success
	}

	// unknown file type
	PDEBUGF(LOG_V0, LOG_FDC, "unknown mode type\n");
	return false;
}

void FloppyDisk::close()
{
	if(fd >= 0) {
		::close(fd);
		fd = -1;
	}
}

void FloppyDisk::read_sector(uint32_t _from_offset, uint8_t *_to_buffer, uint32_t _bytes)
{
	if(lseek(fd, _from_offset, SEEK_SET) < 0) {
		throw std::runtime_error(str_format("cannot perform lseek() to %d", _from_offset).c_str());
	}

	ssize_t ret = ::read(fd, _to_buffer, _bytes);

	if(ret < _bytes) {
		if(ret > 0) {
			memset(_to_buffer + ret, 0, _bytes - ret);
			throw std::runtime_error(str_format("partial read() on floppy image returns %u/%u",
					ret, _bytes).c_str());
		} else {
			memset(_to_buffer, 0, _bytes);
			throw std::runtime_error("read() on floppy image returns 0");
		}
	}
}

void FloppyDisk::write_sector(uint32_t _to_offset, const uint8_t *_from_buffer, uint32_t _bytes)
{
	if(lseek(fd, _to_offset, SEEK_SET) < 0) {
		throw std::runtime_error(str_format("cannot perform lseek() to %d", _to_offset).c_str());
	}

	if(wprot) {
		throw std::runtime_error("cannot write a write protected floppy");
	}

	ssize_t ret = ::write(fd, _from_buffer, _bytes);

	if(ret < _bytes) {
		throw std::runtime_error("cannot perform write() on floppy image file");
	}
}