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

#include "ibmulator.h"
#include "midifile.h"
#include "riff.h"
#include "utils.h"
#include "filesys.h"
#include <cstring>

MIDIHeader MIDIHeader::to_file()
{
	return {
		type,
		to_bigendian_32(length),
		to_bigendian_16(format),
		to_bigendian_16(ntrks),
		to_bigendian_16(division)
	};
}

MIDITrack MIDITrack::to_file()
{
	return {
		type,
		to_bigendian_32(length)
	};
}


MIDIFile::~MIDIFile()
{
	if(is_open()) {
		close_file();
	}
}

void MIDIFile::open_write(const char *_filepath, uint16_t _format, uint16_t _division)
{
	assert(!is_open());

	m_file = FileSys::fopen(_filepath, "wb");
	if(!m_file) {
		throw std::runtime_error("unable to open for writing");
	}

	m_header.type     = FOURCC("MThd");
	m_header.length   = 6;
	m_header.format   = _format;
	m_header.ntrks    = 0;
	m_header.division = _division;
	
	MIDIHeader msbh = m_header.to_file();
	if(fwrite(&msbh, sizeof(MIDIHeader), 1, m_file) != 1) {
		throw std::runtime_error("unable to write MIDI header");
	}
	
	m_path = _filepath;
	m_mex_count = 0;
	m_sys_count = 0;
}

void MIDIFile::close()
{
	if(!is_open()) {
		return;
	}
	
	if(is_open_write()) {
		try {
			write_end();
		} catch(...) {
			close_file();
			throw;
		}
	}
	
	close_file();
}

void MIDIFile::close_file() noexcept
{
	if(is_open()) {
		fclose(m_file);
	}
	m_file = nullptr;
}

void MIDIFile::write_new_track()
{
	assert(is_open_write());
	
	if(m_curtrk_pos) {
		write_end_track();
	}
	
	m_curtrk_h.type = FOURCC("MTrk");
	m_curtrk_h.length = 0;
	
	m_curtrk_pos = get_cur_pos();
	
	MIDITrack msbh = m_curtrk_h.to_file();
	if(fwrite(&msbh, sizeof(MIDITrack), 1, m_file) != 1) {
		throw std::runtime_error("cannot write to file");
	}
	
	m_header.ntrks++;
}

void MIDIFile::write_text(const std::string &_mex)
{
	assert(is_open_write());
	assert(m_curtrk_pos);
	
	write_bytes({ 0x00, 0xFF, 0x01 });
	write_varlen_number(_mex.length());
	if(fwrite(_mex.c_str(), 1, _mex.length(), m_file) != _mex.length()) {
		throw std::runtime_error("cannot write to file");
	}
	m_curtrk_h.length += _mex.length();
	
	PDEBUGF(LOG_V2, LOG_MIDI, "  text to file: %s\n", _mex.c_str());
}

void MIDIFile::write_message(uint8_t *_mex, uint32_t _len, uint32_t _delta)
{
	assert(is_open_write());
	assert(m_curtrk_pos);
	
	write_varlen_number(_delta);
	
	if(fwrite(_mex, 1, _len, m_file) != _len) {
		throw std::runtime_error("cannot write to file");
	}
	
	PDEBUGF(LOG_V2, LOG_MIDI, "  message to file len:%u, delta:%u\n", _len, _delta);
	
	m_curtrk_h.length += _len;
	m_mex_count++;
}

void MIDIFile::write_sysex(uint8_t *_data, uint32_t _len, uint32_t _delta)
{
	assert(is_open_write());
	assert(m_curtrk_pos);
	
	write_varlen_number(_delta);
	
	// sysex messages include the initial 0xf0
	if(_len <= 1) {
		return;
	}
	assert(_data[0] == 0xf0);
	_len--;
	write_byte( 0xf0 );
	write_varlen_number(_len);

	if(fwrite(&_data[1], 1, _len, m_file) != _len) {
		throw std::runtime_error("cannot write to file");
	}
	
	PDEBUGF(LOG_V2, LOG_MIDI, "  sysex to file len:%u, delta:%u\n", _len, _delta);
	
	m_curtrk_h.length += _len;
	m_sys_count++;
}

void MIDIFile::write_varlen_number(uint32_t _val)
{
	if(_val & 0xfe00000) {
		write_byte(uint8_t(0x80|((_val >> 21) & 0x7f)));
	}
	if(_val & 0xfffc000) {
		write_byte(uint8_t(0x80|((_val >> 14) & 0x7f)));
	}
	if(_val & 0xfffff80) {
		write_byte(uint8_t(0x80|((_val >> 7) & 0x7f)));
	}
	write_byte(uint8_t(_val & 0x7f));
}

void MIDIFile::write_byte(uint8_t _val)
{
	assert(is_open_write());
	assert(m_curtrk_pos);
	
	if(fwrite(&_val, 1, 1, m_file) != 1) {
		throw std::runtime_error("cannot write to file");
	}
	
	m_curtrk_h.length++;
}

void MIDIFile::write_bytes(std::vector<uint8_t> _vals)
{
	assert(is_open_write());
	assert(m_curtrk_pos);
	
	if(fwrite(&_vals[0], _vals.size(), 1, m_file) != 1) {
		throw std::runtime_error("cannot write to file");
	}
	
	m_curtrk_h.length += _vals.size();
}

long int MIDIFile::get_cur_pos() const
{
	assert(is_open());
	
	long int pos = ftell(m_file);
	if(pos == -1) {
		throw std::runtime_error("file is too big");
	}
	return pos;
}

void MIDIFile::set_cur_pos(long int _pos)
{
	if(_pos < 0) {
		if(fseek(m_file, 0, SEEK_END) < 0) {
			throw std::runtime_error("cannot set file position");
		}
	} else {
		if(fseek(m_file, _pos, SEEK_SET) < 0) {
			throw std::runtime_error("cannot set file position");
		}
	}
}

void MIDIFile::write_end_track()
{
	assert(is_open_write());
	assert(m_curtrk_pos);
	
	// delta + end of track event
	write_bytes({ 0x00, 0xFF, 0x2F, 0x00 });
	
	set_cur_pos(m_curtrk_pos);

	MIDITrack msbh = m_curtrk_h.to_file();
	if(fwrite(&msbh, sizeof(MIDITrack), 1, m_file) != 1) {
		throw std::runtime_error("cannot write to file");
	}
	
	m_curtrk_pos = 0;
	
	set_cur_pos(-1);
}

void MIDIFile::write_end()
{
	assert(is_open_write());

	if(m_curtrk_pos) {
		write_end_track();
	}
	
	set_cur_pos(0);
	
	MIDIHeader msbh = m_header.to_file();
	if(fwrite(&msbh, sizeof(MIDIHeader), 1, m_file) != 1) {
		throw std::runtime_error("cannot write to file");
	}
	
	set_cur_pos(-1);
}