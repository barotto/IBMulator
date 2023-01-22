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
#include "gl_framebuffer.h"


GLFramebuffer::GLFramebuffer(std::string _name, std::shared_ptr<GLTexture> _target)
: m_name(_name), m_target(_target)
{
	assert(_target);
	GLCALL( glGenFramebuffers(1, &m_gl_name) );

	// When sampling from a Framebuffer texture the Y is inverted.
	// So render "upside-down" (bottom=0.0, top=1.0)
	// TODO move to the renderer? in the current implementation the matrices are constant
	m_mvmat.load_identity();
	m_pmat = mat4_ortho<float>(0.f, 1.f, 0.f, 1.f, 0.f, 1.f);
	m_mvpmat = m_pmat;
	m_mvpmat.multiply(m_mvmat);

	PDEBUGF(LOG_V1, LOG_OGL, "Created Framebuffer %s -> %s, GL:%u\n", _name.c_str(), _target->get_name().c_str(), m_gl_name);
}

void GLFramebuffer::bind_target()
{
	PDEBUGF(LOG_V3, LOG_OGL, "FB binding to GL:%u\n", m_target->get_gl_name());
	if(m_target->get_size().x == 0.f || m_target->get_size().y == 0.f) {
		PDEBUGF(LOG_V3, LOG_OGL, " target GL:%u is not initialized\n", m_target->get_gl_name());
		return;
	}
	GLCALL( glBindFramebuffer(GL_FRAMEBUFFER, m_gl_name) );
	GLCALL( glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_target->get_gl_name(), 0) );
	GLenum status;
	GLCALL( status = glCheckFramebufferStatus(GL_FRAMEBUFFER) );
	if(status != GL_FRAMEBUFFER_COMPLETE) {
		throw std::runtime_error("invalid framebuffer object status");
	}
	m_target_bound = true;
}

void GLFramebuffer::size_updated()
{
	//?
}

void GLFramebuffer::use()
{
	PDEBUGF(LOG_V3, LOG_OGL, "FB GL:%u rendering to target '%s' GL:%u\n",
			m_gl_name, m_target->get_name().c_str(), m_target->get_gl_name());

	if(!m_target_bound) {
		bind_target();
		m_target_bound = true;
	}

	GLCALL( glBindFramebuffer(GL_FRAMEBUFFER, m_gl_name) );
	GLCALL( glViewport(0, 0, m_target->get_size().x, m_target->get_size().y) );
	//GLCALL( glClear(GL_COLOR_BUFFER_BIT) );
	if(m_target->is_srgb()) {
		GLCALL( glEnable(GL_FRAMEBUFFER_SRGB) ); 
	} else {
		GLCALL( glDisable(GL_FRAMEBUFFER_SRGB) );
	}
	m_target->set_dirty(true);
}

void GLFramebuffer::update_target()
{
	m_target->update();
}

void GLFramebuffer::clear()
{
	if(m_target->is_dirty()) {
		PDEBUGF(LOG_V3, LOG_OGL, "FB GL:%u clearing target '%s' GL:%u\n",
				m_gl_name, m_target->get_name().c_str(), m_target->get_gl_name());

		static const GLuint clearColor[4] = {0, 0, 0, 0};
		GLCALL( glBindFramebuffer(GL_FRAMEBUFFER, m_gl_name) );
		GLCALL( glClearBufferuiv(GL_COLOR, 0, clearColor) );
	}
	m_target->set_dirty(false);
}
