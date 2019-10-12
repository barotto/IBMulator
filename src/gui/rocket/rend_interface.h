/*
 * Copyright (C) 2015-2019  Marco Bortolin
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

#ifndef IBMULATOR_GUI_REND_INTERFACE_H
#define IBMULATOR_GUI_REND_INTERFACE_H

#include <SDL.h>
#include <Rocket/Core/RenderInterface.h>


class RocketRenderer : public Rocket::Core::RenderInterface
{
protected:
	SDL_Renderer* m_renderer;
	SDL_Window* m_screen;

public:
	RocketRenderer(SDL_Renderer * _renderer, SDL_Window * _screen);
	virtual ~RocketRenderer();

	virtual void SetDimensions(int _width, int _height);
};

#endif
