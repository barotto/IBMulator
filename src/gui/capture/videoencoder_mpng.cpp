/*
 * Copyright (C) 2020-2021  Marco Bortolin
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
#include "videoencoder_mpng.h"
#include "utils.h"
#include <sstream>
#include "stb/stb.h"

VideoEncoder_MPNG::VideoEncoder_MPNG(int _quality)
:
m_sdl_surface(nullptr),
m_cur_buf(nullptr),
m_cur_buf_len(0),
m_last_frame_enc(0),
m_linecnt(0),
m_quality(_quality)
{
	
}

VideoEncoder_MPNG::~VideoEncoder_MPNG()
{
	free_sdl_surface();
}

std::string VideoEncoder_MPNG::format_string()
{
	std::stringstream ss;
	ss << m_format.bitCount << "bpp Compressed RGB";
	return ss.str();
}

void VideoEncoder_MPNG::setup_compress(BitmapInfoHeader &_format)
{
	if(_format.width <= 0 || _format.height <= 0) {
		throw std::logic_error("invalid frame dimesions");
	}
	
	_format.size = sizeof(BitmapInfoHeader) - (4*4);
	_format.compression = fourcc();
	_format.planes = 1;

	// masks can be set by the caller but we can decide if the caller doesn't specify
	switch(_format.bitCount) {
		case 24:
		case 32:
			_format.bitCount = 24;
			if(_format.clrMasks[0] == 0) {
				_format.clrMasks[0] = 0x00FF0000; // R mask
				_format.clrMasks[1] = 0x0000FF00; // G mask
				_format.clrMasks[2] = 0x000000FF; // B mask
				_format.clrMasks[3] = 0x00000000; // A mask
			}
			_format.clrUsed = 0;
			_format.clrImportant = 0;
			break;
		default:
			// TODO 8/16bit?
			throw std::logic_error("unsupported pixel format");
	}
	_format.xPelsPerMeter = 0;
	_format.yPelsPerMeter = 0;
	_format.sizeImage = needed_buf_size(_format);
	
	m_format = _format;
	
	create_sdl_surface();
}

uint32_t VideoEncoder_MPNG::needed_buf_size(const BitmapInfoHeader &_format)
{
	// max uncompressed frame size
	return _format.height * round_to_dword(_format.width * _format.bitCount);
}

unsigned VideoEncoder_MPNG::prepare_frame(unsigned _flags, uint8_t *_pal, uint8_t *_buf, uint32_t _bufsize)
{
	// TODO other formats?
	assert(m_format.bitCount == 24);
	assert(_pal == nullptr);
	assert(m_sdl_surface);
	
	UNUSED(_flags);
	UNUSED(_pal);
	
	if(_bufsize < m_format.sizeImage) {
		throw std::logic_error("write buffer too small");
	}
	m_cur_buf = _buf;
	m_cur_buf_len = _bufsize;

	m_linecnt = 0;
	
	return ENC_FLAGS_KEYFRAME;
}

void VideoEncoder_MPNG::compress_lines(int _count, const uint8_t *_lines_data[])
{
	assert(m_sdl_surface);
	
	if(SDL_MUSTLOCK(m_sdl_surface)) {
		SDL_LockSurface(m_sdl_surface);
	}
	unsigned linesize = m_format.width * m_format.bitCount/8;
	for(int y=0; y<_count; y++,m_linecnt++) {
		uint8_t *destline = &((uint8_t*)m_sdl_surface->pixels)[(m_linecnt+y)*m_sdl_surface->pitch];
		memcpy(destline, _lines_data[y], linesize);
	}
	if(SDL_MUSTLOCK(m_sdl_surface)) {
		SDL_UnlockSurface(m_sdl_surface);
	}
}

void VideoEncoder_MPNG::png_stbi_callback(void *context, void *data, int size)
{
	VideoEncoder_MPNG *me = static_cast<VideoEncoder_MPNG*>(context);
	assert(size >= 0);
	if(me->m_cur_buf_len < unsigned(size)) {
		me->m_last_frame_enc = 0;
	} else {
		memcpy(static_cast<VideoEncoder_MPNG*>(context)->m_cur_buf, data, size);
		me->m_last_frame_enc = size;
	}
}

uint32_t VideoEncoder_MPNG::finish_frame()
{
	assert(m_sdl_surface);

	stbi_write_png_compression_level = m_quality;
	if(!stbi_write_png_to_func(VideoEncoder_MPNG::png_stbi_callback, this,
			m_sdl_surface->w, m_sdl_surface->h, m_sdl_surface->format->BytesPerPixel,
			m_sdl_surface->pixels, m_sdl_surface->pitch)) {
		throw std::logic_error("error creating PNG frame");
	}
	if(!m_last_frame_enc) {
		throw std::logic_error("error creating PNG frame");
	}

	return m_last_frame_enc;
}

void VideoEncoder_MPNG::create_sdl_surface()
{
	free_sdl_surface();
	m_sdl_surface = SDL_CreateRGBSurface(
		0, // flags (obsolete)
		m_format.width, m_format.height, // dimensions
		m_format.bitCount, // bit depth
		m_format.clrMasks[0], // R mask
		m_format.clrMasks[1], // G mask
		m_format.clrMasks[2], // B mask
		m_format.clrMasks[3]  // A mask
	);
	if(!m_sdl_surface) {
		throw std::logic_error("error creating the screen recording surface");
	}
}

void VideoEncoder_MPNG::free_sdl_surface()
{
	if(m_sdl_surface) {
		SDL_FreeSurface(m_sdl_surface);
		m_sdl_surface = nullptr;
	}
}
