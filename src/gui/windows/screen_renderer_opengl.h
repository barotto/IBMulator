/*
 * Copyright (C) 2019-2021  Marco Bortolin
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

#ifndef IBMULATOR_GUI_SCREEN_RENDERER_OPENGL_H
#define IBMULATOR_GUI_SCREEN_RENDERER_OPENGL_H

#include "screen_renderer.h"
#include <GL/glew.h>

class ScreenRenderer_OpenGL : public ScreenRenderer
{
protected:
	struct {
		int    fb_width; // the framebuffer width
		vec2i  res; // the last VGA image resolution (x can be smaller than fb_width)
		
		GLuint texture;
		GLuint sampler;
		GLuint program;
		
		GLint  glintformat;
		GLenum glformat;
		GLenum gltype;
	
		struct {
			GLint mvmat;
			GLint pmat;
			GLint vga_map;
			GLint vga_scale;
			GLint brightness;
			GLint contrast;
			GLint saturation;
			GLint display_size;
			GLint ambient;
			GLint reflection_map;
			GLint reflection_scale;
			GLint is_monochrome;
		} uniforms;
	} m_vga;

	struct {
		GLuint reflection_map;
		GLuint reflection_sampler;
		GLuint program;

		struct {
			GLint pmat;
			GLint mvmat;
			GLint ambient;
			GLint reflection_map;
		} uniforms;
	} m_monitor;
	
	GLuint  m_vertex_buffer;
	GLfloat m_quad_data[18];
	
	void render_quad();
	
public:
	ScreenRenderer_OpenGL();
	~ScreenRenderer_OpenGL();
	
	void init(VGADisplay &_vga);
	
	void load_vga_program(std::string _vshader, std::string _fshader, unsigned _sampler);
	void load_monitor_program(std::string _vshader, std::string _fshader, std::string _reflection_map);
	
	void store_vga_framebuffer(FrameBuffer &_fb, const vec2i &_vga_res);
	
	void render_vga(const mat4f &_pmat, const mat4f &_mvmat, const vec2i &_display_size, 
		float _brightness, float _contrast, float _saturation, bool _is_monochrome,
		float _ambient, const vec2f &_vga_scale, const vec2f &_reflection_scale);
	void render_monitor(const mat4f &_pmat, const mat4f &_mvmat, float _ambient);
};

#endif