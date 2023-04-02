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

#include "ibmulator.h"
#include "program.h"
#include "shader_preset.h"
#include "shader_exception.h"
#include "utils.h"
#include "filesys.h"
#include "gui.h"
#include <regex>

std::list<std::string> ShaderPreset::read_preset_file(std::string _path)
{
	if(!FileSys::file_exists(_path.c_str())) {
		throw std::runtime_error("file not found");
	}
	std::list<std::string> data;
	std::ifstream stream = FileSys::make_ifstream(_path.c_str());
	if(stream.is_open()) {
		std::string line = "";
		bool comment_block = false;
		while(std::getline(stream, line)) {
			// trimming will force '#' and '/*' to first place
			line = str_trim(line);
			// ok, this is bad, but should work 98.3% of the times
			if(line.size() >= 2) {
				if(!comment_block && line.at(0) == '/' && line.at(1) == '*') {
					comment_block = true;
					continue;
				} else if(comment_block && (
					(line.at(line.size()-2) == '*' && line.at(line.size()-1) == '/'))
				){
					comment_block = false;
					continue;
				}
			}
			if(!comment_block) {
				// don't skip empty lines (for debugging)
				data.emplace_back(line + "\n");
			}
		}
		stream.close();
	} else {
		throw std::runtime_error("cannot open file for reading");
	}
	return data;
}

INIFile ShaderPreset::include_preset_file(std::string _preset)
{
	if(m_references.find(_preset) != m_references.end()) {
		throw std::runtime_error("circular dependency");
	}
	m_references.insert(_preset);

	// TODO? this mess only because i insist in using the "inih" library
	auto data = read_preset_file(_preset);

	auto tmppath = FileSys::get_next_filename(g_program.config().get_cfg_home(), PACKAGE_NAME "-tmp-", ".ini");
	if(tmppath.empty()) {
		throw std::runtime_error("cannot create a temporary file");
	}
	auto tmpfile = FileSys::make_ofstream(tmppath.c_str(), std::ios::binary);
	if(!tmpfile.is_open()) {
		throw std::runtime_error("cannot create a temporary file");
	}
	for(auto & line : data) {
		tmpfile.write(line.c_str(), line.size());
	}
	tmpfile.close();

	INIFile ini;
	try {
		ini.parse(tmppath);
	} catch(std::runtime_error &e) {
		FileSys::remove(tmppath.c_str());
		if(m_error > 0) {
			throw ShaderPresetExc(e.what(), _preset, data, m_error);
		}
		throw;
	}
	FileSys::remove(tmppath.c_str());

	for(auto line=data.begin(); line!=data.end(); line++) {
		if(line->at(0) == '#' && line->find("reference", 1) == 1) {
			std::regex inclreg("#reference\\s+\"?([^\n\"]*)\"?");
			std::smatch m;
			if(std::regex_search(*line, m, inclreg) && m[1].matched) {
				PINFOF(LOG_V1, LOG_OGL, " referencing %s from %s\n", m[1].str().c_str(), _preset.c_str());
				try {
					std::string inclpath = g_program.config().find_shader_asset_relative_to(m[1].str(), _preset);
					INIFile ref = include_preset_file(inclpath);
					ini.apply_defaults(ref);
				} catch(std::runtime_error &e) {
					throw std::runtime_error(str_format("cannot reference '%s': %s", m[1].str().c_str(), e.what()));
				}
			} else {
				throw std::runtime_error(str_format("invalid reference '%s'", line->c_str()));
			}
		}
	}

	int shaders = ini.get_int("", "shaders", 0);
	for(unsigned s=0; s<unsigned(shaders); s++) {
		std::string key = str_format("shader%u", s);
		std::string file_path = ini.get_string("", key);
		if(!FileSys::is_absolute(file_path.c_str(), file_path.size())) {
			file_path = g_program.config().find_shader_asset_relative_to(file_path, _preset);
			ini.set_string("", key, file_path);
		}
	}

	std::string textures = ini.get_string("", "textures", "");
	for(auto & name : parse_tokens(textures, "\\;")) {
		std::string file_path = ini.get_string("", name);
		if(!FileSys::is_absolute(file_path.c_str(), file_path.size())) {
			file_path = g_program.config().find_shader_asset_relative_to(file_path, _preset);
			ini.set_string("", name, file_path);
		}
	}

	return ini;
}

void ShaderPreset::load(std::string _path)
{
	auto ini = include_preset_file(_path);
	m_values = std::move(ini.get_values());
	m_parsed_file = _path;
	
	int shaders = get_int("", "shaders", 0);

	ini_enum_map_t wrap_enums{
		{ "repeat", Wrap::repeat },
		{ "clamp_to_edge", Wrap::clamp_to_edge },
		{ "clamp_to_border", Wrap::clamp_to_border },
		{ "mirrored_repeat", Wrap::mirrored_repeat },
		{ "mirror_clamp_to_edge", Wrap::mirror_clamp_to_edge }
	};
	ini_enum_map_t scale_enums{
		{ "original", Scale::original },
		{ "source", Scale::source },
		{ "viewport", Scale::viewport },
		{ "absolute", Scale::absolute }
	};

	for(unsigned s=0; s<unsigned(shaders); s++) {
		ShaderN sh{};
		std::string keyname;

		sh.num = s;

		sh.shader = get_string("", str_format("shader%u", s));
		if(sh.shader.empty()) {
			throw std::runtime_error(str_format("invalid shader%u path\n",s));
		}


		// INPUT SAMPLER PROPERTIES

		sh.filter_linear = get_bool("", str_format("filter_linear%u",s), true);
		sh.mipmap_input = get_bool("", str_format("mipmap_input%u",s), false);
		keyname = str_format("texture_wrap_mode%u",s);
		if(!is_key_present("", keyname)) {
			keyname = str_format("wrap_mode%u",s);
		}
		sh.wrap_mode = static_cast<Wrap>(get_enum("", keyname, wrap_enums, Wrap::clamp_to_border));


		// OUTPUT FRAMEBUFFER PROPERTIES

		sh.alias = get_string("", str_format("alias%u",s), "");
		sh.float_framebuffer = get_bool("", str_format("float_framebuffer%u",s), false);
		sh.srgb_framebuffer = get_bool("", str_format("srgb_framebuffer%u",s), false);

		//These values control the scaling params from scale_typeN.
		//   The values may be either floating or int depending on the type.
		//   scaleN controls both scaling type in horizontal and vertical directions.
		//
		//   If scaleN is defined, scale_xN and scale_yN have no effect.
		//   scale_xN and scale_yN controls scaling properties for the directions
		//   separately. Should only one of these be defined, the other direction
		//   will assume a "source" scale with value 1.0, i.e. no change in resolution.
		//
		//   Should scale_type_xN and scale_type_yN be set to different values,
		//   the use of scaleN is undefined (i.e. if X-type is absolute (takes int),
		//   and Y-type is source (takes float).)
		bool scale_x_defined = false;
		bool scale_y_defined = false;
		if(is_key_present("", str_format("scale_type%u",s))) {
			Scale scale_type = static_cast<Scale>(get_enum("", str_format("scale_type%u",s), scale_enums));
			sh.scale_type_x = scale_type;
			sh.scale_type_y = scale_type;
			scale_x_defined = true;
			scale_y_defined = true;
		}
		if(is_key_present("", str_format("scale_type_x%u",s))) {
			sh.scale_type_x = static_cast<Scale>(get_enum("", str_format("scale_type_x%u",s), scale_enums));
			scale_x_defined = true;
		}
		if(is_key_present("", str_format("scale_type_y%u",s))) {
			sh.scale_type_y = static_cast<Scale>(get_enum("", str_format("scale_type_y%u",s), scale_enums));
			scale_y_defined = true;
		}
		if(!scale_x_defined) {
			if(s < unsigned(shaders)-1) {
				sh.scale_type_x = Scale::source;
			} else {
				sh.scale_type_x = Scale::viewport;
			}
		}
		if(!scale_y_defined) {
			if(s < unsigned(shaders)-1) {
				sh.scale_type_y = Scale::source;
			} else {
				sh.scale_type_y = Scale::viewport;
			}
		}
		if(s == 0) {
			if(sh.scale_type_x == Scale::source) {
				sh.scale_type_x = Scale::original;
			}
			if(sh.scale_type_y == Scale::source) {
				sh.scale_type_y = Scale::original;
			}
		}
		if(is_key_present("", str_format("scale%u",s))) {
			float scale = get_real("", str_format("scale%u",s));
			sh.scale_x = scale;
			sh.scale_y = scale;
		}
		if(is_key_present("", str_format("scale_x%u",s))) {
			sh.scale_x = get_real("", str_format("scale_x%u",s));
		}
		if(is_key_present("", str_format("scale_y%u",s))) {
			sh.scale_y = get_real("", str_format("scale_y%u",s));
		}


		// OTHER PROPERTIES

		sh.frame_count_mod = get_int("", str_format("frame_count_mod%u",s), 0);
		sh.blending_output = get_bool("", str_format("ibmu_blending_output%u",s), false);

		m_shaders.push_back(sh);
	}


	// Textures

	std::string textures = get_string("", "textures", "");
	if(!textures.empty()) {
		for(auto & name : parse_tokens(textures, "\\;")) {
			Texture tex{};
			std::string keyname;

			tex.name = name;

			tex.path = get_string("", name, "");
			if(tex.path.empty()) {
				throw std::runtime_error(str_format("invalid texture '%s' path", name.c_str()));
			}

			keyname = str_format("%s_filter_linear", name.c_str());
			if(!is_key_present("", keyname)) {
				keyname = str_format("%s_linear", name.c_str());
			}
			tex.filter_linear = get_bool("", keyname, true);

			keyname = str_format("%s_repeat_mode", name.c_str());
			if(!is_key_present("", keyname)) {
				keyname = str_format("%s_wrap_mode", name.c_str());
			}
			tex.wrap_mode = static_cast<Wrap>(get_enum("", keyname, wrap_enums, Wrap::clamp_to_border));

			tex.mipmap = get_bool("", str_format("%s_mipmap", name.c_str()), false);

			m_textures.push_back(tex);
		}
	}


	// Defines

	std::string defines = get_string("", "ibmu_defines", "");
	if(!defines.empty()) {
		for(auto & name : parse_tokens(defines, "\\;")) {
			if(isalpha(name.at(0)) || name.at(0) == '_') {
				std::string value = get_string("", name, "");
				m_defines.emplace_back(name, value);
			}
		}
	}

	m_input_size = static_cast<InputSize>(
		get_enum("", "ibmu_input_size", {
			{ "crtc", InputSize::CRTC },
			{ "video_mode", InputSize::video_mode },
		}, InputSize::input_undef)
	);

	// Realistic interface mode integration

	m_rendering_size = static_cast<RenderingSize>(
		get_enum("", "ibmu_rendering_size", {
			{ "vga", RenderingSize::VGA },
			{ "crt", RenderingSize::CRT },
			{ "monitor", RenderingSize::monitor }
		}, RenderingSize::VGA)
	);

	MonitorGeometry defgeom;
	m_monitor_geometry.width = get_real("", "ibmu_monitor_width", defgeom.width); 
	m_monitor_geometry.height = get_real("", "ibmu_monitor_height", defgeom.height);
	m_monitor_geometry.crt_width = get_real("", "ibmu_crt_width", defgeom.crt_width);
	m_monitor_geometry.crt_height = get_real("", "ibmu_crt_height", defgeom.crt_height);
	m_monitor_geometry.bezel_width = get_real("", "ibmu_monitor_bezelw", defgeom.bezel_width);
	m_monitor_geometry.bezel_height = get_real("", "ibmu_monitor_bezelh", defgeom.bezel_height);
	m_monitor_geometry.vga_scale = get_real("", "ibmu_vga_scale", defgeom.vga_scale);
}

float ShaderPreset::get_parameter_value(std::string _name, float _initial)
{
	return get_real("", _name, _initial);
}

ShaderPreset::InputSize ShaderPreset::get_input_size()
{
	ini_enum_map_t input_size{
		{ "crtc", InputSize::CRTC },
		{ "video_mode", InputSize::video_mode },
	};

	InputSize size = static_cast<InputSize>(g_program.config().get_enum(DISPLAY_SECTION, DISPLAY_SHADER_INPUT, input_size, InputSize::input_undef));

	if(size == InputSize::input_undef) {
		size = static_cast<InputSize>(get_enum("", "ibmu_input_size", input_size, InputSize::video_mode));
	}

	return size;
}

std::string ShaderPreset::get_output_size()
{
	auto viewport_size = get_string("", "ibmu_output_size", "");
	if(viewport_size.empty()) {
		viewport_size = g_program.config().get_string(DISPLAY_SECTION, DISPLAY_SHADER_OUTPUT, "native");
	}
	return viewport_size;
}

ShaderPreset::SamplersMode ShaderPreset::get_samplers_mode()
{
	ini_enum_map_t samplers_mode{
		{ "texture", SamplersMode::texture },
		{ "pass",    SamplersMode::pass    }
	};

	SamplersMode mode = static_cast<SamplersMode>(get_enum("", "ibmu_samplers_mode", samplers_mode, SamplersMode::samplers_undef));

	if(mode == SamplersMode::samplers_undef) {
		mode = static_cast<SamplersMode>(g_program.config().get_enum(DISPLAY_SECTION, DISPLAY_SAMPLERS_MODE, samplers_mode, SamplersMode::texture));
	}

	return mode;
}

void ShaderPreset::write_reference(FILE *_file, std::string _preset)
{
	_preset = "#reference " + _preset + "\n\n";
	if(fwrite(_preset.data(), _preset.size(), 1, _file) != 1) {
		throw std::runtime_error("Cannot write to file.");
	}
}

void ShaderPreset::write_comment(FILE *_file, std::string _comment)
{
	_comment = "// " + _comment + "\n";
	if(fwrite(_comment.data(), _comment.size(), 1, _file) != 1) {
		throw std::runtime_error("Cannot write to file.");
	}
}

void ShaderPreset::write_parameter(FILE *_file, std::string _name, float _value)
{
	std::string line = str_format("%s = %f\n", _name.c_str(), _value);
	if(fwrite(line.data(), line.size(), 1, _file) != 1) {
		throw std::runtime_error("Cannot write to file.");
	}
}