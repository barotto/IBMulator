/*
 * Copyright (C) 2015-2020  Marco Bortolin
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
#include "hardware/cpu.h"

#include <Rocket/Core.h>
#include <Rocket/Controls.h>

event_map_t DebugTools::ms_evt_map = {
	GUI_EVT( "stats",    "click", DebugTools::on_stats ),
	GUI_EVT( "debugger", "click", DebugTools::on_debugger ),
	GUI_EVT( "devices",  "click", DebugTools::on_devices ),
	GUI_EVT( "close",    "click", DebugTools::on_close )
};

DebugTools::DebugTools(GUI *_gui, Machine *_machine, Mixer *_mixer)
:
Window(_gui, "debugtools.rml"),
m_machine(_machine)
{
	assert(m_wnd);
	m_debugger = nullptr; // will be created in the config_changed
	m_stats = new Stats(_machine, _gui, _mixer, get_element("stats"));
	m_stats->enable();
	m_devices = new DevStatus(_gui, get_element("devices"), _machine);
}

DebugTools::~DebugTools()
{
	if(m_debugger) {
		m_debugger->close();
		delete m_debugger;
	}
	if(m_devices) {
		m_devices->close();
		delete m_devices;
	}
	if(m_stats) {
		m_stats->close();
		delete m_stats;
	}
}

void DebugTools::config_changed()
{
	if(m_debugger) {
		bool dbg_is_286 = bool(dynamic_cast<SysDebugger286*>(m_debugger));
		if((dbg_is_286 && CPU_FAMILY>CPU_286) || (!dbg_is_286 && CPU_FAMILY==CPU_286)) {
			bool enabled = m_debugger->m_enabled;
			m_debugger->close();
			delete m_debugger;
			if(CPU_FAMILY >= CPU_386) {
				m_debugger = new SysDebugger386(m_gui, m_machine, get_element("debugger"));
			} else {
				m_debugger = new SysDebugger286(m_gui, m_machine, get_element("debugger"));
			}
			m_debugger->enable(enabled);
			m_debugger->show();
		}
	} else {
		if(CPU_FAMILY >= CPU_386) {
			m_debugger = new SysDebugger386(m_gui, m_machine, get_element("debugger"));
		} else {
			m_debugger = new SysDebugger286(m_gui, m_machine, get_element("debugger"));
		}
	}
	m_devices->config_changed();
	m_stats->config_changed();
}

void DebugTools::on_stats(RC::Event &)
{
	m_stats->toggle();
}

void DebugTools::on_debugger(RC::Event &)
{
	m_debugger->toggle();
}

void DebugTools::on_devices(RC::Event &)
{
	m_devices->toggle();
}

void DebugTools::on_close(RC::Event &)
{
	m_gui->toggle_dbg_windows();
}

void DebugTools::hide()
{
	m_debugger->hide();
	m_stats->hide();
	m_devices->hide();
	Window::hide();
}

void DebugTools::show()
{
	m_debugger->show();
	m_stats->show();
	m_devices->show();
	Window::show();
}

void DebugTools::update()
{
	// quick hack to limit the update freq of the dbg windows
	static bool dbgupdate = true;
	dbgupdate = !dbgupdate;
	if(dbgupdate) {
		m_debugger->update();
		m_devices->update();
	}
	m_stats->update();
}

void DebugTools::show_message(const char* _mex)
{
	if(m_debugger) {
		((SysDebugger*)m_debugger)->show_message(_mex);
	}
}

/*******************************************************************************
 * DebugTools::DebugWindow
 */

DebugTools::DebugWindow::DebugWindow(GUI * _gui, const char *_rml, RC::Element *_button)
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

void DebugTools::DebugWindow::on_close(RC::Event &)
{
	enable(false);
	Window::hide();
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
	} else {
		m_button->SetClass("on", false);
	}
}
