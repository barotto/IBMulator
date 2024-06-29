/*
 * Copyright (C) 2015-2024  Marco Bortolin
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

#ifndef IBMULATOR_GUI_REND_INTERFACE_SDL2D_H
#define IBMULATOR_GUI_REND_INTERFACE_SDL2D_H

#include "rend_interface.h"
#include "gui/matrix.h"


class RmlRenderer_SDL2D : public RmlRenderer
{
protected:
	SDL_Rect m_scissor_region = {};
	bool m_scissor_enabled = false;
	struct GeometryView {
		Rml::Span<const Rml::Vertex> vertices;
		Rml::Span<const int> indices;
	};

public:
	RmlRenderer_SDL2D(SDL_Renderer * _renderer, SDL_Window * _screen);
	~RmlRenderer_SDL2D();

	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices);
	void ReleaseGeometry(Rml::CompiledGeometryHandle geometry);
	void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture);

	void EnableScissorRegion(bool enable);
	void SetScissorRegion(Rml::Rectanglei region);

	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions);
	void ReleaseTexture(Rml::TextureHandle texture_handle);
	
protected:
	uintptr_t LoadTexture(SDL_Surface *_surface);
};

#endif
