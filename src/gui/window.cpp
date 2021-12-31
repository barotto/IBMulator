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
  m_rml_docfile(_rml)
{
}

void Window::add_events()
{
	if(m_evts_added) {
		return;
	}
	assert(m_wnd);
	event_map_t & evtmap = get_event_map();
	for(auto evt : evtmap) {
		// model is alway bubbling
		if(evt.first.first == "*") {
			m_wnd->AddEventListener(evt.first.second, this);
		} else {
			try {
				Rml::Element *el = get_element(evt.first.first);
				el->AddEventListener(evt.first.second, this);
			} catch(std::exception &) {
				m_wnd->AddEventListener(evt.first.second, this);
			}
		}
	}
	m_evts_added = true;
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
	if(!m_wnd) {
		create();
	}
	add_events();
	if(m_wnd && !m_wnd->IsVisible()) {
		if(m_modal != Rml::ModalFlag::None) {
			m_wnd->SetClass("modal", true);
		}
		m_wnd->Show(m_modal);
	}
}

void Window::hide()
{
	if(m_wnd && m_wnd->IsVisible()) {
		m_wnd->Hide();
	}
}

void Window::close()
{
	if(m_wnd) {
		hide();
		m_wnd->Close();
		m_gui->unload_document(m_wnd);
		m_wnd = nullptr;
		m_evts_added = false;
	}
}

void Window::focus()
{
	if(m_wnd) {
		m_wnd->Focus();
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
		PDEBUGF(LOG_V0, LOG_GUI, "element %s not found!\n", _id.c_str());
		throw std::exception();
	}
	return el;
}

void Window::ProcessEvent(Rml::Event &_event)
{
	Rml::Element *el = _event.GetTargetElement();
	std::string el_str = el->GetId();
	std::string fn_el_str = el_str;
	const std::string &type = _event.GetType();
	event_map_t & evtmap = get_event_map();
	auto hit = evtmap.find( event_map_key_t(el->GetId(),type) );
	PDEBUGF(LOG_V1, LOG_GUI, "RmlUi Event '%s' on '%s'", type.c_str(), el_str.c_str());
	event_handler_t fn = nullptr;
	if(hit != evtmap.end()) {
		fn = hit->second.handler;
		if(hit->second.target) {
			_event.StopImmediatePropagation();
		}
	} else {
		Rml::Element *parent = el->GetParentNode();
		while(parent) {
			hit = evtmap.find(event_map_key_t(
				parent->GetId(),
				type
			));
			if(hit != evtmap.end() && !hit->second.target) {
				fn_el_str = parent->GetId();
				fn = hit->second.handler;
				break;
			}
			parent = parent->GetParentNode();
		}
	}
	if(!fn) {
		hit = evtmap.find(event_map_key_t("*", type));
		fn_el_str = "*";
		if(hit != evtmap.end()) {
			fn = hit->second.handler;
		}
	}
	if(fn) {
		PDEBUGF(LOG_V1, LOG_GUI, " ('%s')\n", fn_el_str.c_str());
		(this->*fn)(_event);
	} else {
		PDEBUGF(LOG_V1, LOG_GUI, "\n");
	}
}

Rml::Input::KeyIdentifier Window::get_key_identifier(Rml::Event &_ev)
{
	return (Rml::Input::KeyIdentifier) _ev.GetParameter<int>("key_identifier",
				Rml::Input::KeyIdentifier::KI_UNKNOWN);
}

std::string Window::get_form_input_value(Rml::Event &_ev)
{
	Rml::Element *el = _ev.GetTargetElement();
	do {
		auto input = dynamic_cast<Rml::ElementFormControlInput*>(el);
		if(input) {
			return input->GetValue();
		}
		el = el->GetParentNode();
	} while(el);

	return "";
}

Rml::Element * Window::disable(Rml::Element *_el)
{
	assert(_el);
	_el->SetClass("disabled", true);
	return _el;
}

Rml::Element * Window::enable(Rml::Element *_el)
{
	assert(_el);
	_el->SetClass("disabled", false);
	return _el;
}

Rml::Element * Window::set_disabled(Rml::Element *_el, bool _disabled)
{
	assert(_el);
	_el->SetClass("disabled", _disabled);
	return _el;
}

bool Window::is_disabled(Rml::Element *_el)
{
	assert(_el);
	return (_el->IsClassSet("disabled"));
}

Rml::Element * Window::Window::set_active(Rml::Element *_el, bool _active)
{
	assert(_el);
	_el->SetClass("active", _active);
	return _el;
}

bool Window::is_active(Rml::Element *_el)
{
	assert(_el);
	return (_el->IsClassSet("active"));
}

std::pair<Rml::Element*,int> Window::get_first_visible_element(Rml::Element *_elem_container, Rml::Element *_outer_container, int _starting_at)
{
	auto container_top = _outer_container->GetAbsoluteTop();
	for(int idx = _starting_at; idx < _elem_container->GetNumChildren(); idx++) {
		auto element = _elem_container->GetChild(idx);
		auto element_top = element->GetAbsoluteTop();
		auto element_relative_top = element_top - container_top;
		if(element_relative_top >= 0) {
			return std::make_pair(element,idx);
		}
	}
	return std::make_pair(nullptr,-1);
}

std::pair<Rml::Element*,int> Window::get_last_visible_element(Rml::Element *_elem_container, Rml::Element *_outer_container)
{
	auto container_top = _outer_container->GetAbsoluteTop();
	auto container_height = _outer_container->GetClientHeight();
	for(int idx = _elem_container->GetNumChildren()-1; idx >= 0; idx--) {
		auto elem = _elem_container->GetChild(idx);
		auto elem_top = elem->GetAbsoluteTop();
		auto elem_relative_top = elem_top - container_top;
		if(elem_relative_top < container_height) {
			if(elem->GetClientHeight() <= container_height) {
				if(elem_relative_top + elem->GetClientHeight() <= container_height) {
					return std::make_pair(elem,idx);
				}
			} else {
				return std::make_pair(elem,idx);
			}
		}
	}
	return std::make_pair(nullptr,-1);
}

void Window::scroll_vertical_into_view(Rml::Element *_element, Rml::Element *_container)
{
	assert(_element);
	if(!_container) {
		_container = _element->GetParentNode();
	}
	assert(_container);
	auto container_height = _container->GetClientHeight();
	auto container_top = _container->GetAbsoluteTop();
	auto element_top = _element->GetAbsoluteTop();
	auto element_relative_top = element_top - container_top;
	auto element_relative_bottom = element_relative_top + _element->GetClientHeight();
	if(element_relative_bottom > container_height) {
		if(container_height > _element->GetClientHeight()) {
			_element->ScrollIntoView(false);
		} else {
			_element->ScrollIntoView(true);
		}
	} else if(element_relative_top < 0) {
		_element->ScrollIntoView(true);
	}
	_container->SetScrollLeft(0);
}

void Window::on_cancel(Rml::Event &)
{
	hide();
}

void Window::on_keydown(Rml::Event &_ev)
{
	switch(get_key_identifier(_ev)) {
		case Rml::Input::KeyIdentifier::KI_ESCAPE:
			on_cancel(_ev);
			break;
		default:
			break;
	}
}