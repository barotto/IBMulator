/*
 * Copyright (C) 2021-2025  Marco Bortolin
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
#include "gui.h"

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

void MessageWnd::create()
{
	Window::create();
	get_element("resize")->SetClass("d-none", true);
}

void MessageWnd::show()
{
	Window::show();
	get_element("message")->SetInnerRML(str_to_html(m_message));
	m_gui->tts().enqueue(m_message);
	get_element("action1")->Focus();
}

void MessageWnd::set_title(const std::string &_title)
{
	get_element("title")->SetInnerRML(_title);
}

void MessageWnd::set_message(const std::string &_mex)
{
	m_message = _mex;
}

Rml::ElementPtr MessageWnd::create_button(std::string _label, std::string _id, Rml::ElementDocument *_doc) const
{
	// <button id="_id" class="romshell"><span>_label</span></button>
	Rml::ElementPtr button = _doc->CreateElement("button");
	button->SetClassNames("romshell");
	button->SetId(_id);
	button->SetInnerRML("<span>" + _label + "</span>");
	button->SetAttribute("aria-label", _label);
	return button;
}

void MessageWnd::set_type(Type _type)
{
	m_type = _type;
	auto buttons = get_element("buttons");
	buttons->SetInnerRML("");
	switch(m_type) {
		case Type::MSGW_OK: {
			buttons->AppendChild(create_button("Ok", "action1", m_wnd));
			break;
		}
		case Type::MSGW_YES_NO:
			buttons->AppendChild(create_button("Yes", "action1", m_wnd));
			buttons->AppendChild(create_button("No", "action2", m_wnd));
			break;
	}
}

void MessageWnd::on_action(Rml::Event &_ev)
{
	auto el = _ev.GetCurrentElement();
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

bool MessageWnd::would_handle(Rml::Input::KeyIdentifier _key, int _mod)
{
	return (
		( _mod == 0 && _key == Rml::Input::KeyIdentifier::KI_ESCAPE ) ||
		( _mod == 0 && _key == Rml::Input::KeyIdentifier::KI_Y ) ||
		( _mod == 0 && _key == Rml::Input::KeyIdentifier::KI_N ) ||
		Window::would_handle(_key, _mod)
	);
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
		case Rml::Input::KeyIdentifier::KI_Y:
			if(m_type == Type::MSGW_YES_NO) {
				if(m_action1_clbk) {
					m_action1_clbk();
				}
				hide();
			}
			break;
		case Rml::Input::KeyIdentifier::KI_N:
			if(m_type == Type::MSGW_YES_NO) {
				if(m_action2_clbk) {
					m_action2_clbk();
				}
				hide();
			}
			break;
		default:
			Window::on_keydown(_ev);
			return;
	}
	_ev.StopImmediatePropagation();
}

