/*
 * Copyright (C) 2017  Marco Bortolin
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

#ifndef IBMULATOR_GUI_DEBUGTOOLS_H
#define IBMULATOR_GUI_DEBUGTOOLS_H

#include "window.h"

class SysDebugger;
class Stats;
class DevStatus;


class DebugTools : public Window
{
public:
	class DebugWindow : public Window
	{
		friend class DebugTools;

	protected:
		bool m_enabled;
		RC::Element *m_button;

	public:
		void show();
		void enable(bool _value = true);
		void toggle();
		void on_close(RC::Event &);

		DebugWindow(GUI * _gui, const char *_rml, RC::Element *_button);
	};

private:
	static event_map_t ms_evt_map;
	Machine *m_machine;
	DebugWindow *m_stats, *m_debugger, *m_devices;

	void on_stats(RC::Event &);
	void on_debugger(RC::Event &);
	void on_devices(RC::Event &);

public:
	DebugTools(GUI * _gui, Machine *_machine, Mixer *_mixer);
	~DebugTools();

	void config_changed();
	void show();
	void hide();
	void update();
	void show_message(const char* _mex);

	event_map_t & get_event_map() { return DebugTools::ms_evt_map; }
};



#endif
