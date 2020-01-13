/*
 * Copyright (C) 2019-2020  Marco Bortolin
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

#ifndef IBMULATOR_GUI_SDL2D_H
#define IBMULATOR_GUI_SDL2D_H

#include "gui.h"

class GUI_SDL2D : public GUI
{
protected:
	SDL_Renderer * m_SDL_renderer;
	unsigned m_rendflags;
	
	void create_window(int _flags);
	void create_rocket_renderer();
	void shutdown_SDL();
	
public:
	GUI_SDL2D();
	GUI_SDL2D(unsigned _rendflags);
	~GUI_SDL2D();
	
	GUIRenderer renderer() const { return GUI_RENDERER_SDL2D; }
	SDL_Renderer *sdl_renderer() const { return m_SDL_renderer; }
	void render();
	
	uintptr_t load_texture(SDL_Surface *_surface);
	uintptr_t load_texture(const std::string &_path, vec2i *_texdim=nullptr);
};

#endif