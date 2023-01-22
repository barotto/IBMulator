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

#ifndef IBMULATOR_GL_FRAMEBUFFER
#define IBMULATOR_GL_FRAMEBUFFER


#include "gl_texture.h"
#include "matrix.h"

class GLFramebuffer
{
protected:
	std::string m_name;
	GLuint m_gl_name = 0;
	std::shared_ptr<GLTexture> m_target;
	bool m_target_bound = false;
	mat4f m_pmat;
	mat4f m_mvmat;
	mat4f m_mvpmat;

public:
	GLFramebuffer(std::string _name, std::shared_ptr<GLTexture> _target);

	GLFramebuffer() = delete;
	GLFramebuffer(const GLFramebuffer &) = delete;
	GLFramebuffer &operator=(const GLFramebuffer &) = delete;
	
	void bind_target();
	void use();
	void update_target();
	void clear();
	GLTexture * get_target() const { return m_target.get(); }

	void size_updated();
	const vec4f & get_size() const { return m_target->get_size(); }
	const mat4f & get_pmat() const { return m_pmat; }
	const mat4f & get_mvmat() const { return m_mvmat; }
	const mat4f & get_mvpmat() const { return m_mvpmat; }
};

#endif