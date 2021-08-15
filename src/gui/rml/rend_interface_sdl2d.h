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

#ifndef IBMULATOR_GUI_REND_INTERFACE_SDL2D_H
#define IBMULATOR_GUI_REND_INTERFACE_SDL2D_H

#include "rend_interface.h"
#include "gui/matrix.h"


class RmlRenderer_SDL2D : public RmlRenderer
{
protected:
	SDL_Rect m_scissor_region;
	bool m_scissor_enabled;

public:
	RmlRenderer_SDL2D(SDL_Renderer * _renderer, SDL_Window * _screen);
	~RmlRenderer_SDL2D();

	// Called by RmlUi when it wants to render geometry that it does not wish to optimise.
	void RenderGeometry(Rml::Vertex* vertices, int num_vertices, int* indices,
			int num_indices, Rml::TextureHandle texture, const Rml::Vector2f& translation);

	// Called by RmlUi when it wants to enable or disable scissoring to clip content.
	void EnableScissorRegion(bool enable);
	// Called by RmlUi when it wants to change the scissor region.
	void SetScissorRegion(int x, int y, int width, int height);

	// Called by RmlUi when a texture is required to be built from an internally-generated sequence of pixels.
	bool GenerateTexture(Rml::TextureHandle& texture_handle, const Rml::byte* source,
			const Rml::Vector2i& source_dimensions);
	// Called by RmlUi when a loaded texture is no longer required.
	void ReleaseTexture(Rml::TextureHandle texture_handle);
};

#endif
