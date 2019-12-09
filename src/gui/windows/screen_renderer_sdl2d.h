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

#ifndef IBMULATOR_GUI_SCREEN_RENDERER_SDL2D_H
#define IBMULATOR_GUI_SCREEN_RENDERER_SDL2D_H

#include "screen_renderer.h"

class ScreenRenderer_SDL2D : public ScreenRenderer
{
protected:
	SDL_Renderer *m_sdl_renderer;
	
	struct {
		int fb_width; // the framebuffer width
		int fb_height; // the framebuffer width
		SDL_Rect res; // the last VGA image resolution (can be smaller than fb_width/fb_height)
		SDL_Texture *texture;
	} m_vga;
	
public:
	ScreenRenderer_SDL2D();
	~ScreenRenderer_SDL2D();
	
	void init(VGADisplay &_vga, SDL_Renderer *_sdl_renderer);
	
	void load_vga_program(std::string _vshader, std::string _fshader, unsigned _sampler);
	void load_monitor_program(std::string _vshader, std::string _fshader, std::string _reflection_map);
	
	void store_vga_framebuffer(std::vector<uint32_t> &_fb_data, const vec2i &_vga_res);
	
	void render_vga(const mat4f &_mvmat, const vec2i &_display_size, 
		float _brightness, float _contrast, float _saturation, 
		float _ambient, const vec2f &_vga_scale, const vec2f &_reflection_scale);
	void render_monitor(const mat4f &_mvmat, float _ambient);
};

#endif