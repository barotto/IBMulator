/*
 * Copyright (C) 2015-2023  Marco Bortolin
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

#ifndef IBMULATOR_GUI_REND_INTERFACE_H
#define IBMULATOR_GUI_REND_INTERFACE_H

#include <SDL.h>
#include <RmlUi/Core/RenderInterface.h>


class RmlRenderer : public Rml::RenderInterface
{
protected:
	SDL_Renderer *m_renderer;
	SDL_Window *m_screen;
	std::map<std::string, Rml::TextureHandle> m_named_textures;

public:
	RmlRenderer(SDL_Renderer * _renderer, SDL_Window * _screen);
	virtual ~RmlRenderer();

	virtual bool LoadTexture(Rml::TextureHandle &_texture_handle,
			Rml::Vector2i &_texture_dimensions, const std::string &_source);

	// called by the GUI
	virtual void SetDimensions(int _width, int _height);

	Rml::TextureHandle GetNamedTexture(const std::string &_name);

protected:
	virtual uintptr_t LoadTexture(SDL_Surface *_surface) = 0;
	bool LoadNamedTexture(Rml::TextureHandle &texture_handle_,
			Rml::Vector2i &texture_dimensions_, const std::string &_source);
};

#endif
