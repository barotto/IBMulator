/*
 * Copyright (C) 2023  Marco Bortolin
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

#ifndef IBMULATOR_GL_SHADER_CHAIN
#define IBMULATOR_GL_SHADER_CHAIN

#include "shader_preset.h"
#include "gl_shader_pass.h"
#include "gl_texture.h"

class GLShaderChain
{
public:
	using PassList = std::vector<std::unique_ptr<GLShaderPass>>;

protected:
	std::string m_name;
	ShaderPreset m_preset;
	PassList m_chain;

	using TexList = std::vector<std::shared_ptr<GLTexture>>;
	struct {
		std::shared_ptr<GLTexture> original;
		TexList history;
		TexList output;
		TexList feedback;
		TexList user;
	} m_textures;

	vec2i m_viewport_size;
	bool m_viewport_size_max = false;

	bool m_fb_ready = true;
	bool m_history_ready = true;

public:
	GLShaderChain(std::string _preset);

	const std::string &get_name() const { return m_name; }
	ShaderPreset & get_preset() { return m_preset; }
	PassList & get_passes() { return m_chain; }

	void update_size(unsigned _w, unsigned _h, ShaderPreset::Scale _prop);
	void rotate_output_feedbacks();
	void rotate_original_history();
	GLTexture * get_original();
	GLTexture * get_last_pass_output();
	bool are_framebuffers_ready() const { return m_fb_ready; }
	bool is_history_ready() const { return m_history_ready; }
	unsigned get_history_size() const { return m_textures.history.size(); }
	bool has_feedbacks() const { return m_textures.feedback.size(); }
	void init_framebuffers(const vec2i _source, const vec2i _viewport);
	void init_history(unsigned _width, unsigned _height, GLenum _format, GLenum _type, unsigned _stride, void *_data);
	void clear_framebuffers();

protected:
	void update_size(unsigned _w, unsigned _h, ShaderPreset::Scale _prop, TexList & _target);
	void update_sources_size(TexList & _target);
};

#endif