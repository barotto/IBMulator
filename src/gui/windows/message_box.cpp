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
#include "message_box.h"


event_map_t MessageBox::ms_evt_map = {
	GUI_EVT( "action1", "click",   MessageBox::on_action  ),
	GUI_EVT( "action2", "click",   MessageBox::on_action  ),
	GUI_EVT( "close",   "click",   MessageBox::on_action  ),
	GUI_EVT( "*",       "keydown", MessageBox::on_keydown )
};

MessageBox::MessageBox(GUI * _gui)
:
Window(_gui, "message_box.rml")
{
}

MessageBox::~MessageBox()
{
}

void MessageBox::create()
{
	Window::create();
	get_element("resize")->SetClass("d-none", true);
}

void MessageBox::set_title(const std::string &_title)
{
	get_element("title")->SetInnerRML(_title);
}

void MessageBox::set_message(const std::string &_mex)
{
	get_element("message")->SetInnerRML(_mex);
}

void MessageBox::set_type(Type _type)
{
	m_type = _type;
	switch(m_type) {
		case Type::MSGB_OK:
			get_element("action1")->SetInnerRML("Ok");
			get_element("action2")->SetClass("d-none", true);
			break;
		case Type::MSGB_YES_NO:
			get_element("action1")->SetInnerRML("Yes");
			get_element("action2")->SetInnerRML("No");
			get_element("action2")->SetClass("d-none", false);
			break;
	}
}

void MessageBox::on_action(Rml::Event &_ev)
{
	auto el = _ev.GetTargetElement();
	auto func = m_action1_clbk;
	switch(m_type) {
		case Type::MSGB_OK:
			break;
		case Type::MSGB_YES_NO:
			if(el->GetId() == "close" || el->GetId() == "action2") {
				func = m_action2_clbk;
			}
			break;
	} 
	if(func) {
		func();
	}
	hide();
}

void MessageBox::on_keydown(Rml::Event &_ev)
{
	switch(get_key_identifier(_ev)) {
		case Rml::Input::KeyIdentifier::KI_ESCAPE:
			if(m_type == Type::MSGB_YES_NO && m_action2_clbk) {
				m_action2_clbk();
			}
			hide();
			break;
		default:
			Window::on_keydown(_ev);
			return;
	}
}

