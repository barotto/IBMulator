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

#ifndef IBMULATOR_SHADER_PRESET
#define IBMULATOR_SHADER_PRESET

#include "ini.h"

class ShaderPreset : public INIFile
{
public:
	enum Scale {
		original,
		source,
		viewport,
		absolute
	};
	enum Wrap {
		repeat,
		mirrored_repeat,
		clamp_to_edge,
		clamp_to_border,
		mirror_clamp_to_edge
	};
	enum SamplersMode {
		samplers_undef, texture, pass
	};
	
	inline static std::map<Scale, std::string> ms_scale_str{
		{ original, "original" },
		{ source,   "source" },
		{ viewport, "viewport" },
		{ absolute, "absolute" }
	};
	inline static std::map<Wrap, std::string> ms_wrap_str{
		{ repeat, "repeat" },
		{ mirrored_repeat, "mirrored_reapeat"},
		{ clamp_to_edge, "clamp_to_edge" },
		{ clamp_to_border, "clamp_to_border" },
		{ mirror_clamp_to_edge, "mirror_clamp_to_edge" }
	};

	struct ShaderN {
		unsigned num = 0;
		std::string shader;
		// input sampler properties:
		bool filter_linear = true;
		Wrap wrap_mode = Wrap::clamp_to_border; 
		bool mipmap_input = false;
		// output buffer properties:
		std::string alias;
		bool float_framebuffer = false; // render to floating point buffer?
		bool srgb_framebuffer = false; // render to srgb buffer?
		Scale scale_type_x = Scale::source;
		float scale_x = 1.f;
		Scale scale_type_y = Scale::source;
		float scale_y = 1.f;
		// other:
		int frame_count_mod = 0; // modulo to apply to FrameCount
		bool blending_output = false; // enable blending during rendering
	};

	struct Texture {
		std::string name;
		std::string path;
		bool filter_linear = false;
		Wrap wrap_mode = Wrap::repeat;
		bool mipmap = false;
	};

	using DefinesList = std::vector<std::pair<std::string,std::string>>;

	// default values are from the monitor part of the default texture
	struct MonitorGeometry {
		// values are in pixels (except where noted)
		float width = 1100.f;       // the monitor frame width 
		float height = 860.f;       // the monitor frame height (only the bezel, w/o the base)
		float crt_width = 862.f;    // CRT glass width
		float crt_height = 650.f;   // CRT glass height
		float bezel_width = 119.f;  // bezel width (left and right bezels must be symmetrical)
		float bezel_height = 105.f; // top bezel height
		float vga_scale = 0.85f;    // the size of the VGA image as a scale factor relative to the CRT
	};

	enum InputSize {
		input_undef, CRTC, video_mode
	};
	enum RenderingSize {
		VGA, CRT, monitor
	};

protected:
	std::set<std::string> m_references;
	std::vector<ShaderN> m_shaders;
	std::vector<Texture> m_textures;
	DefinesList m_defines;

	InputSize m_input_size = InputSize::input_undef;

	// Realistic interface mode integration
	RenderingSize m_rendering_size = RenderingSize::VGA;
	MonitorGeometry m_monitor_geometry;

public:
	void load(std::string _path);
	
	const std::vector<ShaderN> & get_shaders() const { return m_shaders; }
	const ShaderN & get_shader(unsigned _n) const { return m_shaders[_n]; }
	const ShaderN & operator[](unsigned _n) const { return m_shaders[_n]; }
	const std::vector<Texture> & get_textures() const { return m_textures; }
	const DefinesList & get_defines() const { return m_defines; }
	RenderingSize get_rendering_size() const { return m_rendering_size; }
	const MonitorGeometry & get_monitor_geometry() const { return m_monitor_geometry; }

	float get_parameter_value(std::string _name, float _initial);

	InputSize get_input_size();
	SamplersMode get_samplers_mode();
	
	static void write_reference(FILE *_file, std::string _preset);
	static void write_comment(FILE *_file, std::string _comment);
	static void write_parameter(FILE *_file, std::string _name, float _value);

protected:
	INIFile include_preset_file(std::string _path);
	std::list<std::string> read_preset_file(std::string _path);
	void resolve_paths(INIFile &);
	static std::string resolve_relative_path(std::string _path, std::string _relative_to);
};

#endif