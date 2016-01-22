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
#include "wav.h"
#include <cstring>

WAVFile::WAVFile()
: m_file(nullptr),
  m_datasize(0)
{
	size_check<WAVHeader,SIZEOF_WAVHEADER>();
}

WAVFile::~WAVFile()
{
	if(m_file) {
		close();
	}
}

void WAVFile::open_read(const char *_filepath)
{
	if(m_file) {
		throw std::runtime_error("already open");
	}

	memset(&m_header, 0, sizeof(m_header));
	m_datasize = 0;
	m_write_mode = false;

	m_file = fopen(_filepath, "rb");
	if(m_file == nullptr) {
		throw std::runtime_error("unable to open");
	}

	if(fread(&m_header, sizeof(m_header), 1, m_file) != 1) {
		throw std::runtime_error("unable to read");
	}

	if(m_header.ChunkID != WAV_HEADER_RIFF || m_header.Format != WAV_HEADER_WAVE ||
	   m_header.Subchunk1ID != WAV_HEADER_FMT)
	{
		throw std::runtime_error("not a wav file");
	}

	if(m_header.AudioFormat != WAV_FORMAT_PCM ||
	   m_header.Subchunk1Size != WAV_HEADER_SC1_SIZE ||
	   m_header.Subchunk2ID != WAV_HEADER_DATA)
	{
		throw std::runtime_error("unsupported format");
	}
}

void WAVFile::open_write(const char *_filepath, uint32_t _rate, uint16_t _bits, uint16_t _channels)
{
	if(m_file) {
		throw std::runtime_error("already open");
	}

	m_datasize = 0;
	m_write_mode = true;

	m_file = fopen(_filepath, "wb");
	if(m_file == nullptr) {
		throw std::runtime_error("unable to open");
	}

	m_header.ChunkSize = 36;
	m_header.NumChannels = _channels;
	m_header.SampleRate = _rate;
	m_header.ByteRate = _rate * _channels * (_bits / 8);
	m_header.BlockAlign = _channels * (_bits / 8);
	m_header.BitsPerSample = _bits;
	m_header.Subchunk2Size = 0;

	if(fwrite(&m_header, sizeof(m_header), 1, m_file) != 1) {
		throw std::runtime_error("unable to write");
	}
}

std::vector<uint8_t> WAVFile::read() const
{
	std::vector<uint8_t> samples;

	if(!m_file || m_write_mode || m_header.Subchunk2Size == 0) {
		return samples;
	}

	samples.resize(m_header.Subchunk2Size);

	size_t bytes = fread(&samples[0], 1, m_header.Subchunk2Size, m_file);
	if(bytes == 0) {
		throw std::runtime_error("unable to read");
	}
	if(bytes < m_header.Subchunk2Size) {
		throw std::runtime_error("the file is of wrong size");
	}

	return samples;
}

void WAVFile::write(const uint8_t *_data, size_t _len)
{
	if(!m_file || !m_write_mode) {
		return;
	}
	if(fwrite(_data, _len, 1, m_file) != 1) {
		throw std::runtime_error("unable to write");
	}
	m_datasize += _len;
}

void WAVFile::close()
{
	if(!m_file) {
		return;
	}

	if(m_write_mode) {
		uint32_t ChunkSize = 36 + m_datasize;
		fseek(m_file, 4, SEEK_SET);
		fwrite(&ChunkSize, sizeof(ChunkSize), 1, m_file);

		uint32_t Subchunk2Size = m_datasize;
		fseek(m_file, 40, SEEK_SET);
		fwrite(&Subchunk2Size, sizeof(Subchunk2Size), 1, m_file);
	}

	fclose(m_file);
	m_file = nullptr;
}

