/*
 * Copyright (C) 2015-2025  Marco Bortolin
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
#include "gui.h"
#include "stats.h"
#include "sysdebugger286.h"
#include "sysdebugger386.h"
#include "devstatus.h"
#include "mixerstate.h"
#include "hardware/cpu.h"
#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>

event_map_t DebugTools::ms_evt_map = {
	GUI_EVT( "stats",    "click", DebugTools::on_stats ),
	GUI_EVT( "debugger", "click", DebugTools::on_debugger ),
	GUI_EVT( "devices",  "click", DebugTools::on_devices ),
	GUI_EVT( "mixer",    "click", DebugTools::on_mixer ),
	GUI_EVT( "rmlui",    "click", DebugTools::on_rmlui ),
	GUI_EVT( "close",    "click", DebugTools::on_close )
};

DebugTools::DebugTools(GUI *_gui, Machine *_machine, Mixer *_mixer)
:
Window(_gui, "debugtools.rml"),
m_machine(_machine),
m_mixer(_mixer)
{
}

void DebugTools::create()
{
	Window::create();

	if(CPU_FAMILY >= CPU_386) {
		m_debuggerw = new_child_window<SysDebugger386>(m_machine, get_element("debugger"));
	} else {
		m_debuggerw = new_child_window<SysDebugger286>(m_machine, get_element("debugger"));
	}
	
	new_child_window<Stats>(m_machine, m_mixer, get_element("stats"));
	new_child_window<DevStatus>(get_element("devices"), m_machine);
	new_child_window<MixerState>(get_element("mixer"), m_mixer);
}

void DebugTools::config_changed(bool _startup)
{
	bool dbg_is_286 = bool(dynamic_cast<SysDebugger286*>(m_debuggerw));
	if((dbg_is_286 && CPU_FAMILY>CPU_286) || (!dbg_is_286 && CPU_FAMILY==CPU_286)) {
		bool enabled = m_debuggerw->m_enabled;
		close_child_windows<SysDebugger>();
		if(CPU_FAMILY >= CPU_386) {
			m_debuggerw = new_child_window<SysDebugger386>(m_machine, get_element("debugger"));
		} else {
			m_debuggerw = new_child_window<SysDebugger286>(m_machine, get_element("debugger"));
		}
		m_debuggerw->enable(enabled);
	}

	Window::config_changed(_startup);
}

void DebugTools::on_stats(Rml::Event &)
{
	get_child_windows<Stats>()[0]->toggle();
}

void DebugTools::on_debugger(Rml::Event &)
{
	m_debuggerw->toggle();
}

void DebugTools::on_devices(Rml::Event &)
{
	get_child_windows<DevStatus>()[0]->toggle();
}

void DebugTools::on_mixer(Rml::Event &)
{
	get_child_windows<MixerState>()[0]->toggle();
}

void DebugTools::on_rmlui(Rml::Event &)
{
	Rml::Debugger::SetVisible(true);
}

void DebugTools::on_close(Rml::Event &)
{
	m_gui->toggle_dbg_windows();
}

void DebugTools::hide()
{
	Rml::Debugger::SetVisible(false);

	Window::hide();
}

void DebugTools::show()
{
	show_children();

	Window::show();
}

void DebugTools::show_message(const char* _mex)
{
	m_debuggerw->show_message(_mex);
}

/*******************************************************************************
 * DebugTools::DebugWindow
 */

DebugTools::DebugWindow::DebugWindow(GUI * _gui, const char *_rml, Rml::Element *_button)
:
Window(_gui, _rml),
m_enabled(false),
m_button(_button)
{
}

void DebugTools::DebugWindow::show()
{
	if(m_enabled) {
		Window::show();
	}
}

void DebugTools::DebugWindow::on_cancel(Rml::Event &)
{
	enable(false);
}

void DebugTools::DebugWindow::toggle()
{
	enable(!m_enabled);
	if(m_enabled) {
		Window::show();
	} else {
		Window::hide();
	}
}

void DebugTools::DebugWindow::enable(bool _value)
{
	m_enabled = _value;
	if(m_enabled) {
		m_button->SetClass("on", true);
		Window::show();
	} else {
		m_button->SetClass("on", false);
		Window::hide();
	}
}
