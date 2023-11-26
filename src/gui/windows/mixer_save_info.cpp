/*
 * Copyright (C) 2023  Marco Bortolin
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
#include "mixer_save_info.h"
#include "filesys.h"
#include "gui.h"
#include "program.h"

event_map_t MixerSaveInfo::ms_evt_map = {
	GUI_EVT( "cancel", "click", MixerSaveInfo::on_cancel ),
	GUI_EVT( "close", "click", MixerSaveInfo::on_cancel ),
	GUI_EVT( "save", "click", MixerSaveInfo::on_save ),
	GUI_EVT( "profile_name", "keydown", MixerSaveInfo::on_keydown ),
	GUI_EVT( "*", "keydown", Window::on_keydown )
};

MixerSaveInfo::MixerSaveInfo(GUI *_gui)
:
Window(_gui, "mixer_save_info.rml")
{
}

void MixerSaveInfo::create()
{
	values.name = g_program.config().get_string(MIXER_SECTION, MIXER_PROFILE);
	//TODO let user pick the dir? maybe not.
	values.directory = g_program.config().get_cfg_home();

	Window::create();
	m_el.name = dynamic_cast<Rml::ElementFormControl*>(get_element("profile_name"));
}

void MixerSaveInfo::setup_data_bindings()
{
	Rml::DataModelConstructor constructor = m_gui->create_data_model("MixerProfileInfo");

	constructor.Bind("name", &values.name);
	constructor.Bind("directory", &values.directory);
}

void MixerSaveInfo::show()
{
	Window::show();
	m_el.name->Focus();
}

void MixerSaveInfo::close()
{
	m_gui->remove_data_model("MixerProfileInfo");
	Window::close();
}

void MixerSaveInfo::on_save(Rml::Event &)
{
	if(m_save_callbk) {
		m_save_callbk();
	}
	Window::hide();
}

void MixerSaveInfo::on_cancel(Rml::Event &_ev)
{
	if(m_cancel_callbk) {
		m_cancel_callbk();
	}
	Window::on_cancel(_ev);
}

void MixerSaveInfo::on_keydown(Rml::Event &_ev)
{
	switch(get_key_identifier(_ev)) {
		case Rml::Input::KeyIdentifier::KI_RETURN:
		case Rml::Input::KeyIdentifier::KI_NUMPADENTER:
			if(_ev.GetTargetElement() == m_el.name) {
				on_save(_ev);
				break;
			}
			[[fallthrough]];
		default:
			Window::on_keydown(_ev);
			return;
	}
	_ev.StopImmediatePropagation();
}


