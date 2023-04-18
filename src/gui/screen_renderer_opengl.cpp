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
#include "gui_opengl.h"
#include "shader_preset.h"
#include "shader_exception.h"
#include "screen_renderer_opengl.h"
#include "program.h"

void ScreenRenderer_OpenGL::init(VGADisplay &_vga)
{
	m_fb_width = _vga.framebuffer().width();
	m_fb_height = _vga.framebuffer().height();

	// prepare the quad vertex data
	GLCALL( glGenVertexArrays(1, &m_vertex_array) );
	GLCALL( glBindVertexArray(m_vertex_array) );
	GLCALL( glGenBuffers(1, &m_vertex_buffer) );
	GLCALL( glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer) );
	GLCALL( glBufferData(GL_ARRAY_BUFFER, sizeof(ms_quad_data), ms_quad_data, GL_STATIC_DRAW) );

	// Vertices
	GLCALL( glVertexAttribPointer(
		0,        // attribute 0. must match the layout in the shader.
		4,        // size
		GL_FLOAT, // type
		GL_FALSE, // normalized?
		6 * sizeof(GLfloat), // stride
		(void*)0  // array buffer offset
	) );
	GLCALL( glEnableVertexAttribArray(0) );

	// Texture
	GLCALL( glVertexAttribPointer(
		1, // attribute 1. must match the layout in the shader.
		2,
		GL_FLOAT,
		GL_FALSE,
		6 * sizeof(GLfloat),
		(void*)(4 * sizeof(GLfloat))
	) );
	GLCALL( glEnableVertexAttribArray(1) );
}

void ScreenRenderer_OpenGL::set_output_sampler(DisplaySampler _sampler_type)
{
	m_output_sampler = _sampler_type;
}

GLShaderChain * ScreenRenderer_OpenGL::load_shader_preset(std::string _preset)
{
	if(!FileSys::file_exists(_preset.c_str())) {
		PERRF(LOG_GUI, "Cannot find shader preset file '%s'\n", _preset.c_str());
		throw std::exception();
	}

	if(FileSys::get_file_ext(_preset) != ".slangp") {
		PERRF(LOG_GUI, "Invalid shader preset file: must be .slangp\n");
		throw std::exception();
	}
	
	std::unique_ptr<GLShaderChain> shader;
	try {
		shader = std::make_unique<GLShaderChain>(_preset);
	} catch(ShaderExc &e) {
		e.log_print(LOG_GUI);
		throw;
	} catch(std::runtime_error &e) {
		PERRF(LOG_GUI, "Error loading the shader preset: %s\n", e.what());
		throw;
	} catch(std::exception &) {
		PERRF(LOG_GUI, "Error loading the shader preset\n");
		throw;
	}

	if(shader->get_passes().empty()) {
		PERRF(LOG_GUI, "No valid render pass defined\n");
		throw std::exception();
	}

	return shader.release();
}

void ScreenRenderer_OpenGL::create_blitter()
{
	if(m_blitter) {
		return;
	}

	// prepare the sampler
	GLCALL( glGenSamplers(1, &m_blitter_sampler) );
	GLCALL( glSamplerParameteri(m_blitter_sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER) );
	GLCALL( glSamplerParameteri(m_blitter_sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER) );
	if(m_output_sampler == DISPLAY_SAMPLER_NEAREST) {
		GLCALL( glSamplerParameteri(m_blitter_sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST) );
		GLCALL( glSamplerParameteri(m_blitter_sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST) );
	} else {
		GLCALL( glSamplerParameteri(m_blitter_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR) );
		GLCALL( glSamplerParameteri(m_blitter_sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR) );
	}

	std::string shader;
	try {
		shader = g_program.config().find_shader_asset("common/fb_blitter.slang");
	} catch(std::runtime_error &e) {
		PERRF(LOG_GUI, "Cannot load the common/fb_blitter.slang shader program: %s\n", e.what());
		throw;
	}
	std::vector<std::string> vs{shader};
	std::vector<std::string> fs{shader};
	std::list<std::string> defs{};

	// select the sampler program
	try {
		if(m_output_sampler == DISPLAY_SAMPLER_NEAREST || m_output_sampler == DISPLAY_SAMPLER_BILINEAR) {
			fs.push_back(g_program.config().find_shader_asset("common/filter_bilinear.slang"));
		} else if(m_output_sampler == DISPLAY_SAMPLER_BICUBIC) {
			fs.push_back(g_program.config().find_shader_asset("common/filter_bicubic.slang"));
		} else {
			PERRF(LOG_GUI, "Invalid upscaling filter type\n");
			throw std::exception();
		}
	} catch(std::runtime_error &e) {
		PERRF(LOG_GUI, "Cannot configure the blitter shader: %s\n", e.what());
		throw;
	}

	try {
		m_blitter = std::make_unique<GLShaderProgram>(vs, fs, defs);
	} catch(ShaderExc &e) {
		e.log_print(LOG_GUI);
		throw;
	} catch(std::runtime_error &e) {
		PERRF(LOG_GUI, "Error loading the shader preset: %s\n", e.what());
		throw;
	} catch(std::exception &) {
		PERRF(LOG_GUI, "Error loading the shader preset\n");
		throw;
	}
}

void ScreenRenderer_OpenGL::load_vga_shader_preset(std::string _preset)
{
	m_vga.shader.reset(load_shader_preset(_preset));

	auto &preset = m_vga.shader->get_preset();
	for(auto & pass : m_vga.shader->get_passes()) {
		for(auto & param : pass->get_program()->get_parameters()) {
			auto pit = std::find_if(m_shader_params.begin(), m_shader_params.end(),
			[&](const ShaderParamsList::value_type &_e) {
				return _e.name == param.name;
			});
			if(pit == m_shader_params.end()) {
				auto &newparam = m_shader_params.emplace_back(param);
				newparam.used = param.uniforms != nullptr;
				newparam.value = newparam.prev_value = preset.get_parameter_value(param.name, param.initial);
			} else {
				pit->used = pit->used || (param.uniforms != nullptr);
			}
		}
	}

	if(m_vga.shader->get_last_pass_output()) {
		create_blitter();
	}

	m_vga.input_size = preset.get_input_size();

	if(m_vga.input_size == ShaderPreset::video_mode) {
		m_input_buff.resize(m_fb_width * m_fb_height);
	}
}

void ScreenRenderer_OpenGL::load_crt_shader_preset(std::string _preset)
{
	m_crt.shader.reset(load_shader_preset(_preset));

	if(m_crt.shader->get_last_pass_output()) {
		create_blitter();
	}
}

const ShaderPreset * ScreenRenderer_OpenGL::get_vga_shader_preset() const
{
	if(m_vga.shader) {
		return &m_vga.shader->get_preset();
	}
	return nullptr;
}

const ShaderPreset * ScreenRenderer_OpenGL::get_crt_shader_preset() const
{
	if(m_crt.shader) {
		return &m_crt.shader->get_preset();
	}
	return nullptr;
}

ShaderPreset::RenderingSize ScreenRenderer_OpenGL::get_rendering_size() const
{
	if(m_vga.shader) {
		return m_vga.shader->get_preset().get_rendering_size();
	}
	return ShaderPreset::VGA;
}

void ScreenRenderer_OpenGL::Shader::update_geometry(const ScreenRenderer::Params::Matrices &_newgeom)
{
	if(!shader) {
		return;
	}

	if(shader->are_framebuffers_ready() && _newgeom.output_size != geometry.output_size) {
		// the shader's viewport is the area of the opengl's viewport onto which the shader is rendererd
		shader->update_size(_newgeom.output_size.x, _newgeom.output_size.y, ShaderPreset::Scale::viewport);
		if(shader->has_feedbacks()) {
			shader->clear_framebuffers();
			rotate_feedbacks();
			shader->update_size(_newgeom.output_size.x, _newgeom.output_size.y, ShaderPreset::Scale::viewport);
			shader->clear_framebuffers();
		}
	}
	geometry = _newgeom;
}

void ScreenRenderer_OpenGL::Shader::update_original(unsigned _width, unsigned _height, GLenum _format, GLenum _type, unsigned _stride, void *_data)
{
	if(!shader) {
		return;
	}

	if(shader->are_framebuffers_ready() &&
	  (_width != unsigned(last_original_size.x) || _height != unsigned(last_original_size.y))) {
		// vga resolution changed, update the outputs that depend on it
		shader->update_size(_width, _height, ShaderPreset::Scale::original);
		if(shader->has_feedbacks()) {
			shader->clear_framebuffers();
			rotate_feedbacks();
			shader->update_size(_width, _height, ShaderPreset::Scale::original);
			shader->clear_framebuffers();
		}
	}
	last_original_size = vec2i(_width, _height);

	GLTexture *original = shader->get_original();

	if(!original) {
		return;
	}

	if(LIKELY(shader->is_history_ready())) {
		rotate_history();
	} else {
		shader->init_history(_width, _height, _format, _type, _stride, _data);
	}

	original->update(_width, _height, _format, _type, _stride, _data);
}

void ScreenRenderer_OpenGL::Shader::rotate_history()
{
	if(shader && history_rotated) {
		shader->rotate_original_history();
		history_rotated = true;
	}
}

void ScreenRenderer_OpenGL::Shader::rotate_feedbacks()
{
	if(shader && !feedbacks_rotated) {
		shader->rotate_output_feedbacks();
		feedbacks_rotated = true;
	}
}

void ScreenRenderer_OpenGL::Shader::render_begin()
{
	if(shader && !shader->are_framebuffers_ready()) {
		shader->init_framebuffers(last_original_size, geometry.output_size);
	}

	rotate_feedbacks();
}

void ScreenRenderer_OpenGL::Shader::render_end()
{
	history_rotated = false;
	feedbacks_rotated = false;
}

void ScreenRenderer_OpenGL::store_screen_params(const ScreenRenderer::Params &_screen)
{
	m_vga.update_geometry(_screen.vga);
	m_crt.update_geometry(_screen.crt);

	m_screen_params = _screen;
	m_screen_params.updated = true;
}

void ScreenRenderer_OpenGL::store_vga_framebuffer(FrameBuffer &_fb, const VideoModeInfo &_mode)
{
	assert(unsigned(_mode.xres * _mode.yres) <= _fb.size());
	assert(_fb.width() == m_fb_width);

	const GLenum fb_format = GL_RGBA;
	const GLenum fb_type = GL_UNSIGNED_INT_8_8_8_8_REV;

	if(m_vga.input_size == ShaderPreset::CRTC) {
		m_vga.update_original(_mode.xres, _mode.yres, fb_format, fb_type, m_fb_width, &_fb[0]);
		m_crt.update_original(_mode.xres, _mode.yres, fb_format, fb_type, m_fb_width, &_fb[0]);
	} else {
		if(_mode.ndots > 1) {
			int x=0,y=0;
			for(int h=0; h<_mode.yres; h+=_mode.nscans) {
				x = 0;
				for(int w=0; w<_mode.xres; w+=_mode.ndots) {
					m_input_buff[uint32_t(y*_mode.imgw) + x] = _fb[uint32_t(h*m_fb_width) + w];
					x++;
				}
				y++;
			}
			m_vga.update_original(_mode.imgw, _mode.imgh, fb_format, fb_type, _mode.imgw, &m_input_buff[0]);
			m_crt.update_original(_mode.imgw, _mode.imgh, fb_format, fb_type, _mode.imgw, &m_input_buff[0]);
		} else {
			m_vga.update_original(_mode.xres, _mode.imgh, fb_format, fb_type, m_fb_width*_mode.nscans, &_fb[0]);
			m_crt.update_original(_mode.xres, _mode.imgh, fb_format, fb_type, m_fb_width*_mode.nscans, &_fb[0]);
		}
	}
}

void ScreenRenderer_OpenGL::render_begin()
{
	PDEBUGF(LOG_V3, LOG_OGL, "Frame: %u\n", m_frame_count);

	m_vga.render_begin();
	m_crt.render_begin();
}

void ScreenRenderer_OpenGL::render_end()
{
	m_frame_count++;
	m_screen_params.updated = false;

	m_vga.render_end();
	m_crt.render_end();
}

void ScreenRenderer_OpenGL::run_shader(GLShaderChain *_shader, const ScreenRenderer::Params::Matrices &_geometry)
{
	PDEBUGF(LOG_V3, LOG_OGL, "Run: %s\n", _shader->get_name().c_str());
	for(auto & pass : _shader->get_passes()) {
		auto prg = pass->get_program();
		prg->use();

		auto fbo = pass->get_framebuffer();
		if(fbo) {
			fbo->use();
			if(m_screen_params.updated) {
				prg->set_uniform_mat4f(prg->get_builtin(GLShaderProgram::MVP), fbo->get_mvpmat());
				prg->set_uniform_mat4f(prg->get_builtin(GLShaderProgram::Projection), fbo->get_pmat());
				prg->set_uniform_mat4f(prg->get_builtin(GLShaderProgram::ModelView), fbo->get_mvmat());
				auto last = _shader->get_last_pass_output();
				if(last) {
					// the last rendering happens on a framebuffer object
					prg->set_uniform_vec4f(prg->get_builtin(GLShaderProgram::FinalViewportSize), last->get_size());
				} else {
					// the last rendering happens direct to backbuffer
					vec4f viewport_size = vec4f(
						_geometry.output_size.x, _geometry.output_size.y,
						1.f/float(_geometry.output_size.x), 1.f/float(_geometry.output_size.y)
					);
					prg->set_uniform_vec4f(prg->get_builtin(GLShaderProgram::FinalViewportSize), viewport_size);
				}
			}
		} else {
			// direct backbuffer rendering
			GLCALL( glBindFramebuffer(GL_FRAMEBUFFER, 0) );
			GLCALL( glViewport(0, 0, m_screen_params.viewport_size.x, m_screen_params.viewport_size.y) );
			GLCALL( glDisable(GL_FRAMEBUFFER_SRGB) );
			if(m_screen_params.updated) {
				prg->set_uniform_mat4f(prg->get_builtin(GLShaderProgram::MVP), _geometry.mvpmat);
				prg->set_uniform_mat4f(prg->get_builtin(GLShaderProgram::Projection), _geometry.pmat);
				prg->set_uniform_mat4f(prg->get_builtin(GLShaderProgram::ModelView), _geometry.mvmat);
				vec4f viewport_size = vec4f(
						_geometry.output_size.x, _geometry.output_size.y,
					1.f/float(_geometry.output_size.x), 1.f/float(_geometry.output_size.y)
				);
				prg->set_uniform_vec4f(prg->get_builtin(GLShaderProgram::FinalViewportSize), viewport_size);
			}
		}

		unsigned pass_frame = m_frame_count;
		if(pass->get_preset().frame_count_mod) {
			pass_frame %= pass->get_preset().frame_count_mod;
		}
		prg->set_uniform_uint(prg->get_builtin(GLShaderProgram::FrameCount), pass_frame);

		if(m_screen_params.updated) {
			prg->set_uniform_float(prg->get_builtin(GLShaderProgram::Brightness), m_screen_params.brightness);
			prg->set_uniform_float(prg->get_builtin(GLShaderProgram::Contrast), m_screen_params.contrast);
			prg->set_uniform_float(prg->get_builtin(GLShaderProgram::Saturation), m_screen_params.saturation);
			prg->set_uniform_float(prg->get_builtin(GLShaderProgram::Ambient), m_screen_params.ambient);
			prg->set_uniform_int(prg->get_builtin(GLShaderProgram::Monochrome), m_screen_params.monochrome);
			prg->set_uniform_int(prg->get_builtin(GLShaderProgram::PowerOn), m_screen_params.poweron);
		}

		for(auto & sampler : prg->get_samplers()) {
			if(!sampler.texture) {
				continue;
			}
			GLuint glname = sampler.texture->get_gl_name();
			if(sampler.tex_uniforms && glname != 0) {
				prg->set_uniform_sampler2D(sampler.tex_uniforms, sampler.gl_sampler, glname);
			}
			if(sampler.size_uniforms) {
				prg->set_uniform_vec4f(sampler.size_uniforms, sampler.texture->get_size());
			}
		}

		render_quad(pass->get_preset().blending_output);

		if(fbo) {
			fbo->update_target();
		}
	}

	if(_shader->get_last_pass_output()) {
		// blit to backbuffer
		PDEBUGF(LOG_V3, LOG_OGL, "Run: blitter\n");
		GLCALL( glBindFramebuffer(GL_FRAMEBUFFER, 0) );
		GLCALL( glViewport(0, 0, m_screen_params.viewport_size.x, m_screen_params.viewport_size.y) );
		GLCALL( glDisable(GL_FRAMEBUFFER_SRGB) );
		const GLTexture *last_output = _shader->get_last_pass_output();
		m_blitter->use();
		m_blitter->set_uniform_sampler2D(
			m_blitter->get_builtin(GLShaderProgram::Source),
			m_blitter_sampler,
			last_output->get_gl_name()
		);
		if(m_screen_params.updated) {
			m_blitter->set_uniform_mat4f(m_blitter->get_builtin(GLShaderProgram::MVP), _geometry.mvpmat);
			m_blitter->set_uniform_mat4f(m_blitter->get_builtin(GLShaderProgram::Projection), _geometry.pmat);
			m_blitter->set_uniform_mat4f(m_blitter->get_builtin(GLShaderProgram::ModelView), _geometry.mvmat);
		}
		render_quad(true);
	}
}

void ScreenRenderer_OpenGL::render_vga()
{
	run_shader(m_vga.shader.get(), m_vga.geometry);
}

void ScreenRenderer_OpenGL::render_crt()
{
	if(m_crt.shader) {
		run_shader(m_crt.shader.get(), m_crt.geometry);
	}
}

void ScreenRenderer_OpenGL::render_quad(bool _blending)
{
	if(_blending) {
		GLCALL( glEnable(GL_BLEND) );
	} else {
		GLCALL( glDisable(GL_BLEND) );
	}

	GLCALL( glBindVertexArray(m_vertex_array) );
	GLCALL( glDrawArrays(GL_TRIANGLES, 0, 6) ); // 2*3 indices starting at 0 -> 2 triangles
	GLCALL( glBindVertexArray(0) );
}

void ScreenRenderer_OpenGL::set_shader_param(std::string _name, float _value)
{
	for(auto & pass : m_vga.shader->get_passes()) {
		auto * prog = pass->get_program();
		auto * param = prog->get_parameter(_name);
		if(param && param->uniforms && param->value != _value) {
			prog->use();
			param->set_uniforms(prog, _value);
		}
	}
}
