/*
 * Copyright (C) 2015-2021  Marco Bortolin
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

#ifndef IBMULATOR_GUI_STATUS_H
#define IBMULATOR_GUI_STATUS_H

#include <Rocket/Core/EventListener.h>

class GUI;
class FloppyCtrl;
class StorageCtrl;
class Serial;

class Status : public Window
{
private:
	enum class LED {
		HIDDEN, IDLE, ACTIVE, ERROR
	};
	struct {
		Rocket::Core::Element *power, *floppy_a, *floppy_b, *hdd;
		Rocket::Core::Element *net;
	} m_indicators;

	struct {
		LED power, floppy_a, floppy_b, hdd, net;
	} m_status;

	Machine *m_machine;
	const FloppyCtrl *m_floppy;
	const StorageCtrl *m_hdd;
	const Serial *m_serial;

public:
	Status(GUI * _gui, Machine *_machine);
	~Status();

	void update();
	void config_changed();

	void ProcessEvent(Rocket::Core::Event & event);
};

#endif
