/*
 * Copyright (C) 2015, 2016  Marco Bortolin
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

#include <Rocket/Core.h>


event_map_t Window::ms_event_map = {};

Window::Window(GUI * _gui, const char *_rml)
: m_gui(_gui)
{
	m_wnd = _gui->load_document(_rml);
	if(!m_wnd) {
		PERRF(LOG_GUI,"unable to load the %s interface\n", _rml);
		return;
	}
}

Window::~Window()
{
	if(m_wnd) {
		m_wnd->RemoveReference();
	}
}

void Window::init_events()
{
	event_map_t & evtmap = get_event_map();
	for(auto i : evtmap) {
		Rocket::Core::Element * el = get_element(i.first.first);
		if(el) {
			el->AddEventListener(i.first.second,this,false);
		}
	}
}

void Window::show()
{
	ASSERT(m_wnd);

	m_wnd->Show();
}

void Window::hide()
{
	ASSERT(m_wnd);

	m_wnd->Hide();
}

bool Window::is_visible()
{
	ASSERT(m_wnd);

	return m_wnd->IsVisible();
}

void Window::update()
{
	ASSERT(m_wnd);
}

RC::Element * Window::get_element(const RC::String &_id)
{
	ASSERT(m_wnd);
	RC::Element * el = m_wnd->GetElementById(_id);
	if(!el) {
		PERRF(LOG_GUI, "element %s not found!\n", _id.CString());
	}
	return el;
}


void Window::ProcessEvent(RC::Event &_event)
{
	RC::Element *el = _event.GetTargetElement();
	const RC::String &type = _event.GetType();
	event_map_t & evtmap = get_event_map();
	auto hit = evtmap.find( event_map_key_t(el->GetId(),type) );
	event_handler_t fn = nullptr;
	if(hit != evtmap.end()) {
		fn = hit->second;
	} else {
		RC::Element *parent = el->GetParentNode();
		while(parent) {
			hit = evtmap.find(event_map_key_t(
				parent->GetId(),
				type
			));
			if(hit != evtmap.end()) {
				fn = hit->second;
				break;
			}
			parent = parent->GetParentNode();
		}
	}
	if(fn) {
		(this->*fn)(_event);
	}
}
