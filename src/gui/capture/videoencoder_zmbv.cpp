/*
 *  Copyright (C) 2002-2015  The DOSBox Team
 *  Copyright (C) 2020  Marco Bortolin
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "ibmulator.h"
#include "videoencoder_zmbv.h"
#include <string.h>
#include <sstream>

#define ZMBV_VERSION_HIGH 0
#define ZMBV_VERSION_LOW  1
#ifdef HAVE_ZLIB
#define ZMBV_COMPRESSION  1 // 1=zlib, 0=none
#else
#define ZMBV_COMPRESSION  0
#endif
#define ZMBV_MAX_VECTOR   16

enum ZMBV_PixelFormat {
	ZMBV_FORMAT_NONE  = 0x00,
	ZMBV_FORMAT_1BPP  = 0x01,
	ZMBV_FORMAT_2BPP  = 0x02,
	ZMBV_FORMAT_4BPP  = 0x03,
	ZMBV_FORMAT_8BPP  = 0x04,
	ZMBV_FORMAT_15BPP = 0x05,
	ZMBV_FORMAT_16BPP = 0x06,
	ZMBV_FORMAT_24BPP = 0x07,
	ZMBV_FORMAT_32BPP = 0x08
};

static uint8_t bpp_to_format(int _bpp)
{
	switch(_bpp) {
		case  1: return ZMBV_FORMAT_1BPP;
		case  2: return ZMBV_FORMAT_2BPP;
		case  4: return ZMBV_FORMAT_4BPP;
		case  8: return ZMBV_FORMAT_8BPP;
		case 15: return ZMBV_FORMAT_15BPP;
		case 16: return ZMBV_FORMAT_16BPP;
		case 24: return ZMBV_FORMAT_24BPP;
		case 32: return ZMBV_FORMAT_32BPP;
	}
	return ZMBV_FORMAT_NONE;
}

enum ZMBV_Masks {
	ZMBV_MASK_KEYFRAME     = 0x01,
	ZMBV_MASK_DELTAPALETTE = 0x02
};


VideoEncoder_ZMBV::VideoEncoder_ZMBV()
{
	create_vector_table();
#ifdef HAVE_ZLIB
	memset(&m_zstream, 0, sizeof(m_zstream));
#endif
}

VideoEncoder_ZMBV::~VideoEncoder_ZMBV()
{
}

std::string VideoEncoder_ZMBV::format_string()
{
	std::stringstream ss;
	ss << m_format.bitCount << "bpp ";
	if(ZMBV_COMPRESSION) {
		ss << "Compressed";
	} else {
		ss << "Uncompressed";
	}
	ss << " RGB";
	return ss.str();
}

void VideoEncoder_ZMBV::setup_compress(BitmapInfoHeader &_format)
{
	if(_format.width <= 0 || _format.height <= 0) {
		throw std::logic_error("invalid frame dimesions");
	}
	
	_format.size = sizeof(BitmapInfoHeader) - (4*4);
	_format.compression = fourcc();
	_format.planes = 1;
	if(_format.bitCount == 0 || _format.bitCount == 24) {
		_format.bitCount = 32;
	}
	// masks are fixed and must be used by the caller
	switch(_format.bitCount) {
		case 32:
			_format.clrMasks[0] = 0x00FF0000; // R mask
			_format.clrMasks[1] = 0x0000FF00; // G mask
			_format.clrMasks[2] = 0x000000FF; // B mask
			_format.clrMasks[3] = 0xFF000000; // A mask
			_format.clrUsed = 0;
			_format.clrImportant = 0;
			break;
		default:
			// TODO 8/16bit?
			throw std::logic_error("unsupported color format");
	}
	_format.xPelsPerMeter = 0;
	_format.yPelsPerMeter = 0;
	_format.sizeImage = needed_buf_size(_format);
	
	m_format = _format;
	m_pixel_fmt = bpp_to_format(m_format.bitCount);
	m_pitch = m_format.width + 2 * ZMBV_MAX_VECTOR;
	
#ifdef HAVE_ZLIB
	if(deflateInit(&m_zstream, 4) != Z_OK) {
		throw std::runtime_error("cannot initialize zlib");
	}
#endif
	
	setup_buffers(16, 16);
	
	m_framecnt = 0;
}

uint32_t VideoEncoder_ZMBV::needed_buf_size(const BitmapInfoHeader &_format)
{
	uint32_t f = 0;
	
	switch (bpp_to_format(_format.bitCount)) {
		case ZMBV_FORMAT_8BPP:
			f = 1;
			break;
		case ZMBV_FORMAT_15BPP:
		case ZMBV_FORMAT_16BPP:
			f = 2;
			break;
		case ZMBV_FORMAT_32BPP:
			f = 4;
			break;
		default:
			throw std::logic_error("invalid pixel format");
	}
	
	f = (f * _format.width * _format.height) + 2 * (1 + (_format.width / 8)) * ( 1 + (_format.height / 8)) + 1024;
	
	return f + f / 1000;
}

unsigned VideoEncoder_ZMBV::prepare_frame(unsigned _flags, uint8_t *_pal, uint8_t *_buf, uint32_t _bufsize)
{
	if(m_framecnt % 300 == 0) {
		_flags |= ZMBV_FLAGS_KEYFRAME;
	}
	
	// replace old frame with new frame
	uint8_t *copyFrame = m_newframe;
	m_newframe = m_oldframe;
	m_oldframe = copyFrame;

	m_compress.linesDone = 0;
	m_compress.writeBuf  = _buf;
	m_compress.writeSize = _bufsize;
	m_compress.writeDone = 1;
	
	// Set a pointer to the first byte which will contain info about this frame
	uint8_t *firstByte = m_compress.writeBuf;
	*firstByte = 0;
	
	// Reset the work buffer
	m_workUsed = 0;
	m_workPos = 0;
	
	unsigned ret = 0;
	
	if(_flags & ZMBV_FLAGS_KEYFRAME) {
		
		// Make a keyframe
		*firstByte |= ZMBV_MASK_KEYFRAME;
		
		KeyframeHeader *header = (KeyframeHeader *)(m_compress.writeBuf + m_compress.writeDone);
		header->high_version = ZMBV_VERSION_HIGH;
		header->low_version  = ZMBV_VERSION_LOW;
		header->compression  = ZMBV_COMPRESSION;
		header->format = m_pixel_fmt;
		header->blockwidth  = 16;
		header->blockheight = 16;
		
		m_compress.writeDone += sizeof(KeyframeHeader);
		
		// Copy the new frame directly over
		if(m_palsize) {
			if(_pal) {
				memcpy(&m_palette, _pal, sizeof(m_palette));
			} else { 
				memset(&m_palette, 0, sizeof(m_palette));
			}
			// keyframes get the full palette
			for(int i=0; i<m_palsize; i++) {
				m_work[m_workUsed++] = m_palette[i*4 + 0];
				m_work[m_workUsed++] = m_palette[i*4 + 1];
				m_work[m_workUsed++] = m_palette[i*4 + 2];
			}
		}
		
#ifdef HAVE_ZLIB
		// Restart deflate
		deflateReset(&m_zstream);
#endif
		
		ret |= ENC_FLAGS_KEYFRAME;
		
	} else {
		
		if(m_palsize && _pal && memcmp(_pal, m_palette, m_palsize * 4)) {
			
			*firstByte |= ZMBV_MASK_DELTAPALETTE;
			
			for(int i=0; i<m_palsize; i++) {
				m_work[m_workUsed++] = m_palette[i*4 + 0] ^ _pal[i*4 + 0];
				m_work[m_workUsed++] = m_palette[i*4 + 1] ^ _pal[i*4 + 1];
				m_work[m_workUsed++] = m_palette[i*4 + 2] ^ _pal[i*4 + 2];
			}
			memcpy(&m_palette, _pal, m_palsize * 4);
		}
		
	}
	
	m_framecnt++;
	
	return ret;
}

void VideoEncoder_ZMBV::compress_lines(int _count, const uint8_t *_lines_data[])
{
	int linePitch = m_pitch * m_pixelsize;
	int lineWidth = m_format.width * m_pixelsize;
	
	uint8_t *destStart = m_newframe + m_pixelsize * (ZMBV_MAX_VECTOR + (m_compress.linesDone + ZMBV_MAX_VECTOR) * m_pitch);
	
	int i = 0;
	while(i < _count && (m_compress.linesDone < m_format.height)) {
		memcpy(destStart, _lines_data[i],  lineWidth);
		destStart += linePitch;
		i++;
		m_compress.linesDone++;
	}
}


uint32_t VideoEncoder_ZMBV::finish_frame()
{
	uint8_t firstByte = *m_compress.writeBuf;
	
	if(firstByte & ZMBV_MASK_KEYFRAME) {
		// Add the full frame data
		uint8_t *readFrame = m_newframe + m_pixelsize * (ZMBV_MAX_VECTOR + ZMBV_MAX_VECTOR * m_pitch);
		for(int i=0; i<m_format.height; i++) {
			memcpy(&m_work[m_workUsed], readFrame, m_format.width * m_pixelsize);
			readFrame += m_pitch * m_pixelsize;
			m_workUsed += m_format.width * m_pixelsize;
		}
	} else {
		// Add the delta frame data
		switch(m_pixel_fmt) {
			case ZMBV_FORMAT_8BPP:
				add_xor_frame<uint8_t>();
				break;
			case ZMBV_FORMAT_15BPP:
			case ZMBV_FORMAT_16BPP:
				add_xor_frame<uint16_t>();
				break;
			case ZMBV_FORMAT_32BPP:
				add_xor_frame<uint32_t>();
				break;
			default:
				throw std::logic_error("invalid pixel format");
		}
	}
	
#ifdef HAVE_ZLIB
	// Create the actual frame with compression
	m_zstream.next_in = (Bytef *)(&m_work[0]);
	m_zstream.avail_in = m_workUsed;
	m_zstream.total_in = 0;

	m_zstream.next_out = (Bytef *)(m_compress.writeBuf + m_compress.writeDone);
	m_zstream.avail_out = m_compress.writeSize - m_compress.writeDone;
	m_zstream.total_out = 0;
	
	deflate(&m_zstream, Z_SYNC_FLUSH);
	
	return m_compress.writeDone + m_zstream.total_out;
#else
	assert(m_workUsed < m_compress.writeSize - m_compress.writeDone);
	memcpy(m_compress.writeBuf + m_compress.writeDone, &m_work[0], m_workUsed);
	return m_compress.writeDone + m_workUsed;
#endif
}

void VideoEncoder_ZMBV::setup_buffers(int _block_width, int _block_height)
{
	m_palsize = 0;
	
	switch(m_pixel_fmt) {
		case ZMBV_FORMAT_8BPP:
			m_pixelsize = 1;
			m_palsize = 256;
			break;
		case ZMBV_FORMAT_15BPP:
		case ZMBV_FORMAT_16BPP:
			m_pixelsize = 2;
			break;
		case ZMBV_FORMAT_32BPP:
			m_pixelsize = 4;
			break;
		default:
			throw std::logic_error("invalid pixel format");
	};
	
	m_bufsize = (m_format.height + 2 * ZMBV_MAX_VECTOR) * m_pitch * m_pixelsize + 2048;

	m_buf1.resize(m_bufsize);
	m_buf2.resize(m_bufsize);
	m_work.resize(m_bufsize);

	std::fill(m_buf1.begin(), m_buf1.end(), 0);
	std::fill(m_buf2.begin(), m_buf2.end(), 0);
	std::fill(m_work.begin(), m_work.end(), 0);
	
	m_oldframe = &m_buf1[0];
	m_newframe = &m_buf2[0];
	
	int xblocks = m_format.width / _block_width;
	int xleft   = m_format.width % _block_width;
	if(xleft) {
		xblocks++;
	}
	int yblocks = m_format.height / _block_height;
	int yleft   = m_format.height % _block_height;
	if(yleft) { 
		yblocks++;
	}
	
	m_blockcount = yblocks * xblocks;
	m_blocks.resize(m_blockcount);

	int i=0;
	for(int y=0; y<yblocks; y++) {
		for(int x=0; x<xblocks; x++) {
			m_blocks[i].start = 
				((y * _block_height) + ZMBV_MAX_VECTOR) * m_pitch +
				 (x * _block_width) + ZMBV_MAX_VECTOR;
			
			if(xleft && x==(xblocks-1)) {
				m_blocks[i].dx = xleft;
			} else {
				m_blocks[i].dx = _block_width;
			}
			
			if(yleft && y==(yblocks-1)) {
				m_blocks[i].dy = yleft;
			} else {
				m_blocks[i].dy = _block_height;
			}
			
			i++;
		}
	}
}

void VideoEncoder_ZMBV::create_vector_table()
{
	m_vector_count = 1;
	m_vector_table[0].x = m_vector_table[0].y = 0;
	
	for(int s = 1; s <= 10; s++) {
		for(int y = 0-s; y <= 0+s; y++) {
			for(int x = 0-s; x <= 0+s; x++) {
				if(abs(x) == s || abs(y) == s) {
					m_vector_table[m_vector_count].x = x;
					m_vector_table[m_vector_count].y = y;
					m_vector_count++;
				}
			}
		}
	}
}

template<class P>
void VideoEncoder_ZMBV::add_xor_frame()
{
	int8_t *vectors = (int8_t*)&m_work[m_workUsed];
	
	// Align the following xor data on 4 byte boundary
	m_workUsed = (m_workUsed + m_blockcount*2 + 3) & ~3;

	for(int b=0; b<m_blockcount; b++) {
		FrameBlock *block= &m_blocks[b];
		int bestvx = 0;
		int bestvy = 0;
		int bestchange = compare_block<P>(0, 0, block);
		int possibles = 64;
		for(int v=0; v<m_vector_count && possibles; v++) {
			if(bestchange < 4) {
				break;
			}
			
			int vx = m_vector_table[v].x;
			int vy = m_vector_table[v].y;
			
			if(possible_block<P>(vx, vy, block) < 4) {
				possibles--;
				int testchange = compare_block<P>(vx, vy, block);
				if(testchange < bestchange) {
					bestchange = testchange;
					bestvx = vx;
					bestvy = vy;
				}
			}
		}
		
		vectors[b*2 + 0] = bestvx << 1;
		vectors[b*2 + 1] = bestvy << 1;
		
		if(bestchange) {
			vectors[b*2 + 0] |= 1;
			add_xor_block<P>(bestvx, bestvy, block);
		}
	}
}

template<class P>
void VideoEncoder_ZMBV::add_xor_block(int _vx, int _vy, FrameBlock *_block)
{
	P *pold = ((P*)m_oldframe) + _block->start + (_vy * m_pitch) + _vx;
	P *pnew = ((P*)m_newframe) + _block->start;
	
	for(int y=0; y<_block->dy; y++) {
		for(int x=0; x<_block->dx; x++) {
			*((P*)&m_work[m_workUsed]) = pnew[x] ^ pold[x];
			m_workUsed += sizeof(P);
		}
		pold += m_pitch;
		pnew += m_pitch;
	}
}

template<class P>
int VideoEncoder_ZMBV::possible_block(int _vx,int _vy,FrameBlock *_block)
{
	P *pold = ((P*)m_oldframe) + _block->start + (_vy * m_pitch) + _vx;
	P *pnew = ((P*)m_newframe) + _block->start;;
	
	int ret = 0;
	
	for(int y=0; y<_block->dy; y+=4) {
		for(int x=0; x<_block->dx; x+=4) {
			int test = 0 - ((pold[x] - pnew[x]) & 0x00ffffff);
			ret -= (test>>31);
		}
		pold += m_pitch * 4;
		pnew += m_pitch * 4;
	}
	
	return ret;
}

template<class P>
int VideoEncoder_ZMBV::compare_block(int _vx, int _vy, FrameBlock *_block)
{
	P *pold = ((P*)m_oldframe) + _block->start + (_vy * m_pitch) + _vx;
	P *pnew = ((P*)m_newframe) + _block->start;
	
	int ret = 0;
	
	for(int y=0; y<_block->dy; y++) {
		for(int x=0; x<_block->dx; x++) {
			int test = 0 - ((pold[x] - pnew[x]) & 0x00ffffff);
			ret -= (test>>31);
		}
		pold += m_pitch;
		pnew += m_pitch;
	}
	
	return ret;
}

