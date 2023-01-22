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

#include "ibmulator.h"
#include "gl_shader_pass.h"
#include "shader_exception.h"
#include "filesys.h"
#include "utils.h"

GLShaderPass::GLShaderPass(ShaderPreset *_preset, unsigned _program_n)
: m_preset(_preset), m_program_n(_program_n)
{
	assert(_preset);
	assert(_program_n < _preset->get_shaders().size());

	auto sh = _preset->get_shader(_program_n);

	std::vector<std::string> vs{sh.shader};
	std::vector<std::string> fs{sh.shader};
	std::list<std::string> defs{str_format("#define IBMU_PASS_NUMBER %u\n", _program_n)};
	for(auto & [name, value] : _preset->get_defines()) {
		defs.push_back(str_format("#define %s %s\n", name.c_str(), value.c_str()));
	}
	try {
		m_program = std::make_unique<GLShaderProgram>(vs,fs,defs);
	} catch(ShaderLinkExc &e) {
		throw ShaderLinkExc(e.what(), _program_n);
	}

	m_name = m_program->get_name();
	if(m_name.empty()) {
		m_name = sh.alias;
		if(m_name.empty()) {
			m_name = str_format("Pass%u", m_program_n);
		}
	}

	PINFOF(LOG_V2, LOG_OGL, "Created shader '%s'\n", m_name.c_str());

	// prepare the input sampler
	m_input_sampler = GLTexture::create_gl_sampler(sh.wrap_mode, sh.filter_linear, sh.mipmap_input);
	PDEBUGF(LOG_V1, LOG_OGL, " input sampler GL:%u: %s %s %s\n",
			m_input_sampler,
			ShaderPreset::ms_wrap_str[sh.wrap_mode].c_str(),
			sh.filter_linear ? "linear" : "nearest",
			sh.mipmap_input ? "mipmap" : ""
	);

	// initialize parameter values and constant builtins
	// TODO this should be moved elsewhere (renderer?)
	m_program->use();
	for(auto & param : m_program->get_parameters()) {
		if(param.uniforms) {
			float value = m_preset->get_parameter_value(param.name, param.initial);
			param.set_uniforms(m_program.get(), value);
		}
	}
	// TODO? direction never chages 
	m_program->set_uniform_int(m_program->get_builtin(GLShaderProgram::FrameDirection), 1);
	m_program->set_uniform_uint(m_program->get_builtin(GLShaderProgram::PassNumber), _program_n);
}

void GLShaderPass::create_framebuffer(std::string _name, std::shared_ptr<GLTexture> _target)
{
	m_fbo = std::make_unique<GLFramebuffer>(_name, _target);
}
