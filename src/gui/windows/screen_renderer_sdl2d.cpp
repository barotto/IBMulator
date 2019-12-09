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
	m_vga.fb_width = _vga.get_fb_width();
	m_vga.fb_height = _vga.get_fb_height();
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
		std::vector<uint32_t> &_fb_data, const vec2i &_vga_res)
{
	assert(unsigned(_vga_res.x * _vga_res.y) <= _fb_data.size());
	
	m_vga.res = {0, 0, _vga_res.x, _vga_res.y};
	int result = SDL_UpdateTexture(m_vga.texture, &m_vga.res, &_fb_data[0], m_vga.fb_width*4);
	if(!result) {
		PDEBUGF(LOG_V0, LOG_GUI, "Cannot update VGA texture: %s\n", SDL_GetError());
	}
}

void ScreenRenderer_SDL2D::render_vga(const mat4f &_mvmat, const vec2i &_display_size, 
		float _brightness, float _contrast, float _saturation, 
		float _ambient, const vec2f &_vga_scale, const vec2f &_reflection_scale)
{
	UNUSED(_brightness);
	UNUSED(_contrast);
	UNUSED(_saturation);
	UNUSED(_ambient);
	UNUSED(_vga_scale);
	UNUSED(_reflection_scale);
	UNUSED(_display_size);
	
	if(!m_vga.texture) {
		PDEBUGF(LOG_V0, LOG_GUI, "VGA texture is not ready!");
		return;
	}
	SDL_Rect vport;
	SDL_RenderGetViewport(m_sdl_renderer, &vport);
	float vw = float(vport.w), vh = float(vport.h);
	
	SDL_Rect dest = {0,0,0,0};
	
	vec3f scale = _mvmat.get_scale();
	dest.w = vw * scale.x;
	dest.h = vh * scale.y;
	
	vec3f tr = _mvmat.get_translation();
	float xpos = (vw - float(dest.w)) / 2.f;
	dest.x = xpos + ((vw * tr.x) / 2.f);
	float ypos = (vh - float(dest.h)) / 2.f;
	dest.y = ypos - ((vh * tr.y) / 2.f);
	
	SDL_RenderCopy(m_sdl_renderer, m_vga.texture, &m_vga.res, &dest);
}

void ScreenRenderer_SDL2D::render_monitor(const mat4f &_mvmat, float _ambient)
{
	UNUSED(_mvmat);
	UNUSED(_ambient);
}

