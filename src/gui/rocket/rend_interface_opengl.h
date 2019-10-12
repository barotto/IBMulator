/*
 * Copyright (C) 2015-2019  Marco Bortolin
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


class RocketRenderer_OpenGL : public RocketRenderer
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
	RocketRenderer_OpenGL(SDL_Renderer * _renderer, SDL_Window * _screen);
	~RocketRenderer_OpenGL();

	/// Called by Rocket when it wants to render geometry that it does not wish to optimise.
	void RenderGeometry(Rocket::Core::Vertex* vertices, int num_vertices, int* indices, int num_indices, Rocket::Core::TextureHandle texture, const Rocket::Core::Vector2f& translation);

	/// Called by Rocket when it wants to enable or disable scissoring to clip content.
	void EnableScissorRegion(bool enable);
	/// Called by Rocket when it wants to change the scissor region.
	void SetScissorRegion(int x, int y, int width, int height);

	/// Called by Rocket when a texture is required by the library.
	bool LoadTexture(Rocket::Core::TextureHandle& texture_handle, Rocket::Core::Vector2i& texture_dimensions, const Rocket::Core::String& source);
	/// Called by Rocket when a texture is required to be built from an internally-generated sequence of pixels.
	bool GenerateTexture(Rocket::Core::TextureHandle& texture_handle, const Rocket::Core::byte* source, const Rocket::Core::Vector2i& source_dimensions);
	/// Called by Rocket when a loaded texture is no longer required.
	void ReleaseTexture(Rocket::Core::TextureHandle texture_handle);

	void SetDimensions(int _width, int _height);
};

#endif
