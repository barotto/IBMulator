/*
 * Copyright (C) 2015, 2016  Marco Bortolin
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

#include <Rocket/Core.h>
#include <SDL.h>
#include <SDL_image.h>
#include <GL/glew.h>

#include "gui.h"
#include "rend_interface.h"
#include "program.h"


#if !(SDL_VIDEO_RENDER_OGL)
    #error "Only the opengl sdl backend is supported."
#endif

using namespace Rocket::Core;

RocketRenderer::RocketRenderer(SDL_Renderer * _renderer, SDL_Window * _screen)
{
	mRenderer = _renderer;
	mScreen = _screen;
	try {
		std::vector<std::string> vs,fs;
		std::string shadersdir = GUI::get_shaders_dir();
		vs.push_back(shadersdir + "gui.vs");
		fs.push_back(shadersdir + "gui.fs");
		m_program = GUI::load_GLSL_program(vs,fs);
		GLCALL( m_uniforms.textured = glGetUniformLocation(m_program, "textured") );
		GLCALL( m_uniforms.guitex = glGetUniformLocation(m_program, "guitex") );
		GLCALL( m_uniforms.P = glGetUniformLocation(m_program, "P") );
		GLCALL( m_uniforms.MV = glGetUniformLocation(m_program, "MV") );
	} catch(std::exception &e) {
		PERRF(LOG_GUI, "Unable to load the GUI renderer shader program!\n");
	}

	GLCALL( glGenBuffers(1, &m_vb) );
	GLCALL( glGenSamplers(1, &m_sampler) );
	GLCALL( glSamplerParameteri(m_sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE) );
	GLCALL( glSamplerParameteri(m_sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE) );
	GLCALL( glSamplerParameteri(m_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR) );
	GLCALL( glSamplerParameteri(m_sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR) );
}

// Called by Rocket when it wants to render geometry that it does not wish to optimise.
void RocketRenderer::RenderGeometry(Vertex* vertices,
		int num_vertices, int* indices, int num_indices,
		const TextureHandle texture,
		const Vector2f& translation)
{
	GLCALL( glBindSampler(0, m_sampler) );
	GLCALL( glUseProgram(m_program) );

	mat4f mv = mat4f::I;
	mv.load_translation(translation.x, translation.y, 0);

	if(texture)	{
		GLCALL( glUniform1i(m_uniforms.textured, 1) );
		GLCALL( glActiveTexture(GL_TEXTURE0) );
		GLCALL( glBindTexture(GL_TEXTURE_2D, texture) );
		GLCALL( glUniform1i(m_uniforms.guitex, 0) );
	} else {
		GLCALL( glUniform1i(m_uniforms.textured, 0) );
	}

	GLCALL( glUniformMatrix4fv(m_uniforms.P, 1, GL_FALSE, m_projmat.data()) );
	GLCALL( glUniformMatrix4fv(m_uniforms.MV, 1, GL_FALSE, mv.data()) );

	GLCALL( glBindBuffer(GL_ARRAY_BUFFER, m_vb) );
	GLCALL( glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * num_vertices, vertices, GL_DYNAMIC_DRAW) );
	GLCALL( glEnableVertexAttribArray(0) );
	GLCALL( glEnableVertexAttribArray(1) );
	GLCALL( glEnableVertexAttribArray(2) );
	GLCALL( glVertexAttribPointer(
            0,        // attribute 0 = vertices
            2,        // size
            GL_FLOAT, // type
            GL_FALSE, // normalized?
            sizeof(Vertex), // stride
            (void*)0  // array buffer offset
    ) );

	GLCALL( glVertexAttribIPointer(
            1,        // attribute 1 = colour
            4,        // size
		    GL_UNSIGNED_BYTE, // type
            sizeof(Vertex), // stride
            (GLvoid*) offsetof(struct Vertex, colour)  // array buffer offset
    ) );

	GLCALL( glVertexAttribPointer(
            2,        // attribute 2 = texcoords
            2,        // size
            GL_FLOAT, // type
            GL_FALSE, // normalized?
            sizeof(Vertex), // stride
            (GLvoid*) offsetof(struct Vertex, tex_coord)  // array buffer offset
    ) );

	GLCALL( glEnable(GL_BLEND) );
	GLCALL( glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) );
	GLCALL( glDrawElements(GL_TRIANGLES, num_indices, GL_UNSIGNED_INT, indices) );

	GLCALL( glDisableVertexAttribArray(0) );
	GLCALL( glDisableVertexAttribArray(1) );
	GLCALL( glDisableVertexAttribArray(2) );

	if(texture) {
		GLCALL( glBindTexture(GL_TEXTURE_2D, 0) );
	}
}


// Called by Rocket when it wants to enable or disable scissoring to clip content.
void RocketRenderer::EnableScissorRegion(bool enable)
{
	if(enable) {
		GLCALL( glEnable(GL_SCISSOR_TEST) );
	} else {
		GLCALL( glDisable(GL_SCISSOR_TEST) );
	}
}

// Called by Rocket when it wants to change the scissor region.
void RocketRenderer::SetScissorRegion(int x, int y, int width, int height)
{
	int w_width, w_height;
	SDL_GetWindowSize(mScreen, &w_width, &w_height);
	GLCALL( glScissor(x, w_height - (y + height), width, height) );
}

// Called by Rocket when a texture is required by the library.
bool RocketRenderer::LoadTexture(TextureHandle& texture_handle,
		Vector2i& texture_dimensions, const String& source)
{
	PDEBUGF(LOG_V2, LOG_GUI, "Loading texture %s\n", source.CString());
	FileInterface* file_interface = GetFileInterface();
	FileHandle file_handle = file_interface->Open(source);
	if (!file_handle)
		return false;

	file_interface->Seek(file_handle, 0, SEEK_END);
	size_t buffer_size = file_interface->Tell(file_handle);
	file_interface->Seek(file_handle, 0, SEEK_SET);

	std::vector<uint8_t> buffer(buffer_size);
	file_interface->Read(&buffer[0], buffer_size, file_handle);
	file_interface->Close(file_handle);

	size_t i;
	for(i = source.Length() - 1; i > 0; i--) {
		if(source[i] == '.') {
			break;
		}
	}

	String extension = source.Substring(i+1, source.Length()-i);
	SDL_Surface* surface = IMG_LoadTyped_RW(SDL_RWFromMem(&buffer[0], buffer_size), 1,
			extension.CString());

	if(!surface) {
		return false;
	}
	try {
		texture_handle = GUI::load_texture(surface);
	} catch(std::exception &e) {
		PERRF(LOG_GUI, "%s\n", e.what());
		SDL_FreeSurface(surface);
		return false;
	}
	texture_dimensions = Vector2i(surface->w, surface->h);
	SDL_FreeSurface(surface);
	return true;
}

// Called by Rocket when a texture is required to be built from an
//internally-generated sequence of pixels.
bool RocketRenderer::GenerateTexture(TextureHandle& texture_handle,
		const byte* source, const Vector2i& source_dimensions)
{
	#if SDL_BYTEORDER == SDL_BIG_ENDIAN
		/*
		Uint32 rmask = 0xff000000;
		Uint32 gmask = 0x00ff0000;
		Uint32 bmask = 0x0000ff00;
		Uint32 amask = 0x000000ff;
		*/
		GLenum type = GL_UNSIGNED_INT_8_8_8_8;
	#else
		/*
		Uint32 rmask = 0x000000ff;
		Uint32 gmask = 0x0000ff00;
		Uint32 bmask = 0x00ff0000;
		Uint32 amask = 0xff000000;
		*/
		GLenum type = GL_UNSIGNED_INT_8_8_8_8_REV;
	#endif

	GLuint gltex;
	GLCALL( glGenTextures(1, &gltex) );
	GLCALL( glBindTexture(GL_TEXTURE_2D, gltex) );
	GLCALL( glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
			source_dimensions.x, source_dimensions.y,
			0, GL_RGBA, type,
			source
			)
	);
	texture_handle = gltex;

	return true;
}

// Called by Rocket when a loaded texture is no longer required.
void RocketRenderer::ReleaseTexture(TextureHandle texture_handle)
{
	GLuint gltex = texture_handle;
	GLCALL( glDeleteTextures(1, &gltex) );
}

void RocketRenderer::SetDimensions(int _width, int _height)
{
	m_projmat = mat4_ortho<float>(0, _width, _height, 0, 0, 1);
}
