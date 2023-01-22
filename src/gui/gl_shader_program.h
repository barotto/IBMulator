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

#ifndef IBMULATOR_GL_SHADER_PROGRAM
#define IBMULATOR_GL_SHADER_PROGRAM

#include "matrix.h"
#include "gl_framebuffer.h"
#include "gl_texture.h"
#include "opengl.h"


class GLShaderProgram
{
public:
	struct Uniform {
		GLuint program;
		GLuint index;
		GLint location;

		std::string name;
		GLenum type;
		GLint binding = -1;
		GLint size;
		GLint block = -1;
		std::string member_name;
		GLint offset = -1;
		GLint array_stride = -1;
		GLint matrix_stride = -1;

		Uniform(GLuint _program, GLuint _index, GLint _binding);
		bool is_sampler() const;
		std::string str() const;
		std::string dbg_str(bool _block) const;
	};
	using UniformList = std::vector<GLuint>; // elements are indices to both OpenGL and m_uniforms

	struct Parameter {
		std::string name;
		std::string desc;
		float initial;
		float min;
		float max;
		float step;
		float value;

		const UniformList *uniforms = nullptr;

		std::string str() const;
		void set_uniforms(GLShaderProgram *, float _value);
	};
	using ParameterList = std::list<Parameter>;
	using ParameterMap = std::map<std::string, Parameter*>;

	struct Sampler2D {
		enum Category {
			Original, Source, History, Output, Feedback, User
		};
		Category category;
		int number = 0;
		const UniformList *tex_uniforms = nullptr; // pointer to a m_uniform_names's list
		const UniformList *size_uniforms = nullptr; // pointer to a m_uniform_names's list
		std::shared_ptr<GLTexture> texture;
		GLuint gl_sampler = -1;
	};
	using SamplerList = std::list<Sampler2D>;

	struct UBO {
		GLuint buffer;
		GLuint program;
		GLuint index;
		std::string name;
		GLint binding;
		GLint data_size;
		UniformList uniforms;

		UBO(GLuint program, GLuint index, GLint _binding);
		void update_uniform(const Uniform &_uni, unsigned _data_size, const void *_data);
	};

	enum BuiltinUniform {
		MVP,
		FinalViewportSize, // render target size for the final pass (should have been called FinalOutputSize...)
		FrameCount, FrameDirection,
		Source, Original,

		Brightness, Contrast, Saturation, Ambient,
		Monochrome,
		PowerOn,
		PassNumber,
		ModelView, Projection,

		BuiltinCount
	};
	inline static std::map<std::string, BuiltinUniform> ms_builtin_uniform_names = {
		// libretro's:
		{ "MVP",               MVP        },
		{ "FrameCount",        FrameCount },
		{ "FrameDirection",    FrameDirection },
		{ "FinalViewportSize", FinalViewportSize },
		{ "Source",            Source },
		{ "Original",          Original },
		// IBMulator specific:
		{ "ibmu_Brightness", Brightness },
		{ "ibmu_Contrast",   Contrast   },
		{ "ibmu_Saturation", Saturation },
		{ "ibmu_Ambient",    Ambient    },
		{ "ibmu_Monochrome", Monochrome },
		{ "ibmu_PowerOn",    PowerOn    },
		{ "ibmu_ModelView",  ModelView  },
		{ "ibmu_Projection", Projection },
		{ "ibmu_PassNumber", PassNumber }
	};
	using BuiltinList = std::array<UniformList,BuiltinCount>;
	
protected:
	std::string m_name;
	GLuint m_gl_name = 0;
	unsigned m_version = -1;
	GLTexture::Format m_fbformat = GLTexture::Format::UNDEFINED;
	ParameterList m_parameters;
	ParameterMap m_parameters_map;
	std::vector<UBO> m_blocks;
	std::vector<Uniform> m_uniforms; // vector's indices are the same as OpenGL's uniform indices (not locations!)
	BuiltinList m_builtins;
	SamplerList m_samplers;
	Sampler2D * find_sampler(Sampler2D::Category, int);
	std::vector<const Sampler2D*> m_feedback_samplers;
	std::vector<const Sampler2D*> m_output_samplers;

	// map to all the uniforms that have the same name and type in all the blocks (including the -1 block)
	std::map<std::string,UniformList> m_uniform_names; 

	unsigned m_history_size = 0;
	bool m_original_needed = false;
	bool m_source_needed = false;

public:
	GLShaderProgram(std::vector<std::string> _vs_paths, std::vector<std::string> _fs_paths, const std::list<std::string> &_defines);

	GLShaderProgram() = delete;
	GLShaderProgram(const GLShaderProgram &) = delete;
	GLShaderProgram &operator=(const GLShaderProgram &) = delete;

	const std::string & get_name() const { return m_name; }

	void update_samplers(const std::vector<std::string> &_pass_names, const std::vector<std::string> &_texture_names);
	SamplerList & get_samplers() { return m_samplers; }
	ParameterList & get_parameters() { return m_parameters; }
	Parameter * get_parameter(std::string _name) const;
	BuiltinList & get_builtins() { return m_builtins; }

	GLTexture::Format get_output_format() const { return m_fbformat; }
	unsigned get_history_size() const { return m_history_size; }
	bool is_original_needed() const { return m_original_needed; }
	bool is_source_needed() const { return m_source_needed; }
	const std::vector<const Sampler2D*> & get_feedback_samplers() const { return m_feedback_samplers; }
	const std::vector<const Sampler2D*> & get_output_samplers() const { return m_output_samplers; }
	const UniformList * find_uniform(std::string);
	const UniformList * get_builtin(BuiltinUniform);
	static const char * get_builtin_name(BuiltinUniform);
	const Uniform & get_uniform(GLuint _index);
	void add_alias(const UniformList *_uniforms, BuiltinUniform _to);

	void use();
	void set_uniform_int(const UniformList *, GLint);
	void set_uniform_uint(const UniformList *, GLuint);
	void set_uniform_float(const UniformList *, GLfloat);
	void set_uniform_vec4f(const UniformList *, const vec4f &);
	void set_uniform_mat4f(const UniformList *, const mat4f &);
	void set_uniform_sampler2D(const UniformList *, GLuint _sampler, GLuint _texture);

	std::vector<std::string> get_uniform_names(const UniformList *) const;

protected:
	std::list<std::string> get_shader_defines(GLuint _sh_type, const std::list<std::string> &_defines) const;
	std::vector<GLuint> attach_shaders(const std::vector<std::string> &_sh_paths, GLenum _sh_type, const std::list<std::string> &_defines);
	std::list<std::string> include_shader_file(const std::string &_path, GLenum _sh_type, GLenum &_sh_stage, const std::list<std::string> &_defines);
	static std::list<std::string> load_shader_file(const std::string &_path);

};

#endif