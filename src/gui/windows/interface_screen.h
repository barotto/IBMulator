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

#ifndef IBMULATOR_GUI_INTERFACE_SCREEN_H
#define IBMULATOR_GUI_INTERFACE_SCREEN_H

#include "gui/screen_renderer.h"

class GUI;

class InterfaceScreen
{
protected:
	std::unique_ptr<ScreenRenderer> m_renderer;
	GUI *m_gui;
	VGADisplay m_display; // GUI-Machine interface
	
public:
	ScreenRenderer::Params params;
	
public:
	InterfaceScreen(GUI *_gui);
	virtual ~InterfaceScreen() {}

	void set_brightness(float);
	void set_contrast(float);
	void set_saturation(float);
	void set_ambient(float);
	void set_monochrome(bool);
	
	ScreenRenderer * renderer() { return m_renderer.get(); }
	VGADisplay * display() { return &m_display; }

	virtual void render();

protected:
	void sync_with_device();
};

#endif
