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

#include "ibmulator.h"
#include "gui.h"
#include "machine.h"
#include "program.h"
#include "normal_interface.h"
#include "utils.h"
#include <sys/stat.h>

#include <Rocket/Core.h>


event_map_t NormalInterface::ms_evt_map = {
	GUI_EVT( "power",     "click", Interface::on_power ),
	GUI_EVT( "pause",     "click", NormalInterface::on_pause ),
	GUI_EVT( "save",      "click", NormalInterface::on_save ),
	GUI_EVT( "restore",   "click", NormalInterface::on_restore ),
	GUI_EVT( "exit",      "click", NormalInterface::on_exit ),
	GUI_EVT( "fdd_select","click", Interface::on_fdd_select ),
	GUI_EVT( "fdd_eject", "click", Interface::on_fdd_eject ),
	GUI_EVT( "fdd_mount", "click", Interface::on_fdd_mount )
};

NormalInterface::NormalInterface(Machine *_machine, GUI * _gui)
:
Interface(_machine, _gui, "normal_interface.rml")
{
	ASSERT(m_wnd);

	m_wnd->AddEventListener("click", this, false);

	m_sysunit = get_element("sysunit");
	m_sysunit->SetClass("disk", m_floppy_present);
	m_btn_pause = get_element("pause");
	m_led_pause = false;
	m_gui_mode = g_program.config().get_enum(GUI_SECTION, GUI_MODE, GUI::ms_gui_modes);
}

NormalInterface::~NormalInterface()
{
}

void NormalInterface::update_size(uint _width, uint _height)
{
	Interface::update_size(_width, _height);

	if(m_gui_mode == GUI_MODE_NORMAL) {
		char buf[10];
		snprintf(buf, 10, "%upx", _width);
		m_sysunit->SetProperty("width", buf);
		snprintf(buf, 10, "%upx", _height);
		m_sysunit->SetProperty("height", buf);
	}
}

void NormalInterface::update()
{
	Interface::update();

	if(m_floppy_present) {
		m_sysunit->SetClass("disk", true);
	} else {
		m_sysunit->SetClass("disk", false);
	}

	if(m_machine->is_paused() && m_led_pause==false) {
		m_led_pause = true;
		m_btn_pause->SetClass("resume", true);
	} else if(!m_machine->is_paused() && m_led_pause==true){
		m_led_pause = false;
		m_btn_pause->SetClass("resume", false);
	}
}

void NormalInterface::on_pause(RC::Event &)
{
	if(m_machine->is_paused()) {
		m_machine->cmd_resume();
	} else {
		m_machine->cmd_pause();
	}
}

void NormalInterface::on_save(RC::Event &)
{
	//TODO file select window to choose the destination
	g_program.save_state("", [this]() {
		show_message("State saved");
	}, nullptr);
}

void NormalInterface::on_restore(RC::Event &)
{
	//TODO file select window to choose the source
	g_program.restore_state("", [this]() {
		show_message("State restored");
	}, nullptr);
}

void NormalInterface::on_exit(RC::Event &)
{
	g_program.stop();
}


