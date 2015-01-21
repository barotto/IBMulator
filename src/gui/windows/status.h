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

#ifndef IBMULATOR_GUI_STATUS_H
#define IBMULATOR_GUI_STATUS_H

#include <Rocket/Core/EventListener.h>

class Machine;
class GUI;

class Status : public Window
{
private:

	struct {
		Rocket::Core::Element *power_led, *floppy_a_led, *floppy_b_led, *hdd_led;
	} m_status;

	struct {
		bool power, floppy_a, floppy_b, hdd;
	} m_leds;

	Machine * m_machine;

public:

	Status(GUI * _gui);
	~Status();

	void update();

	void ProcessEvent(Rocket::Core::Event & event);
};

#endif
