/*
 * Copyright (C) 2019-2024  Marco Bortolin
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
: RmlRenderer(_renderer, _screen)
{
}

RmlRenderer_SDL2D::~RmlRenderer_SDL2D()
{
}

Rml::CompiledGeometryHandle RmlRenderer_SDL2D::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
{
	GeometryView *data = new GeometryView{vertices, indices};
	return reinterpret_cast<Rml::CompiledGeometryHandle>(data);
}

void RmlRenderer_SDL2D::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
	delete reinterpret_cast<GeometryView*>(geometry);
}

void RmlRenderer_SDL2D::RenderGeometry(Rml::CompiledGeometryHandle _handle,
		Rml::Vector2f _translation, Rml::TextureHandle _texture)
{
	const GeometryView *geometry = reinterpret_cast<GeometryView*>(_handle);
	if(geometry->indices.empty()) {
		return;
	}
	const Rml::Vertex *vertices = geometry->vertices.data();
	const size_t num_vertices = geometry->vertices.size();
	const int* indices = geometry->indices.data();
	const size_t num_indices = geometry->indices.size();
	SDL_Texture *sdl_texture = reinterpret_cast<SDL_Texture*>(_texture);

	static std::vector<SDL_FPoint> positions(4);
	positions.resize(num_vertices);

	for (size_t i = 0; i < num_vertices; i++) {
		positions[i].x = vertices[i].position.x + _translation.x;
		positions[i].y = vertices[i].position.y + _translation.y;
	}
	
	SDL_RenderGeometryRaw(
		m_renderer,
		sdl_texture,
		&positions[0].x, // xy: Vertex positions
		sizeof(SDL_FPoint), // xy_stride
		(const SDL_Color*)&vertices->colour, // color
		sizeof(Rml::Vertex), // color_stride
		&vertices->tex_coord.x, // uv
		sizeof(Rml::Vertex), // uv_stride
		(int)num_vertices,
		indices,
		(int)num_indices,
		4 // size_indices (4=int)
	);
}

void RmlRenderer_SDL2D::EnableScissorRegion(bool _enable)
{
	if(_enable) {
		SDL_RenderSetClipRect(m_renderer, &m_scissor_region);
	} else {
		SDL_RenderSetClipRect(m_renderer, nullptr);
	}

	m_scissor_enabled = _enable;
}

void RmlRenderer_SDL2D::SetScissorRegion(Rml::Rectanglei region)
{
	m_scissor_region.x = region.Left();
	m_scissor_region.y = region.Top();
	m_scissor_region.w = region.Width();
	m_scissor_region.h = region.Height();

	if(m_scissor_enabled) {
		SDL_RenderSetClipRect(m_renderer, &m_scissor_region);
	}
}

uintptr_t RmlRenderer_SDL2D::LoadTexture(SDL_Surface *_surface)
{
	assert(_surface);

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	SDL_Texture *texture = SDL_CreateTextureFromSurface(m_renderer, _surface);

	if(!texture) {
		throw std::runtime_error(SDL_GetError());
	}

	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

	return uintptr_t((void*)texture);
}

Rml::TextureHandle RmlRenderer_SDL2D::GenerateTexture(Rml::Span<const Rml::byte> _source, Rml::Vector2i _source_dim)
{
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	SDL_Texture *texture = SDL_CreateTexture(m_renderer,
			SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_STATIC,
			_source_dim.x,
			_source_dim.y);
	SDL_UpdateTexture(texture, nullptr, _source.data(), _source_dim.x * 4);
	SDL_SetTextureBlendMode(texture, GUI_SDL2D::blend_mode());

	return reinterpret_cast<Rml::TextureHandle>(texture);
}

void RmlRenderer_SDL2D::ReleaseTexture(Rml::TextureHandle _texture)
{
	SDL_DestroyTexture(reinterpret_cast<SDL_Texture*>(_texture));
}

