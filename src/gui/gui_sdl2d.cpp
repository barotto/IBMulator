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
#include "gui_sdl2d.h"
#include "rocket/rend_interface_sdl2d.h"
#include <SDL.h>
#include <SDL_image.h>


GUI_SDL2D::GUI_SDL2D()
: m_rendflags(SDL_RENDERER_ACCELERATED),
  GUI()
{
}

GUI_SDL2D::GUI_SDL2D(unsigned _rendflags)
: m_rendflags(_rendflags),
  GUI()
{
}

GUI_SDL2D::~GUI_SDL2D()
{
}

void GUI_SDL2D::render()
{
	SDL_Rect rect{0,0,m_width,m_height};
	SDL_RenderSetViewport(m_SDL_renderer, &rect);
	GUI::render();
}

void GUI_SDL2D::create_window(int _flags)
{
	if(m_rendflags & SDL_RENDERER_ACCELERATED) {
		PINFOF(LOG_V0, LOG_GUI, "Using the hardware accelerated renderer\n");
	} else {
		PINFOF(LOG_V0, LOG_GUI, "Using the software renderer\n");
	}
	
	m_SDL_window = SDL_CreateWindow(m_wnd_title.c_str(), 
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 
		m_width, m_height, _flags);

	if(!m_SDL_window) {
		PERRF(LOG_GUI, "SDL_CreateWindow(): %s\n", SDL_GetError());
		throw std::exception();
	}
	
	set_window_icon();

	if(m_vsync) {
		m_rendflags |= SDL_RENDERER_PRESENTVSYNC;
	}
	m_SDL_renderer = SDL_CreateRenderer(m_SDL_window, -1, m_rendflags);

	if(!m_SDL_renderer) {
		PERRF(LOG_GUI, "SDL_CreateRenderer(): %s\n", SDL_GetError());
		throw std::exception();
	}
}

void GUI_SDL2D::create_rocket_renderer()
{
	m_rocket_renderer = std::make_unique<RocketRenderer_SDL2D>(m_SDL_renderer, m_SDL_window);
}

uintptr_t GUI_SDL2D::load_texture(SDL_Surface *_surface)
{
	assert(_surface);
	
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	SDL_Texture *texture = SDL_CreateTextureFromSurface(m_SDL_renderer, _surface);

	if(!texture) {
		throw std::runtime_error(SDL_GetError());
	}

	return uintptr_t((void*)texture);
}

uintptr_t GUI_SDL2D::load_texture(const std::string &_path, vec2i *_texdim)
{
	SDL_Surface *surface = IMG_Load(_path.c_str());
	if(!surface) {
		throw std::runtime_error("Unable to load image file");
	}
	SDL_Texture *texture;
	try {
		texture = (SDL_Texture*)(void*)(load_texture(surface));
	} catch(std::exception &e) {
		SDL_FreeSurface(surface);
		throw;
	}
	if(_texdim) {
		*_texdim = vec2i(surface->w, surface->h);
	}
	SDL_FreeSurface(surface);
	return uintptr_t((void*)texture);
}
