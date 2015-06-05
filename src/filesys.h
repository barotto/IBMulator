/*
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IBMULATOR_FILESYS_H
#define IBMULATOR_FILESYS_H

#ifndef _WIN32
	#define FILETIME time_t
#else
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
#endif

#include <memory>

typedef std::unique_ptr<FILE, int (*)(FILE *)> unique_file_ptr;
typedef std::shared_ptr<FILE> shared_file_ptr;

class FileSys
{
public:
	static std::string get_next_filename(const std::string &_dir,
			const std::string &_basename, const std::string &_ext);

	static bool is_directory(const char *_path);
	static bool file_exists(const char *_path);
	static bool is_file_readable(const char *_path);
	static bool is_file_writeable(const char *_path);
	static uint64_t get_file_size(const char *_path);
	static void create_dir(const char *_path);
	static int get_file_stats(const char *_path, uint64_t *_fsize, FILETIME *_mtime);
	static const char* get_temp_dir();
	static bool get_path_parts(const char *_path,
			std::string &_dir, std::string &_base, std::string &_ext);
	static bool extract_file(const char *_archive, const char *_filename,
			const char *_extract_to);

	static shared_file_ptr make_shared_file(const char *_filename, const char *_flags);
	static unique_file_ptr make_file(const char *_filename, const char *_flags);
};

#endif
