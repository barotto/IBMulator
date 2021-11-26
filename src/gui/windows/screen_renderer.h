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

#ifndef IBMULATOR_GUI_SCREEN_RENDERER_H
#define IBMULATOR_GUI_SCREEN_RENDERER_H

#include "gui/vector.h"
#include "gui/matrix.h"
#include "hardware/devices/vga.h"

class ScreenRenderer
{
public:
	ScreenRenderer() {}
	virtual ~ScreenRenderer() {}
	
	virtual void load_vga_program(std::string _vshader, std::string _fshader, unsigned _sampler) = 0;
	virtual void store_vga_framebuffer(FrameBuffer &_fb_data, const vec2i &_vga_res) = 0;
	virtual void render_vga(const mat4f &_pmat, const mat4f &_mvmat, const vec2i &_display_size,
		float _brightness, float _contrast, float _saturation, bool _is_monochrome,
		float _ambient, const vec2f &_vga_scale, const vec2f &_reflection_scale) = 0;
	
	virtual void load_monitor_program(std::string _vshader, std::string _fshader, std::string _reflection_map) = 0;
	virtual void render_monitor(const mat4f &_pmat, const mat4f &_mvmat, float _ambient) = 0;
};

#endif