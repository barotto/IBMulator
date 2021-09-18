/*
 * Copyright (C) 2021  Marco Bortolin
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

#ifndef IBMULATOR_GUI_STATESAVE_H
#define IBMULATOR_GUI_STATESAVE_H

#include "state_dialog.h"


class StateSave : public StateDialog
{
private:

	static event_map_t ms_evt_map;

public:

	StateSave(GUI * _gui) : StateDialog(_gui, "state_save.rml") {}
	virtual ~StateSave() {}

	virtual void update();

	void action_on_record(std::string _rec_name);

	event_map_t & get_event_map() { return StateSave::ms_evt_map; }

private:

	void on_entry(Rml::Event &);
};



#endif
