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

#ifndef IBMULATOR_GL_SHADER_PASS
#define IBMULATOR_GL_SHADER_PASS

#include "vector.h"
#include "shader_preset.h"
#include "gl_shader_program.h"

class GLShaderPass
{
protected:
	ShaderPreset *m_preset;
	unsigned m_program_n;
	std::string m_name;
	GLuint m_input_sampler;
	std::unique_ptr<GLShaderProgram> m_program;
	std::unique_ptr<GLFramebuffer> m_fbo;

public:
	GLShaderPass(ShaderPreset *_preset, unsigned _program_n);

	const std::string & get_name() const { return m_name; }
	unsigned get_index() const { return m_program_n; }
	const ShaderPreset::ShaderN & get_preset() const { return m_preset->get_shader(m_program_n); }
	GLShaderProgram * get_program() const { return m_program.get(); }
	GLuint get_input_sampler() const { return m_input_sampler; }

	void create_framebuffer(std::string _name, std::shared_ptr<GLTexture> _target);
	GLFramebuffer * get_framebuffer() const { return m_fbo.get(); }
	GLTexture * get_output() const { return m_fbo ? m_fbo->get_target() : nullptr; }
};

#endif