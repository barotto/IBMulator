/*
 * Copyright (C) 2019-2023  Marco Bortolin
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
#include "rml/rend_interface_sdl2d.h"
#include <RmlUi/Core.h>
#include <SDL.h>
#include "stb/stb.h"


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
	// GUI controls are rendered later by the RmlUi context
	m_windows.interface->render_screen();

	ms_rml_mutex.lock();
	m_rml_context->Render();
	ms_rml_mutex.unlock();

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

void GUI_SDL2D::create_renderer()
{
	m_rml_renderer = std::make_unique<RmlRenderer_SDL2D>(m_SDL_renderer, m_SDL_window, m_rendflags);
}

void GUI_SDL2D::shutdown_SDL()
{
	if(m_SDL_renderer) {
		SDL_DestroyRenderer(m_SDL_renderer);
		m_SDL_renderer = nullptr;
	}
	GUI::shutdown_SDL();
}

void GUI_SDL2D::update_texture(uintptr_t _texture, SDL_Surface *_data)
{
	assert(_data);
	SDL_Texture *texture = reinterpret_cast<SDL_Texture*>(_texture);
	if(!texture) {
		throw std::runtime_error("Invalid texture");
	}
	int w, h;
	SDL_QueryTexture(texture, NULL, NULL, &w, &h);
	if(w != _data->w || h != _data->h) {
		throw std::runtime_error(str_format("Invalid texture size: %dx%d (exp: %dx%d)", _data->w,_data->h, w,h));
	}
	SDL_LockSurface(_data);
	SDL_UpdateTexture(texture, NULL, _data->pixels, _data->pitch);
	SDL_UnlockSurface(_data);
}