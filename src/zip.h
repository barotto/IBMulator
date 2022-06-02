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

#ifndef IBMULATOR_ZIP_H
#define IBMULATOR_ZIP_H

#if HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#else
#include "miniz/miniz.h"
#endif


class ZipFile
{
public:
	ZipFile() {}
	ZipFile(const char *_archive_path);
	~ZipFile();

	void open(const char *_filepath);

	bool read_next_entry();
	std::string get_entry_name();
	int64_t get_entry_size();
	int64_t read_entry_data(uint8_t *_dest, int64_t _size);
	void extract_entry_data(const char *_dest);

private:
#if HAVE_LIBARCHIVE
	struct archive *m_archive = nullptr;
	struct archive_entry *m_cur_entry = nullptr;
#else
	bool m_is_open = false;
	mz_zip_archive m_archive;
	int m_archive_size = 0;
	mz_zip_archive_file_stat m_cur_entry;
	int m_cur_entry_index = -1;
#endif
};

#endif