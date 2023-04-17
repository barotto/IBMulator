/*
 * Copyright (C) 2020-2023  Marco Bortolin
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

#ifndef IBMULATOR_MIDIFILE_H
#define IBMULATOR_MIDIFILE_H

#include <vector>
#include <cstdio>

// MIDI files are made up of chunks, each having a 4-character type and a 32-bit length.
// The length refers to the number of bytes of data which follow: the eight bytes of type
// and length are not included.

// MIDI files resemble RIFF files but they are not, although a MIDI file can easily be
// contained in a RIFF file (see the RMID file format).

// Integer numbers in headers are stored MSB first (big endian).

// There are two types of chunks: header chunks and track chunks.
// A header chunk provides a minimal amount of information pertaining to the entire MIDI file.
struct MIDIHeader {
	uint32_t type;     // "MThd"
	uint32_t length;   // the number 6 (always)
	uint16_t format;   // 0, 1, or 2
	uint16_t ntrks;    // the number of track chunks in the file; always 1 for a format 0 file.
	uint16_t division; // the meaning of the delta-times
	
	MIDIHeader to_file();
} GCC_ATTRIBUTE(packed);

// A track chunk contains a sequential stream of MIDI data which may contain information
// for up to 16 MIDI channels. The concepts of multiple tracks, multiple MIDI outputs,
// patterns, sequences, and songs may all be implemented using several track chunks.
struct MIDITrack {
	uint32_t type;     // "MTrk"
	uint32_t length;   // number of bytes of the following data
	// 1 or more <MTrk event> follow 
	
	MIDITrack to_file();
} GCC_ATTRIBUTE(packed);


class MIDIFile
{
protected:
	std::string m_path;
	FILE *      m_file = nullptr;
	MIDIHeader  m_header = {};
	MIDITrack   m_curtrk_h = {};
	long int    m_curtrk_pos = 0;
	unsigned    m_mex_count = 0;
	unsigned    m_sys_count = 0;
	
public:
	~MIDIFile();

	inline bool is_open() const { return m_file != nullptr; }
	inline bool is_open_write() const { return is_open(); }
	inline bool is_open_read() const { return false; } // TODO ?
	
	void open_write(const char *_filepath, uint16_t _format, uint16_t _division);
	
	void close();
	void close_file() noexcept;
	
	void write_new_track();
	void write_text(const std::string &_mex);
	void write_message(uint8_t *_mex, uint32_t _len, uint32_t _delta);
	void write_sysex(uint8_t *_data, uint32_t _len, uint32_t _delta);
	
	const char * path() const { return m_path.c_str(); }
	unsigned mex_count() const { return m_mex_count; }
	unsigned sys_count() const { return m_sys_count; }
	
protected:
	long int get_cur_pos() const;
	void set_cur_pos(long int _pos);
	void write_byte(uint8_t _val);
	void write_bytes(std::vector<uint8_t> _vals);
	void write_varlen_number(uint32_t _value);
	void write_end();
	void write_end_track();
};

#endif
