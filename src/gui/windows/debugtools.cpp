/*
 * Copyright (C) 2015-2024  Marco Bortolin
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

DebugTools::~DebugTools()
{
}

void DebugTools::close()
{
	if(m_debuggerw) {
		m_debuggerw->close();
	}
	if(m_devicesw) {
		m_devicesw->close();
	}
	if(m_statsw) {
		m_statsw->close();
	}
	if(m_mixerw) {
		m_mixerw->close();
	}
	Window::close();
}

void DebugTools::create()
{
	Window::create();

	if(m_debuggerw) {
		bool dbg_is_286 = bool(dynamic_cast<SysDebugger286*>(m_debuggerw.get()));
		if((dbg_is_286 && CPU_FAMILY>CPU_286) || (!dbg_is_286 && CPU_FAMILY==CPU_286)) {
			bool enabled = m_debuggerw->m_enabled;
			m_debuggerw->close();
			m_debuggerw.reset(nullptr);
			if(CPU_FAMILY >= CPU_386) {
				m_debuggerw = std::make_unique<SysDebugger386>(m_gui, m_machine, get_element("debugger"));
			} else {
				m_debuggerw = std::make_unique<SysDebugger286>(m_gui, m_machine, get_element("debugger"));
			}
			m_debuggerw->create();
			m_debuggerw->enable(enabled);
			m_debuggerw->show();
		}
	} else {
		if(CPU_FAMILY >= CPU_386) {
			m_debuggerw = std::make_unique<SysDebugger386>(m_gui, m_machine, get_element("debugger"));
		} else {
			m_debuggerw = std::make_unique<SysDebugger286>(m_gui, m_machine, get_element("debugger"));
		}
		m_debuggerw->create();
	}
	if(!m_statsw) {
		m_statsw = std::make_unique<Stats>(m_machine, m_gui, m_mixer, get_element("stats"));
		m_statsw->create();
	}
	if(!m_devicesw) {
		m_devicesw = std::make_unique<DevStatus>(m_gui, get_element("devices"), m_machine);
		m_devicesw->create();
	}
	if(!m_mixerw) {
		m_mixerw = std::make_unique<MixerState>(m_gui, get_element("mixer"), m_mixer);
		m_mixerw->create();
	}
}

void DebugTools::config_changed(bool _startup)
{
	create();

	m_debuggerw->config_changed(_startup);
	m_devicesw->config_changed(_startup);
	m_statsw->config_changed(_startup);
	m_mixerw->config_changed(_startup);
}

void DebugTools::on_stats(Rml::Event &)
{
	m_statsw->toggle();
}

void DebugTools::on_debugger(Rml::Event &)
{
	m_debuggerw->toggle();
}

void DebugTools::on_devices(Rml::Event &)
{
	m_devicesw->toggle();
}

void DebugTools::on_mixer(Rml::Event &)
{
	m_mixerw->toggle();
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
	m_debuggerw->hide();
	m_statsw->hide();
	m_devicesw->hide();
	m_mixerw->hide();
	Rml::Debugger::SetVisible(false);
	Window::hide();
}

void DebugTools::show()
{
	m_debuggerw->show();
	m_statsw->show();
	m_devicesw->show();
	m_mixerw->show();
	Window::show();
}

void DebugTools::update()
{
	// quick hack to limit the update freq of the dbg windows
	static bool dbgupdate = true;
	dbgupdate = !dbgupdate;
	if(dbgupdate) {
		m_debuggerw->update();
		m_devicesw->update();
	}
	m_statsw->update();
	m_mixerw->update();
}

void DebugTools::show_message(const char* _mex)
{
	if(m_debuggerw) {
		auto debugger = dynamic_cast<SysDebugger*>(m_debuggerw.get());
		if(!debugger) {
			assert(false);
			return;
		}
		debugger->show_message(_mex);
	}
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
