/*
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ibmulator.h"
#include "wav.h"

WAVFile::WAVFile()
: m_file(NULL),
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

void WAVFile::open(const char *_filepath, uint32_t _rate, uint16_t _bits, uint16_t _channels)
{
	if(m_file) {
		PERRF(LOG_FS, "file is already opened\n");
		throw std::exception();
	}

	m_datasize = 0;

	m_file = fopen(_filepath, "wb");
	if(m_file == NULL) {
		throw std::exception();
	}

	WAVHeader header;
	header.ChunkSize = 36;
	header.NumChannels = _channels;
	header.SampleRate = _rate;
	header.ByteRate = _rate * _channels * (_bits / 8);
	header.BlockAlign = _channels * (_bits / 8);
	header.BitsPerSample = _bits;
	header.Subchunk2Size = 0;

	if(fwrite(&header, sizeof(header), 1, m_file) != 1) {
		PERRF(LOG_FS, "error writing to file\n");
		throw std::exception();
	}
}

void WAVFile::save(const uint8_t *_data, size_t _len)
{
	if(!m_file) {
		return;
	}
	if(fwrite(_data, _len, 1, m_file) != 1) {
		PERRF(LOG_FS, "error writing to file\n");
		throw std::exception();
	}
	m_datasize += _len;
}

void WAVFile::close()
{
	if(!m_file) {
		return;
	}

	uint32_t ChunkSize = 36 + m_datasize;
	fseek(m_file, 4, SEEK_SET);
	fwrite(&ChunkSize, sizeof(ChunkSize), 1, m_file);

	uint32_t Subchunk2Size = m_datasize;
	fseek(m_file, 40, SEEK_SET);
	fwrite(&Subchunk2Size, sizeof(Subchunk2Size), 1, m_file);

	fclose(m_file);
	m_file = NULL;
}

void WAVFile::save(const char *_filepath, uint32_t _rate, uint16_t _bits,
		uint16_t _channels, const uint8_t *_data, size_t _data_size)
{
	ASSERT(sizeof(WAVHeader) == SIZEOF_WAVHEADER);
	ASSERT(_bits == 8 || _bits == 16);

	FILE * file = fopen(_filepath, "wb");
	if(file == NULL) {
		throw std::exception();
	}

	WAVHeader header;

	//header.ChunkID = 0x46464952;
	//header.Format = 0x45564157;
	header.ChunkSize = 36 + _data_size;
	//header.Subchunk1ID = 0x20746d66;
	//header.Subchunk1Size = 16;
	//header.AudioFormat = 1;
	header.NumChannels = _channels;
	header.SampleRate = _rate;
	header.ByteRate = _rate * _channels * (_bits / 8);
	header.BlockAlign = _channels * (_bits / 8);
	header.BitsPerSample = _bits;
	//header.Subchunk2ID = 0x61746164;
	header.Subchunk2Size = _data_size;

	fwrite(&header, sizeof(header), 1, file);
	fwrite(_data, _data_size, 1, file);
	fclose(file);
}
