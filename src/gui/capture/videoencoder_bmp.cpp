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
#include "videoencoder_bmp.h"
#include "utils.h"
#include <SDL_image.h>
#include <sstream>

VideoEncoder_BMP::VideoEncoder_BMP()
:
m_write_buf(nullptr),
m_linecnt(0),
m_stride(0)
{
	memset(&m_format, 0, sizeof(BitmapInfoHeader));
}

VideoEncoder_BMP::~VideoEncoder_BMP()
{

}

std::string VideoEncoder_BMP::format_string()
{
	std::stringstream ss;
	ss << m_format.bitCount << "bpp Uncompressed RGB";
	if(m_format.height < 0) {
		ss << ", top-down";
	}
	return ss.str();
}

void VideoEncoder_BMP::setup_compress(BitmapInfoHeader &_format)
{
	if(_format.width <= 0 || _format.height <= 0) {
		throw std::logic_error("invalid frame dimesions");
	}
	
	// Negative height tells the player this is a top-down BMP.
	// Unfortunately VLC seems to be bugged. It considers dimensions as unsigned ints
	// and reports a wrong height. I don't care, as nobody will really use BMP
	// as a video format anyway. Other players work as intended.
	// The BMP encoder is just a proof of concept.
	_format.height = -_format.height;
	
	_format.planes = 1;
	if(_format.bitCount == 0) {
		_format.bitCount = 24;
	}
	
	// masks are fixed and must be used by the caller
	switch(_format.bitCount) {
		case 24:
		case 32:
			_format.size = sizeof(BitmapInfoHeader) - (4*4);
			_format.compression = BI_RGB;
			_format.clrUsed = 0;
			_format.clrImportant = 0; 
			_format.clrMasks[0] = 0x00FF0000;
			_format.clrMasks[1] = 0x0000FF00;
			_format.clrMasks[2] = 0x000000FF;
			_format.clrMasks[3] = 0xFF000000;
			break;
		default:
			// TODO 8/15/16bit?
			throw std::logic_error("unsupported image format");
	}
	_format.sizeImage = needed_buf_size(_format);
	_format.xPelsPerMeter = 0;
	_format.yPelsPerMeter = 0;
	
	m_format = _format;
	m_stride = get_stride(m_format);
}


uint32_t VideoEncoder_BMP::needed_buf_size(const BitmapInfoHeader &_format)
{
	const unsigned stride = get_stride(_format);
	// use abs as BMPs can have a negative height
	return stride * abs(_format.height);
}

uint32_t VideoEncoder_BMP::get_stride(const BitmapInfoHeader &_format)
{
	// For uncompressed RGB formats, the minimum stride is always the image 
	// width in bytes, rounded up to the nearest dword.
	return round_to_dword(_format.width * _format.bitCount);
}

unsigned VideoEncoder_BMP::prepare_frame(unsigned _flags, uint8_t *_pal, uint8_t *_buf, uint32_t _bufsize)
{
	// TODO 8/15/16bit
	assert(m_format.bitCount == 24 || m_format.bitCount == 32);
	assert(_pal == nullptr);
	
	UNUSED(_flags);
	UNUSED(_pal);
	
	if(_bufsize < m_format.sizeImage) {
		throw std::logic_error("write buffer too small");
	}
	m_write_buf = _buf;
	m_linecnt = 0;
	
	return ENC_FLAGS_KEYFRAME;
}

void VideoEncoder_BMP::compress_lines(int _count, const uint8_t *_lines_data[])
{
	unsigned linesize = m_format.width * m_format.bitCount/8;
	
	for(int y=0; y<_count; y++,m_linecnt++) {
		uint8_t *destline = &m_write_buf[(m_linecnt+y) * m_stride];
		memcpy(destline, _lines_data[y], linesize);
	}
}

uint32_t VideoEncoder_BMP::finish_frame()
{
	return m_linecnt * m_stride;
}


