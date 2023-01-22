/*
 * Copyright (C) 2019-2023  Marco Bortolin
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

#ifndef IBMULATOR_GUI_OPENGL_H
#define IBMULATOR_GUI_OPENGL_H

#include "gui.h"
#include "opengl.h"


class GUI_OpenGL : public GUI
{
protected:
	SDL_GLContext m_SDL_glcontext;
	int m_gl_errors_count;
	
	void create_window(int _flags);
	void check_device_GL_caps();
	void create_renderer();
	
	static void GL_debug_output(GLenum source, GLenum type, GLuint id,
		GLenum severity, GLsizei length,
		const GLchar* message, GLvoid* userParam);

public:
	GUI_OpenGL();
	~GUI_OpenGL();
	
	GUIRenderer renderer() const { return GUI_RENDERER_OPENGL; }
	void render();

	void update_texture(uintptr_t _texture, SDL_Surface *_data);
};

#endif