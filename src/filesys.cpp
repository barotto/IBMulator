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
#include "filesys.h"
#include "utils.h"
#include "zip.h"
#ifdef _WIN32
#include "wincompat.h"
#endif
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sstream>
#include <vector>
#include <climits>
#include <libgen.h>
#include <algorithm>
#include <string.h>

void FileSys::create_dir(const char *_path)
{
	if(!file_exists(_path)) {
		PDEBUGF(LOG_V0, LOG_FS, "Creating '%s'\n", _path);
		if(::mkdir(to_native(_path).c_str()
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

	if(::stat(to_native(_path).c_str(), &sb) != 0) {
		return false;
	}

	return (S_ISDIR(sb.st_mode));
}

bool FileSys::is_file_readable(const char *_path)
{
	if(_path == nullptr) return false;
	return (::access(to_native(_path).c_str(), R_OK) == 0);
}

bool FileSys::is_file_writeable(const char *_path)
{
	if(_path == nullptr) return false;
	return (::access(to_native(_path).c_str(), W_OK) == 0);
}

bool FileSys::is_dir_writeable(const char *_path)
{
#ifdef _WIN32

	TCHAR szTempFileName[MAX_PATH];
	if(GetTempFileName(to_native(_path).c_str(), PACKAGE_NAME, 0, szTempFileName) == 0) {
		return false;
	}
	DeleteFile(szTempFileName);
	return true;

#else

	return is_file_writeable(_path);

#endif
}

bool FileSys::file_exists(const char *_path)
{
	if(_path == nullptr) return false;
	return (::access(to_native(_path).c_str(), F_OK) == 0);
}

uint64_t FileSys::get_file_size(const char *_path)
{
	if(_path == nullptr) {
		return 0;
	}
	struct stat sb;
	if(::stat(to_native(_path).c_str(), &sb) != 0) {
		return 0;
	}
	return (uint64_t)sb.st_size;
}

int FileSys::get_file_stats(const char *_path, uint64_t *_fsize, FILETIME *_mtime)
{
#ifdef _WIN32

	HANDLE hFile = CreateFile(to_native(_path).c_str(), 
			0, //dwDesiredAccess: if this parameter is zero, the application can query certain metadata
			FILE_SHARE_READ, //dwShareMode
			nullptr, //lpSecurityAttributes
			OPEN_EXISTING, //dwCreationDisposition
			FILE_FLAG_BACKUP_SEMANTICS, //dwFlagsAndAttributes: to open a directory use this
			nullptr //hTemplateFile
	);
	if(hFile == INVALID_HANDLE_VALUE) {
		return -1;
	}
	if(_fsize != nullptr) {
		ULARGE_INTEGER FileSize;
		FileSize.LowPart = GetFileSize(hFile, &FileSize.HighPart);
		if((FileSize.LowPart != INVALID_FILE_SIZE) || (GetLastError() == NO_ERROR)) {
			*_fsize = FileSize.QuadPart;
		} else {
			return -1;
		}
	}
	if(_mtime != nullptr) {
		GetFileTime(hFile, nullptr, nullptr, _mtime);
	}
	CloseHandle(hFile);

#else

	struct stat stat_buf;
	if(::stat(_path, &stat_buf)) {
		return -1;
	}
	if(_fsize != nullptr) {
		*_fsize = (uint64_t)stat_buf.st_size;
	}
	if(_mtime != nullptr) {
		*_mtime = stat_buf.st_mtime;
	}

#endif

	return 0;
}

time_t FileSys::filetime_to_time_t(const FILETIME &_ftime)
{
#ifdef _WIN32

	ULARGE_INTEGER large;
	large.LowPart = _ftime.dwLowDateTime;
	large.HighPart = _ftime.dwHighDateTime;
	return large.QuadPart / 10000000ULL - 11644473600ULL;

#else

	return _ftime;

#endif
}

std::string FileSys::get_basename(const char *_path)
{
	// POSIX's basename() version modifies the path argument
	// The GNU version returns the empty string when path has a trailing slash
	std::string path(_path);
	if(path.empty()) {
		return "";
	}
	if(path.size() > 1 && path.back() == FS_SEP[0]) {
		path.resize(path.size() - 1);
	}
	return to_utf8( basename(to_native(path.data()).data()) );
}

bool FileSys::get_path_parts(const char *_path,
		std::string &_dir, std::string &_base, std::string &_ext)
{
	auto native = to_native(_path);
	_dir = ::dirname(&native[0]);
	native = to_native(_path);
	_base = basename(&native[0]);
	_ext = "";
	const size_t period_idx = _base.rfind('.');
	if(period_idx != std::string::npos) {
		_ext = _base.substr(period_idx);
		_base.erase(period_idx);
	}
	_base = to_utf8(_base);
	_ext = to_utf8(_ext);
	std::vector<char> rpbuf(PATH_MAX);
	if(::realpath(_dir.c_str(), &rpbuf[0]) == nullptr) {
		_dir = to_utf8(_dir);
		return false;
	}
	_dir = to_utf8(&rpbuf[0]);
	return true;
}

void FileSys::get_file_parts(const char *_filename, std::string &_base, std::string &_ext)
{
	_base = _filename;
	_ext = "";
	const size_t period_idx = _base.rfind('.');
	if(period_idx != std::string::npos) {
		_ext = _base.substr(period_idx);
		_base.erase(period_idx);
	}
}

int FileSys::open(const char *_path, int _flags)
{
	return ::open(to_native(_path).c_str(), _flags);
}

int FileSys::open(const char *_path, int _flags, mode_t _mode)
{
	return ::open(to_native(_path).c_str(), _flags, _mode);
}

DIR * FileSys::opendir(const char *_path)
{
	return ::opendir(to_native(_path).c_str());
}

int FileSys::stat(const char *_path, struct stat *_buf)
{
	return ::stat(to_native(_path).c_str(), _buf);
}

int FileSys::access(const char *_path, int _mode)
{
	if(_path == nullptr) return -1;
	return ::access(to_native(_path).c_str(), _mode);
}

int FileSys::remove(const char *_path)
{
#ifdef _WIN32

	int res;
	if(is_directory(_path)) {
		res = RemoveDirectory(to_native(_path).c_str());
	} else {
		res = DeleteFile(to_native(_path).c_str());
	}
	if(res == 0) {
		return -1;
	}
	return 0;

#else

	return ::remove(to_native(_path).c_str());

#endif
}

int FileSys::mkostemp(std::string &_template, int _flags)
{
	std::string native(to_native(_template));
	int fd;
	if((fd = ::mkostemp(native.data(), _flags)) < 0) {
		return fd;
	}
	_template = to_utf8(native);
	return fd;
}

char* FileSys::realpath(const char *_path, char *_resolved)
{
	return ::realpath(to_native(_path).c_str(), _resolved);
}

std::string FileSys::get_next_filename_time(const std::string &_path)
{
	std::string dir, base, ext;
	if(!get_path_parts(_path.c_str(), dir, base, ext)) {
		return "";
	}
	time_t t = time(NULL);
	if(t != (time_t)(-1)) {
		base += str_format_time(t, "-%Y-%m-%d-%H%M%S");
	}
	std::string dest = dir + FS_SEP + base + ext;
	if(!file_exists(dest.c_str())) {
		return dest;
	}
	return get_next_filename(dir, base, ext);
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

std::string FileSys::get_next_dirname(const std::string &_basedir,
		const std::string &_basename, unsigned _limit)
{
	std::stringstream ss;
	unsigned counter = 0;
	std::string dname, dpath;
	do {
		ss.str("");
		ss << _basename;
		ss.width(4);
		ss.fill('0');
		ss << counter;
		counter++;
		dname = ss.str();
		dpath = _basedir + FS_SEP + dname;
	} while(is_directory(dpath.c_str()) && counter<_limit);
	
	if(counter >= _limit) {
		throw std::runtime_error("limit reached");
	}
	
	return dname;
}

bool FileSys::extract_file(const char *_archive, const char *_filename, const char *_extract_to)
{
	ZipFile zip(_archive);

	std::string fname = _filename;
	std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
	while(zip.read_next_entry()) {
		std::string name = zip.get_entry_name();
		std::transform(name.begin(), name.end(), name.begin(), ::tolower);
		if(name.compare(fname)==0) {
			zip.extract_entry_data(_extract_to);
			return true;
		}
	}
	return false;
}

void FileSys::copy_file(const char *_from, const char *_to)
{
	// TODO maybe use C++17 copy_file in the future?
	std::ifstream src(to_native(_from).c_str(), std::ios::binary);
	std::ofstream dst(to_native(_to).c_str(), std::ios::binary);
	dst << src.rdbuf();
}

void FileSys::rename_file(const char *_from, const char *_to)
{
	std::rename(to_native(_from).c_str(), to_native(_to).c_str());
}

bool FileSys::is_same_file(const char *_path1, const char *_path2)
{
#ifdef _WIN32

	// not going to work always but for the sake of this program this will do.
	// for the current usage failure is inconsequential.
	char buf1[PATH_MAX], buf2[PATH_MAX];
	if(!realpath(_path1, buf1)) {
		return false;
	}
	if(!realpath(_path2, buf2)) {
		return false;
	}
	return memcmp(buf1, buf2, strlen(buf1)) == 0;

#else

	struct stat path1_s, path2_s;
	if(FileSys::stat(_path1, &path1_s) != 0) {
		return false;
	}
	if(FileSys::stat(_path2, &path2_s) != 0) {
		return false;
	}
	return (path1_s.st_dev == path2_s.st_dev && path1_s.st_ino == path2_s.st_ino);

#endif
}

FILE * FileSys::fopen(const char *_filename, const char *_flags)
{
	return ::fopen(to_native(_filename).c_str(), _flags);
}

FILE * FileSys::fopen(std::string _filename, const char *_flags)
{
	return ::fopen(to_native(_filename).c_str(), _flags);
}

shared_file_ptr FileSys::make_shared_file(const char *_filename, const char *_flags)
{
	FILE * const fp = FileSys::fopen(_filename, _flags);
	return fp ? shared_file_ptr(fp, ::fclose) : shared_file_ptr();
}

unique_file_ptr FileSys::make_file(const char *_filename, const char *_flags)
{
	//unique_ptr only invokes the deleter if the pointer is non-zero
	return unique_file_ptr(FileSys::fopen(_filename, _flags), ::fclose);
}

std::ifstream FileSys::make_ifstream(const char *_path, std::ios::openmode _mode)
{
	return std::move(std::ifstream(to_native(_path), _mode));
}

std::ofstream FileSys::make_ofstream(const char *_path, std::ios::openmode _mode)
{
	return std::move(std::ofstream(to_native(_path), _mode));
}

bool FileSys::write_at(std::ofstream &_file, std::ofstream::pos_type _pos,
		const void *_buffer, std::streamsize _length)
{
	_file.seekp(_pos);
	if(_file.fail()) {
		return false;
	}
	_file.write(reinterpret_cast<const std::ofstream::char_type*>(_buffer), _length);
	if(_file.fail()) {
		return false;
	}
	return true;
}

bool FileSys::append(std::ofstream &_file, const void *_buffer,
		std::streamsize _length)
{
	_file.seekp(0, std::ios::end);
	if(_file.fail()) {
		return false;
	}
	_file.write(reinterpret_cast<const std::ofstream::char_type*>(_buffer), _length);
	if(_file.fail()) {
		return false;
	}
	return true;
}

#ifdef _WIN32

std::string FileSys::to_utf8(const std::string &_ansi_path)
{
	// MinGW readdir() returns ANSI encoded names, so input path is in ANSI encoding
	if(_ansi_path.empty()) {
		return std::string();
	}

	std::wstring widestr(_ansi_path.length()+1, 0);
	int wsz = MultiByteToWideChar(CP_ACP, 0, &_ansi_path[0], -1, &widestr[0], _ansi_path.length()+1);
	if(!wsz) {
		return str_format("conv.error.%d", GetLastError());
	}

	int nsz = WideCharToMultiByte(CP_UTF8, 0, &widestr[0], -1, 0, 0, 0, 0);
	if(!nsz) {
		return str_format("conv.error.%d", GetLastError());
	}
	std::string utf8out(nsz, 0);
	WideCharToMultiByte(CP_UTF8, 0, &widestr[0], -1, &utf8out[0], nsz, 0, 0);
	utf8out.resize(nsz - 1); // output is null-terminated

	return utf8out;
}

std::string FileSys::to_native(const std::string &_utf8_path)
{
	// input path is in UTF-8: back to ANSI, the encoding of the champions!
	if(_utf8_path.empty()) {
		return std::string();
	}

	int wsz = MultiByteToWideChar(CP_UTF8, 0, &_utf8_path[0], -1, 0, 0);
	if(!wsz) {
		return str_format("conv.error.%d", GetLastError());
	}
	std::wstring widestr(wsz, 0);
	MultiByteToWideChar(CP_UTF8, 0, &_utf8_path[0], -1, &widestr[0], wsz);
	widestr.resize(wsz - 1);

	std::string ansiout(widestr.length()+1, 0);
	int nsz = WideCharToMultiByte(CP_ACP, 0, &widestr[0], -1, &ansiout[0], widestr.length()+1, 0, 0);
	if(!nsz) {
		return str_format("conv.error.%d", GetLastError());
	}

	return ansiout;
}

#endif