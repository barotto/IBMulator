/*_
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

#include "ibmulator.h"
#include "opengl.h"
#include "gl_shader_program.h"
#include "shader_exception.h"
#include "filesys.h"
#include "utils.h"
#include <GL/glew.h>
#include <regex>


GLShaderProgram::Uniform::Uniform(GLuint _program, GLuint _index, GLint _binding)
: program(_program), index(_index)
{
	GLint name_len = 0;
	GLCALL( glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_NAME_LENGTH, &name_len) );
	// *_NAME_LENGTH includes the nul-terminator, which std::string::resize won't consider
	// remove it from the size or string comparisons won't work 
	name.resize(name_len-1);
	GLCALL( glGetActiveUniformName(program, index, name_len, NULL, &name.at(0)) );

	GLCALL( location = glGetUniformLocation(program, name.c_str()) );

	GLCALL( glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_TYPE, (GLint*)&type) );

	switch(type) {
		case GL_SAMPLER_2D:
		case GL_INT_SAMPLER_2D:
		case GL_UNSIGNED_INT_SAMPLER_2D:
			if(_binding < 0) {
				GLCALL( glGetUniformiv(program, index, &binding) );
				if(binding < 0) {
					PWARNF(LOG_V0, LOG_OGL, "The returned binding value for sampler '%s' is %d.\n", name.c_str(), binding);
				}
			} else {
				binding = _binding;
			}
			break;
		default: break;
	}

	GLCALL( glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_SIZE, &size) );
	GLCALL( glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_BLOCK_INDEX, &block) );

	if(block != -1) {
		member_name = name.substr(name.find('.')+1);
		GLCALL( glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_OFFSET, &offset) );
		GLCALL( glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_ARRAY_STRIDE, &array_stride) );
		GLCALL( glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_MATRIX_STRIDE, &matrix_stride) );
	} else {
		member_name = name;
	}
}

bool GLShaderProgram::Uniform::is_sampler() const
{
	return (type == GL_SAMPLER_2D || type == GL_INT_SAMPLER_2D || type == GL_UNSIGNED_INT_SAMPLER_2D);
}

std::string GLShaderProgram::Uniform::str() const
{
	std::string str = str_format(
		"%s %s",
		GL_get_uniform_type_string(type),
		name.c_str()
	);
	if(block != -1) {
		str += str_format(", offset=%d", offset);
	}
	if(size > 1) {
		str += str_format(", size=%d", size);
	}
	if(array_stride > 0) {
		str += str_format(", array_stride=%d", array_stride);
	}
	if(matrix_stride > 0) {
		str += str_format(", matrix_stride=%d", matrix_stride);
	}
	if(binding != -1) {
		str += str_format(", binding=%d", binding);
	}
	return str;
}

std::string GLShaderProgram::Uniform::dbg_str(bool _is_block) const
{
	std::string str = str_format(
		"%u:%d  %s '%s'",
		index, location,
		GL_get_uniform_type_string(type),
		(_is_block && block != -1) ? member_name.c_str() : name.c_str()
	);
	if(_is_block && block != -1) {
		str += str_format(", offset=%d", offset);
	}
	if(size > 1) {
		str += str_format(", size=%d", size);
	}
	if(array_stride > 0) {
		str += str_format(", array_stride=%d", array_stride);
	}
	if(matrix_stride > 0) {
		str += str_format(", matrix_stride=%d", matrix_stride);
	}
	if(binding != -1) {
		str += str_format(", binding=%d", binding);
	}
	return str;
}

std::string GLShaderProgram::Parameter::str() const
{
	return str_format("%s \"%s\" %f %f %f %f", name.c_str(), desc.c_str(), initial, min, max, step);
}

void GLShaderProgram::Parameter::set_uniforms(GLShaderProgram *_prg, float _value)
{
	if(!uniforms) {
		return;
	}
	GLenum type = _prg->get_uniform(uniforms->front()).type;
	switch(type) {
		case GL_INT:
		case GL_BOOL:
			_prg->set_uniform_int(uniforms, GLint(_value));
			break;
		case GL_UNSIGNED_INT:
			_prg->set_uniform_uint(uniforms, GLuint(_value));
			break;
		case GL_FLOAT:
			_prg->set_uniform_float(uniforms, GLfloat(_value));
			break;
		default:
			throw std::runtime_error(str_format("Invalid unfirm type for paramter '%s'", name.c_str()));
	}
	value = _value;
}

GLShaderProgram::UBO::UBO(GLuint _program, GLuint _index, GLint _binding)
: program(_program), index(_index)
{
	GLint name_len = 0;
	GLCALL( glGetActiveUniformBlockiv(program, index, GL_UNIFORM_BLOCK_NAME_LENGTH, &name_len) );
	name.resize(name_len-1);
	GLCALL( glGetActiveUniformBlockName(program, index, name_len, NULL, &name.at(0)) );

	if(_binding < 0) {
		GLCALL( glGetActiveUniformBlockiv(program, index, GL_UNIFORM_BLOCK_BINDING, &binding) );
	} else {
		binding = _binding;
		GLCALL( glUniformBlockBinding(program, index, binding) );
	}
	GLCALL( glGetActiveUniformBlockiv(program, index, GL_UNIFORM_BLOCK_DATA_SIZE, &data_size) );
	
	GLint active_u = 0;
	GLCALL( glGetActiveUniformBlockiv(program, index, GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS, &active_u) );
	uniforms.resize(active_u);
	GLCALL( glGetActiveUniformBlockiv(program, index, GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES, (GLint*)&uniforms[0]) );

	GLCALL( glGenBuffers(1, &buffer) );
	GLCALL( glBindBuffer(GL_UNIFORM_BUFFER, buffer) );
	GLCALL( glBufferData(GL_UNIFORM_BUFFER, data_size, nullptr, GL_DYNAMIC_DRAW) );
}

void GLShaderProgram::UBO::update_uniform(const Uniform &_uni, unsigned _data_size, const void *_data)
{
	GLCALL( glBindBuffer(GL_UNIFORM_BUFFER, buffer) );
	GLCALL( glBufferSubData(GL_UNIFORM_BUFFER, _uni.offset, _data_size, _data) );
}

GLShaderProgram::GLShaderProgram(std::vector<std::string> _vs_paths, std::vector<std::string> _fs_paths, const std::list<std::string> &_defines)
{
	if(_vs_paths.size() == 1 && _fs_paths.size() == 1 && _vs_paths[0] == _fs_paths[0]) {
		PINFOF(LOG_V1, LOG_OGL, "Loading GLSL program: %s\n", _vs_paths[0].c_str());
	} else {
		PINFOF(LOG_V1, LOG_OGL, "Loading GLSL program:\n");
		PINFOF(LOG_V1, LOG_OGL, " vertex:\n");
		for(auto &s : _vs_paths) {
			PINFOF(LOG_V1, LOG_OGL, "  %s\n", s.c_str());
		}
		PINFOF(LOG_V1, LOG_OGL, " fragment:\n");
		for(auto &s : _fs_paths) {
			PINFOF(LOG_V1, LOG_OGL, "  %s\n", s.c_str());
		}
	}

	// Create the Program
	GLCALL( m_gl_name = glCreateProgram() );

	// Load and attach Shaders to Program
	std::vector<GLuint> vsids = attach_shaders(_vs_paths, GL_VERTEX_SHADER, _defines);
	std::vector<GLuint> fsids = attach_shaders(_fs_paths, GL_FRAGMENT_SHADER, _defines);

	// Link the Program
	GLCALL( glLinkProgram(m_gl_name) );

	for(auto shid : vsids) {
		// a shader won't actually be deleted by glDeleteShader until it's been detached
		GLCALL( glDetachShader(m_gl_name, shid) );
		GLCALL( glDeleteShader(shid) );
	}
	for(auto shid : fsids) {
		GLCALL( glDetachShader(m_gl_name, shid) );
		GLCALL( glDeleteShader(shid) );
	}

	// Check linking result
	GLint result = GL_FALSE;
	int infologlen;
	GLCALL( glGetProgramiv(m_gl_name, GL_LINK_STATUS, &result) );
	GLCALL( glGetProgramiv(m_gl_name, GL_INFO_LOG_LENGTH, &infologlen) );
	if(!result) {
		if(infologlen > 1) {
			std::vector<char> progerr(infologlen+1);
			GLCALL( glGetProgramInfoLog(m_gl_name, infologlen, nullptr, &progerr[0]) );
			throw ShaderLinkExc(std::string(&progerr[0]), m_gl_name);
		}
		throw ShaderLinkExc("", m_gl_name);
	}

	PINFOF(LOG_V2, LOG_OGL, " version: %u\n", m_version);

	// Do introspection
	std::string name_str;

	GLint active_blocks = 0;
	GLCALL( glGetProgramiv(m_gl_name, GL_ACTIVE_UNIFORM_BLOCKS, &active_blocks) );

	GLint active_uniforms = 0;
	GLCALL( glGetProgramiv(m_gl_name, GL_ACTIVE_UNIFORMS, &active_uniforms) );

	PINFOF(LOG_V2, LOG_OGL, " uniforms: %d\n", active_uniforms);
	GLint ubinding = 0;
	for(GLuint uidx=0; uidx<GLuint(active_uniforms); uidx++) {
		auto & uni = m_uniforms.emplace_back(m_gl_name, uidx, ubinding);
		if(uni.is_sampler()) {
			ubinding = uni.binding + 1;
		}
		auto stduni = ms_builtin_uniform_names.find(uni.member_name);
		if(stduni != ms_builtin_uniform_names.end()) {
			m_builtins[stduni->second].push_back(uidx);
			PINFOF(LOG_V2, LOG_OGL, "  [B] %s\n", uni.dbg_str(false).c_str());
		} else {
			PINFOF(LOG_V2, LOG_OGL, "      %s\n", uni.dbg_str(false).c_str());
		}
		if(m_uniform_names.find(uni.member_name) != m_uniform_names.end()) {
			auto *other = &m_uniforms[m_uniform_names[uni.member_name].front()];
			if(other->type != uni.type) {
				throw std::runtime_error(str_format("'%s' has a different type than '%s'", uni.str().c_str(), other->str().c_str()));
			}
		}
		m_uniform_names[uni.member_name].push_back(uidx);
	}

	PINFOF(LOG_V2, LOG_OGL, " uniform blocks: %d\n", active_blocks);
	ubinding = -1;
	if(m_version < 420) {
		ubinding = 0;
	}
	for(GLuint bidx=0; bidx<GLuint(active_blocks); bidx++) {
		auto & block = m_blocks.emplace_back(m_gl_name, bidx, ubinding);
		if(m_version < 420) {
			ubinding = block.binding + 1;
		}
		PINFOF(LOG_V2, LOG_OGL, "  %d: \"%s\", binding=%d, data_size=%d\n",
				m_blocks.back().index, m_blocks.back().name.c_str(), m_blocks.back().binding, m_blocks.back().data_size);
		for(auto uni : m_blocks.back().uniforms) {
			PINFOF(LOG_V2, LOG_OGL, "     %s\n", m_uniforms[uni].dbg_str(true).c_str());
		}
	}

	// Bind uniforms to parameters 
	for(auto & [name, uni] : m_uniform_names) {
		GLuint type = m_uniforms[uni.front()].type;
		if(type == GL_FLOAT || type == GL_INT || type == GL_UNSIGNED_INT || type == GL_BOOL) {
			auto param = m_parameters_map.find(name);
			if(param != m_parameters_map.end()) {
				param->second->uniforms = &uni;
			}
		}
	}
}

std::list<std::string> GLShaderProgram::load_shader_file(const std::string &_path)
{
	if(!FileSys::file_exists(_path.c_str())) {
		throw std::runtime_error("file not found");
	}
	std::list<std::string> shdata;
	std::ifstream shstream = FileSys::make_ifstream(_path.c_str());
	if(shstream.is_open()) {
		std::string line = "";
		while(std::getline(shstream, line)) {
			// trimming will force '#' to first place
			line = str_trim(line);
			// don't skip empty lines (for shader debugging)
			shdata.emplace_back(line + "\n");
		}
		shstream.close();
	} else {
		throw std::runtime_error("cannot open file for reading");
	}
	return shdata;
}

std::list<std::string> GLShaderProgram::get_shader_defines(GLuint _sh_type, const std::list<std::string> &_defines) const
{
	UNUSED(_sh_type);

	std::list<std::string> lines;

	// don't pollute global space with useless defines 
	//if(_sh_type == GL_VERTEX_SHADER) {
	//	lines.emplace_back("#define VERTEX 1\n");
	//} else if(_sh_type == GL_FRAGMENT_SHADER) {
	//	lines.emplace_back("#define FRAGMENT 1\n");
	//} else {
	//	throw std::logic_error("invalid shader type");
	//}
	//lines.emplace_back("#define PARAMETER_UNIFORM 1\n");

	lines.insert(lines.end(), _defines.begin(), _defines.end());

	return lines;
}

std::list<std::string> GLShaderProgram::include_shader_file(const std::string &_path, GLenum _sh_type, GLenum &_sh_stage, const std::list<std::string> &_defines)
{
	// Read Shader code from file
	auto shcode = load_shader_file(_path);

	// Analyze and Inject code
	unsigned version = 0;
	std::list<std::string> result_code;
	int linen=1;

	// regexes are the heaviest substance in the universe and their compilation extends to the Big Chill
	std::regex layout_reg("layout\\(([^\\)]*)\\)\\s*(.*)");
	std::regex include_reg("#pragma\\s+include\\s+\"(.*)\"");
	std::regex params_reg("#pragma\\s+parameter\\s+([^\\s]+)\\s+\"(.*)\"\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)\\s*([^\\s]*)");
	std::regex spaces_reg("\\s+");
	std::regex comma_reg(",");

	for(auto line=shcode.begin(); line!=shcode.end(); line++,linen++) {
		// first of all determine the current stage
		if(line->at(0) == '#' && line->find("pragma",1) == 1) {
			*line = str_compress_spaces(*line);
			if(line->find("pragma stage", 1) == 1) {
				auto toks = str_parse_tokens_re(*line, spaces_reg);
				if(toks.size() >= 3) {
					if(toks[2] == "vertex") {
						_sh_stage = GL_VERTEX_SHADER;
					} else if(toks[2] == "fragment") {
						_sh_stage = GL_FRAGMENT_SHADER;
					} else {
						result_code.push_back(*line);
						throw ShaderCompileExc(str_format("invalid stage type: %s", toks[2].c_str()), _path, result_code, result_code.size());
					}
				}
				continue;
			}
		}
		// then skip all the lines of different stages than the current one
		if(_sh_stage != GL_INVALID_ENUM && _sh_stage != _sh_type) {
			continue;
		}
		if(line->at(0) == 'l' && line->find("layout", 0) == 0) {
			// remove unsupported vulkan specific identifiers
			std::smatch m;
			if(std::regex_search(*line, m, layout_reg) && m.size() >= 3) {
				auto identifiers = m[1].str();
				str_replace_all(identifiers, " ", "");
				auto toks = str_parse_tokens_re(identifiers, comma_reg);
				std::vector<std::string> new_identifiers;
				for(auto & id : toks) {
					if(id.find("set", 0) == id.npos && id.find("push") == id.npos) {
						new_identifiers.push_back(id);
					} else if(id.find("push") != id.npos) {
						new_identifiers.push_back("std140");
						if(m_version >= 420) {
							GLint max_bindings;
							GLCALL( glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &max_bindings) );
							new_identifiers.push_back(str_format("binding=%d", max_bindings-1));
						}
					}
				}
				if(new_identifiers.empty()) {
					*line = m[2].str();
				} else {
					*line = str_format("layout(%s) %s\n", str_implode(new_identifiers,",").c_str(), m[2].str().c_str());
				}
			} else {
				continue;
			}
		} else if(line->at(0) == '#') {
			if(!version && line->find("version", 1) == 1) {
				// inject common shader defines
				auto defines = get_shader_defines(_sh_type, _defines);
				auto defines_size = defines.size();
				shcode.splice(std::next(line), std::move(defines));
				linen += defines_size;
				version = unsigned(str_parse_int_num(line->substr(9,11)));
				m_version = std::min(version, m_version);
			} else if(line->find("include", 1) == 1) {
				// the glsl #include directive exists, but implementing it would require more work
				// compared to this bespoke #pragma and would also require the GL_ARB_shading_language_include extension
				line->insert(1, "pragma ");
				shcode.insert(std::next(line), *line);
				continue;
			} else if(line->find("pragma", 1) == 1) {
				auto toks = str_parse_tokens_re(*line, spaces_reg);
				if(toks.size() <= 1) {
					continue;
				}
				if(toks[1] == "include") {
					std::smatch m;
					if(std::regex_search(*line, m, include_reg) && m.size() == 2) {
						PINFOF(LOG_V2, LOG_OGL, " including %s from %s\n", m[1].str().c_str(), _path.c_str());
						std::string incldir = FileSys::get_path_dir(_path.c_str());
						std::string inclpath = incldir + '/' + m[1].str();
						std::list<std::string> include;
						try {
							inclpath = FileSys::realpath(inclpath.c_str());
							include = std::move(include_shader_file(inclpath, _sh_type, _sh_stage, _defines));
						} catch(std::runtime_error &e) {
							result_code.push_back(*line);
							throw ShaderCompileExc(str_format("cannot include '%s': %s\n", inclpath.c_str(), e.what()), _path, result_code, result_code.size());
						}
						result_code.splice(result_code.end(), std::move(include));
					}
					continue;
				} else if(toks[1] == "name") {
					if(toks.size() >= 3) {
						m_name = toks[2];
					}
					continue;
				} else if(toks[1] == "parameter") {
					if(toks.size() < 7) {
						continue;
					}
					std::smatch m;
					if(std::regex_search(*line, m, params_reg) && m.size() >= 6) {
						Parameter p;
						p.name = m[1].str();
						p.desc = m[2].str();
						try {
							p.initial = str_parse_real_num(m[3].str());
							p.value = p.initial;
							p.min = str_parse_real_num(m[4].str());
							p.max = str_parse_real_num(m[5].str());
						} catch(std::runtime_error &) {
							PDEBUGF(LOG_V0, LOG_OGL, "invalid number: %s", line->c_str());
							continue;
						}
						p.step = 0.0;
						if(m.size() >= 7) {
							try {
								p.step = str_parse_real_num(m[6].str());
							} catch(...) {}
						}
						m_parameters.push_back(p);
						m_parameters_map[p.name] = &m_parameters.back();
					}
					continue;
				} else if(toks[1] == "format") {
					if(toks.size() >= 3) {
						m_fbformat = GLTexture::find_format(toks[2]);
						if(m_fbformat == GLTexture::Format::UNDEFINED) {
							throw std::runtime_error(str_format("invalid output format '%s'", toks[2].c_str()));
						}
					}
					continue;
				}
			}
		}
		result_code.push_back(*line);
	}

	return result_code;
}

std::vector<GLuint> GLShaderProgram::attach_shaders(
	const std::vector<std::string> &_sh_paths, GLenum _sh_type, const std::list<std::string> &_defines)
{
	assert(_sh_type == GL_VERTEX_SHADER || _sh_type == GL_FRAGMENT_SHADER);

	if(m_gl_name == 0) {
		throw std::logic_error("invalid program id");
	}
	std::vector<GLuint> sh_ids;
	for(auto &sh : _sh_paths) {

		GLenum stage = GL_INVALID_ENUM;
		auto sourcecode = include_shader_file(sh, _sh_type, stage, _defines);

		if(m_version == unsigned(-1)) {
			throw ShaderCompileExc("#version directive not found", sh, sourcecode, 1);
		}

		std::vector<const char *> ptrs;
		ptrs.reserve(sourcecode.size() + 1);
		for(auto & line : sourcecode) {
			ptrs.push_back(line.data());
		}

		// Create and compile
		GLuint shid;
		GLCALL( shid = glCreateShader(_sh_type) );
		GLCALL( glShaderSource(shid, ptrs.size(), ptrs.data(), nullptr) );
		GLCALL( glCompileShader(shid) );

		// Check for errors
		GLint result = GL_FALSE;
		int infologlen;
		GLCALL( glGetShaderiv(shid, GL_COMPILE_STATUS, &result) );
		GLCALL( glGetShaderiv(shid, GL_INFO_LOG_LENGTH, &infologlen) );
		if(!result && infologlen > 1) {
			std::string sherr;
			// includes the null terminator, string is longer but keep it anyway
			sherr.resize(infologlen);
			GLCALL( glGetShaderInfoLog(shid, infologlen, nullptr, &sherr[0]) );
			throw ShaderCompileExc(sherr, sh, sourcecode, 0);
		} else {
			int l=1;
			for(auto & line : sourcecode) {
				PDEBUGF(LOG_V3, LOG_OGL, "  %d: %s", l++, line.c_str());
			}
		}

		// Attach Vertex Shader to Program
		GLCALL( glAttachShader(m_gl_name, shid) );
		sh_ids.push_back(shid);
	}

	return sh_ids;
}

GLShaderProgram::Sampler2D * GLShaderProgram::find_sampler(Sampler2D::Category _cat, int _num)
{
	auto s = std::find_if(m_samplers.begin(), m_samplers.end(), [&](const Sampler2D &_s) {
		return _s.category == _cat && _s.number == _num;
	});
	if(s != m_samplers.end()) {
		return &(*s);
	}
	return nullptr;
}

void GLShaderProgram::update_samplers(const std::vector<std::string> &_pass_names, const std::vector<std::string> &_user_names)
{
	// It is valid to use a size variable without declaring the texture itself.

	m_history_size = 0;

	std::regex orignal_history_re("^OriginalHistory(Size)?([0-9]+)$");
	std::regex pass_output_re("^PassOutput(Size)?([0-9]+)$");
	std::regex pass_feedback_re("^PassFeedback(Size)?([0-9]+)$");
	std::regex named_pass_re("^(.*)Size$");
	std::regex named_feedback_re("^(.*)Feedback(Size)?$");

	for(auto & [name, unilist] : m_uniform_names) {
		bool is_sampler = false;
		bool is_vec4 = false;
		auto *uniform = &m_uniforms[unilist.front()];

		switch(uniform->type) {
			case GL_SAMPLER_2D:
			case GL_INT_SAMPLER_2D:
			case GL_UNSIGNED_INT_SAMPLER_2D:
				is_sampler = true;
				break;
			case GL_FLOAT_VEC4:
				is_vec4 = true;
				break;
			default:
				continue;
		}

		PINFOF(LOG_V2, LOG_OGL, "    %s %s\n", is_vec4?"vec4":"sampler2D", uniform->name.c_str());

		Sampler2D sampler;
		std::smatch m;
		if(std::regex_search(uniform->member_name, m, orignal_history_re))
		{
			sampler.category = Sampler2D::Category::History;
			if(is_vec4 && !m[1].matched) {
				PWARNF(LOG_V1, LOG_OGL, "    'vec4 %s' doesn't have proper \"Size\" name\n", uniform->member_name.c_str());
				continue;
			}
			sampler.number = str_parse_int_num(m[2].str());
			m_history_size = std::max(m_history_size, unsigned(sampler.number));
		}
		else if(uniform->member_name == "Original" || uniform->member_name == "OriginalSize")
		{
			if(is_vec4 && uniform->member_name != "OriginalSize") {
				PWARNF(LOG_V1, LOG_OGL, "    'vec4 %s' doesn't have proper \"Size\" name\n", uniform->member_name.c_str());
				continue;
			}
			sampler.category = Sampler2D::Category::Original;
			sampler.number = 0;
			m_original_needed = true;
		}
		else if(uniform->member_name == "Source" || uniform->member_name == "SourceSize")
		{
			if(is_vec4 && uniform->member_name != "SourceSize") {
				PWARNF(LOG_V1, LOG_OGL, "    'vec4 %s' doesn't have proper \"Size\" name\n", uniform->member_name.c_str());
				continue;
			}
			sampler.category = Sampler2D::Category::Source;
			sampler.number = 0;
			m_source_needed = true;
		}
		else if(std::regex_search(uniform->member_name, m, pass_output_re))
		{
			sampler.category = Sampler2D::Category::Output;
			if(is_vec4 && !m[1].matched) {
				PWARNF(LOG_V1, LOG_OGL, "    'vec4 %s' doesn't have proper \"Size\" name\n", uniform->member_name.c_str());
				continue;
			}
			sampler.number = str_parse_int_num(m[2].str());
		}
		else if(uniform->member_name == "OutputSize")
		{
			if(!is_vec4) {
				throw std::runtime_error(str_format("invalid uniform type for '%s'", uniform->name.c_str()));
			}
			sampler.category = Sampler2D::Category::Output;
			sampler.number = -1;
		}
		else if(std::regex_search(uniform->member_name, m, pass_feedback_re))
		{
			sampler.category = Sampler2D::Category::Feedback;
			if(is_vec4 && !m[1].matched) {
				PWARNF(LOG_V1, LOG_OGL, "    'vec4 %s' doesn't have proper \"Size\" name\n", uniform->member_name.c_str());
				continue;
			}
			sampler.number = str_parse_int_num(m[2].str());
		}
		else
		{
			// can be a reference to a named Pass or a User texture
			// is it a PassFeedback?
			if(std::regex_search(uniform->member_name, m, named_feedback_re) && m.size() >= 2) {
				if(is_vec4 && m.size() != 3) {
					// a uniform declared as "vec4 NAMEFeedback;" ?
					continue;
				}
				auto it = std::find(_pass_names.begin(), _pass_names.end(), m[1].str());
				if(it != _pass_names.end()) {
					// yes
					sampler.category = Sampler2D::Category::Feedback;
					sampler.number = it - _pass_names.begin();
				} else {
					// nope
					throw std::runtime_error(uniform->member_name + " is not a valid feedback sampler name");
				}
			} else {
				// is it a named Pass or a User texture?
				if(uniform->member_name == "FinalViewportSize") {
					// special case, the FinalViewport doesn't have a texture
					continue;
				}
				std::string base_name;
				if(is_vec4) {
					if(!std::regex_search(uniform->member_name, m, named_pass_re)) {
						// a uniform declared as "vec4 NAME;"
						continue;
					}
					base_name = m[1].str();
				} else {
					base_name = uniform->member_name;
				}
				auto it = std::find(_user_names.begin(), _user_names.end(), base_name);
				if(it != _user_names.end()) {
					// User texture!
					sampler.category = Sampler2D::Category::User;
					sampler.number = it - _user_names.begin();
				} else {
					// maybe a named Pass?
					it = std::find(_pass_names.begin(), _pass_names.end(), base_name);
					if(it != _pass_names.end()) {
						sampler.category = Sampler2D::Category::Output;
						sampler.number = it - _pass_names.begin();
					} else {
						// nope, bail
						throw std::runtime_error(base_name + " is not a valid texture name");
					}
				}
			}
		}

		Sampler2D *s = find_sampler(sampler.category, sampler.number);
		if(s) {
			if((is_vec4 && s->size_uniforms) || (is_sampler && s->tex_uniforms)) {
				// duplicate? it should probably be an assert.
				throw std::runtime_error(str_format("invalid declaration for %s uniform '%s'",
						is_vec4?"vec4":"sampler", uniform->name.c_str()));
			}
			if(is_sampler) {
				s->tex_uniforms = &unilist;
			}
			if(is_vec4) {
				s->size_uniforms = &unilist;
			}
		} else {
			if(is_sampler) {
				sampler.tex_uniforms = &unilist;
			}
			if(is_vec4) {
				sampler.size_uniforms = &unilist;
			}
			m_samplers.push_back(sampler);
			if(sampler.category == Sampler2D::Category::Output) {
				m_output_samplers.push_back(&m_samplers.back());
			} else if(sampler.category == Sampler2D::Category::Feedback) {
				m_feedback_samplers.push_back(&m_samplers.back());
			}
		}
	}
}

GLShaderProgram::Parameter * GLShaderProgram::get_parameter(std::string _name) const
{
	auto param = m_parameters_map.find(_name);
	if(param != m_parameters_map.end()) {
		return param->second;
	}
	return nullptr;
}

void GLShaderProgram::add_alias(const UniformList *_uniforms, BuiltinUniform _to)
{
	m_builtins[_to].insert(m_builtins[_to].end(), _uniforms->begin(), _uniforms->end());
}

void GLShaderProgram::use()
{
	GLCALL( glUseProgram(m_gl_name) );

	for(auto & block : m_blocks) {
		GLCALL( glBindBufferBase(GL_UNIFORM_BUFFER, block.binding, block.buffer) );
	}
}

const GLShaderProgram::UniformList * GLShaderProgram::find_uniform(std::string _name)
{
	auto u = m_uniform_names.find(_name);
	if(u != m_uniform_names.end()) {
		return &(u->second);
	}
	return nullptr;
}

const GLShaderProgram::UniformList * GLShaderProgram::get_builtin(BuiltinUniform _name)
{
	// will never return nullptr
	return &m_builtins[_name];
}

const char * GLShaderProgram::get_builtin_name(BuiltinUniform _builtin)
{
	for (const auto & [name, builtin] : ms_builtin_uniform_names) {
		if(builtin == _builtin) {
			return name.c_str();
		}
	}
	return "";
}

const GLShaderProgram::Uniform & GLShaderProgram::get_uniform(GLuint _index)
{
	return m_uniforms[_index];
}

std::vector<std::string> GLShaderProgram::get_uniform_names(const GLShaderProgram::UniformList *_uni) const
{
	std::vector<std::string> names;
	for(auto uid : *_uni) {
		names.push_back(m_uniforms[uid].name);
	}
	return names;
}

void GLShaderProgram::set_uniform_sampler2D(const UniformList *_name, GLuint _sampler, GLuint _texture)
{
	assert(_name);
	for(auto idx : *_name) {
		GLCALL( glActiveTexture(GL_TEXTURE0 + m_uniforms[idx].binding) );
		GLCALL( glBindTexture(GL_TEXTURE_2D, _texture) );
		// Here, unit​ is an integer, not an enum the way it is for glActiveTexture:
		GLCALL( glBindSampler(m_uniforms[idx].binding, _sampler) );
		GLCALL( glUniform1i(m_uniforms[idx].location, m_uniforms[idx].binding) );
	}
}

void GLShaderProgram::set_uniform_int(const UniformList *_name, GLint _value)
{
	assert(_name);
	for(auto idx : *_name) {
		if(m_uniforms[idx].block >= 0) {
			m_blocks[m_uniforms[idx].block].update_uniform(m_uniforms[idx], sizeof(GLint), &_value);
		} else {
			GLCALL( glUniform1i(m_uniforms[idx].location, _value) );
		}
	}
}

void GLShaderProgram::set_uniform_uint(const UniformList *_name, GLuint _value)
{
	assert(_name);
	for(auto idx : *_name) {
		if(m_uniforms[idx].block >= 0) {
			m_blocks[m_uniforms[idx].block].update_uniform(m_uniforms[idx], sizeof(GLuint), &_value);
		} else {
			GLCALL( glUniform1ui(m_uniforms[idx].location, _value) );
		}
	}
}

void GLShaderProgram::set_uniform_float(const UniformList *_name, GLfloat _value)
{
	assert(_name);
	for(auto idx : *_name) {
		if(m_uniforms[idx].block >= 0) {
			m_blocks[m_uniforms[idx].block].update_uniform(m_uniforms[idx], sizeof(GLfloat), &_value);
		} else {
			GLCALL( glUniform1f(m_uniforms[idx].location, _value) );
		}
	}
}

void GLShaderProgram::set_uniform_vec4f(const UniformList *_name, const vec4f &_value)
{
	assert(_name);
	for(auto idx : *_name) {
		if(m_uniforms[idx].block >= 0) {
			m_blocks[m_uniforms[idx].block].update_uniform(m_uniforms[idx], 4*sizeof(GLfloat), static_cast<const float*>(_value));
		} else {
			GLCALL( glUniform4fv(m_uniforms[idx].location, 1, static_cast<const float*>(_value)) );
		}
	}
}

void GLShaderProgram::set_uniform_mat4f(const UniformList *_name, const mat4f &_value)
{
	assert(_name);
	for(auto idx : *_name) {
		if(m_uniforms[idx].block >= 0) {
			m_blocks[m_uniforms[idx].block].update_uniform(m_uniforms[idx], 16*sizeof(GLfloat), _value.data());
		} else {
			GLCALL( glUniformMatrix4fv(m_uniforms[idx].location, 1, GL_FALSE, _value.data()) );
		}
	}
}