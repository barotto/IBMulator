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

#include "ibmulator.h"
#include "gui_sdl2d.h"
#include "rocket/rend_interface_sdl2d.h"
#include <Rocket/Core.h>
#include <SDL.h>
#include <SDL_image.h>


GUI_SDL2D::GUI_SDL2D()
: GUI(),
  m_SDL_renderer(nullptr),
  m_rendflags(SDL_RENDERER_ACCELERATED)
{
}

GUI_SDL2D::GUI_SDL2D(unsigned _rendflags)
: GUI(),
  m_SDL_renderer(nullptr),
  m_rendflags(_rendflags)
{
}

GUI_SDL2D::~GUI_SDL2D()
{
}

void GUI_SDL2D::render()
{
	SDL_Rect rect{0,0,m_width,m_height};
	SDL_RenderSetViewport(m_SDL_renderer, &rect);
	
	SDL_SetRenderDrawColor(m_SDL_renderer, m_backcolor.r, m_backcolor.g, m_backcolor.b, m_backcolor.a);
	SDL_RenderClear(m_SDL_renderer);
	
	// this is a rendering of the screen only (which includes the VGA image).
	// GUI controls are rendered later by the rocket context
	m_windows.interface->render_screen();

	ms_rocket_mutex.lock();
	m_rocket_context->Render();
	ms_rocket_mutex.unlock();

	SDL_RenderPresent(m_SDL_renderer);
}

void GUI_SDL2D::create_window(int _flags)
{
	if(m_rendflags & SDL_RENDERER_ACCELERATED) {
		PINFOF(LOG_V0, LOG_GUI, "Using the hardware accelerated renderer\n");
	} else {
		PINFOF(LOG_V0, LOG_GUI, "Using the software renderer\n");
		if(m_vsync) {
			// force vsync disabled or the renderer creation will fail
			m_vsync = false;
			PINFOF(LOG_V0, LOG_GUI, "VSync is unsupported by this renderer and will be disabled\n");
		}
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

void GUI_SDL2D::shutdown_SDL()
{
	if(m_SDL_renderer) {
		SDL_DestroyRenderer(m_SDL_renderer);
		m_SDL_renderer = nullptr;
	}
	GUI::shutdown_SDL();
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
