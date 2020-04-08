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

#ifndef IBMULATOR_VENC_MPNG_H
#define IBMULATOR_VENC_MPNG_H

#include "videoencoder.h"
#include "riff.h"
#include <vector>
#include <SDL.h>

class VideoEncoder_MPNG : public VideoEncoder
{
	// I don't want to use yet another library so I'll reuse SDL_Image
	SDL_Surface *m_sdl_surface;
	SDL_RWops *m_sdl_rwops;
	uint8_t *m_sdl_rwops_buf;
	uint32_t m_sdl_rwops_size;
	std::vector<uint8_t> m_sdl_rwbuf;
	BitmapInfoHeader m_format;
	int m_linecnt;
	
public:
	VideoEncoder_MPNG();
	~VideoEncoder_MPNG();
	
	const char *name() { return "Motion PNG (MPNG)"; }
	uint32_t fourcc() { return FOURCC("MPNG"); }
	const BitmapInfoHeader &format() { return m_format; }
	std::string format_string();
	
	void setup_compress(BitmapInfoHeader &_format);
	
	uint32_t needed_buf_size(const BitmapInfoHeader &_format);
	
	unsigned prepare_frame(unsigned _flags, uint8_t *_pal, uint8_t *_buf, uint32_t _bufsize);
	void compress_lines(int _count, const uint8_t *_lines_data[]);
	uint32_t finish_frame();

private:
	void create_sdl_surface();
	void free_sdl_surface();
	void create_sdl_rwops(uint8_t *_buf, uint32_t _bufsize);
	void free_sdl_rwops();
};

#endif