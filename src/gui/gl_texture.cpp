/*
 * Copyright (C) 2023-2025  Marco Bortolin
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
#include "gl_texture.h"
#include <SDL.h>
#include "stb/stb.h"
#include "utils.h"


GLTexture::GLTexture(std::string _name, GLTexture::Format _format, bool _mipmap)
: m_name(_name), m_format(_format), m_mipmap(_mipmap)
{
	m_gl_intformat = ms_formats[_format].gl_name;
	if(m_gl_intformat <= 0) {
		throw std::runtime_error("invalid GL texture format");
	}

	GLCALL( glGenTextures(1, &m_gl_name) );

	// set default values. shader programs should use sampler objects.
	GLCALL( glBindTexture(GL_TEXTURE_2D, m_gl_name) );
	GLCALL( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER) );
	GLCALL( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER) );
	if(m_mipmap) {
		GLCALL( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR) );
	} else {
		GLCALL( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR) );
	}
	GLCALL( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR) );

	GLfloat color[4]={.0f,.0f,.0f,0.f};
	GLCALL( glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, color) );

	PDEBUGF(LOG_V1, LOG_OGL, "Created Texture '%s', GL:%u, format:%s\n", m_name.c_str(), m_gl_name, ms_formats[_format].str);
}

void GLTexture::create_sampler(ShaderPreset::Wrap _wrap, bool _linear)
{
	m_gl_sampler = create_gl_sampler(_wrap, _linear, m_mipmap);
}

void GLTexture::update(unsigned _width, unsigned _height, GLenum _format, GLenum _type, unsigned _stride, void *_data)
{
	assert(_width && _height);
	assert(_data);

	GLCALL( glBindTexture(GL_TEXTURE_2D, m_gl_name) );

	GLCALL( glPixelStorei(GL_UNPACK_ROW_LENGTH, _stride) );
	if(m_size.x == float(_width) && m_size.y == float(_height)) {
		GLCALL( glTexSubImage2D(
				GL_TEXTURE_2D, 0, // target, level
				0, 0,             // xoffset, yoffset
				_width, _height,
				_format, _type,
				_data
		) );
	} else {
		GLCALL( glTexImage2D(
				GL_TEXTURE_2D, 0, // target, level
				m_gl_intformat,
				_width, _height,
				0,                // border
				_format, _type,
				_data
		) );
		m_size.x = float(_width);
		m_size.y = float(_height);
		m_size.z = 1.f / m_size.x;
		m_size.w = 1.f / m_size.y;
	}
	GLCALL( glPixelStorei(GL_UNPACK_ROW_LENGTH, 0) );

	if(m_mipmap) {
		GLCALL( glGenerateMipmap(GL_TEXTURE_2D) );
	}
}

void GLTexture::update(unsigned _width, unsigned _height)
{
	vec4f new_dim = m_size;

	if(_width) {
		new_dim.x = float(_width);
		new_dim.z = 1.f / new_dim.x;
	}
	if(_height) {
		new_dim.y = float(_height);
		new_dim.w = 1.f / new_dim.y;
	}

	GLCALL( glBindTexture(GL_TEXTURE_2D, m_gl_name) );

	if(new_dim.x && new_dim.y && (m_size.x != new_dim.x || m_size.y != new_dim.y)) {

		GLCALL( glTexImage2D(
				GL_TEXTURE_2D, // target
				0, // level
				m_gl_intformat,
				_width, _height,
				0, // border
				GL_RGBA, GL_UNSIGNED_BYTE, // format and type are not used but need to be valid
				nullptr
		) );

		PDEBUGF(LOG_V1, LOG_OGL, "Texture '%s' GL:%u is now %ux%u\n", m_name.c_str(), m_gl_name, _width, _height);
	}

	m_size = new_dim;

	if(m_size.x && m_size.y && m_mipmap) {
		GLCALL( glGenerateMipmap(GL_TEXTURE_2D) );
	}
}

void GLTexture::update()
{
	if(m_mipmap && m_size.x && m_size.y) {
		GLCALL( glBindTexture(GL_TEXTURE_2D, m_gl_name) );
		GLCALL( glGenerateMipmap(GL_TEXTURE_2D) );
	}
}

void GLTexture::update(const std::string &_path)
{
	SDL_Surface *surface = stbi_load(_path.c_str());

	if(!surface) {
		throw std::runtime_error("cannot create texture surface");
	}

	try {
		if(surface->format->BytesPerPixel != 4) {
			throw std::runtime_error("unsupported image format");
		}
		SDL_LockSurface(surface);
		const GLenum fb_format = GL_RGBA;
		const GLenum fb_type = GL_UNSIGNED_INT_8_8_8_8_REV;
		update(surface->w, surface->h, fb_format, fb_type, surface->w, surface->pixels);
		SDL_UnlockSurface(surface);
	} catch(std::exception &e) {
		SDL_FreeSurface(surface);
		throw;
	}

	SDL_FreeSurface(surface);
}

void GLTexture::swap(GLTexture &_tex)
{
	std::swap(m_gl_name, _tex.m_gl_name);
	std::swap(m_gl_intformat, _tex.m_gl_intformat);
	std::swap(m_size, _tex.m_size);
	std::swap(m_dirty, _tex.m_dirty);
	PDEBUGF(LOG_V3, LOG_OGL, "Texture '%s' is now GL:%u (%ux%u)\n",
			m_name.c_str(), m_gl_name, unsigned(m_size.x), unsigned(m_size.y));
	PDEBUGF(LOG_V3, LOG_OGL, "Texture '%s' is now GL:%u (%ux%u)\n",
			_tex.m_name.c_str(), _tex.m_gl_name, unsigned(_tex.m_size.x), unsigned(_tex.m_size.y));
	//std::swap(m_mipmap, _tex.m_mipmap);
}

GLTexture::Format GLTexture::find_format(const std::string &_str)
{
	auto f = std::find_if(ms_formats.begin(), ms_formats.end(), [&](const FormatProp &_p){ return _str.compare(_p.str) == 0; });
	if(f == ms_formats.end()) {
		return Format::UNDEFINED;
	}
	return f->name;
}

GLuint GLTexture::create_gl_sampler(ShaderPreset::Wrap _wrap, bool _linear, bool _mipmap)
{
	GLuint sampler = 0;
	GLCALL( glGenSamplers(1, &sampler) );
	switch(_wrap) {
		case ShaderPreset::clamp_to_border: {
			GLCALL( glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER) );
			GLCALL( glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER) );
			GLfloat color[4]={.0f,.0f,.0f,.0f};
			GLCALL( glSamplerParameterfv(sampler, GL_TEXTURE_BORDER_COLOR, color) );
			break;
		}
		case ShaderPreset::clamp_to_edge:
			GLCALL( glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE) );
			GLCALL( glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE) );
			break;
		case ShaderPreset::repeat:
			GLCALL( glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, GL_REPEAT) );
			GLCALL( glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, GL_REPEAT) );
			break;
		case ShaderPreset::mirrored_repeat:
			GLCALL( glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT) );
			GLCALL( glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT) );
			break;
		case ShaderPreset::mirror_clamp_to_edge:
			GLCALL( glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, GL_MIRROR_CLAMP_TO_EDGE) );
			GLCALL( glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, GL_MIRROR_CLAMP_TO_EDGE) );
			break;
	}
	if(_linear) {
		GLCALL( glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR) );
		if(_mipmap) {
			GLCALL( glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR) );
		} else {
			GLCALL( glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR) );
		}
	} else {
		GLCALL( glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST) );
		if(_mipmap) {
			GLCALL( glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST) );
		} else {
			GLCALL( glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST) );
		}
	}
	return sampler;
}