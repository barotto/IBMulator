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

#ifndef IBMULATOR_WAV_H
#define IBMULATOR_WAV_H

#include "riff.h"

#define FOURCC_WAVE      FOURCC("WAVE")
#define FOURCC_WAVE_FMT  FOURCC("fmt ")
#define FOURCC_WAVE_DATA FOURCC("data")

#define WAV_FORMAT_PCM        0x0001
#define WAV_FORMAT_IEEE_FLOAT 0x0003

//
// The "WAVE" format consists of two subchunks: "fmt " and "data":
// The "fmt " subchunk describes the sound data's format
// The "data" subchunk contains the size of the data and the actual sound
//
//
// chunk1ID    12/4    FOURCC "fmt "
// chunk1Size  16/4    = 16 for PCM. This is the size of the
//                     rest of the chunk which follows this number.
// WAVFormatEx 20/16   (size is 16 for PCM)
// ExtraParams X       Space for extra parameters (not used for PCM)
// chunk2ID    36/4    FOURCC "data"
// chunk2Size  40/4    = NumSamples * NumChannels * BitsPerSample/8
//                     This is the number of bytes in the data.

#define WAV_FMT_CHUNK_SIZE    16
#define WAV_SUBCHUNK2SIZE_POS 40
#define WAV_PCM_HEADER_SIZE   36

struct WAVFormatEx {
	uint16_t audioFormat;   // 20/2   PCM = 1 (i.e. Linear quantization)
	uint16_t numChannels;   // 22/2   Mono = 1, Stereo = 2, etc.
	uint32_t sampleRate;    // 24/4   8000, 44100, etc.
	uint32_t byteRate;      // 28/4   == SampleRate * NumChannels * BitsPerSample/8
	uint16_t blockAlign;    // 32/2   == NumChannels * BitsPerSample/8
	                        //        The number of bytes for one sample including
	                        //        all channels. I wonder what happens when
	                        //        this number isn't an integer?
	uint16_t bitsPerSample; // 34/2   8 bits = 8, 16 bits = 16, etc.

	uint16_t extraParamSize;// 36/2   if PCM then doesn't exist
} GCC_ATTRIBUTE(packed);

#define WAV_PCM_FORMAT_HEADER_SIZE 36

class WAVFile : public RIFFFile
{
protected:
	WAVFormatEx m_format = {}; // format information contained in the fmt chunk

public:

	WAVFile();
	~WAVFile();

	RIFFHeader open_read(const char *_filepath);
	void open_write(const char *_filepath, uint32_t _rate, uint16_t _bits, uint16_t _channels);

	std::vector<uint8_t> read_audio_data() const;
	void write_audio_data(const void *_data, uint32_t _len);
	
	uint16_t channels() const { return m_format.numChannels; }
	uint32_t rate()     const { return m_format.sampleRate; }
	uint16_t bits()     const { return m_format.bitsPerSample; }
	uint16_t format()   const { return m_format.audioFormat; }
};

#endif
