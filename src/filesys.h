/*
 * Copyright (C) 2015-2023  Marco Bortolin
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

#ifndef IBMULATOR_FILESYS_H
#define IBMULATOR_FILESYS_H

#ifndef _WIN32
	#define FILETIME time_t
	#define FS_PATH_MIN 1
#else
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
	#define FS_PATH_MIN 3
#endif
#include <dirent.h>
#include <memory>
#include <utility>

typedef std::unique_ptr<FILE, int (*)(FILE *)> unique_file_ptr;
typedef std::shared_ptr<FILE> shared_file_ptr;

class FileSys
{
public:
#ifdef _WIN32
	static std::string to_utf8(const std::string &_path);
	static std::string to_native(const std::string &_path);
#else
	static std::string to_utf8(std::string _path) { return _path; }
	static std::string to_native(std::string _path) { return _path; }
#endif
	static int open(const char *_filename, int _flags);
	static int open(const char *_filename, int _flags, mode_t _mode);
	static DIR* opendir(const char *_path);
	static int stat(const char *_path, struct stat *_buf);
	static int access(const char *_path, int _mode);
	static int remove(const char *_path);
	static int mkostemp(std::string &_template, int _flags);
	static char* realpath(const char *_path, char *_resolved);
	static std::string realpath(const char *_path);

 	static std::string get_next_filename(const std::string &_dir,
			const std::string &_basename, const std::string &_ext);
 	static std::string get_next_filename_time(const std::string &_path);
	static std::string get_next_dirname(const std::string &_basedir,
			const std::string &_basename, unsigned _limit = 10000);
	static bool is_absolute(const char *_path, int _len);
	static bool is_directory(const char *_path);
	static bool file_exists(const char *_path);
	static bool is_file_readable(const char *_path);
	static bool is_file_writeable(const char *_path);
	static bool is_dir_writeable(const char *_path);
	static uint64_t get_file_size(const char *_path);
	static void create_dir(const char *_path);
	static int get_file_stats(const char *_path, uint64_t *_fsize, FILETIME *_mtime);
	static std::string get_basename(const char *_path);
	static std::string get_file_ext(const std::string _path);
	static std::string get_path_dir(const char *_path);
	static bool get_path_parts(const char *_path,
			std::string &_dir, std::string &_base, std::string &_ext);
	static bool get_path_parts(const char *_path,
			std::string &_dir, std::string &_filename);
	static void get_file_parts(const char *_filename, std::string &_base, std::string &_ext);
	static bool extract_file(const char *_archive, const char *_filename,
			const char *_extract_to);
	static void copy_file(const char *_from, const char *_to);
	static void rename_file(const char *_from, const char *_to);
	static bool is_same_file(const char *_path1, const char *_path2);

	static FILE* fopen(const char *_filename, const char *_flags);
	static FILE* fopen(std::string _filename, const char *_flags);
	static shared_file_ptr make_shared_file(const char *_filename, const char *_flags);
	static unique_file_ptr make_file(const char *_filename, const char *_flags);
	static unique_file_ptr make_tmpfile();

	static std::ifstream make_ifstream(const char *_path, std::ios::openmode _mode = std::ios::in);
	static std::ofstream make_ofstream(const char *_path, std::ios::openmode _mode = std::ios::out);
	static bool write_at(std::ofstream &_file, std::ofstream::pos_type _pos, const void *_buffer, std::streamsize _length);
	static bool append(std::ofstream &_file, const void *_buffer, std::streamsize _length);

	static time_t filetime_to_time_t(const FILETIME &_ftime);
};

#endif
