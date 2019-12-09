/*
 * Copyright (C) 2019  Marco Bortolin
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
#include <Rocket/Core.h>
#include <SDL_image.h>

using namespace Rocket::Core;

RocketRenderer_SDL2D::RocketRenderer_SDL2D(SDL_Renderer * _renderer, SDL_Window * _screen)
: RocketRenderer(_renderer, _screen),
m_scissor_enabled(false)
{
}

RocketRenderer_SDL2D::~RocketRenderer_SDL2D()
{
}

// Called by Rocket when it wants to render geometry that it does not wish to optimise.
void RocketRenderer_SDL2D::RenderGeometry(
		Vertex *_vertices, int _num_vertices,
		int *_indices, int _num_indices,
		const TextureHandle _texture,
		const Vector2f &_translation)
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
		Vertex *tl = &_vertices[id], *br = tl;
		for(int v=i; v<i+6; v++) {
			Vertex *vert = &_vertices[_indices[v]];
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
			SDL_RenderCopy(m_renderer, (SDL_Texture*)_texture, &src, &rect);
		} else {
			SDL_SetRenderDrawColor(m_renderer,
				tl->colour.red, tl->colour.green, tl->colour.blue, tl->colour.alpha);
			SDL_RenderFillRect(m_renderer, &rect);
		}
	}
}


// Called by Rocket when it wants to enable or disable scissoring to clip content.
void RocketRenderer_SDL2D::EnableScissorRegion(bool _enable)
{
	m_scissor_enabled = _enable;
}

// Called by Rocket when it wants to change the scissor region.
void RocketRenderer_SDL2D::SetScissorRegion(int _x, int _y, int _width, int _height)
{
	m_scissor_region = {_x, _y, _width, _height};
}

// Called by Rocket when a texture is required to be built from an
// internally-generated sequence of pixels.
bool RocketRenderer_SDL2D::GenerateTexture(TextureHandle &_texture,
		const byte *_source, const Vector2i &_source_dim)
{
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	SDL_Texture *texture = SDL_CreateTexture(m_renderer,
			SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_STATIC,
			_source_dim.x,
			_source_dim.y);
	SDL_UpdateTexture(texture, nullptr, _source, _source_dim.x * 4);
	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	
	_texture = TextureHandle((void*)texture);
	
	return true;
}

// Called by Rocket when a loaded texture is no longer required.
void RocketRenderer_SDL2D::ReleaseTexture(TextureHandle _texture)
{
	SDL_DestroyTexture((SDL_Texture*)_texture);
}

