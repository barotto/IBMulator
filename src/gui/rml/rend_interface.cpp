/*
 * Copyright (C) 2015-2021  Marco Bortolin
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
#include "rend_interface.h"
#include "gui.h"
#include <RmlUi/Core.h>
#include <SDL_image.h>

RmlRenderer::RmlRenderer(SDL_Renderer * _renderer, SDL_Window * _screen)
: Rml::RenderInterface(),
m_renderer(_renderer),
m_screen(_screen)
{
}

RmlRenderer::~RmlRenderer()
{
}

// Called by RmlUi when a texture is required by the library.
bool RmlRenderer::LoadTexture(Rml::TextureHandle &texture_handle,
		Rml::Vector2i &texture_dimensions, const std::string &source)
{
	PDEBUGF(LOG_V2, LOG_GUI, "Loading texture %s\n", source.c_str());
	Rml::FileInterface *file_interface = Rml::GetFileInterface();
	Rml::FileHandle file_handle = file_interface->Open(source);
	if (!file_handle)
		return false;

	file_interface->Seek(file_handle, 0, SEEK_END);
	size_t buffer_size = file_interface->Tell(file_handle);
	file_interface->Seek(file_handle, 0, SEEK_SET);

	std::vector<uint8_t> buffer(buffer_size);
	file_interface->Read(&buffer[0], buffer_size, file_handle);
	file_interface->Close(file_handle);

	size_t i;
	for(i = source.length() - 1; i > 0; i--) {
		if(source[i] == '.') {
			break;
		}
	}

	std::string extension = source.substr(i+1, source.length()-i);
	SDL_Surface *surface = IMG_LoadTyped_RW(SDL_RWFromMem(&buffer[0], buffer_size), 1,
			extension.c_str());

	if(!surface) {
		return false;
	}
	try {
		texture_handle = GUI::instance()->load_texture(surface);
	} catch(std::exception &e) {
		PERRF(LOG_GUI, "%s\n", e.what());
		SDL_FreeSurface(surface);
		return false;
	}
	texture_dimensions = Rml::Vector2i(surface->w, surface->h);
	SDL_FreeSurface(surface);
	return true;
}

void RmlRenderer::SetDimensions(int, int)
{
}