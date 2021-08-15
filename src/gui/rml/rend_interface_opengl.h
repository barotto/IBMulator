/*
 * Copyright (C) 2015-2021  Marco Bortolin
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
#include <GL/glew.h>

#if !(SDL_VIDEO_RENDER_OGL)
	#error "Only the opengl sdl backend is supported."
#endif


class RmlRenderer_OpenGL : public RmlRenderer
{
protected:
	GLuint m_program;
	GLuint m_vb;
	GLuint m_sampler;
	mat4f  m_projmat;

	struct uniforms {
		GLint textured, guitex, P, MV;
		uniforms() : textured(-1), guitex(-1), P(-1), MV(-1) {}
	} m_uniforms;

public:
	RmlRenderer_OpenGL(SDL_Renderer *_renderer, SDL_Window *_screen);
	~RmlRenderer_OpenGL();

	/// Called by RmlUi when it wants to render geometry that it does not wish to optimise.
	void RenderGeometry(Rml::Vertex *vertices, int num_vertices, int *indices,
			int num_indices, Rml::TextureHandle texture, const Rml::Vector2f &translation);

	/// Called by RmlUi when it wants to enable or disable scissoring to clip content.
	void EnableScissorRegion(bool enable);
	/// Called by RmlUi when it wants to change the scissor region.
	void SetScissorRegion(int x, int y, int width, int height);

	/// Called by RmlUi when a texture is required to be built from an internally-generated sequence of pixels.
	bool GenerateTexture(Rml::TextureHandle &texture_handle, const Rml::byte *source,
			const Rml::Vector2i &source_dimensions);
	/// Called by RmlUi when a loaded texture is no longer required.
	void ReleaseTexture(Rml::TextureHandle texture_handle);

	void SetDimensions(int _width, int _height);
};

#endif
