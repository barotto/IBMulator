/*
 * Copyright (C) 2021  Marco Bortolin
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
#include "filesys.h"
#include "utils.h"
#include <SDL.h>

#ifdef HAVE_ZLIB
#include <zlib.h>
unsigned char *stbiw_zlib_compress(unsigned char *data, int data_len, int *out_len, int quality);
#define STBIW_ZLIB_COMPRESS stbiw_zlib_compress
#endif
#ifdef _WIN32
#define STBIW_WINDOWS_UTF8
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "stb/stb_image.h"

SDL_Surface *stbi_load_from_file(FILE *f)
{
	int w,h,n;
	unsigned char *data = stbi_load_from_file(f, &w, &h, &n, 4);
	if(!data) {
		throw std::runtime_error("Invalid or unsupported texture");
	}

	SDL_Surface *surface = SDL_CreateRGBSurface(0, w, h, 32,
		0x000000FF,
		0x0000FF00,
		0x00FF0000,
		0xFF000000
	);
	if(!surface) {
		free(data);
		throw std::runtime_error("Error creating SDL buffer surface");
	}

	SDL_LockSurface(surface);
	memcpy(surface->pixels, data, w*h*4);
	free(data);
	SDL_UnlockSurface(surface);

	return surface;
}

SDL_Surface *stbi_load(char const *filename)
{
	auto f = FileSys::make_file(filename, "rb");
	if(!f) {
		throw std::runtime_error(str_format("Image file does not exist: %s", filename));
	}
	return stbi_load_from_file(f.get());
}

#ifdef HAVE_ZLIB
unsigned char *stbiw_zlib_compress(unsigned char *data, int data_len, int *out_len, int quality)
{
	// not MT compatible
	static z_stream zstream;
	static bool init = false;
	if(!init) {
		zstream.zalloc = Z_NULL;
		zstream.zfree = Z_NULL;
		zstream.opaque = Z_NULL;
		if(deflateInit(&zstream, quality) != Z_OK) {
			return nullptr;
		}
		init = true;
	} else {
		if(deflateReset(&zstream) != Z_OK) {
			return nullptr;
		}
		if(deflateParams(&zstream, quality, Z_DEFAULT_STRATEGY) != Z_OK) {
			return nullptr;
		}
	}

	zstream.next_in = (Bytef *)(data);
	zstream.avail_in = data_len;
	zstream.total_in = 0;

	unsigned long buf_size = deflateBound(&zstream, data_len);
	Bytef *buf = (Bytef *)(malloc(buf_size));
	if(!buf) {
		return nullptr;
	}

	zstream.next_out = buf;
	zstream.avail_out = buf_size;
	zstream.total_out = 0;

	if(deflate(&zstream, Z_FINISH) != Z_STREAM_END) {
		free(buf);
		return nullptr;
	}
	*out_len = zstream.total_out;

	// caller will free() the buffer
	return buf;
}
#endif
