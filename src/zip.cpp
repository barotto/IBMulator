/*
 * Copyright (C) 2022-2023  Marco Bortolin
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
#include "zip.h"
#include "filesys.h"
#include "utils.h"

ZipFile::ZipFile(const char *_archive_path)
{
	open(_archive_path);
}

#if HAVE_LIBARCHIVE

ZipFile::~ZipFile()
{
	if(m_archive) {
		archive_read_free(m_archive);
	}
}

void ZipFile::open(const char *_archive_path)
{
	if(m_archive) {
		archive_read_free(m_archive);
	}
	m_archive = archive_read_new();
	m_cur_entry = nullptr;
	archive_read_support_filter_all(m_archive);
	archive_read_support_format_all(m_archive);
	if(archive_read_open_filename(m_archive, FileSys::to_native(_archive_path).c_str(), 10240) != ARCHIVE_OK) {
		archive_read_free(m_archive);
		throw std::runtime_error("Error opening archive");
	}
}

bool ZipFile::read_next_entry()
{
	if(!m_archive) {
		throw std::runtime_error("Archive is not open");
	}
	return archive_read_next_header(m_archive, &m_cur_entry) == ARCHIVE_OK;
}

std::string ZipFile::get_entry_name()
{
	if(!m_cur_entry) {
		throw std::runtime_error("Invalid entry");
	}
	return archive_entry_pathname(m_cur_entry);
}

int64_t ZipFile::get_entry_size()
{
	if(!m_cur_entry) {
		throw std::runtime_error("Invalid entry");
	}
	return archive_entry_size(m_cur_entry);
}

int64_t ZipFile::read_entry_data(uint8_t *_dest, int64_t _size)
{
	if(!m_cur_entry) {
		throw std::runtime_error("Invalid entry");
	}
	return archive_read_data(m_archive, _dest, _size);
}

void ZipFile::extract_entry_data(const char *_dest)
{
	if(!m_cur_entry) {
		throw std::runtime_error("Invalid entry");
	}
	archive_entry_set_pathname(m_cur_entry, FileSys::to_native(_dest).c_str());
	if(archive_read_extract(m_archive, m_cur_entry, 0) != 0) {
		throw std::runtime_error(str_format("Error extracting file from archive: %s",
				archive_error_string(m_archive)).c_str());
	}
}

#else

ZipFile::ZipFile()
{
	mz_zip_zero_struct(&m_archive);
	memset(&m_cur_entry, 0, sizeof(m_cur_entry));
}

ZipFile::~ZipFile()
{
	if(m_is_open) {
		mz_zip_reader_end(&m_archive);
	}
}

void ZipFile::open(const char *_archive_path)
{
	if(m_is_open) {
		mz_zip_reader_end(&m_archive);
		m_is_open = false;
	}
	mz_zip_zero_struct(&m_archive);
	m_cur_entry_index = -1;
	if(!mz_zip_reader_init_file(&m_archive, FileSys::to_native(_archive_path).c_str(), 0)) {
		throw std::runtime_error("Error opening archive");
	}
	m_archive_size = mz_zip_reader_get_num_files(&m_archive);
	m_is_open = true;
}

bool ZipFile::read_next_entry()
{
	if(!m_is_open) {
		throw std::runtime_error("Archive is not open");
	}
	if(m_cur_entry_index >= m_archive_size-1) {
		return false;
	}
	m_cur_entry_index++;
	return mz_zip_reader_file_stat(&m_archive, m_cur_entry_index, &m_cur_entry);
}

std::string ZipFile::get_entry_name()
{
	if(m_cur_entry_index < 0) {
		throw std::runtime_error("Invalid entry");
	}
	return m_cur_entry.m_filename;
}

int64_t ZipFile::get_entry_size()
{
	if(m_cur_entry_index < 0) {
		throw std::runtime_error("Invalid entry");
	}
	return m_cur_entry.m_uncomp_size;
}

int64_t ZipFile::read_entry_data(uint8_t *_dest, int64_t _size)
{
	if(m_cur_entry_index < 0) {
		throw std::runtime_error("Invalid entry");
	}
	if(mz_zip_reader_extract_to_mem(&m_archive, m_cur_entry_index, _dest, _size, 0)) {
		return _size;
	}
	return 0;
}

void ZipFile::extract_entry_data(const char *_dest)
{
	if(m_cur_entry_index < 0) {
		throw std::runtime_error("Invalid entry");
	}
	size_t size;
	void *p = mz_zip_reader_extract_to_heap(&m_archive, m_cur_entry_index, &size, 0);
	if(!p) {
		throw std::runtime_error("Data extraction failed");
	}
	auto f = FileSys::make_file(_dest, "wb");
	if(!f) {
		free(p);
		throw std::runtime_error("Cannot create file");
	}
	if(fwrite(p, size, 1, f.get()) != 1) {
		free(p);
		throw std::runtime_error("Cannot write to file");
	}
	free(p);
}

#endif