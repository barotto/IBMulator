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
#include "stb/stb.h"

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
	PDEBUGF(LOG_V2, LOG_GUI, "Loading texture '%s'\n", source.c_str());

	Rml::FileInterface *file_interface = Rml::GetFileInterface();
	Rml::FileHandle file_handle = file_interface->Open(source);
	if(!file_handle) {
		PERRF(LOG_GUI, "Cannot find texture file: '%s'\n", source.c_str());
		return false;
	}

	SDL_Surface *surface = nullptr;
	try {
		surface = stbi_load_from_file(reinterpret_cast<FILE*>(file_handle));
	} catch(std::runtime_error &err) {
		PERRF(LOG_GUI, "Error loading texture '%s': %s\n", err.what());
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