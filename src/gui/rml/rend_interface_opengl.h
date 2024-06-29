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

#ifndef IBMULATOR_GUI_REND_INTERFACE_OPENGL_H
#define IBMULATOR_GUI_REND_INTERFACE_OPENGL_H

#include "rend_interface.h"
#include "gui/matrix.h"
#include "gui/gl_shader_program.h"
#include <GL/glew.h>
#include <list>

#if !(SDL_VIDEO_RENDER_OGL)
	#error "Only the opengl sdl backend is supported."
#endif


class RmlRenderer_OpenGL : public RmlRenderer
{
protected:
	std::unique_ptr<GLShaderProgram> m_program_color;
	std::unique_ptr<GLShaderProgram> m_program_texture;
	const GLShaderProgram::UniformList *m_mult_alpha_uniform;

	GLuint m_vao;
	GLuint m_vbo;

	struct CompiledGeometry {
		GLuint gl_vao = 0, gl_vbo = 0, gl_ibo = 0;
		GLsizei draw_count = 0;
	};
	struct CompiledTexture {
		GLuint gl_texture;
		bool mult_alpha;
	};

	std::map<Rml::TextureHandle, CompiledTexture> m_textures;
	
public:
	RmlRenderer_OpenGL(SDL_Renderer *_renderer, SDL_Window *_screen);
	~RmlRenderer_OpenGL();

	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices);
	virtual void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture);
	virtual void ReleaseGeometry(Rml::CompiledGeometryHandle geometry);

	void EnableScissorRegion(bool enable);
	void SetScissorRegion(Rml::Rectanglei region);

	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source_data, Rml::Vector2i source_dimensions);
	void ReleaseTexture(Rml::TextureHandle texture_handle);

	void SetDimensions(int _width, int _height);

protected:
	uintptr_t LoadTexture(SDL_Surface *_surface);
};

#endif
