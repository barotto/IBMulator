/*
 * Copyright (C) 2015-2020  Marco Bortolin
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
#include "wav.h"
#include "utils.h"
#include <cstring>

WAVFile::WAVFile()
: RIFFFile()
{
	size_check<WAVFormatEx,16+2>();
}

WAVFile::~WAVFile()
{
}

RIFFHeader WAVFile::open_read(const char *_filepath)
{
	RIFFFile::open_read(_filepath);
	if(m_header.fileType != FOURCC_WAVE) {
		throw std::runtime_error("not a wave file");
	}
	
	// FMT header
	RIFFChunkHeader fmt = read_chunk_header();
	if(fmt.chunkID != FOURCC_WAVE_FMT) {
		throw std::runtime_error("invalid FMT header");
	}
	
	read(&m_format, sizeof(m_format));
	
	if(m_format.audioFormat != WAV_FORMAT_PCM) {
		throw std::runtime_error("unsupported format (not a PCM file)");
	}

	// Find the DATA chunk skipping any other RIFF extensions
	read_skip_chunk();
	try {
		read_find_chunk(FOURCC_WAVE_DATA);
	} catch(std::runtime_error &) {
		throw std::runtime_error("unable to find the DATA chunk");
	}
	
	return m_header;
}

void WAVFile::open_write(const char *_filepath, uint32_t _rate, uint16_t _bits, uint16_t _channels)
{
	RIFFFile::open_write(_filepath, FOURCC_WAVE);

	m_format.audioFormat   = WAV_FORMAT_PCM;
	m_format.numChannels   = _channels;
	m_format.sampleRate    = _rate;
	m_format.byteRate      = _rate * _channels * (_bits / 8);
	m_format.blockAlign    = _channels * (_bits / 8);
	m_format.bitsPerSample = _bits;
	
	// -2 because there's no ExtraParamSize for PCM
	write_chunk(FOURCC_WAVE_FMT, &m_format, sizeof(WAVFormatEx)-2);

	write_chunk_start(FOURCC_WAVE_DATA);
}

std::vector<uint8_t> WAVFile::read_audio_data() const
{
	return read_chunk_data();
}

void WAVFile::write_audio_data(const void *_data, uint32_t _len)
{
	write_chunk_data(_data, _len);
}

