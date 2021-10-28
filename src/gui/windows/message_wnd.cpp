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
#include "message_wnd.h"


event_map_t MessageWnd::ms_evt_map = {
	GUI_EVT( "action1", "click",   MessageWnd::on_action  ),
	GUI_EVT( "action2", "click",   MessageWnd::on_action  ),
	GUI_EVT( "close",   "click",   MessageWnd::on_action  ),
	GUI_EVT( "*",       "keydown", MessageWnd::on_keydown )
};

MessageWnd::MessageWnd(GUI * _gui)
:
Window(_gui, "message_wnd.rml")
{
}

MessageWnd::~MessageWnd()
{
}

void MessageWnd::create()
{
	Window::create();
	get_element("resize")->SetClass("d-none", true);
}

void MessageWnd::set_title(const std::string &_title)
{
	get_element("title")->SetInnerRML(_title);
}

void MessageWnd::set_message(const std::string &_mex)
{
	get_element("message")->SetInnerRML(_mex);
}

void MessageWnd::set_type(Type _type)
{
	m_type = _type;
	switch(m_type) {
		case Type::MSGW_OK:
			get_element("action1")->SetInnerRML("Ok");
			get_element("action2")->SetClass("d-none", true);
			break;
		case Type::MSGW_YES_NO:
			get_element("action1")->SetInnerRML("Yes");
			get_element("action2")->SetInnerRML("No");
			get_element("action2")->SetClass("d-none", false);
			break;
	}
}

void MessageWnd::on_action(Rml::Event &_ev)
{
	auto el = _ev.GetTargetElement();
	auto func = m_action1_clbk;
	switch(m_type) {
		case Type::MSGW_OK:
			break;
		case Type::MSGW_YES_NO:
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

void MessageWnd::on_keydown(Rml::Event &_ev)
{
	switch(get_key_identifier(_ev)) {
		case Rml::Input::KeyIdentifier::KI_ESCAPE:
			if(m_type == Type::MSGW_YES_NO && m_action2_clbk) {
				m_action2_clbk();
			}
			hide();
			break;
		default:
			Window::on_keydown(_ev);
			return;
	}
}

