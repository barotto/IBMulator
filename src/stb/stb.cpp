/*
 * Copyright (C) 2021-2022  Marco Bortolin
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

#if HAVE_ZLIB
#include <zlib.h>
#else
#include "miniz/miniz.h"
#endif

unsigned char *stbiw_zlib_compress(unsigned char *data, int data_len, int *out_len, int quality);
#define STBIW_ZLIB_COMPRESS stbiw_zlib_compress

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

unsigned char *stbiw_zlib_compress(unsigned char *data, int data_len, int *out_len, int quality)
{
	uLongf buf_size = compressBound(data_len);
	Bytef *buf = (Bytef *)(malloc(buf_size));
	if(!buf) {
		return nullptr;
	}

	compress2(buf, &buf_size, (const Bytef *)data, data_len, quality);
	*out_len = (int)buf_size;

	// caller will free() the buffer
	return buf;
}
