/*
 * Copyright (C) 2019-2021  Marco Bortolin
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
#include "rend_interface_sdl2d.h"
#include "gui_sdl2d.h"
#include "program.h"
#include <RmlUi/Core.h>


RmlRenderer_SDL2D::RmlRenderer_SDL2D(SDL_Renderer * _renderer, SDL_Window * _screen)
: RmlRenderer(_renderer, _screen),
m_scissor_enabled(false)
{
}

RmlRenderer_SDL2D::~RmlRenderer_SDL2D()
{
}

// Called by RmlUi when it wants to render geometry that it does not wish to optimise.
void RmlRenderer_SDL2D::RenderGeometry(
		Rml::Vertex *_vertices, int _num_vertices,
		int *_indices, int _num_indices,
		const Rml::TextureHandle _texture,
		const Rml::Vector2f &_translation)
{
	UNUSED(_num_vertices);
	
	// we're making many assumptions here, but for the sake of this renderer this will do
	int quads = _num_indices / 6;
	//PDEBUGF(LOG_V2, LOG_GUI, "verts: %d, idx: %d, quads: %d\n", num_vertices, num_indices, quads);
	assert(quads);
	
	SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
	if(m_scissor_enabled) {
		SDL_RenderSetClipRect(m_renderer, &m_scissor_region);
	} else {
		SDL_RenderSetClipRect(m_renderer, NULL);
	}
	
	for(int i=0; i<_num_indices; i+=6) {
		int id = _indices[i];
		assert(id < _num_vertices);
		Rml::Vertex *tl = &_vertices[id], *br = tl;
		for(int v=i; v<i+6; v++) {
			Rml::Vertex *vert = &_vertices[_indices[v]];
			if(vert->position.x <= tl->position.x && vert->position.y <= tl->position.y) {
				tl = vert;
			}
			if(vert->position.x >= br->position.x && vert->position.y >= br->position.y) {
				br = vert;
			}
		}
		assert(tl->position.x <= br->position.x && tl->position.y <= br->position.y);

		SDL_Rect rect;
		rect.x = tl->position.x;
		rect.y = tl->position.y;
		rect.w = br->position.x - tl->position.x;
		rect.h = br->position.y - tl->position.y;
		rect.x += _translation.x;
		rect.y += _translation.y;
		
		if(_texture) {
			int w,h;
			SDL_QueryTexture((SDL_Texture*)_texture, NULL, NULL, &w, &h);
			SDL_Rect src;
			src.x = tl->tex_coord.x * float(w);
			src.y = tl->tex_coord.y * float(h);
			src.w = br->tex_coord.x * float(w) - src.x;
			src.h = br->tex_coord.y * float(h) - src.y;
			SDL_SetTextureColorMod((SDL_Texture*)_texture,
				tl->colour.red, tl->colour.green, tl->colour.blue);
			SDL_SetTextureAlphaMod((SDL_Texture*)_texture,
				tl->colour.alpha);
			SDL_RenderCopy(m_renderer, (SDL_Texture*)_texture, &src, &rect);
		} else {
			SDL_SetRenderDrawColor(m_renderer,
				tl->colour.red, tl->colour.green, tl->colour.blue, tl->colour.alpha);
			SDL_RenderFillRect(m_renderer, &rect);
		}
	}
}


// Called by RmlUi when it wants to enable or disable scissoring to clip content.
void RmlRenderer_SDL2D::EnableScissorRegion(bool _enable)
{
	m_scissor_enabled = _enable;
}

// Called by RmlUi when it wants to change the scissor region.
void RmlRenderer_SDL2D::SetScissorRegion(int _x, int _y, int _width, int _height)
{
	m_scissor_region = {_x, _y, _width, _height};
}

// Called by RmlUi when a texture is required to be built from an
// internally-generated sequence of pixels.
bool RmlRenderer_SDL2D::GenerateTexture(Rml::TextureHandle &_texture,
		const Rml::byte *_source, const Rml::Vector2i &_source_dim)
{
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	SDL_Texture *texture = SDL_CreateTexture(m_renderer,
			SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_STATIC,
			_source_dim.x,
			_source_dim.y);
	SDL_UpdateTexture(texture, nullptr, _source, _source_dim.x * 4);
	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	
	_texture = reinterpret_cast<Rml::TextureHandle>(texture);
	
	return true;
}

// Called by RmlUi when a loaded texture is no longer required.
void RmlRenderer_SDL2D::ReleaseTexture(Rml::TextureHandle _texture)
{
	SDL_DestroyTexture(reinterpret_cast<SDL_Texture*>(_texture));
}

