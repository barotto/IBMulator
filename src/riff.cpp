/*
 * Copyright (C) 2020  Marco Bortolin
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
#include "riff.h"
#include "utils.h"
#include <cstring>


RIFFFile::RIFFFile()
:
m_file(nullptr)
{
	reset();
	size_check<RIFFHeader,12>();
	size_check<RIFFChunkHeader,8>();
}

RIFFFile::~RIFFFile()
{
	if(is_open()) {
		close_file();
	}
}

void RIFFFile::reset()
{
	m_lists_w = std::stack<long int>();
	m_write_mode = false;
	m_write_size = 0;
	m_chunk_rpos = 0;
	m_chunk_wstart = false;
	m_chunk_wpos = 0;
}

RIFFHeader RIFFFile::open_read(const char *_filepath)
{
	assert(!is_open());

	m_write_mode = false;
	
	m_file = fopen(_filepath, "rb");
	if(!m_file) {
		throw std::runtime_error("unable to open for reading");
	}
	if(fread(&m_header, sizeof(RIFFHeader), 1, m_file) != 1) {
		throw std::runtime_error("unable to read the header");
	}
	if(m_header.RIFF != FOURCC_RIFF) {
		throw std::runtime_error("not a RIFF file");
	}
	return m_header;
}

void RIFFFile::open_write(const char *_filepath, uint32_t _file_type)
{
	assert(!is_open());

	m_file = fopen(_filepath, "wb");
	if(!m_file) {
		throw std::runtime_error("unable to open for writing");
	}

	m_header.RIFF     = FOURCC_RIFF;
	m_header.fileSize = 0;
	m_header.fileType = _file_type;
	
	if(fwrite(&m_header, sizeof(RIFFHeader), 1, m_file) != 1) {
		throw std::runtime_error("unable to write RIFF header");
	}
	
	m_write_mode = true;
	m_write_size = sizeof(RIFFHeader);
}

uint32_t RIFFFile::file_size() const
{
	if(m_write_mode) {
		return m_write_size;
	} else {
		return m_header.fileSize + 8;
	}
}

void RIFFFile::close()
{
	assert(is_open());
	if(is_open_write()) {
		write_end();
	}
	close_file();
}

void RIFFFile::close_file() noexcept
{
	if(is_open()) {
		reset();
		fclose(m_file);
	}
	m_file = nullptr;
}

void RIFFFile::read(void *_buffer, uint32_t _size)
{
	assert(is_open_read());
	assert(_buffer);
	assert(_size);
	
	if(fread(_buffer, _size, 1, m_file) != 1) {
		throw std::runtime_error("unable to read data");
	}
}

void RIFFFile::write(const void *_data, uint32_t _len)
{
	assert(is_open_write());
	assert(_data);
	assert(_len);

	if(fwrite(_data, _len, 1, m_file) != 1) {
		throw std::runtime_error("unable to write data");
	}
	
	m_write_size += _len;
}

RIFFChunkHeader RIFFFile::read_chunk_header()
{
	assert(is_open_read());
	
	m_chunk_rpos = ftell(m_file);
	
	if(fread(&m_chunk_rhead, sizeof(RIFFChunkHeader), 1, m_file) != 1) {
		throw std::runtime_error("unable to read chunk header");
	}
	
	return m_chunk_rhead;
}

std::vector<uint8_t> RIFFFile::read_chunk_data() const
{
	assert(is_open_read());
	assert(m_chunk_rpos);
	
	std::vector<uint8_t> data;

	if(m_chunk_rhead.chunkSize == 0) {
		return data;
	}

	read_rewind_chunk(ChunkPosition::DATA);
	
	data.resize(m_chunk_rhead.chunkSize);

	size_t bytes = fread(&data[0], 1, m_chunk_rhead.chunkSize, m_file);
	if(bytes == 0) {
		throw std::runtime_error("unable to read");
	}
	if(bytes < m_chunk_rhead.chunkSize) {
		throw std::runtime_error("the file is of wrong size");
	}
	if(m_chunk_rhead.chunkSize & 1) {
		// chunkSize does not include the padding
		if(fseek(m_file, 1, SEEK_CUR) != 0) {
			throw std::runtime_error("error while skipping chunk pad");
		}
	}
	
	return data;
}

void RIFFFile::read_rewind_chunk(ChunkPosition _pos) const
{
	assert(is_open_read());
	assert(m_chunk_rpos);
	
	long int offset = m_chunk_rpos;
	if(_pos == ChunkPosition::DATA) {
		offset += sizeof(RIFFChunkHeader);
		if(offset < 0) {
			throw std::runtime_error("offset overflow");
		}
	}
	if(fseek(m_file, offset, SEEK_SET) != 0) {
		throw std::runtime_error("unable to find chunk header");
	}
}

void RIFFFile::read_skip_chunk() const
{
	long int data_start = m_chunk_rpos + sizeof(RIFFChunkHeader);
	long int ckoff = data_start + get_ckdata_size(m_chunk_rhead, data_start);

	if(fseek(m_file, ckoff, SEEK_SET) != 0) {
		throw std::runtime_error("unable to find next chunk");
	}
}

RIFFChunkHeader RIFFFile::read_find_chunk(uint32_t _code)
{
	assert(is_open_read());
	
	//
	// the file cursor must already be at the start of a chunk header
	//
	
	RIFFChunkHeader header = read_chunk_header();
	
	while(header.chunkID != _code) {
		long int offset = get_ckdata_size(header);
		if(fseek(m_file, offset, SEEK_CUR) != 0) {
			throw std::runtime_error("invalid chunk");
		}
		try {
			header = read_chunk_header();
		} catch(...) {
			throw std::runtime_error("unable to find chunk");
		}
	}
	return header;
}

long int RIFFFile::write_list_start(uint32_t _code)
{
	assert(is_open_write());

	if(m_write_size > (RIFF_MAX_FILESIZE - sizeof(RIFFListHeader))) {
		throw std::runtime_error("file too big");
	}
	
	long int curpos = get_cur_pos();

	RIFFListHeader hdr;
	hdr.LIST = FOURCC_LIST;
	hdr.listSize = 0;
	hdr.listType = _code;
	
	if(fwrite(&hdr, sizeof(RIFFListHeader), 1, m_file) != 1) {
		throw std::runtime_error("unable to write LIST header");
	}
	
	m_lists_w.push(curpos);
	m_write_size += sizeof(RIFFListHeader);
	
	// return the lists's data position
	return get_cur_pos();
}

void RIFFFile::write_list_end()
{
	assert(is_open_write());
	assert(!m_lists_w.empty());
	
	long int curpos = get_cur_pos();
	long int listpos = m_lists_w.top();
	long int datastart = listpos + 8;
	m_lists_w.pop();
	
	assert(curpos >= datastart);
	
	uint64_t size = curpos - datastart;
	if(is_offset_overflow(datastart, size)) {
		throw std::runtime_error("file too big");
	}
	
	// move to listSize field
	fseek(m_file, listpos + 4, SEEK_SET);
	
	// update listSize field
	uint32_t s32 = size;
	if(fwrite(&s32, 4, 1, m_file) != 1) {
		throw std::runtime_error("unable to write LIST header");
	}
	
	// move back where we were (should be the end of file but not necessarily)
	fseek(m_file, curpos, SEEK_SET);
}

long int RIFFFile::write_chunk(uint32_t _code, const void *_data, uint32_t _len)
{
	long int data_pos = write_chunk_start(_code);
	write_chunk_data(_data, _len);
	write_chunk_end();
	return data_pos;
}

long int RIFFFile::write_chunk_start(uint32_t _code)
{
	assert(is_open_write());

	if(m_write_size > (RIFF_MAX_FILESIZE - sizeof(RIFFChunkHeader))) {
		throw std::runtime_error("file too big");
	}
	
	m_chunk_wpos = ftell(m_file);
	if(m_chunk_wpos == -1) {
		throw std::runtime_error("unable to write chunk");
	}
	
	m_chunk_whead.chunkID = _code;
	m_chunk_whead.chunkSize = 0;
	
	if(fwrite(&m_chunk_whead, sizeof(RIFFChunkHeader), 1, m_file) != 1) {
		throw std::runtime_error("unable to write chunk");
	}
	
	m_write_size += sizeof(RIFFChunkHeader);
	m_chunk_wstart = true;
	
	// return the chunk's data position so that it can easily be updated
	return get_cur_pos();
}

void RIFFFile::write_chunk_data(const void *_data, uint32_t _len)
{
	assert(is_open_write());
	assert(m_chunk_wstart);
	
	if((sizeof(RIFFChunkHeader) + m_chunk_whead.chunkSize) > (UINT32_MAX - _len)
		|| m_write_size > (RIFF_MAX_FILESIZE - _len))
	{
		throw std::runtime_error("file is too big");
	}
	
	if(fwrite(_data, _len, 1, m_file) != 1) {
		throw std::runtime_error("unable to write data");
	}
	
	m_write_size += _len;
	m_chunk_whead.chunkSize += _len;
}

uint32_t RIFFFile::write_chunk_end()
{
	assert(is_open_write());
	assert(m_chunk_wstart);
	
	if(m_chunk_whead.chunkSize & 1) {
		// The data is always padded to nearest WORD boundary.
		// chunkSize does not include the padding.
		uint8_t pad = 0;
		if(fwrite(&pad, 1, 1, m_file) != 1) {
			throw std::runtime_error("unable to write data pad");
		}
		m_write_size++;
		if(m_write_size > RIFF_MAX_FILESIZE) {
			throw std::runtime_error("file is too big");
		}
	}
	
	long int lastpos = ftell(m_file);
	if(lastpos < 0) {
		throw std::runtime_error("unable to write chunk header");
	}
	if(fseek(m_file, m_chunk_wpos, SEEK_SET) != 0) {
		throw std::runtime_error("unable to find chunk header");
	}
	
	if(fwrite(&m_chunk_whead, sizeof(RIFFChunkHeader), 1, m_file) != 1) {
		throw std::runtime_error("unable to write chunk header");
	}
	
	if(fseek(m_file, lastpos, SEEK_SET) != 0) {
		throw std::runtime_error("error while seeking the end of chunk");
	}
	
	m_chunk_wstart = false;
	
	// return the total chunk size: header + data + pad
	return sizeof(RIFFChunkHeader) + m_chunk_whead.chunkSize + (m_chunk_whead.chunkSize & 1);
}

void RIFFFile::write_update(long int _pos, const void *_data, uint32_t _len)
{
	long int cur_size = get_cur_size();
	long int last_pos = get_cur_pos();
	
	assert(_pos + _len <= cur_size);
	
	if(fseek(m_file, _pos, SEEK_SET) != 0) {
		throw std::runtime_error("unable to find chunk data");
	}
	
	if(fwrite(_data, _len, 1, m_file) != 1) {
		throw std::runtime_error("unable to write data");
	}
	
	assert(get_cur_pos() <= cur_size);
	
	if(fseek(m_file, last_pos, SEEK_SET) != 0) {
		throw std::runtime_error("unable to complete write_update");
	}
}

void RIFFFile::write_end()
{
	assert(is_open_write());

	if(m_chunk_wstart) {
		write_chunk_end();
	}
	while(!m_lists_w.empty()) {
		write_list_end();
	}
	
	if(m_write_size > RIFF_MAX_FILESIZE) {
		throw std::runtime_error("file is too big");
	}
	
	fseek(m_file, RIFF_HEADER_FILESIZE_POS, SEEK_SET);
	// fileSize includes the size of RIFFHeader::fileType plus the size of the data that follows
	m_header.fileSize = m_write_size - 8;
	if(fwrite(&m_header.fileSize, 4, 1, m_file) != 1) {
		throw std::runtime_error("unable to update file header");
	}
	
	m_write_size = 0;
	m_write_mode = false;
}

long int RIFFFile::get_cur_pos() const
{
	assert(is_open());
	
	long int pos = ftell(m_file);
	if(pos == -1) {
		throw std::runtime_error("file too big");
	}
	return pos;
}

long int RIFFFile::get_cur_size() const
{
	assert(is_open());
	
	long int lastpos = get_cur_pos();
	
	if(fseek(m_file, 0, SEEK_END) < 0) {
		throw std::runtime_error("cannot get file size");
	}
	
	long int size = get_cur_pos();
	
	if(fseek(m_file, lastpos, SEEK_SET) < 0) {
		throw std::runtime_error("cannot get file size");
	}
	
	return size;
}


void RIFFFile::set_cur_pos(long int _pos)
{
	if(fseek(m_file, _pos, SEEK_SET) < 0) {
		throw std::runtime_error("cannot set file position");
	}
}

long int RIFFFile::get_ckdata_size(const RIFFChunkHeader &_hdr, long int _data_pos) const
{
	uint64_t size = _hdr.chunkSize;
	// The data is always padded to nearest WORD boundary.
	// chunkSize does not include the padding.
	if(size & 1) {
		size++;
	}
	if(_data_pos < 0) {
		_data_pos = ftell(m_file);
	}
	if(is_offset_overflow(_data_pos, size)) {
		throw std::runtime_error("offset overflow");
	}
	return size;
}

bool RIFFFile::is_offset_overflow(long int _pos, uint64_t _size) const
{
	if(_pos < 0) {
		return true;
	}
	
	uint64_t offset = _pos + _size;
	
	if(offset > UINT32_MAX || ((long int)offset) < 0) {
		return true;
	}
	
	return false;
}
