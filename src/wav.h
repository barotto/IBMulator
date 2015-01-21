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

#ifndef IBMULATOR_WAV_H
#define IBMULATOR_WAV_H

#include <cstdio>

struct WAVHeader {

	const uint32_t ChunkID = 0x46464952;
	                     //0/4  Contains the letters "RIFF" in ASCII form
	                     //     (0x52494646 big-endian form).
	uint32_t ChunkSize;  //4/4  36 + SubChunk2Size, or more precisely:
	                     //     4 + (8 + SubChunk1Size) + (8 + SubChunk2Size)
	                     //     This is the size of the rest of the chunk
	                     //     following this number.  This is the size of the
	                     //     entire file in bytes minus 8 bytes for the
	                     //     two fields not included in this count:
	                     //     ChunkID and ChunkSize.
	const uint32_t Format = 0x45564157;
	                     //8/4  Contains the letters "WAVE"
	                     //     (0x57415645 big-endian form).
	/*
	 * The "WAVE" format consists of two subchunks: "fmt " and "data":
	 * The "fmt " subchunk describes the sound data's format:
	 */
	const uint32_t Subchunk1ID = 0x20746d66;
	                        //12/4   Contains the letters "fmt "
	                        //       (0x666d7420 big-endian form).
	const uint32_t Subchunk1Size = 16;
	                        //16/4   16 for PCM.  This is the size of the
	                        //       rest of the Subchunk which follows this number.
	const uint16_t AudioFormat = 1;
	                        //20/2   PCM = 1 (i.e. Linear quantization)
	                        //       Values other than 1 indicate some
	                        //       form of compression.
	uint16_t NumChannels;   //22/2   Mono = 1, Stereo = 2, etc.
	uint32_t SampleRate;    //24/4   8000, 44100, etc.
	uint32_t ByteRate;      //28/4   == SampleRate * NumChannels * BitsPerSample/8
	uint16_t BlockAlign;    //32/2   == NumChannels * BitsPerSample/8
	                        //       The number of bytes for one sample including
	                        //       all channels. I wonder what happens when
	                        //       this number isn't an integer?
	uint16_t BitsPerSample; //34/2   8 bits = 8, 16 bits = 16, etc.

	//uint16_t ExtraParamSize    2   if PCM, then doesn't exist
	//ExtraParams                X   space for extra parameters

	/*
	The "data" subchunk contains the size of the data and the actual sound:
	*/
	const uint32_t Subchunk2ID = 0x61746164;
	                        //36-4   Contains the letters "data"
	                        //       (0x64617461 big-endian form).
	uint32_t Subchunk2Size; //40-4   == NumSamples * NumChannels * BitsPerSample/8
                            //       This is the number of bytes in the data.
                            //       You can also think of this as the size
                            //       of the read of the subchunk following this
                            //       number.
} GCC_ATTRIBUTE(packed);


#define SIZEOF_WAVHEADER 44


class WAVFile
{
	FILE * m_file;
	size_t m_datasize;

public:

	WAVFile();
	~WAVFile();

	void open(const char *_filepath, uint32_t _rate, uint16_t _bits, uint16_t _channels);
	inline bool is_open() const { return m_file!=NULL; }
	void save(const uint8_t *_data, size_t _len);
	void close();

	static void save(const char *_filepath, uint32_t _rate, uint16_t _bits,
			uint16_t _channels, const uint8_t *_data, size_t _data_size);
};

#endif
