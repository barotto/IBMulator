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
#include "screen_renderer_sdl2d.h"

ScreenRenderer_SDL2D::ScreenRenderer_SDL2D()
: m_sdl_renderer(nullptr)
{
}

ScreenRenderer_SDL2D::~ScreenRenderer_SDL2D()
{
}

void ScreenRenderer_SDL2D::init(VGADisplay &_vga, SDL_Renderer *_sdl_renderer)
{
	m_sdl_renderer = _sdl_renderer;
	m_vga.fb_width = _vga.framebuffer().width();
	m_vga.fb_height = _vga.framebuffer().height();
	m_vga.texture = nullptr;
}

void ScreenRenderer_SDL2D::set_output_sampler(DisplaySampler _sampler_type)
{
	if(_sampler_type == DISPLAY_SAMPLER_NEAREST) {
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
	} else {
		if(_sampler_type == DISPLAY_SAMPLER_BICUBIC) {
			PINFOF(LOG_V1, LOG_GUI, "The bicubic sampler is not supported by this renderer, using bilinear.\n");
		}
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	}
}

void ScreenRenderer_SDL2D::load_vga_shader_preset(std::string _preset)
{
	if(!_preset.empty()) {
		PWARNF(LOG_V1, LOG_GUI, "Shaders are not supported by this renderer.\n");
	}
	if(m_vga.texture) {
		SDL_DestroyTexture(m_vga.texture);
	}
	m_vga.texture = SDL_CreateTexture(m_sdl_renderer,
			SDL_PIXELFORMAT_ABGR8888,
			SDL_TEXTUREACCESS_STREAMING,
			m_vga.fb_width, m_vga.fb_height);
}

void ScreenRenderer_SDL2D::load_crt_shader_preset(std::string)
{
	// no shaders for this renderer
}

void ScreenRenderer_SDL2D::store_screen_params(const ScreenRenderer::Params &_screen)
{
	m_vga.vga_rect = to_rect(_screen.vga.mvpmat);
	m_vga.crt_rect = to_rect(_screen.crt.mvpmat);
}

void ScreenRenderer_SDL2D::store_vga_framebuffer(
		FrameBuffer &_fb, const VideoModeInfo &_mode)
{
	assert(unsigned(_mode.xres * _mode.yres) <= _fb.size());
	assert(_fb.width() == m_vga.fb_width);
	
	m_vga.res = {0, 0, _mode.xres, _mode.yres};
	int result = SDL_UpdateTexture(m_vga.texture, &m_vga.res, &_fb[0], _fb.pitch());
	if(result < 0) {
		PDEBUGF(LOG_V0, LOG_GUI, "Cannot update VGA texture: %s\n", SDL_GetError());
	}
}

SDL_Rect ScreenRenderer_SDL2D::to_rect(const mat4f &_mvpmat)
{
	SDL_Rect vport;
	SDL_Rect dest = {0,0,0,0};
	SDL_RenderGetViewport(m_sdl_renderer, &vport);
	vec3f v0 = (_mvpmat * vec4f(0.f,0.f,0.f,1.f)).xyz();
	vec3f v1 = (_mvpmat * vec4f(1.f,1.f,0.f,1.f)).xyz();
	v0 = (v0 + 1.f) / 2.f;
	v1 = (v1 + 1.f) / 2.f;
	dest.x = v0.x * vport.w;
	dest.y = (1.f - v0.y) * vport.h;
	dest.w = std::abs(v1.x - v0.x) * vport.w;
	dest.h = std::abs(v1.y - v0.y) * vport.h;
	return dest;
}

void ScreenRenderer_SDL2D::render_vga()
{
	if(!m_vga.texture) {
		PDEBUGF(LOG_V0, LOG_GUI, "VGA texture is not ready!");
		return;
	}
	SDL_RenderCopy(m_sdl_renderer, m_vga.texture, &m_vga.res, &m_vga.vga_rect);
}

void ScreenRenderer_SDL2D::render_crt()
{
	SDL_SetRenderDrawColor(m_sdl_renderer, 0,0,0,255);
	SDL_RenderFillRect(m_sdl_renderer, &m_vga.crt_rect);
}

