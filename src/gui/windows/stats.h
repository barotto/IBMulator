/*
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IBMULATOR_GUI_STATS_H
#define IBMULATOR_GUI_STATS_H

#include <Rocket/Core/EventListener.h>

class Machine;
class GUI;
class Mixer;

class Stats : public Window
{
private:

	struct {
		Rocket::Core::Element *fps, *machine, *mixer;
	} m_stats;

	Machine * m_machine;
	Mixer * m_mixer;

public:

	Stats(Machine *_machine, GUI * _gui, Mixer *_mixer);
	~Stats();

	void update();

	void ProcessEvent(Rocket::Core::Event & event);
};

#endif
