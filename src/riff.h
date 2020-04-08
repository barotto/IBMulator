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

#ifndef IBMULATOR_RIFF_H
#define IBMULATOR_RIFF_H

#include <vector>
#include <stack>
#include <cstdio>

typedef const char fourcc_str_t[5];

constexpr uint32_t FOURCC(fourcc_str_t _code)
{
	return
		(uint32_t(_code[3]) << 24) |
		(uint32_t(_code[2]) << 16) |
		(uint32_t(_code[1]) <<  8) |
		 uint32_t(_code[0]);
}

#define FOURCC_RIFF  FOURCC("RIFF")
#define FOURCC_LIST  FOURCC("LIST")

struct RIFFHeader {
	uint32_t RIFF;       // 0/4  FOURCC code "RIFF"
	uint32_t fileSize;   // 4/4  This is the size of the entire file in bytes
	                     //      minus 8 bytes for the two fields not included in
	                     //      this count: RIFF and fileSize.
	uint32_t fileType;   // 8/4  FOURCC code
} GCC_ATTRIBUTE(packed);

#define RIFF_HEADER_FILESIZE_POS  4
#define RIFF_MAX_FILESIZE         UINT32_MAX

struct RIFFChunkHeader {
	uint32_t chunkID;   // FOURCC that identifies the data contained in the chunk
	uint32_t chunkSize; // the size of the data in chunkData
	// chunkData follows, always padded to nearest WORD boundary.
	// chunkSize gives the size of the valid data in the chunk; it does not
	// include the padding, the size of chunkID, or the size of chunkSize.
} GCC_ATTRIBUTE(packed);

struct RIFFListHeader {
	uint32_t LIST;     // FOURCC code "LIST"
	uint32_t listSize; // the size of the list
	uint32_t listType; // FOURCC code
	// data follows, consisting of chunks or lists, in any order.
	// The value of listSize includes the size of listType plus the size of data;
	// it does not include the 'LIST' FOURCC or the size of listSize.
} GCC_ATTRIBUTE(packed);

class RIFFFile
{
protected:

	RIFFHeader  m_header;
	FILE       *m_file;
	bool        m_write_mode;
	uint64_t    m_write_size;
	
	long int        m_chunk_rpos;
	RIFFChunkHeader m_chunk_rhead;
	
	std::stack<long int> m_lists_w; // starting offsets of list headers
	
	bool            m_chunk_wstart;
	long int        m_chunk_wpos;
	RIFFChunkHeader m_chunk_whead;
	
public:

	RIFFFile();
	virtual ~RIFFFile();

	virtual RIFFHeader open_read(const char *_filepath);
	virtual void open_write(const char *_filepath, uint32_t _file_type);
	
	inline bool is_open() const { return m_file != nullptr; }
	inline bool is_open_read() const { return is_open() && !m_write_mode; }
	inline bool is_open_write() const { return is_open() && m_write_mode; }
	
	uint32_t file_size() const ;
	
	void close();
	void close_file() noexcept;
	
protected:

	enum class ChunkPosition {
		HEADER,
		DATA
	};
	
	void reset();
	
	void read(void *_buffer, uint32_t _size);
	void write(const void *_data, uint32_t _len);
	
	RIFFChunkHeader read_chunk_header();
	std::vector<uint8_t> read_chunk_data() const;
	void read_rewind_chunk(ChunkPosition) const;
	void read_skip_chunk() const;
	RIFFChunkHeader read_find_chunk(uint32_t _code);
	
	long int write_list_start(uint32_t _code);
	void write_list_end();
	long int write_chunk(uint32_t _code, const void *_data, uint32_t _len);
	long int write_chunk_start(uint32_t _code);
	void write_chunk_data(const void *_data, uint32_t _len);
	uint32_t write_chunk_end();
	void write_update(long int _pos, const void *_data, uint32_t _len);
	virtual void write_end();

	long int get_cur_pos() const;
	long int get_cur_size() const;
	void set_cur_pos(long int _pos);

private:
	long int get_ckdata_size(const RIFFChunkHeader&, long int _pos = -1) const;
	bool is_offset_overflow(long int _pos, uint64_t _size) const;
};

#endif