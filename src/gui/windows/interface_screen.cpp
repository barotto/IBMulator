/*
 * Copyright (C) 2015-2023  Marco Bortolin
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
#include "interface_screen.h"
#include "gui/gui.h"
#include "gui/gui_sdl2d.h"
#include "gui/screen_renderer_opengl.h"
#include "gui/screen_renderer_sdl2d.h"
#include "program.h"

InterfaceScreen::InterfaceScreen(GUI *_gui)
{
	m_gui = _gui;
	switch(_gui->renderer()) {
		case GUI_RENDERER_OPENGL: {
			m_renderer = std::make_unique<ScreenRenderer_OpenGL>();
			ScreenRenderer_OpenGL *renderer = dynamic_cast<ScreenRenderer_OpenGL*>(m_renderer.get());
			if(!renderer) {
				assert(false);
				throw std::exception();
			}
			renderer->init(m_display);
			break;
		}
		case GUI_RENDERER_SDL2D: {
			m_renderer = std::make_unique<ScreenRenderer_SDL2D>();
			GUI_SDL2D *gui = dynamic_cast<GUI_SDL2D*>(_gui);
			if(!gui) {
				assert(false);
				throw std::exception();
			}
			ScreenRenderer_SDL2D *renderer = dynamic_cast<ScreenRenderer_SDL2D*>(m_renderer.get());
			if(!renderer) {
				assert(false);
				throw std::exception();
			}
			renderer->init(m_display, gui->sdl_renderer());
			break;
		}
		default: {
			// errors should be detected during GUI object creation
			PDEBUGF(LOG_V0, LOG_GUI, "Invalid renderer!\n");
			return;
		}
	}

	params.vga.mvmat.load_identity();
	params.vga.pmat = mat4_ortho<float>(0.0, 1.0, 1.0, 0.0, 0.0, 1.0);
	params.vga.mvpmat = params.vga.pmat;

	params.crt.mvmat.load_identity();
	params.crt.pmat = mat4_ortho<float>(0.0, 1.0, 1.0, 0.0, 0.0, 1.0);
	params.crt.mvpmat = params.crt.pmat;
}

void InterfaceScreen::set_brightness(float _v)
{
	params.brightness = _v;
	params.updated = true;
}

void InterfaceScreen::set_contrast(float _v)
{
	params.contrast = _v;
	params.updated = true;
}

void InterfaceScreen::set_saturation(float _v)
{
	params.saturation = _v;
	params.updated = true;
}

void InterfaceScreen::set_ambient(float _v)
{
	params.ambient = _v;
	params.updated = true;
}

void InterfaceScreen::set_monochrome(bool _v)
{
	m_display.set_monochrome(_v);
	params.monochrome = _v;
	params.updated = true;
}

void InterfaceScreen::render()
{
	sync_with_device();

	if(params.updated) {
		m_renderer->store_screen_params(params);
		params.updated = false;
	}
	m_renderer->render_begin();
	m_renderer->render_vga();
	m_renderer->render_end();
}

void InterfaceScreen::sync_with_device()
{
	// TODO The machine is a different thread and these methods are not thread safe.
	// They could return garbage, but the worst that would happen is some sporadic
	// tearing or stuttering. The wait could be skipped (tearing) or could be
	// called without reason (stuttering) but in any case the program should
	// not end in a deadlock.
	if(m_gui->threads_sync_enabled())
	{
		if(
			g_machine.is_on() && 
			!g_machine.is_paused() && 
			g_machine.cycles_factor() == 1.0 &&
			g_machine.get_bench().load < 1.0
		) {
			try {
				// Wait for no more than 2 frames.
				// Using a timeout let us simplify the code at the expense of possible
				// stuttering, which would happen only in specific and non meaningful
				// cases like when the user pauses the machine.
				// I think this is acceptable. 
				m_display.wait_for_device(g_program.heartbeat() * 2);
			} catch(std::exception &) {}

			g_program.pacer().skip();
		}
	}
	else
	{
		g_program.pacer().skip();
	}

	if(m_gui->vga_buffering_enabled()) {
		m_display.lock();
		VideoModeInfo vga_mode = m_display.last_mode();
		// this intermediate buffer is to reduce the blocking effect of glTexSubImage2D:
		// when the program runs with the default shaders, the load on the GPU is very low
		// so the drivers lower the GPU's clocks to the minimum value;
		// the result is the GPU's memory controller load goes high and glTexSubImage2D takes
		// a lot of time to complete, bloking the machine emulation thread.
		// PBOs are a possible alternative, but a memcpy is way simpler.
		FrameBuffer vga_buf = m_display.last_framebuffer();
		m_display.unlock();
		// now the Machine thread is free to continue emulation
		// meanwhile we start rendering the last VGA image
		m_renderer->store_vga_framebuffer(vga_buf, vga_mode);
	} else if(m_display.fb_updated() || m_renderer->needs_vga_updates()) {
		m_display.lock();
		FrameBuffer vga_buf = m_display.framebuffer();
		VideoModeInfo vga_mode = m_display.mode();
		m_display.clear_fb_updated();
		m_display.unlock();
		m_renderer->store_vga_framebuffer(vga_buf, vga_mode);
	}
}
