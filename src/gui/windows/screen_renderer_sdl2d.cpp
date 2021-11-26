/*
 * Copyright (C) 2019-2021  Marco Bortolin
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
#include "gui/gui_sdl2d.h"
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

// Loads the shader program for the VGA part of the screen.
// _vshader : vertex shader
// _fshader : fragment shader
// _sampler : quality of the VGA texture sampler (see gui.h:DisplaySampler)
void ScreenRenderer_SDL2D::load_vga_program(std::string _vshader, std::string _fshader, unsigned _sampler)
{
	UNUSED(_vshader);

	if(m_vga.texture) {
		SDL_DestroyTexture(m_vga.texture);
	}
	if(_sampler == DISPLAY_SAMPLER_NEAREST) {
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
	} else {
		if(_sampler == DISPLAY_SAMPLER_BICUBIC) {
			PINFOF(LOG_V1, LOG_GUI, "The bicubic sampler is not supported by this renderer, using bilinear.\n");
		}
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	}
	m_vga.texture = SDL_CreateTexture(m_sdl_renderer,
			SDL_PIXELFORMAT_ABGR8888,
			SDL_TEXTUREACCESS_STREAMING,
			m_vga.fb_width, m_vga.fb_height);

	if(!_fshader.empty()) {
		PINFOF(LOG_V1, LOG_GUI, "VGA shaders are not supported by this renderer.\n");
	}
}

// Loads the shader program for the monitor (VGA chrome)
// _vshader : vertex shader
// _fshader : fragment shader
// _reflection_map : texture map for the screen reflections
void ScreenRenderer_SDL2D::load_monitor_program(
	std::string _vshader, std::string _fshader,
	std::string _reflection_map)
{
	// no VGA shaders for this renderer
	UNUSED(_vshader);
	UNUSED(_fshader);
	UNUSED(_reflection_map);
}

// Stores the VGA pixels into the OpenGL texture.
// _fb_data : the framebuffer pixel data, can be larger than the current VGA resolution
// _vga_res : the current VGA resolution, can be smaller than the framebuffer data
void ScreenRenderer_SDL2D::store_vga_framebuffer(
		FrameBuffer &_fb, const vec2i &_vga_res)
{
	assert(unsigned(_vga_res.x * _vga_res.y) <= _fb.size());
	assert(_fb.width() == m_vga.fb_width);
	
	m_vga.res = {0, 0, _vga_res.x, _vga_res.y};
	int result = SDL_UpdateTexture(m_vga.texture, &m_vga.res, &_fb[0], _fb.pitch());
	if(result < 0) {
		PDEBUGF(LOG_V0, LOG_GUI, "Cannot update VGA texture: %s\n", SDL_GetError());
	}
}

SDL_Rect ScreenRenderer_SDL2D::to_rect(const mat4f &_pmat, const mat4f &_mvmat)
{
	SDL_Rect vport;
	SDL_Rect dest = {0,0,0,0};
	SDL_RenderGetViewport(m_sdl_renderer, &vport);
	vec3f v0 = (_pmat * (_mvmat * vec4f(0.f,0.f,0.f,1.f))).xyz();
	vec3f v1 = (_pmat * (_mvmat * vec4f(1.f,1.f,0.f,1.f))).xyz();
	v0 = (v0 + 1.f) / 2.f;
	v1 = (v1 + 1.f) / 2.f;
	dest.x = v0.x * vport.w;
	dest.y = (1.f - v0.y) * vport.h;
	dest.w = std::abs(v1.x - v0.x) * vport.w;
	dest.h = std::abs(v1.y - v0.y) * vport.h;
	return dest;
}

void ScreenRenderer_SDL2D::render_vga(const mat4f &_pmat, const mat4f &_mvmat,
		const vec2i &_display_size, 
		float _brightness, float _contrast, float _saturation, bool _is_monochrome, 
		float _ambient, const vec2f &_vga_scale, const vec2f &_reflection_scale)
{
	UNUSED(_brightness);
	UNUSED(_contrast);
	UNUSED(_saturation);
	UNUSED(_is_monochrome);
	UNUSED(_ambient);
	UNUSED(_vga_scale);
	UNUSED(_reflection_scale);
	UNUSED(_display_size);
	
	if(!m_vga.texture) {
		PDEBUGF(LOG_V0, LOG_GUI, "VGA texture is not ready!");
		return;
	}
	SDL_Rect rect = to_rect(_pmat, _mvmat);
	SDL_RenderCopy(m_sdl_renderer, m_vga.texture, &m_vga.res, &rect);
}

void ScreenRenderer_SDL2D::render_monitor(const mat4f &_pmat, const mat4f &_mvmat, float _ambient)
{
	UNUSED(_ambient);
	
	SDL_Rect rect = to_rect(_pmat, _mvmat);
	SDL_SetRenderDrawColor(m_sdl_renderer, 0,0,0,255);
	SDL_RenderFillRect(m_sdl_renderer, &rect);
}

