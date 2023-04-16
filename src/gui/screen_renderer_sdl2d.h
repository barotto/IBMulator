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
		SDL_Rect vga_rect;
		SDL_Rect crt_rect;
	} m_vga = {};

public:
	ScreenRenderer_SDL2D();
	~ScreenRenderer_SDL2D();

	void init(VGADisplay &_vga, SDL_Renderer *_sdl_renderer);

	void set_output_sampler(DisplaySampler _sampler_type);
	void load_vga_shader_preset(std::string _preset);
	void load_crt_shader_preset(std::string _preset);
	
	void store_screen_params(const ScreenRenderer::Params &);
	void store_vga_framebuffer(FrameBuffer &_fb, const VideoModeInfo &_mode);

	void render_vga();
	void render_crt();

private:
	SDL_Rect to_rect(const mat4f &_mvpmat);
};

#endif