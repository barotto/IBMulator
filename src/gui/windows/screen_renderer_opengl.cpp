/*
 * Copyright (C) 2019-2020  Marco Bortolin
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
#include "gui/gui_opengl.h"
#include "screen_renderer_opengl.h"

ScreenRenderer_OpenGL::ScreenRenderer_OpenGL()
:
m_vertex_buffer(GL_INVALID_VALUE),
m_quad_data{
	-1.0f, -1.0f, 0.0f,
	 1.0f, -1.0f, 0.0f,
	-1.0f,  1.0f, 0.0f,
	-1.0f,  1.0f, 0.0f,
	 1.0f, -1.0f, 0.0f,
	 1.0f,  1.0f, 0.0f
}
{
	m_monitor.reflection_map = GL_INVALID_VALUE;
}

ScreenRenderer_OpenGL::~ScreenRenderer_OpenGL()
{
}

void ScreenRenderer_OpenGL::init(VGADisplay &_vga)
{
	m_vga.fb_width = _vga.framebuffer().width();
	
	// prepare the VGA framebuffer texture
	m_vga.glintformat = GL_RGBA;
	m_vga.glformat = GL_RGBA;
	m_vga.gltype = GL_UNSIGNED_INT_8_8_8_8_REV;
	
	GLCALL( glGenTextures(1, &m_vga.texture) );
	GLCALL( glBindTexture(GL_TEXTURE_2D, m_vga.texture) );
	GLCALL( glTexImage2D(GL_TEXTURE_2D, 0, m_vga.glintformat,
			m_vga.fb_width, m_vga.fb_width,
			0, m_vga.glformat, m_vga.gltype, nullptr) );
	GLCALL( glBindTexture(GL_TEXTURE_2D, 0) );
	
	// prepare the quad vertex data
	GLCALL( glGenBuffers(1, &m_vertex_buffer) );
	GLCALL( glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer) );
	GLCALL( glBufferData(GL_ARRAY_BUFFER, sizeof(m_quad_data), m_quad_data, GL_DYNAMIC_DRAW) );
	GLCALL( glBindBuffer(GL_ARRAY_BUFFER, 0) );
}

// Loads the shader program for the VGA part of the screen).
// _vshader : vertex shader
// _fshader : fragment shader
// _sampler : quality of the VGA texture sampler (see gui.h:DisplaySampler)
void ScreenRenderer_OpenGL::load_vga_program(std::string _vshader, std::string _fshader, unsigned _sampler)
{
	std::vector<std::string> vs, fs;
	std::string shadersdir = GUI::shaders_dir();

	// select the sampler program
	if(_sampler == DISPLAY_SAMPLER_NEAREST || _sampler == DISPLAY_SAMPLER_BILINEAR) {
		fs.push_back(shadersdir + "filter_bilinear.fs");
	} else if(_sampler == DISPLAY_SAMPLER_BICUBIC) {
		fs.push_back(shadersdir + "filter_bicubic.fs");
	} else {
		PERRF(LOG_GUI, "Invalid sampler interpolation method\n");
		throw std::exception();
	}

	// prepare the VGA sampler
	GLCALL( glGenSamplers(1, &m_vga.sampler) );
	GLCALL( glSamplerParameteri(m_vga.sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER) );
	GLCALL( glSamplerParameteri(m_vga.sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER) );
	if(_sampler == DISPLAY_SAMPLER_NEAREST) {
		GLCALL( glSamplerParameteri(m_vga.sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST) );
		GLCALL( glSamplerParameteri(m_vga.sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST) );
	} else {
		GLCALL( glSamplerParameteri(m_vga.sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR) );
		GLCALL( glSamplerParameteri(m_vga.sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR) );
	}
	
	// prepare the program sources
	PINFOF(LOG_V1, LOG_GUI, "Using VGA shader: %s\n", _fshader.c_str());
	vs.push_back(_vshader);
	fs.push_back(shadersdir + "color_functions.glsl");
	fs.push_back(_fshader);

	// load the program
	try {
		m_vga.program = GUI_OpenGL::load_program(vs, fs);
	} catch(std::exception &e) {
		PERRF(LOG_GUI, "Unable to create the shader program!\n");
		throw std::exception();
	}

	//find the uniforms
	GLCALL( m_vga.uniforms.vga_map = glGetUniformLocation(m_vga.program, "iVGAMap") );
	if(m_vga.uniforms.vga_map == -1) {
		PWARNF(LOG_V2, LOG_GUI, "iVGAMap not found in shader program\n");
	}
	GLCALL( m_vga.uniforms.brightness = glGetUniformLocation(m_vga.program, "iBrightness") );
	if(m_vga.uniforms.brightness == -1) {
		PWARNF(LOG_V2, LOG_GUI, "iBrightness not found in shader program\n");
	}
	GLCALL( m_vga.uniforms.contrast = glGetUniformLocation(m_vga.program, "iContrast") );
	if(m_vga.uniforms.contrast == -1) {
		PWARNF(LOG_V2, LOG_GUI, "iContrast not found in shader program\n");
	}
	GLCALL( m_vga.uniforms.saturation = glGetUniformLocation(m_vga.program, "iSaturation") );
	if(m_vga.uniforms.saturation == -1) {
		PWARNF(LOG_V2, LOG_GUI, "iSaturation not found in shader program\n");
	}
	GLCALL( m_vga.uniforms.mvmat = glGetUniformLocation(m_vga.program, "iModelView") );
	if(m_vga.uniforms.mvmat == -1) {
		PWARNF(LOG_V2, LOG_GUI, "iModelView not found in shader program\n");
	}
	GLCALL( m_vga.uniforms.display_size = glGetUniformLocation(m_vga.program, "iDisplaySize") );
	if(m_vga.uniforms.display_size == -1) {
		PWARNF(LOG_V2, LOG_GUI, "iDisplaySize not found in shader program\n");
	}

	// additional uniforms, don't notify the user
	GLCALL( m_vga.uniforms.vga_scale = glGetUniformLocation(m_vga.program, "iVGAScale") );
	GLCALL( m_vga.uniforms.ambient = glGetUniformLocation(m_vga.program, "iAmbientLight") );
	GLCALL( m_vga.uniforms.reflection_map = glGetUniformLocation(m_vga.program, "iReflectionMap") );
	GLCALL( m_vga.uniforms.reflection_scale = glGetUniformLocation(m_vga.program, "iReflectionScale") );
}

// Loads the shader program for the monitor (VGA chrome)
// _vshader : vertex shader
// _fshader : fragment shader
// _reflection_map : texture map for the screen reflections
void ScreenRenderer_OpenGL::load_monitor_program(
	std::string _vshader, std::string _fshader,
	std::string _reflection_map)
{
	try {
		m_monitor.program = GUI_OpenGL::load_program({_vshader}, {_fshader});
	} catch(std::exception &e) {
		PERRF(LOG_GUI, "Unable to create the shader program!\n");
		throw std::exception();
	}
	
	GLCALL( m_monitor.uniforms.mvmat = glGetUniformLocation(m_monitor.program, "iModelView") );
	GLCALL( m_monitor.uniforms.ambient = glGetUniformLocation(m_monitor.program, "iAmbientLight") );
	GLCALL( m_monitor.uniforms.reflection_map = glGetUniformLocation(m_monitor.program, "iReflectionMap") );
	
	m_monitor.reflection_map = GUI::instance()->load_texture(_reflection_map);

	GLCALL( glGenSamplers(1, &m_monitor.reflection_sampler) );
	GLCALL( glSamplerParameteri(m_monitor.reflection_sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER) );
	GLCALL( glSamplerParameteri(m_monitor.reflection_sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER) );
	GLCALL( glSamplerParameteri(m_monitor.reflection_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR) );
	GLCALL( glSamplerParameteri(m_monitor.reflection_sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR) );
}

// Stores the VGA pixels into the OpenGL texture.
// _fb_data : the framebuffer pixel data, can be larger than the current VGA resolution
// _vga_res : the current VGA resolution, can be smaller than the framebuffer data
void ScreenRenderer_OpenGL::store_vga_framebuffer(
		FrameBuffer &_fb, const vec2i &_vga_res)
{
	assert(unsigned(_vga_res.x * _vga_res.y) <= _fb.size());
	assert(_fb.width() == m_vga.fb_width);
	
	GLCALL( glActiveTexture(GL_TEXTURE0) );
	GLCALL( glBindTexture(GL_TEXTURE_2D, m_vga.texture) );
	
	GLCALL( glPixelStorei(GL_UNPACK_ROW_LENGTH, m_vga.fb_width) );
	if(!(m_vga.res == _vga_res)) {
		GLCALL( glTexImage2D(
				GL_TEXTURE_2D, 0,       // target, level
				m_vga.glintformat,
				_vga_res.x, _vga_res.y, // width, height
				0,                      // border
				m_vga.glformat, m_vga.gltype,
				&_fb[0]
		) );
		m_vga.res = _vga_res;
	} else {
		GLCALL( glTexSubImage2D(
			GL_TEXTURE_2D, 0,       // target, level
			0, 0,                   // xoffset, yoffset
			_vga_res.x, _vga_res.y, // width, height
			m_vga.glformat, m_vga.gltype,
			&_fb[0]
		) );
	}
	GLCALL( glPixelStorei(GL_UNPACK_ROW_LENGTH, 0) );
}

void ScreenRenderer_OpenGL::render_vga(const mat4f &_mvmat, const vec2i &_display_size, 
		float _brightness, float _contrast, float _saturation, 
		float _ambient, const vec2f &_vga_scale, const vec2f &_reflection_scale)
{
	// enable VGA shader program and set its uniforms
	GLCALL( glUseProgram(m_vga.program) );
	GLCALL( glUniformMatrix4fv(m_vga.uniforms.mvmat, 1, GL_FALSE, _mvmat.data()) );
	GLCALL( glUniform2iv(m_vga.uniforms.display_size, 1, _display_size) );
	if(g_machine.is_on()) {
		GLCALL( glUniform1f(m_vga.uniforms.brightness, _brightness) );
		GLCALL( glUniform1f(m_vga.uniforms.contrast,   _contrast) );
		GLCALL( glUniform1f(m_vga.uniforms.saturation, _saturation) );
	} else {
		GLCALL( glUniform1f(m_vga.uniforms.brightness, 1.f) );
		GLCALL( glUniform1f(m_vga.uniforms.contrast,   1.f) );
		GLCALL( glUniform1f(m_vga.uniforms.saturation, 1.f) );
	}
	GLCALL( glUniform1f(m_vga.uniforms.ambient, _ambient) );
	GLCALL( glUniform2fv(m_vga.uniforms.vga_scale, 1, _vga_scale) );
	GLCALL( glUniform2fv(m_vga.uniforms.reflection_scale, 1, _reflection_scale) );
	
	// texunit0 is the VGA image
	GLCALL( glActiveTexture(GL_TEXTURE0) );
	GLCALL( glBindTexture(GL_TEXTURE_2D, m_vga.texture) );
	GLCALL( glBindSampler(0, m_vga.sampler) );
	GLCALL( glUniform1i(m_vga.uniforms.vga_map, 0) );
	
	// texunit1 is the reflection map
	if(m_monitor.reflection_map != GL_INVALID_VALUE) {
		GLCALL( glActiveTexture(GL_TEXTURE1) );
		GLCALL( glBindTexture(GL_TEXTURE_2D, m_monitor.reflection_map) ); // map and sampler are the same as the monitor's
		GLCALL( glBindSampler(1, m_monitor.reflection_sampler) );
		GLCALL( glUniform1i(m_vga.uniforms.reflection_map, 1) );
	}
	
	// render!
	render_quad();
}

void ScreenRenderer_OpenGL::render_monitor(const mat4f &_mvmat, float _ambient)
{
	// draw the base structure with reflections, onto which the VGA image will be superimposed
	
	GLCALL( glUseProgram(m_monitor.program) );
	
	GLCALL( glActiveTexture(GL_TEXTURE0) );
	GLCALL( glBindTexture(GL_TEXTURE_2D, m_monitor.reflection_map) );
	GLCALL( glBindSampler(0, m_monitor.reflection_sampler) );
	GLCALL( glUniform1i(m_monitor.uniforms.reflection_map, 0) );
	
	GLCALL( glUniformMatrix4fv(m_monitor.uniforms.mvmat, 1, GL_FALSE, _mvmat.data()) );
	GLCALL( glUniform1f(m_monitor.uniforms.ambient, _ambient) );
	
	render_quad();
}

void ScreenRenderer_OpenGL::render_quad()
{
	GLCALL( glDisable(GL_BLEND) );
	GLCALL( glEnableVertexAttribArray(0) );
	GLCALL( glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer) );
	GLCALL( glVertexAttribPointer(
		0,        // attribute 0. must match the layout in the shader.
		3,        // size
		GL_FLOAT, // type
		GL_FALSE, // normalized?
		0,        // stride
		(void*)0  // array buffer offset
	) );
	GLCALL( glDrawArrays(GL_TRIANGLES, 0, 6) ); // 2*3 indices starting at 0 -> 2 triangles
	GLCALL( glDisableVertexAttribArray(0) );
	GLCALL( glBindBuffer(GL_ARRAY_BUFFER, 0) );
}