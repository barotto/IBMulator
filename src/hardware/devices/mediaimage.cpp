/*
 * Copyright (C) 2002-2014  The Bochs Project
 * Copyright (C) 2015-2021  Marco Bortolin
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
#include "mediaimage.h"

#if HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif
#ifdef __linux__
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#include "wincompat.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>


// helper functions
int read_image(int fd, int64_t offset, void *buf, int count)
{
	if(lseek(fd, offset, SEEK_SET) == -1) {
		return -1;
	}
	return read(fd, buf, count);
}

int write_image(int fd, int64_t offset, void *buf, int count)
{
	if(lseek(fd, offset, SEEK_SET) == -1) {
		return -1;
	}
	return write(fd, buf, count);
}

int hdimage_open_file(const char *_pathname, int _flags, uint64_t *_fsize, FILETIME *_mtime)
{
	if(FileSys::get_file_stats(_pathname,_fsize,_mtime) < 0) {
		return -1;
	}

	int fd = FileSys::open(_pathname, _flags
#ifdef O_BINARY
			| O_BINARY
#endif
	);

	if(fd < 0) {
		return fd;
	}

	return fd;
}

int hdimage_open_temp(const char *_pathname, std::string &_template, int _flags,
		uint64_t *_fsize, FILETIME *_mtime)
{
	if(FileSys::get_file_stats(_pathname,_fsize,_mtime) < 0) {
		return -1;
	}

	int tmpfd = FileSys::mkostemp(_template, _flags
#ifdef O_BINARY
			| O_BINARY
#endif
	);
	int srcfd;
	if(tmpfd >= 0) {
		srcfd = hdimage_open_file(_pathname, O_RDONLY, _fsize, nullptr);
		if(srcfd >= 0) {
			if(!hdimage_backup_file(srcfd, tmpfd)) {
				::close(tmpfd);
				tmpfd = -1;
			}
			::close(srcfd);
		} else {
			::close(tmpfd);
			tmpfd = -1;
		}
	}

	return tmpfd;
}

int hdimage_detect_image_mode(const char *pathname)
{
	int result = HDIMAGE_MODE_UNKNOWN;
	uint64_t image_size = 0;

	int fd = hdimage_open_file(pathname, O_RDONLY, &image_size, nullptr);
	if(fd < 0) {
		return result;
	}

	if(FlatMediaImage::check_format(fd, image_size) == HDIMAGE_FORMAT_OK) {
		result = HDIMAGE_MODE_FLAT;
	}
	::close(fd);

	return result;
}

// if return_time==0, this returns the fat_date, else the fat_time
uint16_t fat_datetime(FILETIME time, bool return_time)
{
#ifndef _WIN32

	struct tm* t;
	struct tm t1;

	t = &t1;
	localtime_r(&time, t);
	if(return_time) {
		return htod16((t->tm_sec/2) | (t->tm_min<<5) | (t->tm_hour<<11));
	}
	return htod16((t->tm_mday) | ((t->tm_mon+1)<<5) | ((t->tm_year-80)<<9));

#else

	SYSTEMTIME gmtsystime, systime;
	TIME_ZONE_INFORMATION tzi;

	FileTimeToSystemTime(&time, &gmtsystime);
	GetTimeZoneInformation(&tzi);
	SystemTimeToTzSpecificLocalTime(&tzi, &gmtsystime, &systime);
	if(return_time) {
		return htod16((systime.wSecond/2) | (systime.wMinute<<5) | (systime.wHour<<11));
	}
	return htod16((systime.wDay) | (systime.wMonth<<5) | ((systime.wYear-1980)<<9));

#endif
}

bool hdimage_backup_file(int _from_fd, int _backup_fd)
{
	char *buf;
	off_t offset;
	int nread, size;
	bool ret = true;

	offset = 0;
	size = 0x20000;
	buf = (char*)malloc(size);
	if(buf == nullptr) {
		return false;
	}
	while((nread = read_image(_from_fd, offset, buf, size)) > 0) {
		if(write_image(_backup_fd, offset, buf, nread) < 0) {
			ret = false;
			break;
		}
		if(nread < size) {
			break;
		}
		offset += size;
	};
	if(nread < 0) {
		ret = false;
	}
	free(buf);

	return ret;
}

bool hdimage_backup_file(int fd, const char *backup_fname)
{
	int backup_fd = FileSys::open(backup_fname, O_RDWR | O_CREAT | O_TRUNC
#ifdef O_BINARY
	| O_BINARY
#endif
	, S_IWUSR | S_IRUSR
#ifdef S_IRGRP
	| S_IRGRP | S_IWGRP
#endif
	);
	if(backup_fd >= 0) {
		bool ret = hdimage_backup_file(fd, backup_fd);
		::close(backup_fd);
		return ret;
	}
	return 0;
}


bool hdimage_copy_file(const char *src, const char *dst)
{
#ifdef _WIN32

	return (bool)CopyFile(FileSys::to_native(src).c_str(), FileSys::to_native(dst).c_str(), FALSE);

#elif defined(__linux__)

	pid_t pid, ws;

	if((src == nullptr) || (dst == nullptr)) {
		return 0;
	}

	if(!(pid = fork())) {
		execl("/bin/cp", "/bin/cp", src, dst, (char *)0);
		return 0;
	}
	wait(&ws);
	if(!WIFEXITED(ws)) {
		return -1;
	}
	return (WEXITSTATUS(ws) == 0);

#else

	int fd1, fd2;
	char *buf;
	off_t offset;
	int nread, size;
	bool ret = 1;

	fd1 = ::open(src, O_RDONLY
		#ifdef O_BINARY
		| O_BINARY
		#endif
	);
	if(fd1 < 0) {
		return 0;
	}
	fd2 = ::open(dst, O_RDWR | O_CREAT | O_TRUNC
		#ifdef O_BINARY
		| O_BINARY
		#endif
		, S_IWUSR | S_IRUSR | S_IRGRP | S_IWGRP
	);
	if(fd2 < 0) {
		return 0;
	}
	offset = 0;
	size = 0x20000;
	buf = (char*)malloc(size);
	if(buf == nullptr) {
		::close(fd1);
		::close(fd2);
		return 0;
	}
	while ((nread = read_image(fd1, offset, buf, size)) > 0) {
		if(write_image(fd2, offset, buf, nread) < 0) {
			ret = 0;
			break;
		}
		if(nread < size) {
			break;
		}
		offset += size;
	};
	if(nread < 0) {
		ret = 0;
	}
	free(buf);
	::close(fd1);
	::close(fd2);
	return ret;

#endif
}


/*******************************************************************************
 * base class MediaImage
 */

MediaImage::MediaImage()
:
m_size(0)
{
#ifdef _WIN32
	m_mtime = {0,0};
#else
	m_mtime = 0;
#endif
}

int MediaImage::open(const char* _pathname)
{
	return open(_pathname, O_RDWR);
}

uint32_t MediaImage::get_capabilities()
{
	return (m_geometry.cylinders == 0) ? HDIMAGE_AUTO_GEOMETRY : 0;
}

uint32_t MediaImage::get_timestamp()
{
	return (fat_datetime(m_mtime, 1) | (fat_datetime(m_mtime, 0) << 16));
}


/*******************************************************************************
 * FlatMediaImage
 */

FlatMediaImage::FlatMediaImage()
:
m_fd(-1)
{
	m_geometry = {0,0,0,0,0};
}

FlatMediaImage::~FlatMediaImage()
{
	FlatMediaImage::close();
}

int FlatMediaImage::open(const char* _pathname, int _flags)
{
	if((m_fd = hdimage_open_file(_pathname, _flags, &m_size, &m_mtime)) < 0) {
		return -1;
	}
	if(!is_valid()) {
		close();
		return -1;
	}
	m_pathname = _pathname;
	return m_fd;
}

int FlatMediaImage::open_temp(const char* _pathname, std::string &_template)
{
	if((m_fd = hdimage_open_temp(_pathname, _template, O_RDWR, &m_size, &m_mtime)) < 0) {
		return -1;
	}
	if(!is_valid()) {
		FileSys::remove(_template.c_str());
		close();
		return -1;
	}
	m_pathname = _template;
	return m_fd;
}

void FlatMediaImage::close()
{
	if(m_fd > -1) {
		::close(m_fd);
		m_fd = -1;
	}
	m_size = 0;
}

int64_t FlatMediaImage::lseek(int64_t _offset, int _whence)
{
	return (int64_t)::lseek(m_fd, (off_t)_offset, _whence);
}

ssize_t FlatMediaImage::read(void *_buf, size_t _count)
{
	return ::read(m_fd, (char*)_buf, _count);
}

ssize_t FlatMediaImage::write(const void *_buf, size_t _count)
{
	return ::write(m_fd, (char*)_buf, _count);
}

int FlatMediaImage::check_format(int _fd, uint64_t _imgsize)
{
	char buffer[512];

	if((_imgsize <= 0) || ((_imgsize % 512) != 0)) {
		return HDIMAGE_SIZE_ERROR;
	} else if(read_image(_fd, 0, buffer, 512) < 0) {
		return HDIMAGE_READ_ERROR;
	} else {
		return HDIMAGE_FORMAT_OK;
	}
}

bool FlatMediaImage::save_state(const char *_backup_fname)
{
	return hdimage_backup_file(m_fd, _backup_fname);
}

void FlatMediaImage::restore_state(const char *_backup_fname)
{
	close();
	if(!hdimage_copy_file(_backup_fname, m_pathname.c_str())) {
		PERRF(LOG_HDD, "Failed to restore image '%s'\n", m_pathname.c_str());
		throw std::exception();
	}
	if(MediaImage::open(m_pathname.c_str()) < 0) {
		PERRF(LOG_HDD, "Failed to open restored image '%s'\n", m_pathname.c_str());
		throw std::exception();
	}
}

void FlatMediaImage::create(const char* _pathname, unsigned _sectors)
{
	if(_sectors == 0) {
		throw std::exception();
	}
	std::ofstream ofs = FileSys::make_ofstream(_pathname, std::ios::binary | std::ios::out);
	if(!ofs.is_open()) {
		PERRF(LOG_HDD, "Cannot create '%s'. Does the destination directory exist? Is it writible?\n", _pathname);
		throw std::exception();
	}
	unsigned bytes = _sectors * 512;
	ofs.seekp(bytes - 1);
	ofs.write("", 1);
	if(!ofs.good()) {
		PERRF(LOG_HDD, "Cannot pre-allocate %u bytes for '%s'. Check the available space on the destination drive.\n",
			bytes, _pathname);
		throw std::exception();
	}
}

bool FlatMediaImage::is_valid()
{
	PINFOF(LOG_V2, LOG_HDD, "image size: %llu\n", m_size);

	if(m_size <= 0) {
		PERRF(LOG_HDD, "Size of the disk image not detected / invalid\n");
		return false;
	}

	unsigned sectors = m_geometry.cylinders * m_geometry.heads * m_geometry.spt;
	if(m_size != 512 * sectors) {
		PERRF(LOG_HDD, "The size of the disk image is wrong: %d bytes found, %d bytes expected\n",
				m_size, (512 * sectors));
		return false;
	}

	return true;
}
