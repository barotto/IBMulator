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

#ifndef IBMULATOR_AVI_H
#define IBMULATOR_AVI_H

#include "videoencoder.h"
#include "riff.h"
#include <cstdio>
#include <vector>
#include <SDL.h>

struct AVIMainHeader {
	uint32_t microSecPerFrame; // Number of usec between frames. It indicates the overall timing for the file.
	uint32_t maxBytesPerSec;   // Approximate max data rate of the file.
	uint32_t padGranularity;   // Alignment for data, in bytes. Pad the data to multiples of this value.
	uint32_t flags;            // see AVIFlags
	uint32_t totalFrames;      // Total number of frames of data
	uint32_t initialFrames;    // Initial frame for interleaved files. Noninterleaved files should specify 0. 
	uint32_t streams;          // Number of streams in the file. File with audio and video = 2.
	uint32_t suggBufSize;      // Suggested buffer size for reading the file. Should be the largest chunk in the file.
	uint32_t width;            // Width in pixels.
	uint32_t height;           // Height in pixels.
	uint32_t reserved[4];      // Set to zero.
} GCC_ATTRIBUTE(packed);

enum AVIFlags {
	AVIF_HASINDEX        = 0x00000010, // Has an index
	AVIF_MUSTUSEINDEX    = 0x00000020, // Must use the index to determine order
	AVIF_ISINTERLEAVED   = 0x00000100, // AVI file is interleaved
	AVIF_TRUSTCKTYPE     = 0x00000800, // The keyframe flags in the index are reliable.
	AVIF_WASCAPTUREFILE  = 0x00010000, // The file was captured. The interleave might be weird.
	AVIF_COPYRIGHTED     = 0x00020000  // Contains copyrighted data.
};

struct AVIStreamHeader {
	uint32_t type;           // FOURCC code 'auds' = audio stream, 'mids' = MIDI stream,
	                         // 'txts' = text stream, 'vids' = video stream
	uint32_t handler;        // FOURCC code for specific data handler.
	                         // The data handler is the preferred handler for the stream.
	                         // For audio and video streams, this specifies the codec for decoding the stream.
	uint32_t flags;          // see AVISFlags
	uint16_t priority;
	uint16_t language;
	uint32_t initialFrames;  // How far audio data is skewed ahead of the video frames in interleaved files
	uint32_t scale;          // Used with rate to specify the time scale that this stream will use.
	                         // Dividing rate by scale gives the number of samples per second.
	                         // For video streams, this is the frame rate. For audio streams, this
	                         // rate corresponds to the time needed to play blockAlign bytes of audio,
	                         // which for PCM audio is the just the sample rate.
	uint32_t rate;
	uint32_t start;          // The starting time for this stream. The units are defined by the
	                         // rate and scale members in the main file header. Usually zero.
	uint32_t length;         // The length of this stream. The units are defined by the rate and scale.
	uint32_t suggBufSize;    // How large a buffer should be used to read this stream. 0 = unknown.
	uint32_t quality;        // Quality is represented as a number between 0 and 10,000. -1 = default.
	uint32_t sampleSize;     // The size of a single sample of data.
	                         // 0 = each sample of data (such as a video frame) must be in a separate chunk.
	                         // nonzero = multiple samples of data can be grouped into a single chunk within the file.
	                         // For video streams it's typically 0, can be nonzero if all video frames are the same size.
	                         // For audio streams should be the same as the blockAlign member of the WAVFormatEx structure
	                         // describing the audio.
	struct {
		int16_t left;
		int16_t top;
		int16_t right;
		int16_t bottom;
	} frame;                // The destination rectangle for a text or video stream within the movie rectangle.
} GCC_ATTRIBUTE(packed);

enum AVISFlags {
	AVISF_DISABLED         = 0x00000001, // Indicates this stream should not be enabled by default.
	AVISF_VIDEO_PALCHANGES = 0x00010000  // Indicates this video stream contains palette changes.
};

struct AVIOldIndex {
	uint32_t chunkId; // FOURCC that identifies the stream
	uint32_t flags;   // See AVIIdxFlags
	uint32_t offset;  // Location of the chunk's header from the start of the 'movi' list's data
	uint32_t size;    // Size of the chunk's data
} GCC_ATTRIBUTE(packed);

enum AVIIdxFlags {
	AVIIF_LIST     = 0x001, // The data chunk is a 'rec ' list.
	AVIIF_KEYFRAME = 0x010, // The data chunk is a key frame
	AVIIF_NO_TIME  = 0x100, // The data chunk does not affect the timing of the stream (palette changes) 
};



/* General structure of an AVI file:
RIFF ('AVI '
      LIST ('hdrl'
            'avih'(<Main AVI Header>)
            LIST ('strl'
                  'strh'(<Stream header>)
                  'strf'(<Stream format>)
                  [ 'strd'(<Additional header data>) ]
                  [ 'strn'(<Stream name>) ]
                  ...
                 )
             ...
           )
      LIST ('movi'
            {SubChunk | LIST ('rec '
                              SubChunk1
                              SubChunk2
                              ...
                             )
               ...
            }
            ...
           )
      ['idx1' (<AVI Index>) ]
     )
*/

enum AVIVideoEncoders
{
	AVI_VIDEO_BMP,
	AVI_VIDEO_MPNG,
	AVI_VIDEO_ZMBV
};

enum AVIAudioEncoders
{
	AVI_AUDIO_PCM
};

struct AVIWriteOptions {
	SDL_PixelFormat *video_srcpixels; // source pixel format, memory must be managed by SDL2
	unsigned video_encoder; // video encoder id
	unsigned video_quality; // video encoding quality (value depends on encoder)
	uint32_t video_width;   // frame width
	uint32_t video_height;  // frame height
	uint32_t video_scale;   // rate / scale = fps
	uint32_t video_rate;    // rate / scale = fps
	unsigned audio_encoder; // audio encoder id
	unsigned audio_quality; // audio encoding quality (value depends on encoder)
	unsigned audio_freq;    // audio frequency (audio frames per second)
	unsigned audio_channels;// audio channels
};


class AVIFile : public RIFFFile
{
	AVIWriteOptions m_write_options;
	
	AVIMainHeader m_avimain_hdr;
	long int m_avimain_hdr_pos;
	long int m_movi_list_pos;

	struct {
		AVIStreamHeader stream_hdr;
		long int stream_hdr_pos;
		uint32_t chunk_fcc;
		std::unique_ptr<VideoEncoder> encoder;
		std::vector<uint8_t> enc_buffer;
		SDL_PixelFormat *enc_pixformat;
		bool convert;
		std::vector<uint8_t> linebuf;
	} m_video;
	
	struct {
		AVIStreamHeader stream_hdr;
		long int stream_hdr_pos;
		uint32_t chunk_fcc;
		// TODO only PCM, encoder=0
		unsigned encoder;
	} m_audio;
	
	std::vector<AVIOldIndex> m_index;
	
public:

	AVIFile();
	~AVIFile();

	RIFFHeader open_read(const char *) { throw std::exception(); }
	void open_write(const char *_filepath, AVIWriteOptions &_options);

	void write_video_frame(const void *_data, uint32_t _stride);
	void write_audio_samples(const int16_t *_samples, uint32_t _count);
	bool write_size_limit_reached();
	
	VideoEncoder *video_encoder() const { return m_video.encoder.get(); }
	
	uint32_t video_frames_count() { return m_video.stream_hdr.length; }
	uint32_t audio_frames_count() { return m_audio.stream_hdr.length; }
	
	std::string time_len_string();
	
private:
	void write_end();
	void add_chunk_index(uint32_t _data_pos, uint32_t _fcc, unsigned _venc_flags);
	void convert_pixformat(const uint8_t *_srcline, uint32_t _len);
};

#endif
