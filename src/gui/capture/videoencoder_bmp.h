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

#ifndef IBMULATOR_VENC_BMP_H
#define IBMULATOR_VENC_BMP_H

#include "videoencoder.h"

class VideoEncoder_BMP : public VideoEncoder
{
	BitmapInfoHeader m_format;
	uint8_t *m_write_buf;
	int m_linecnt;
	uint32_t m_stride;
	
public:
	VideoEncoder_BMP();
	~VideoEncoder_BMP();
	
	const char *name() { return "Bitmap"; }
	uint32_t fourcc() { return 0; }
	const BitmapInfoHeader &format() { return m_format; }
	std::string format_string();
	
	void setup_compress(BitmapInfoHeader &_format);
	
	uint32_t needed_buf_size(const BitmapInfoHeader &_format);
	
	unsigned prepare_frame(unsigned _flags, uint8_t *_pal, uint8_t *_buf, uint32_t _bufsize);
	void compress_lines(int _count, const uint8_t *_lines_data[]);
	uint32_t finish_frame();
	
private:
	uint32_t get_stride(const BitmapInfoHeader &_format);
};

#endif