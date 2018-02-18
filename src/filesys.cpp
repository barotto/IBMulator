/*
 * Copyright (C) 2015, 2016  Marco Bortolin
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
#include "filesys.h"
#ifdef _WIN32
#include "wincompat.h"
#endif
#include <unistd.h>
#include <sys/stat.h>
#include <sstream>
#include <vector>
#include <climits>
#include <libgen.h>
#include <algorithm>
#if HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif


void FileSys::create_dir(const char *_path)
{
	if(!file_exists(_path)) {
		PDEBUGF(LOG_V0, LOG_FS, "Creating '%s'\n", _path);
		if(mkdir(_path
#ifndef _WIN32
			, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH
#endif
		) != 0) {
			PERRF(LOG_FS, "Unable to create '%s'\n", _path);
			throw std::exception();
		}
	}
}

bool FileSys::is_directory(const char *_path)
{
	if(_path == nullptr) {
		return false;
	}

	struct stat sb;

	if(stat(_path, &sb) != 0) {
		return false;
	}

	return (S_ISDIR(sb.st_mode));
}

bool FileSys::is_file_readable(const char *_path)
{
	if(_path == nullptr) return false;
	return (access(_path, R_OK) == 0);
}

bool FileSys::is_file_writeable(const char *_path)
{
	if(_path == nullptr) return false;
	return (access(_path, W_OK) == 0);
}

bool FileSys::file_exists(const char *_path)
{
	if(_path == nullptr) return false;
	return (access(_path, F_OK) == 0);
}

uint64_t FileSys::get_file_size(const char *_path)
{
	if(_path == nullptr) {
		return 0;
	}
	struct stat sb;
	if(stat(_path, &sb) != 0) {
		return 0;
	}
	return (uint64_t)sb.st_size;
}

int FileSys::get_file_stats(const char *_path, uint64_t *_fsize, FILETIME *_mtime)
{
#ifdef _WIN32
	if(_fsize != nullptr) {
		HANDLE hFile = CreateFile(_path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, nullptr);
		if(hFile != INVALID_HANDLE_VALUE) {
			ULARGE_INTEGER FileSize;
			FileSize.LowPart = GetFileSize(hFile, &FileSize.HighPart);
			if(_mtime != nullptr) {
				GetFileTime(hFile, nullptr, nullptr, _mtime);
			}
			CloseHandle(hFile);
			if((FileSize.LowPart != INVALID_FILE_SIZE) || (GetLastError() == NO_ERROR)) {
				*_fsize = FileSize.QuadPart;
			} else {
				return -1;
			}
		} else {
			return -1;
		}
	}
#else
	if(_fsize != nullptr) {
		struct stat stat_buf;
		if(stat(_path, &stat_buf)) {
			return -1;
		}

		*_fsize = (uint64_t)stat_buf.st_size;

		if(_mtime != nullptr) {
			*_mtime = stat_buf.st_mtime;
		}
	}
#endif

	return 0;
}

bool FileSys::get_path_parts(const char *_path,
		std::string &_dir, std::string &_base, std::string &_ext)
{
	std::vector<char> rpbuf(PATH_MAX);
	if(realpath(_path, &rpbuf[0]) == nullptr) {
		return false;
	}
	std::vector<char> dirbuf(rpbuf);
	_dir = dirname(&dirbuf[0]);
	_base = basename(&rpbuf[0]);
	_ext = "";
	const size_t period_idx = _base.rfind('.');
	if(period_idx != std::string::npos)	{
		_ext = _base.substr(period_idx);
		_base.erase(period_idx);
	}

	return true;
}

std::string FileSys::get_next_filename(const std::string &_dir,
		const std::string &_basename, const std::string &_ext)
{
	std::stringstream ss;
	int counter = 0;
	std::string fname;
	do {
		ss.str("");
		ss << _dir << FS_SEP << _basename;
		ss.width(4);
		ss.fill('0');
		ss << counter;
		counter++;
		fname = ss.str() + _ext;
	} while(file_exists(fname.c_str()) && counter<10000);
	if(counter>=10000) {
		return "";
	}
	return fname;
}

bool FileSys::extract_file(const char *_archive, const char *_filename, const char *_extract_to)
{
#if HAVE_LIBARCHIVE
	struct archive *ar;
	struct archive_entry *entry;
	int res;

	ar = archive_read_new();
	archive_read_support_filter_all(ar);
	archive_read_support_format_all(ar);
	res = archive_read_open_filename(ar, _archive, 10240);
	if(res != ARCHIVE_OK) {
		PERRF(LOG_FS, "Error opening archive '%s'\n", _archive);
		archive_read_free(ar);
		throw std::exception();
	}

	bool found = false;
	std::string fname = _filename;
	std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
	while(archive_read_next_header(ar, &entry) == ARCHIVE_OK) {
		std::string name = archive_entry_pathname(entry);
		std::transform(name.begin(), name.end(), name.begin(), ::tolower);
		if(name.compare(fname)==0) {
			found = true;
			archive_entry_set_pathname(entry, _extract_to);
			if(archive_read_extract(ar, entry, 0) != 0) {
				PERRF(LOG_FS, "Error extracting '%s' from archive: %s\n",
						_filename,
						archive_error_string(ar));
				archive_read_free(ar);
				throw std::exception();
			}
		}
	}
	archive_read_free(ar);
	return found;
#else
	return throw std::exception();
#endif
}

shared_file_ptr FileSys::make_shared_file(const char *_filename, const char *_flags)
{
	FILE * const fp = fopen(_filename, _flags);
	return fp ? shared_file_ptr(fp, fclose) : shared_file_ptr();
}

unique_file_ptr FileSys::make_file(const char *_filename, const char *_flags)
{
	//unique_ptr only invokes the deleter if the pointer is non-zero
	return unique_file_ptr(fopen(_filename, _flags), fclose);
}
