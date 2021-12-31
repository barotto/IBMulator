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

#include "ibmulator.h"
#include "state_save_info.h"


event_map_t StateSaveInfo::ms_evt_map = {
	GUI_EVT( "cancel", "click",   StateSaveInfo::on_cancel  ),
	GUI_EVT( "close",  "click",   StateSaveInfo::on_cancel  ),
	GUI_EVT( "save",   "click",   StateSaveInfo::on_save    ),
	GUI_EVT( "desc",   "keydown", StateSaveInfo::on_keydown ),
	GUI_EVT( "*",      "keydown", Window::on_keydown        )
};

StateSaveInfo::StateSaveInfo(GUI * _gui)
:
Window(_gui, "state_save_info.rml")
{
}

StateSaveInfo::~StateSaveInfo()
{
}

void StateSaveInfo::show()
{
	m_desc_el->SetValue(m_state_info.user_desc);
	Window::show();
	m_desc_el->Focus();
}

void StateSaveInfo::create()
{
	Window::create();
	m_desc_el = dynamic_cast<Rml::ElementFormControlInput*>(get_element("desc"));
}

void StateSaveInfo::set_state(StateRecord::Info _info)
{
	m_state_info = _info;
	if(!_info.name.empty()) {
		get_element("name")->SetInnerRML(std::string(" for slot ") + _info.name);
	} else {
		get_element("name")->SetInnerRML("");
	}
}

void StateSaveInfo::on_save(Rml::Event &)
{
	m_state_info.user_desc = m_desc_el->GetValue();
	if(m_save_callbk) {
		m_save_callbk(m_state_info);
	}
	hide();
}

void StateSaveInfo::on_cancel(Rml::Event &_ev)
{
	if(m_cancel_callbk) {
		m_cancel_callbk();
	}
	Window::on_cancel(_ev);
}

void StateSaveInfo::on_keydown(Rml::Event &_ev)
{
	switch(get_key_identifier(_ev)) {
		case Rml::Input::KeyIdentifier::KI_RETURN:
		case Rml::Input::KeyIdentifier::KI_NUMPADENTER:
			if(_ev.GetTargetElement() == m_desc_el) {
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


