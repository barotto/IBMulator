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

#ifndef IBMULATOR_GUI_SCREEN_RENDERER_H
#define IBMULATOR_GUI_SCREEN_RENDERER_H

#include "vector.h"
#include "shader_preset.h"
#include "matrix.h"
#include "hardware/devices/vga.h"
#include "gl_shader_program.h"

enum DisplaySampler {
	DISPLAY_SAMPLER_NEAREST,
	DISPLAY_SAMPLER_BILINEAR,
	DISPLAY_SAMPLER_BICUBIC,
};

enum DisplayAspect {
	DISPLAY_ASPECT_FIXED,
	DISPLAY_ASPECT_VGA,
	DISPLAY_ASPECT_AREA,
	DISPLAY_ASPECT_ORIGINAL
};

constexpr double ORIGINAL_MONITOR_RATIO = 4.0 / 3.0;

enum DisplayScale {
	DISPLAY_SCALE_1X,
	DISPLAY_SCALE_FILL,
	DISPLAY_SCALE_INTEGER
};

class ScreenRenderer
{
public:
	struct Params {
		vec2i viewport_size; // size of the entire viewport in pixels

		struct Matrices {
			vec2i output_size = 0; // size of the output rendering inside the viewport in pixels 
			mat4f mvmat;  // ModelView matrix (offset and scale of the output inside the viewport)
			mat4f pmat;   // Projection matrix
			mat4f mvpmat; // MVP matrix
		};

		Matrices vga;
		Matrices crt;

		float brightness = 1.f;
		float contrast = 1.f;
		float saturation = 1.f;
		float ambient = 1.f;
		bool monochrome = false;
		bool poweron = false;

		bool updated = true;
	};

	struct ShaderParam {
		std::string name, desc;
		float min = 0.0, max = 0.0, step = 0.0;
		float value = 0.0, prev_value = 0.0;
		bool used = false;

		ShaderParam() {}
		ShaderParam(const GLShaderProgram::Parameter &_p) :
			name(_p.name), desc(_p.desc),
			min(_p.min), max(_p.max), step(_p.step),
			value(_p.value), prev_value(_p.value) {}

		std::string get_value_str() const {
			return str_format("%g", value);
		}
	};
	using ShaderParamsList = std::vector<ShaderParam>;

public:
	ScreenRenderer() {}
	virtual ~ScreenRenderer() {}

	virtual void set_output_sampler(DisplaySampler _sampler_type) = 0;

	virtual void load_vga_shader_preset(std::string _preset) = 0;
	virtual void load_crt_shader_preset(std::string _preset) = 0;
	virtual const ShaderPreset * get_vga_shader_preset() const { return nullptr; }
	virtual const ShaderPreset * get_crt_shader_preset() const { return nullptr; }
	virtual ShaderPreset::RenderingSize get_rendering_size() const { return ShaderPreset::VGA; }

	virtual bool needs_vga_updates() const { return false; }
	virtual void store_vga_framebuffer(FrameBuffer &_fb_data, const VideoModeInfo &_mode) = 0;
	virtual void store_screen_params(const ScreenRenderer::Params &) = 0;

	virtual void render_begin() {}
	virtual void render_vga() = 0;
	virtual void render_crt() = 0;
	virtual void render_end() {}

	virtual const ShaderParamsList * get_shader_params() const { return nullptr; }
	virtual void set_shader_param(std::string, float) {}
};

#endif