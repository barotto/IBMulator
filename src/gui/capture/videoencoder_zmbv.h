/*
 *  Copyright (C) 2002-2015  The DOSBox Team
 *  Copyright (C) 2020-2022  Marco Bortolin
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

#ifndef IBMULATOR_VENC_ZMBV_H
#define IBMULATOR_VENC_ZMBV_H

#include "videoencoder.h"
#include "riff.h"
#include <vector>
#if HAVE_ZLIB
#include <zlib.h>
#else
#include "miniz/miniz.h"
#endif


enum ZMBV_Flags {
	ZMBV_FLAGS_KEYFRAME = 0x1
};

class VideoEncoder_ZMBV : public VideoEncoder
{
private:
	struct FrameBlock {
		int start;
		int dx, dy;
	};
	
	struct CodecVector {
		int x, y;
		int slot;
	};
	
	struct KeyframeHeader {
		uint8_t high_version;
		uint8_t low_version;
		uint8_t compression;
		uint8_t format;
		uint8_t blockwidth;
		uint8_t blockheight;
	} GCC_ATTRIBUTE(packed);

	struct {
		int linesDone;
		int writeSize;
		int writeDone;
		uint8_t *writeBuf;
	} m_compress;
	
	CodecVector m_vector_table[512];
	int m_vector_count;

	uint8_t *m_oldframe, *m_newframe;
	std::vector<uint8_t> m_buf1, m_buf2, m_work;
	int m_bufsize;

	int m_blockcount; 
	std::vector<FrameBlock> m_blocks;

	int m_workUsed, m_workPos;

	int m_palsize;
	uint8_t m_palette[256*4];
	
	BitmapInfoHeader m_format;
	uint8_t m_pixel_fmt;
	int m_pitch;
	int m_pixelsize;
	int m_framecnt;

	z_stream m_zstream;
	
	int m_quality;

public:
	VideoEncoder_ZMBV(int _quality);
	~VideoEncoder_ZMBV();
	
	const char *name() { return "DOSBox Capture Codec (ZMBV)"; }
	uint32_t fourcc() { return FOURCC("ZMBV"); }
	const BitmapInfoHeader &format() { return m_format; }
	std::string format_string();
	
	void setup_compress(BitmapInfoHeader &_format);
	
	uint32_t needed_buf_size(const BitmapInfoHeader &_format);

	unsigned prepare_frame(unsigned _flags, uint8_t *_pal, uint8_t *_buf, uint32_t _bufsize);
	void compress_lines(int _count, const uint8_t *_lines_data[]);
	uint32_t finish_frame();
	
private: 
	void create_vector_table();
	void setup_buffers(int _block_width, int _block_height);
	
	template<class P> void add_xor_frame();
	template<class P> void add_xor_block(int _vx, int _vy, FrameBlock *_block);
	template<class P> int possible_block(int _vx, int _vy, FrameBlock *_block);
	template<class P> int compare_block(int _vx, int _vy, FrameBlock *_block);
};

#endif