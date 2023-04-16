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
#include "avi.h"
#include "videoencoder_bmp.h"
#include "videoencoder_mpng.h"
#include "videoencoder_zmbv.h"
#include "audio/wav.h"
#include "timers.h"
#include "utils.h"
#include <cstring>
#include <algorithm>

/*
AVI file for IBMulator limited case, 1 video stream + 1 audio stream:
RIFF avi
	LIST
		hdrl
			avih
			LIST
				strl
					strh (vids)
					strf
			LIST
				strl
					strh (auds)
					strf
	LIST
		INFO
	LIST
		movi
			00db
			01wb
			...
			idx1
*/

AVIFile::AVIFile()
{
}

AVIFile::~AVIFile()
{
	if(m_video.enc_pixformat) {
		SDL_FreeFormat(m_video.enc_pixformat);
		m_video.enc_pixformat = nullptr;
	}
}
	
void AVIFile::open_write(const char *_filepath, AVIWriteOptions &_options)
{
	RIFFFile::open_write(_filepath, FOURCC("AVI "));

	m_write_options = _options;
	
	/// > LIST hdrl
	write_list_start(FOURCC("hdrl"));
	
	const double fps = double(_options.video_rate) / _options.video_scale;
	
	
	// Video format setup

	switch(_options.video_encoder) {
		case AVI_VIDEO_BMP: {
			m_video.chunk_fcc = FOURCC("00db");
			m_video.encoder = std::make_unique<VideoEncoder_BMP>();
			break;
		}
		case AVI_VIDEO_MPNG: {
			m_video.chunk_fcc = FOURCC("00dc");
			m_video.encoder = std::make_unique<VideoEncoder_MPNG>(_options.video_quality);
			break;
		}
		case AVI_VIDEO_ZMBV: {
			m_video.chunk_fcc = FOURCC("00dc");
			m_video.encoder = std::make_unique<VideoEncoder_ZMBV>(_options.video_quality);
			break;
		}
		default:
			throw std::logic_error("invalid video format");
	}

	BitmapInfoHeader video_format;
	memset(&video_format, 0, sizeof(BitmapInfoHeader));
	video_format.width = _options.video_width;
	video_format.height = _options.video_height;
	
	// color format is just a suggestion for the encoder, we must adapt to its
	// request later.
	video_format.bitCount = _options.video_srcpixels->BitsPerPixel;
	video_format.clrMasks[0] = _options.video_srcpixels->Rmask;
	video_format.clrMasks[1] = _options.video_srcpixels->Gmask;
	video_format.clrMasks[2] = _options.video_srcpixels->Bmask;
	video_format.clrMasks[3] = _options.video_srcpixels->Amask;
	if(video_format.bitCount == 32) {
		// favor size, alpha channel is useless
		video_format.bitCount = 24;
	}
	m_video.encoder->setup_compress(video_format);
	
	// determine the encoder's pixel format
	uint32_t pixf = SDL_MasksToPixelFormatEnum(
		video_format.bitCount,
		video_format.clrMasks[0],
		video_format.clrMasks[1],
		video_format.clrMasks[2],
		video_format.clrMasks[3]
	);
	if(pixf == SDL_PIXELFORMAT_UNKNOWN) {
		throw std::logic_error("unknown encoder pixel format");
	}
	m_video.enc_pixformat = SDL_AllocFormat(pixf);
	
	// setup pixel format conversion
	if(_options.video_srcpixels->format != m_video.enc_pixformat->format ||
		_options.video_srcpixels->BytesPerPixel != m_video.enc_pixformat->BytesPerPixel
	) {
		m_video.convert = true;
		// +3 is for the last dword write in convert_pixformat()
		size_t size = _options.video_width * m_video.enc_pixformat->BytesPerPixel + 3;
		m_video.linebuf.resize(size);
	}
	
	uint32_t video_buf_size = video_format.sizeImage;
	if(!video_buf_size) {
		video_buf_size = m_video.encoder->needed_buf_size(video_format);
	}
	m_video.enc_buffer.resize(video_buf_size);
	uint32_t video_bytes_sec = video_buf_size * fps;
	
	
	// Audio format setup
	
	switch(_options.audio_encoder) {
		case AVI_AUDIO_PCM:
			m_audio.chunk_fcc = FOURCC("01wb");
			m_audio.encoder = 0;
			break;
		default:
			// TODO Only PCM audio?
			throw std::logic_error("invalid audio format");
	}
	uint32_t audio_bytes_frame = (_options.audio_channels * 2);
	uint32_t audio_bytes_sec = audio_bytes_frame * _options.audio_freq;
	int32_t audio_buffer_size = (_options.audio_freq / fps) * audio_bytes_frame;
	
	WAVFormatEx audio_format;
	memset(&audio_format, 0, sizeof(WAVFormatEx));
	audio_format.audioFormat = WAV_FORMAT_PCM;
	audio_format.numChannels = _options.audio_channels;
	audio_format.sampleRate = _options.audio_freq;
	audio_format.byteRate = audio_bytes_sec;
	audio_format.blockAlign = audio_bytes_frame;
	audio_format.bitsPerSample = 16;
	
	
	// Main header
	
	memset(&m_avimain_hdr, 0, sizeof(AVIMainHeader));
	m_avimain_hdr.microSecPerFrame = uint32_t( USEC_PER_SECOND / fps );
	m_avimain_hdr.maxBytesPerSec = video_bytes_sec + audio_bytes_sec;
	m_avimain_hdr.flags = AVIF_HASINDEX | AVIF_ISINTERLEAVED | AVIF_TRUSTCKTYPE | AVIF_WASCAPTUREFILE;
	m_avimain_hdr.totalFrames = 0; // < to be updated at the end
	m_avimain_hdr.streams = 1 + bool(_options.audio_channels);
	m_avimain_hdr.suggBufSize = video_buf_size + audio_buffer_size;
	m_avimain_hdr.width = _options.video_width;
	m_avimain_hdr.height = _options.video_height;
	m_avimain_hdr_pos = write_chunk(FOURCC("avih"), &m_avimain_hdr, sizeof(AVIMainHeader));
	
	
	/// > LIST strl for the VIDEO STREAM
	write_list_start(FOURCC("strl"));
	
	memset(&m_video.stream_hdr, 0, sizeof(AVIStreamHeader));
	m_video.stream_hdr.type = FOURCC("vids");
	m_video.stream_hdr.handler = m_video.encoder->fourcc();
	m_video.stream_hdr.scale = _options.video_scale;
	m_video.stream_hdr.rate = _options.video_rate;
	m_video.stream_hdr.length = 0; // < to be updated at the end
	m_video.stream_hdr.suggBufSize = video_buf_size; // the largest chunk present in the stream
	m_video.stream_hdr.quality = -1; // default quality
	m_video.stream_hdr.sampleSize = 0; // 0 for video
	m_video.stream_hdr.frame.right = _options.video_width;
	m_video.stream_hdr.frame.bottom = _options.video_height;
	m_video.stream_hdr_pos = write_chunk(FOURCC("strh"), &m_video.stream_hdr, sizeof(AVIStreamHeader));
	
	write_chunk(FOURCC("strf"), &m_video.encoder->format(), m_video.encoder->format().size);
	
	write_list_end();
	/// < LIST strl
	
	/// > LIST strl for the AUDIO STREAM
	if(_options.audio_channels) {
		write_list_start(FOURCC("strl"));
		
		memset(&m_audio.stream_hdr, 0, sizeof(AVIStreamHeader));
		m_audio.stream_hdr.type = FOURCC("auds");
		m_audio.stream_hdr.handler = m_audio.encoder;
		m_audio.stream_hdr.scale = 1;
		m_audio.stream_hdr.rate = _options.audio_freq;
		m_audio.stream_hdr.length = 0; // < to be updated at the end
		m_audio.stream_hdr.suggBufSize = audio_buffer_size;
		m_audio.stream_hdr.quality = -1;
		m_audio.stream_hdr.sampleSize = audio_bytes_frame;
		m_audio.stream_hdr_pos = write_chunk(FOURCC("strh"), &m_audio.stream_hdr, sizeof(AVIStreamHeader));

		write_chunk(FOURCC("strf"), &audio_format, sizeof(WAVFormatEx)-2);
		
		write_list_end();
	}
	/// < LIST strl

	write_list_end(); 
	/// < LIST hdrl

	/// > LIST INFO
	write_list_start(FOURCC("INFO"));
	std::string info(PACKAGE_STRING);
	write_chunk(FOURCC("ISFT"), info.data(), info.length() + 1);
	write_list_end(); 
	/// < LIST INFO
	
	m_index.clear();
	
	/// > LIST movi
	m_movi_list_pos = write_list_start(FOURCC("movi")) - 4;
}

void AVIFile::write_video_frame(const void *_data, uint32_t _stride)
{
	if(write_size_limit_reached()) {
		throw std::overflow_error("AVI size limit reached");
	}
	
	unsigned fflags = m_video.encoder->prepare_frame(0, nullptr, 
			&m_video.enc_buffer[0], m_video.enc_buffer.size());
	
	const uint8_t *srcptr = (uint8_t*)_data;
	const uint8_t *lineptr = srcptr;
	
	if(m_video.convert) {
		lineptr = &m_video.linebuf[0];
	}
	
	for(unsigned y=0; y<m_write_options.video_height; y++) {
		if(m_video.convert) {
			convert_pixformat(srcptr, _stride);
		}
		m_video.encoder->compress_lines(1, &lineptr);
		srcptr += _stride;
	}
	
	uint32_t written = m_video.encoder->finish_frame();

	long int data_pos = write_chunk_start(m_video.chunk_fcc);
	write_chunk_data(&m_video.enc_buffer[0], written);
	write_chunk_end();
	
	add_chunk_index(data_pos, m_video.chunk_fcc, fflags);
	
	m_avimain_hdr.totalFrames++;
	m_video.stream_hdr.length++;
}

void AVIFile::write_audio_samples(const int16_t *_samples, uint32_t _count)
{
	uint32_t data_pos = write_chunk(m_audio.chunk_fcc, _samples, _count*2);
	
	add_chunk_index(data_pos, m_audio.chunk_fcc, 0);
	
	m_audio.stream_hdr.length += m_chunk_whead.chunkSize / m_audio.stream_hdr.sampleSize;
}

void AVIFile::add_chunk_index(uint32_t _data_pos, uint32_t _fcc, unsigned _enc_flags)
{
	/* Example of movi LIST + data chunks + index:
	       488  "LIST"
	       492   size=1209804
	+0     496   "movi"
	+4     500  "00dc"
	       504   size=8509
	       508   data
	       9017  pad
	+8522  9018 "00dc"
	       9022  size=263
	       9026  data
	       9289  pad
	+8794  9290 "01wb"
	       9294  size=2688
	       9298  data
	       ...
	       1210300 "idx1"
	       1210308 "00dc
	       1210312  flags=0x10 // keyframe
	       1210316  offset=4
	       1210320  size=8509
	       1210324 "00dc"
	       1210328  flags=0
	       1210332  offset=8522
	       1210336  size=263
	       1210340 "01wb"
	       1210344  flags=0
	       1210348  offset=8794
	       1210352  size=2688
	*/

	uint32_t abs_offset = _data_pos - sizeof(RIFFChunkHeader);
	uint32_t movi_offset = abs_offset - m_movi_list_pos;
	uint32_t idx_flags = 0;
	
	if(_enc_flags & ENC_FLAGS_KEYFRAME) {
		idx_flags |= AVIIF_KEYFRAME;
	}
	
	m_index.push_back({
		_fcc,
		idx_flags,
		movi_offset,
		m_chunk_whead.chunkSize
	});
}

bool AVIFile::write_size_limit_reached()
{
	// just a back of the envelope estimate
	// space for 1 video frame and audio samples for a video frame duration
	
	// current size
	uint64_t size = m_write_size;
	// + 1 video frame + audio samples for 1 frame duration
	size += m_avimain_hdr.suggBufSize + sizeof(RIFFChunkHeader) * 2;
	// + the index
	size += (m_index.size()+1) * sizeof(AVIOldIndex) + sizeof(RIFFListHeader);
	// + some leeway... (how much is enough?)
	size += 100;
	
	// 4GB AVI files with an idx1 index are not standard, but all my video
	// players read them just fine so whatever.
	return (size > UINT32_MAX);
}

void AVIFile::write_end()
{
	write_list_end();
	/// < LIST movi
	
	/// INDEX
	if(m_index.size()) {
		write_chunk(FOURCC("idx1"), &m_index[0], m_index.size() * sizeof(AVIOldIndex));
	}
	///
	
	// Update main and stream headers
	write_update(m_avimain_hdr_pos, &m_avimain_hdr, sizeof(AVIMainHeader));
	write_update(m_video.stream_hdr_pos, &m_video.stream_hdr, sizeof(AVIStreamHeader));
	if(m_write_options.audio_channels) {
		write_update(m_audio.stream_hdr_pos, &m_audio.stream_hdr, sizeof(AVIStreamHeader));
	}
	
	RIFFFile::write_end();
}

void AVIFile::convert_pixformat(const uint8_t *_srcline, uint32_t _len)
{
	// Naive and general pixel format converter, reads and writes 4 bytes per pixel.
	// Source and destination buffers must have 0 to 3 more bytes at the end,
	// depending on their pixel formats.

	for(uint32_t x=0; x<m_write_options.video_width; x++) {
		
		size_t srcbyte = x * m_write_options.video_srcpixels->BytesPerPixel;
		assert(srcbyte + 4 <= _len);
		
		uint32_t pix = *(uint32_t*)(&_srcline[srcbyte]);
		
		uint8_t r,g,b;
		SDL_GetRGB(pix, m_write_options.video_srcpixels, &r, &g, &b);
		pix = SDL_MapRGB(m_video.enc_pixformat, r, g, b);
		
		size_t destbyte = x * m_video.enc_pixformat->BytesPerPixel;
		assert(destbyte + 4 <= m_video.linebuf.size());
		
		*(uint32_t*)(&m_video.linebuf[destbyte]) = pix;
	}
}
