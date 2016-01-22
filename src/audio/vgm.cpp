/*
 * Copyright (C) 2015, 2016  Marco Bortolin
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
#include <cstring>
#include <cmath>

VGMFile::VGMFile()
:
m_chip(SN76489)
{
	size_check<VGMHeader,SIZEOF_VGMHEADER>();
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

void VGMFile::command(uint64_t _time, uint8_t _command, uint32_t _data)
{
	m_events.push_back({_time,_command,_data});
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
	for(auto e : m_events) {

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

		//the command
		if(fwrite(&e.cmd, 1, 1, file.get()) != 1) {
			PERRF(LOG_FS, "error writing to file\n");
			throw std::exception();
		}
		//the command data
		size_t wresult = 1;
		switch(e.cmd) {
			case 0x50:
				//PSG (SN76489/SN76496) write value dd
				wresult = fwrite(&e.data, 1, 1, file.get());
				break;
			default:
				PERRF(LOG_FS, "unsupported command\n");
				throw std::exception();
		}
		if(wresult != 1) {
			PERRF(LOG_FS, "error writing to file\n");
			throw std::exception();
		}
	}

	fseek(file.get(), 0x18, SEEK_SET);
	fwrite(&total_samples, 4, 1, file.get());

	fseek(file.get(), 0, SEEK_END);
	uint32_t file_len = ftell(file.get());
	file_len -= 4;
	fseek(file.get(), 0x04, SEEK_SET);
	fwrite(&file_len, 4, 1, file.get());
}
