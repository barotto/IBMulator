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

#include "ibmulator.h"
#include "vgm.h"
#include "filesys.h"
#include "utils.h"
#include <cstring>
#include <cmath>

VGMFile::VGMFile()
:
m_chip(SN76489)
{
	size_check<VGMHeader,SIZEOF_VGMHEADER>();
	size_check<GD3TagHeader,SIZEOF_GD3HEADER>();
	m_events.reserve(50000);
}

VGMFile::~VGMFile()
{
}

void VGMFile::open(std::string _filepath)
{
	m_filepath = _filepath;
	m_events.clear();
	m_chip = SN76489;
	memset(&m_header, 0, sizeof(m_header));
}

void VGMFile::set_chip(ChipType _chip)
{
	m_chip = _chip;
}

void VGMFile::set_clock(uint32_t _hz)
{
	((uint32_t*)&m_header)[m_chip] = _hz;
}

void VGMFile::set_SN76489_feedback(uint16_t _value)
{
	m_header.SN76489_feedback = _value;
}

void VGMFile::set_SN76489_shift_width(uint8_t _value)
{
	m_header.SN76489_shift_width = _value;
}

void VGMFile::set_SN76489_flags(uint8_t _value)
{
	m_header.SN76489_flags = _value;
}

void VGMFile::set_tag_track(const std::string &_track)
{
	std::u16string u16tmp(_track.begin(), _track.end());
	m_gd3tag.track = u16tmp;
}

void VGMFile::set_tag_game(const std::string &_game)
{
	std::u16string u16tmp(_game.begin(), _game.end());
	m_gd3tag.game = u16tmp;
}

void VGMFile::set_tag_system(const std::string &_system)
{
	std::u16string u16tmp(_system.begin(), _system.end());
	m_gd3tag.system = u16tmp;
}

void VGMFile::set_tag_notes(const std::string &_notes)
{
	std::u16string u16tmp(_notes.begin(), _notes.end());
	m_gd3tag.notes = u16tmp;
}

void VGMFile::command(uint64_t _time, uint8_t _command, uint32_t _data)
{
	m_events.push_back({_time,_command,0,0,_data});
}

void VGMFile::command(uint64_t _time, uint8_t _command, uint8_t _chip, 
		uint32_t _reg, uint32_t _data)
{
	m_events.push_back({_time,_command,_chip,_reg,_data});
}

void VGMFile::write_event(FILE *file, const VGMEvent &e)
{
	if(fwrite(&e.cmd, 1, 1, file) != 1) {
		PERRF(LOG_FS, "error writing to file\n");
		throw std::exception();
	}
	//the command data
	size_t wresult = 0;
	size_t size = 0;
	switch(e.cmd) {
		case 0x50: // PSG (SN76489/SN76496)
			// write value dd
			size = 1;
			wresult = fwrite(&e.data, 1, 1, file);
			break;
		case 0x5A: // OPL2
		case 0xAA: // second OPL2
		case 0x5E: // OPL3 port 0
		case 0x5F: // OPL3 port 1
			// write value aa dd
			size = 2;
			wresult = fwrite(&e.reg, 1, 1, file);
			wresult += fwrite(&e.data, 1, 1, file);
			break;
		default:
			PERRF(LOG_FS, "unsupported command\n");
			throw std::exception();
	}
	if(wresult != size) {
		PERRF(LOG_FS, "error writing to file\n");
		throw std::exception();
	}
}

void VGMFile::close()
{
	if(m_filepath.empty() || m_events.empty()) {
		return;
	}
	std::string path = m_filepath;
	m_filepath.clear();
	auto file = FileSys::make_file(path.c_str(), "wb");
	if(file.get() == nullptr) {
		PERRF(LOG_FS, "unable to open '%s' for writing\n", path.c_str());
		throw std::exception();
	}

	VGMHeader header;
	memset(&header, 0, sizeof(header));
	header.ident = VGM_IDENT;
	header.version = VGM_VERSION;
	header.data_offset = VGM_DATA_OFFSET;
	((uint32_t*)&header)[m_chip] = ((uint32_t*)&m_header)[m_chip];
	if(m_chip == SN76489) {
		header.SN76489_feedback = m_header.SN76489_feedback;
		header.SN76489_shift_width = m_header.SN76489_shift_width;
		header.SN76489_flags = m_header.SN76489_flags;
	}

	if(fwrite(&header, sizeof(header), 1, file.get()) != 1) {
		PERRF(LOG_FS, "error writing to file\n");
		throw std::exception();
	}

	uint64_t prev_time = m_events[0].time;
	const double samples_per_us = 44100.0 / 1e6;
	const uint8_t wait_cmd = 0x61;
	uint32_t total_samples = 0;
	for(auto &e : m_events) {

		uint64_t time_elapsed = e.time - prev_time;
		if(time_elapsed) {
			//wait
			int samples = int(round(samples_per_us * time_elapsed));
			total_samples += samples;
			while(samples>0) {
				uint16_t samples16;
				if(samples > 65535) {
					samples16 = 65535;
				} else {
					samples16 = samples;
				}
				samples -= 65535;
				if(fwrite(&wait_cmd, 1, 1, file.get()) != 1) {
					PERRF(LOG_FS, "error writing to file\n");
					throw std::exception();
				}
				if(fwrite(&samples16, 2, 1, file.get()) != 1) {
					PERRF(LOG_FS, "error writing to file\n");
					throw std::exception();
				}
			}
		}
		prev_time = e.time;

		write_event(file.get(), e);
	}

	// end of sound data command
	uint8_t endofdata = 0x66;
	if(fwrite(&endofdata, 1, 1, file.get()) != 1) {
		PERRF(LOG_FS, "error writing to file\n");
		throw std::exception();
	}
	
	// tot_num_samples
	if(fseek(file.get(), 0x18, SEEK_SET) < 0) {
		PERRF(LOG_FS, "error accessing file\n");
		throw std::exception();
	}
	if(fwrite(&total_samples, 4, 1, file.get()) != 1) {
		PERRF(LOG_FS, "error writing to file\n");
		throw std::exception();
	}

	// GD3 tag
	if(fseek(file.get(), 0, SEEK_END) < 0) {
		PERRF(LOG_FS, "error accessing file\n");
		throw std::exception();
	}
	uint32_t gd3_pos = ftell(file.get());
	GD3TagHeader gd3_header;
	gd3_header.ident = GD3_IDENT;
	gd3_header.version = GD3_VERSION;
	gd3_header.datalen = 0;
	if(fwrite(&gd3_header, SIZEOF_GD3HEADER, 1, file.get()) != 1) {
		PERRF(LOG_FS, "error writing to file\n");
		throw std::exception();
	}
	
	char16_t null = 0;
	
	// "Track name (in English characters)\0"
	// since C++11 the returned string array is null-terminated
	gd3_header.datalen += fwrite(m_gd3tag.track.data(), 1, m_gd3tag.track.size()*2 + 2, file.get());
	// "Track name (in Japanese characters)\0"
	gd3_header.datalen += fwrite(&null, 1, 2, file.get());
	
	// "Game name (in English characters)\0"
	gd3_header.datalen += fwrite(m_gd3tag.game.data(), 1, m_gd3tag.game.size()*2 + 2, file.get());
	// "Game name (in Japanese characters)\0"
	gd3_header.datalen += fwrite(&null, 1, 2, file.get());
	
	// "System name (in English characters)\0"
	gd3_header.datalen += fwrite(m_gd3tag.system.data(), 1, m_gd3tag.system.size()*2 + 2, file.get());
	// "System name (in Japanese characters)\0"
	gd3_header.datalen += fwrite(&null, 1, 2, file.get());
	
	// "Name of Original Track Author (in English characters)\0"
	gd3_header.datalen += fwrite(&null, 1, 2, file.get());
	// "Name of Original Track Author (in Japanese characters)\0"
	gd3_header.datalen += fwrite(&null, 1, 2, file.get());
	
	// "Date of game's release written in the form yyyy/mm/dd, or just yyyy/mm or yyyy if month and day is not known\0"
	gd3_header.datalen += fwrite(&null, 1, 2, file.get());
	
	// "Name of person who converted it to a VGM file.\0"
	std::string pkgstr(PACKAGE_STRING);
	std::u16string u16tmp(pkgstr.begin(), pkgstr.end());
	gd3_header.datalen += fwrite(u16tmp.data(), 1, u16tmp.size() * 2 + 2, file.get());
	
	// "Notes\0"
	gd3_header.datalen += fwrite(m_gd3tag.notes.data(), 1, m_gd3tag.notes.size()*2 + 2, file.get());
	
	if(fseek(file.get(), gd3_pos+8, SEEK_SET) < 0) {
		PERRF(LOG_FS, "error accessing file\n");
		throw std::exception();
	}
	if(fwrite(&gd3_header.datalen, 4, 1, file.get()) != 1) {
		PERRF(LOG_FS, "error writing to file\n");
		throw std::exception();
	}
	
	if(fseek(file.get(), 20, SEEK_SET) < 0) {
		PERRF(LOG_FS, "error accessing file\n");
		throw std::exception();
	}
	gd3_pos -= 20;
	if(fwrite(&gd3_pos, 4, 1, file.get()) != 1) {
		PERRF(LOG_FS, "error writing to file\n");
		throw std::exception();
	}
	
	// EOF_offset
	if(fseek(file.get(), 0, SEEK_END) < 0) {
		PERRF(LOG_FS, "error accessing file\n");
		throw std::exception();
	}
	uint32_t file_len = ftell(file.get());
	file_len -= 4;
	if(fseek(file.get(), 0x04, SEEK_SET) < 0) {
		PERRF(LOG_FS, "error accessing file\n");
		throw std::exception();
	}
	if(fwrite(&file_len, 4, 1, file.get()) != 1) {
		PERRF(LOG_FS, "error writing to file\n");
		throw std::exception();
	}
}
