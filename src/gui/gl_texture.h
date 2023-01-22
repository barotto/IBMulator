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

#ifndef IBMULATOR_GL_TEXTURE
#define IBMULATOR_GL_TEXTURE

#include "vector.h"
#include "opengl.h"
#include "shader_preset.h"

class GLTexture
{
public:
	enum Format {
		UNDEFINED,

		// 8-bit
		R8_UNORM,
		R8_UINT,
		R8_SINT,
		R8G8_UNORM,
		R8G8_UINT,
		R8G8_SINT,
		R8G8B8A8_UNORM,
		R8G8B8A8_UINT,
		R8G8B8A8_SINT,
		R8G8B8A8_SRGB,

		// 10-bit
		A2B10G10R10_UNORM_PACK32,
		A2B10G10R10_UINT_PACK32,

		// 16-bit
		R16_UINT,
		R16_SINT,
		R16_SFLOAT,
		R16G16_UINT,
		R16G16_SINT,
		R16G16_SFLOAT,
		R16G16B16A16_UINT,
		R16G16B16A16_SINT,
		R16G16B16A16_SFLOAT,

		// 32-bit
		R32_UINT,
		R32_SINT,
		R32_SFLOAT,
		R32G32_UINT,
		R32G32_SINT,
		R32G32_SFLOAT,
		R32G32B32A32_UINT,
		R32G32B32A32_SINT,
		R32G32B32A32_SFLOAT,

		FMT_COUNT
	};

	struct FormatProp {
		Format name;
		GLenum gl_name;
		size_t bytes;
		const char *str;
	};
	inline static std::array<FormatProp,FMT_COUNT> ms_formats{ {
		{ UNDEFINED,                GLenum(-1),      0,  "UNDEFINED"                },

		{ R8_UNORM,                 GL_R8,           1,  "R8_UNORM"                 },
		{ R8_UINT,                  GL_R8UI,         1,  "R8_UINT"                  },
		{ R8_SINT,                  GL_R8I,          1,  "R8_SINT"                  },
		{ R8G8_UNORM,               GL_RG8,          2,  "R8G8_UNORM"               },
		{ R8G8_UINT,                GL_RG8UI,        2,  "R8G8_UINT"                },
		{ R8G8_SINT,                GL_RG8I,         2,  "R8G8_SINT"                },
		{ R8G8B8A8_UNORM,           GL_RGBA8,        4,  "R8G8B8A8_UNORM"           },
		{ R8G8B8A8_UINT,            GL_RGBA8UI,      4,  "R8G8B8A8_UINT"            },
		{ R8G8B8A8_SINT,            GL_RGBA8I,       4,  "R8G8B8A8_SINT"            },
		{ R8G8B8A8_SRGB,            GL_SRGB8_ALPHA8, 4,  "R8G8B8A8_SRGB"            },

		{ A2B10G10R10_UNORM_PACK32, GL_RGB10_A2,     4,  "A2B10G10R10_UNORM_PACK32" },
		{ A2B10G10R10_UINT_PACK32,  GL_RGB10_A2UI,   4,  "A2B10G10R10_UINT_PACK32"  },

		{ R16_UINT,                 GL_R16UI,        2,  "R16_UINT"                 },
		{ R16_SINT,                 GL_R16I,         2,  "R16_SINT"                 },
		{ R16_SFLOAT,               GL_R16F,         2,  "R16_SFLOAT"               },
		{ R16G16_UINT,              GL_RG16UI,       4,  "R16G16_UINT"              },
		{ R16G16_SINT,              GL_RG16I,        4,  "R16G16_SINT"              },
		{ R16G16_SFLOAT,            GL_RG16F,        4,  "R16G16_SFLOAT"            },
		{ R16G16B16A16_UINT,        GL_RGBA16UI,     8,  "R16G16B16A16_UINT"        },
		{ R16G16B16A16_SINT,        GL_RGBA16I,      8,  "R16G16B16A16_SINT"        },
		{ R16G16B16A16_SFLOAT,      GL_RGBA16F,      8,  "R16G16B16A16_SFLOAT"      },

		{ R32_UINT,                 GL_R32UI,        4,  "R32_UINT"                 },
		{ R32_SINT,                 GL_R32I,         4,  "R32_SINT"                 },
		{ R32_SFLOAT,               GL_R32F,         4,  "R32_SFLOAT"               },
		{ R32G32_UINT,              GL_RG32UI,       8,  "R32G32_UINT"              },
		{ R32G32_SINT,              GL_RG32I,        8,  "R32G32_SINT"              },
		{ R32G32_SFLOAT,            GL_RG32F,        8,  "R32G32_SFLOAT"            },
		{ R32G32B32A32_UINT,        GL_RGBA32UI,     16, "R32G32B32A32_UINT"        },
		{ R32G32B32A32_SINT,        GL_RGBA32I,      16, "R32G32B32A32_SINT"        },
		{ R32G32B32A32_SFLOAT,      GL_RGBA32F,      16, "R32G32B32A32_SFLOAT"      }
	} };

	std::string m_name;
	vec4f m_size;
	GLuint m_gl_name = 0;
	GLint m_gl_intformat = 0;
	Format m_format = Format::UNDEFINED;
	bool m_mipmap = false;
	GLuint m_gl_sampler = -1;
	bool m_dirty = false;

public:
	GLTexture(std::string _name, Format _format, bool _mipmap);

	GLTexture() = delete;
	GLTexture(const GLTexture &) = delete;
	GLTexture &operator=(const GLTexture &) = delete;

	const std::string & get_name() const { return m_name; }
	GLuint get_gl_name() const { return m_gl_name; }
	GLuint get_gl_sampler() const { return m_gl_sampler; }
	const vec4f & get_size() const { return m_size; }
	bool is_srgb() const { return m_format == R8G8B8A8_SRGB; }
	Format get_format() const { return m_format; }

	void update(unsigned _width, unsigned _height, GLenum _format, GLenum _type, unsigned _stride, void *_data);
	void update(unsigned _width, unsigned _height);
	void update();
	void update(const std::string &_path);
	
	bool is_dirty() const { return m_dirty; }
	void set_dirty(bool _dirty) { m_dirty = _dirty; }
	
	void swap(GLTexture &_tex);

	static Format find_format(const std::string &);
	static const FormatProp & get_format_prop(Format _format) { return ms_formats[_format]; }

	void create_sampler(ShaderPreset::Wrap _wrap, bool _linear);
	static GLuint create_gl_sampler(ShaderPreset::Wrap _wrap, bool _linear, bool _mipmap);
};

#endif