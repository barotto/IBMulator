/*
 * Copyright (C) 2015, 2016  Marco Bortolin
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
#include "gui/gui.h"
#include "desktop.h"
#include <Rocket/Core.h>


Desktop::Desktop(GUI * _gui)
:
Window(_gui, "desktop.rml")
{
	ASSERT(m_wnd);
	m_wnd->AddEventListener("click", this, false);
}

Desktop::~Desktop()
{
}

void Desktop::ProcessEvent(Rocket::Core::Event &)
{
	//Rocket::Core::Element * el = event.GetTargetElement();

}
