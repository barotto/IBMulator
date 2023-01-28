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

#ifndef IBMULATOR_GUI_SCREEN_RENDERER_OPENGL_H
#define IBMULATOR_GUI_SCREEN_RENDERER_OPENGL_H

#include "screen_renderer.h"
#include "gl_shader_chain.h"
#include <GL/glew.h>

class ScreenRenderer_OpenGL : public ScreenRenderer
{
protected:

	struct Shader {
		std::unique_ptr<GLShaderChain> shader;
		ScreenRenderer::Params::Matrices geometry;

		bool feedbacks_rotated = false;
		bool history_rotated = false;
		ShaderPreset::InputSize input_size = ShaderPreset::input_undef;
		vec2i last_original_size;

		void update_geometry(const ScreenRenderer::Params::Matrices &_mats);
		void update_original(unsigned _width, unsigned _height, GLenum _format, GLenum _type, unsigned _stride, void *_data);

		void rotate_history();
		void rotate_feedbacks();

		void render_begin();
		void render_end();
	};

	Shader m_vga;
	Shader m_crt;

	int m_fb_width = 0; // the framebuffer width
	int m_fb_height = 0; // the framebuffer height
	std::vector<uint32_t> m_input_buff;

	std::unique_ptr<GLShaderProgram> m_blitter;
	GLuint m_blitter_sampler;
	DisplaySampler m_output_sampler;
	unsigned m_frame_count = 0;

	GLuint m_vertex_array = -1;
	GLuint m_vertex_buffer = -1;
	inline static GLfloat ms_quad_data[]{
		// Vertices           // Texture coordinates
		0.0, 0.0, 0.0, 1.0,   0.0, 0.0,  // top-left
		1.0, 0.0, 0.0, 1.0,   1.0, 0.0,  // top-right
		0.0, 1.0, 0.0, 1.0,   0.0, 1.0,  // bottom-left
		1.0, 0.0, 0.0, 1.0,   1.0, 0.0,  // top-right
		0.0, 1.0, 0.0, 1.0,   0.0, 1.0,  // bottom-left
		1.0, 1.0, 0.0, 1.0,   1.0, 1.0,  // bottom-right
	};
	ScreenRenderer::Params m_screen_params;

	ShaderParamsList m_shader_params;
	bool m_shader_params_updated = false;
	


	void render_quad(bool _blending = false);

public:
	void init(VGADisplay &_vga);

	void set_output_sampler(DisplaySampler _sampler_type);

	void load_vga_shader_preset(std::string _preset);
	void load_crt_shader_preset(std::string _preset);
	const ShaderPreset * get_vga_shader_preset() const;
	const ShaderPreset * get_crt_shader_preset() const;
	ShaderPreset::RenderingSize get_rendering_size() const;
	
	bool needs_vga_updates() const { return m_vga.shader->get_history_size() || (m_crt.shader && m_crt.shader->get_history_size()); }
	void store_screen_params(const ScreenRenderer::Params &);
	void store_vga_framebuffer(FrameBuffer &_fb, const VideoModeInfo &_mode);

	void render_begin();
	void render_vga();
	void render_crt();
	void render_end();

	const ShaderParamsList * get_shader_params() const { return &m_shader_params; }
	void set_shader_param(std::string _name, float _value);

private:
	GLShaderChain * load_shader_preset(std::string _preset);
	void create_blitter();
	void run_shader(GLShaderChain *, const ScreenRenderer::Params::Matrices &_geometry);
};

#endif
