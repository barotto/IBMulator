/*
 * Copyright (C) 2015-2021  Marco Bortolin
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

#include <RmlUi/Core.h>


event_map_t Window::ms_event_map = {};

Window::Window(GUI * _gui, const char *_rml)
: m_gui(_gui),
  m_rml_docfile(_rml),
  m_wnd(nullptr),
  m_evts_added(false)
{
}

void Window::add_events()
{
	if(m_evts_added) {
		return;
	}
	assert(m_wnd);
	event_map_t & evtmap = get_event_map();
	std::set<std::string> evtnames;
	for(auto i : evtmap) {
		evtnames.insert(i.first.second);
	}
	for(auto e : evtnames) {
		m_wnd->AddEventListener(e, this);
	}
	m_evts_added = true;
}

void Window::add_listener(const std::string &_element, const std::string &_event, Rml::EventListener *_listener)
{
	Rml::Element *el = m_wnd->GetElementById(_element);
	if(el) {
		el->AddEventListener(_event, _listener);
	}
}

void Window::remove_events()
{
	assert(m_wnd);
	event_map_t & evtmap = get_event_map();
	std::set<std::string> evtnames;
	for(auto i : evtmap) {
		evtnames.insert(i.first.second);
	}
	for(auto e : evtnames) {
		m_wnd->RemoveEventListener(e, this);
	}
	m_evts_added = false;
}

void Window::create()
{
	// it turns out with RmlUi you can't load a document in the constructor, bummer
	if(m_rml_docfile.empty()) {
		throw std::exception();
	}
	if(!m_wnd) {
		m_wnd = m_gui->load_document(m_rml_docfile);
		if(!m_wnd) {
			PERRF(LOG_GUI, "Cannot load the '%s' document file\n", m_rml_docfile.c_str());
			m_rml_docfile.clear();
			throw std::exception();
		}
	}
}

void Window::show()
{
	create();
	add_events();
	m_wnd->Show();
}

void Window::hide()
{
	if(m_wnd) {
		m_wnd->Hide();
	}
}

void Window::close()
{
	if(m_wnd) {
		m_wnd->Close();
		remove_events();
	}
}

bool Window::is_visible()
{
	if(m_wnd) {
		return m_wnd->IsVisible();
	}
	return false;
}

void Window::update()
{
}

Rml::Element * Window::get_element(const std::string &_id)
{
	assert(m_wnd);
	Rml::Element *el = m_wnd->GetElementById(_id);
	if(!el) {
		PERRF(LOG_GUI, "element %s not found!\n", _id.c_str());
	}
	return el;
}

void Window::ProcessEvent(Rml::Event &_event)
{
	Rml::Element *el = _event.GetTargetElement();
	const std::string &type = _event.GetType();
	event_map_t & evtmap = get_event_map();
	auto hit = evtmap.find( event_map_key_t(el->GetId(),type) );
	event_handler_t fn = nullptr;
	if(hit != evtmap.end()) {
		fn = hit->second;
	} else {
		Rml::Element *parent = el->GetParentNode();
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
