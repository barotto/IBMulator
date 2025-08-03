/*
 * Copyright (C) 2017-2025  Marco Bortolin
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


class DebugTools final : public Window
{
public:
	class DebugWindow : public Window
	{
		friend class DebugTools;

	protected:
		bool m_enabled;
		Rml::Element *m_button;

	public:
		void show() override;
		void enable(bool _value = true);
		void toggle();
		void on_cancel(Rml::Event &) override;

		DebugWindow(GUI * _gui, const char *_rml, Rml::Element *_button);
		virtual ~DebugWindow() {}
	};

private:
	static event_map_t ms_evt_map;
	Machine *m_machine = nullptr;
	Mixer *m_mixer = nullptr;
	SysDebugger *m_debuggerw = nullptr;

	void on_stats(Rml::Event &);
	void on_debugger(Rml::Event &);
	void on_devices(Rml::Event &);
	void on_mixer(Rml::Event &);
	void on_rmlui(Rml::Event &);
	void on_close(Rml::Event &);

public:
	DebugTools(GUI * _gui, Machine *_machine, Mixer *_mixer);

	void show() override;
	void hide() override;
	void config_changed(bool) override;

	void show_message(const char* _mex);

protected:
	void create() override;
	event_map_t & get_event_map() override { return DebugTools::ms_evt_map; }
};


#endif
