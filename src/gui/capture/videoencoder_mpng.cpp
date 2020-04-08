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

#include "ibmulator.h"
#include "videoencoder_mpng.h"
#include "utils.h"
#include <SDL_image.h>
#include <sstream>

VideoEncoder_MPNG::VideoEncoder_MPNG()
:
m_sdl_surface(nullptr),
m_sdl_rwops(nullptr),
m_sdl_rwops_buf(nullptr),
m_sdl_rwops_size(0),
m_linecnt(0)
{
	
}

VideoEncoder_MPNG::~VideoEncoder_MPNG()
{
	free_sdl_surface();
	free_sdl_rwops();
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
	if(m_sdl_rwops_buf != _buf || m_sdl_rwops_size != _bufsize) {
		create_sdl_rwops(_buf, _bufsize);
	}
	
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

uint32_t VideoEncoder_MPNG::finish_frame()
{
	assert(m_sdl_surface);
	
	SDL_RWseek(m_sdl_rwops, 0, RW_SEEK_SET);
	
	if(IMG_SavePNG_RW(m_sdl_surface, m_sdl_rwops, false) != 0) {
		throw std::logic_error("error creating PNG frame");
	}
	
	int64_t len = SDL_RWtell(m_sdl_rwops);
	if(len <= 0) {
		throw std::logic_error("error creating PNG frame");
	}
	
	return len;
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

void VideoEncoder_MPNG::create_sdl_rwops(uint8_t *_buf, uint32_t _bufsize)
{
	// Can't use SDL_RWFromFP because, according to SDL's wiki, it's not available on Windows. 
	assert(_bufsize >= m_format.sizeImage);
	
	m_sdl_rwops_buf = _buf;
	m_sdl_rwops_size = _bufsize;
	
	m_sdl_rwops = SDL_RWFromMem(_buf, _bufsize);
	if(!m_sdl_rwops) {
		throw std::logic_error("error creating screen recording i/o");
	}
}

void VideoEncoder_MPNG::free_sdl_rwops()
{
	if(m_sdl_rwops) {
		if(SDL_RWclose(m_sdl_rwops) != 0) {
			SDL_FreeRW(m_sdl_rwops);
		}
		m_sdl_rwops = nullptr;
	}
}
