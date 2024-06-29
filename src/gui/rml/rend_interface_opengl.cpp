/*
 * Copyright (C) 2015-2024  Marco Bortolin
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
#include "rend_interface_opengl.h"
#include "gui_opengl.h"
#include "program.h"
#include <RmlUi/Core.h>
#include "shader_exception.h"

#if !(SDL_VIDEO_RENDER_OGL)
    #error "Only the opengl sdl backend is supported."
#endif


RmlRenderer_OpenGL::RmlRenderer_OpenGL(SDL_Renderer *_renderer, SDL_Window *_screen)
: RmlRenderer(_renderer, _screen)
{
	try {
		std::vector<std::string> sh{g_program.config().find_shader_asset("gui/color.slang")};
		std::list<std::string> defs;
		m_program_color = std::make_unique<GLShaderProgram>(sh,sh,defs);
	} catch(ShaderExc &e) {
		e.log_print(LOG_GUI);
		throw;
	} catch(std::runtime_error &e) {
		PERRF(LOG_GUI, "Error loading 'gui/color.slang': %s\n", e.what());
		throw;
	} catch(std::exception &) {
		PERRF(LOG_GUI, "Error loading 'gui/color.slang'\n");
		throw;
	}

	try {
		std::vector<std::string> sh{g_program.config().find_shader_asset("gui/texture.slang")};
		std::list<std::string> defs;
		m_program_texture = std::make_unique<GLShaderProgram>(sh,sh,defs);
		m_program_texture->update_samplers({},{});
		if(!m_program_texture->is_source_needed()) {
			throw std::runtime_error("gui/texture.slang error: no Source sampler2D found");
		}
		for(auto &sampler : m_program_texture->get_samplers()) {
			if(sampler.category == GLShaderProgram::Sampler2D::Category::Source) {
				GLCALL( glGenSamplers(1, &sampler.gl_sampler) );
				GLCALL( glSamplerParameteri(sampler.gl_sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE) );
				GLCALL( glSamplerParameteri(sampler.gl_sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE) );
				GLCALL( glSamplerParameteri(sampler.gl_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR) );
				GLCALL( glSamplerParameteri(sampler.gl_sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR) );
				break;
			}
		}
		m_mult_alpha_uniform = m_program_texture->find_uniform("uMultAlpha");
	} catch(ShaderExc &e) {
		e.log_print(LOG_GUI);
		throw;
	} catch(std::runtime_error &e) {
		PERRF(LOG_GUI, "Error loading 'gui/texture.slang': %s\n", e.what());
		throw;
	} catch(std::exception &) {
		PERRF(LOG_GUI, "Error loading 'gui/texture.slang'\n");
		throw;
	}

	// objects for non-compiled geometries

	GLCALL( glGenVertexArrays(1, &m_vao) );
	GLCALL( glBindVertexArray(m_vao) );

	GLCALL( glGenBuffers(1, &m_vbo) );
	GLCALL( glBindBuffer(GL_ARRAY_BUFFER, m_vbo) );

	GLCALL( glVertexAttribPointer(
		0,        // attribute 0 = vertices
		2,        // size
		GL_FLOAT, // type
		GL_FALSE, // normalized?
		sizeof(Rml::Vertex), // stride
		(GLvoid*) offsetof(Rml::Vertex, position)
	) );
	GLCALL( glEnableVertexAttribArray(0) );

	GLCALL( glVertexAttribIPointer(
		1,        // attribute 1 = colour
		4,        // size
		GL_UNSIGNED_BYTE, // type
		sizeof(Rml::Vertex), // stride
		(GLvoid*) offsetof(Rml::Vertex, colour)
	) );
	GLCALL( glEnableVertexAttribArray(1) );

	GLCALL( glVertexAttribPointer(
		2,        // attribute 2 = texcoords
		2,        // size
		GL_FLOAT, // type
		GL_FALSE, // normalized?
		sizeof(Rml::Vertex), // stride
		(GLvoid*) offsetof(Rml::Vertex, tex_coord)
	) );
	GLCALL( glEnableVertexAttribArray(2) );
}

RmlRenderer_OpenGL::~RmlRenderer_OpenGL()
{
}

Rml::CompiledGeometryHandle RmlRenderer_OpenGL::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
{
	GLuint vao = 0;
	GLuint vbo = 0;
	GLuint ibo = 0;

	GLCALL( glGenVertexArrays(1, &vao) );
	GLCALL( glGenBuffers(1, &vbo) );
	GLCALL( glGenBuffers(1, &ibo) );
	GLCALL( glBindVertexArray(vao) );

	GLCALL( glBindBuffer(GL_ARRAY_BUFFER, vbo) );
	GLCALL( glBufferData(GL_ARRAY_BUFFER, sizeof(Rml::Vertex) * vertices.size(), (const void*)vertices.data(), GL_STATIC_DRAW) );

	GLCALL( glVertexAttribPointer(
		0,        // attribute 0 = vertices
		2,        // size
		GL_FLOAT, // type
		GL_FALSE, // normalized?
		sizeof(Rml::Vertex), // stride
		(GLvoid*) offsetof(Rml::Vertex, position)
	) );
	GLCALL( glEnableVertexAttribArray(0) );

	GLCALL( glVertexAttribIPointer(
		1,        // attribute 1 = colour
		4,        // size
		GL_UNSIGNED_BYTE, // type
		sizeof(Rml::Vertex), // stride
		(GLvoid*) offsetof(Rml::Vertex, colour)
	) );
	GLCALL( glEnableVertexAttribArray(1) );

	GLCALL( glVertexAttribPointer(
		2,        // attribute 2 = texcoords
		2,        // size
		GL_FLOAT, // type
		GL_FALSE, // normalized?
		sizeof(Rml::Vertex), // stride
		(GLvoid*) offsetof(Rml::Vertex, tex_coord)
	) );
	GLCALL( glEnableVertexAttribArray(2) );

	GLCALL( glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo) );
	GLCALL( glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(int) * indices.size(),
			(const void*)indices.data(), GL_STATIC_DRAW) );
	GLCALL( glBindVertexArray(0) );

	PDEBUGF(LOG_V5, LOG_GUI, "Compiled geometry\n");

	CompiledGeometry *geometry = new CompiledGeometry;
	geometry->gl_vao = vao;
	geometry->gl_vbo = vbo;
	geometry->gl_ibo = ibo;
	geometry->draw_count = (GLsizei)indices.size();

	return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry);
}

void RmlRenderer_OpenGL::RenderGeometry(Rml::CompiledGeometryHandle _geometry,
		Rml::Vector2f _translation, Rml::TextureHandle _texture)
{
	CompiledGeometry *geometry = reinterpret_cast<CompiledGeometry*>(_geometry);

	mat4f mv = mat4f::I;
	mv.load_translation(_translation.x, _translation.y, 0);

	if(_texture) {
		auto tex = m_textures.find(_texture);
		if(tex == m_textures.end()) {
			PDEBUGF(LOG_V0, LOG_GUI, "Cannot find texture %llu!\n", _texture);
			return;
		}
		m_program_texture->use();
		for(auto &sampler : m_program_texture->get_samplers()) {
			if(sampler.category == GLShaderProgram::Sampler2D::Category::Source) {
				PDEBUGF(LOG_V5, LOG_GUI, "Using tex %u\n", _texture);
				m_program_texture->set_uniform_sampler2D(sampler.tex_uniforms, sampler.gl_sampler, tex->second.gl_texture);
				if(m_mult_alpha_uniform) {
					m_program_texture->set_uniform_int(m_mult_alpha_uniform, tex->second.mult_alpha);
				}
				break;
			}
		}
		m_program_texture->set_uniform_mat4f(m_program_texture->get_builtin(GLShaderProgram::ModelView), mv);
	} else {
		m_program_color->use();
		m_program_color->set_uniform_mat4f(m_program_color->get_builtin(GLShaderProgram::ModelView), mv);
	}

	GLCALL( glBindVertexArray(geometry->gl_vao) );
	GLCALL( glDrawElements(GL_TRIANGLES, geometry->draw_count, GL_UNSIGNED_INT, (const GLvoid*)0) );
	GLCALL( glBindVertexArray(0) );
}

void RmlRenderer_OpenGL::ReleaseGeometry(Rml::CompiledGeometryHandle handle)
{
	CompiledGeometry *geometry = reinterpret_cast<CompiledGeometry*>(handle);

	GLCALL( glDeleteVertexArrays(1, &geometry->gl_vao) );
	GLCALL( glDeleteBuffers(1, &geometry->gl_vbo) );
	GLCALL( glDeleteBuffers(1, &geometry->gl_ibo) );

	delete geometry;
}

void RmlRenderer_OpenGL::EnableScissorRegion(bool enable)
{
	if(enable) {
		GLCALL( glEnable(GL_SCISSOR_TEST) );
	} else {
		GLCALL( glDisable(GL_SCISSOR_TEST) );
	}
}

void RmlRenderer_OpenGL::SetScissorRegion(Rml::Rectanglei region)
{
	if(region.Valid()) {
		GLCALL( glEnable(GL_SCISSOR_TEST) );
		int w_width, w_height;
		SDL_GetWindowSize(m_screen, &w_width, &w_height);
		const int x = Rml::Math::Clamp(region.Left(), 0, w_width);
		const int y = Rml::Math::Clamp(w_height - region.Bottom(), 0, w_height);
		GLCALL( glScissor(x, y, region.Width(), region.Height()) );
	} else {
		GLCALL( glDisable(GL_SCISSOR_TEST) );
	}
}

Rml::TextureHandle RmlRenderer_OpenGL::GenerateTexture(
		Rml::Span<const Rml::byte> source_data, Rml::Vector2i source_dimensions)
{
	GLuint gltex;
	GLCALL( glGenTextures(1, &gltex) );
	GLCALL( glBindTexture(GL_TEXTURE_2D, gltex) );
	GLCALL( glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
			source_dimensions.x, source_dimensions.y,
			0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV,
			source_data.data()
			)
	);
	GLCALL( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR) );
	GLCALL( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR) );
	GLCALL( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT) );
	GLCALL( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT) );
	GLCALL( glBindTexture(GL_TEXTURE_2D, 0) );

	Rml::TextureHandle handle = static_cast<Rml::TextureHandle>(gltex);
	m_textures[handle] = CompiledTexture{ gltex, false };

	PDEBUGF(LOG_V4, LOG_GUI, "Generated ephemeral tex %llu, count: %llu\n", gltex, m_textures.size());

	return gltex;
}

Rml::TextureHandle RmlRenderer_OpenGL::LoadTexture(SDL_Surface *_surface)
{
	assert(_surface);
	if(_surface->format->BytesPerPixel != 4) {
		throw std::runtime_error("Unsupported image format: must be 4 bytes per pixel");
	}

	SDL_LockSurface(_surface);
	GLuint gltex;
	GLCALL( glGenTextures(1, &gltex) );
	GLCALL( glBindTexture(GL_TEXTURE_2D, gltex) );
	GLCALL( glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
		_surface->w, _surface->h,
		0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV,
		_surface->pixels
		)
	);
	SDL_UnlockSurface(_surface);

	Rml::TextureHandle handle = static_cast<Rml::TextureHandle>(gltex);
	m_textures[handle] = CompiledTexture{ gltex, true };

	PDEBUGF(LOG_V4, LOG_GUI, "Generated tex %llu, count: %llu\n", handle, m_textures.size());

	return handle;
}

void RmlRenderer_OpenGL::ReleaseTexture(Rml::TextureHandle _handle)
{
	auto tex = m_textures.find(_handle);
	if(tex == m_textures.end()) {
		PDEBUGF(LOG_V0, LOG_GUI, "Cannot release texture %llu: not found!\n", _handle);
		return;
	}
	GLCALL( glDeleteTextures(1, &tex->second.gl_texture) );
	m_textures.erase(tex);
	PDEBUGF(LOG_V4, LOG_GUI, "Released texture %llu, count: %llu\n", _handle, m_textures.size());
}

void RmlRenderer_OpenGL::SetDimensions(int _width, int _height)
{
	mat4f projmat = mat4_ortho<float>(0, _width, _height, 0, 0, 1);
	m_program_texture->use();
	m_program_texture->set_uniform_mat4f(m_program_texture->get_builtin(GLShaderProgram::Projection), projmat);
	m_program_color->use();
	m_program_color->set_uniform_mat4f(m_program_color->get_builtin(GLShaderProgram::Projection), projmat);
}
