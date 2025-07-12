/*
 * Copyright (C) 2015-2025  Marco Bortolin
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
	PDEBUGF(LOG_V2, LOG_GUI, "Adding events for '%s'\n", title().c_str());
	event_map_t & evtmap = get_event_map();
	for(auto & [evt,handler] : evtmap) {
		// model is always bubbling
		if(evt.first == "*") {
			m_wnd->AddEventListener(evt.second, this);
			PDEBUGF(LOG_V0, LOG_GUI, "Adding catch-all for '%s' event\n", evt.second.c_str());
		} else {
			if(evt.first.find("class:") == 0) {
				Rml::ElementList elements;
				m_wnd->GetElementsByClassName(elements, evt.first.substr(6));
				for(auto & el : elements) {
					register_handler(el, evt.second, handler);
				}
			} else {
				try {
					Rml::Element *el = get_element(evt.first);
					el->AddEventListener(evt.second, this);
				} catch(std::exception &) {
					m_wnd->AddEventListener(evt.second, this);
					PDEBUGF(LOG_V0, LOG_GUI, "Adding catch-all for '%s' event\n", evt.second.c_str());
				}
			}
		}
	}

	register_handler(m_wnd, "focus", true, static_cast<event_handler_t>(&Window::on_focus));
	register_handler(m_wnd, "show", true, static_cast<event_handler_t>(&Window::on_show));

	add_aria_events(m_wnd, {});

	m_evts_added = true;
}

void Window::add_aria_events(Rml::Element *_container, const AriaEventsExclusions &_exclusions)
{
	Rml::ElementList input_elems;
	_container->GetElementsByTagName(input_elems, "button");
	_container->GetElementsByTagName(input_elems, "input");
	_container->GetElementsByTagName(input_elems, "select");
	_container->GetElementsByTagName(input_elems, "tab");
	_container->GetElementsByTagName(input_elems, "tabset");
	_container->GetElementsByTagName(input_elems, "spinbutton");
	for(auto elem : input_elems) {
		if(elem->GetAttribute("aria-label")) {
			std::string event;
			if(elem->GetTagName() == "tabset") {
				event = "tabchange";
			} else {
				event = "change";
			}
			if(std::find(_exclusions.begin(), _exclusions.end(), std::make_pair(event, elem->GetId())) == _exclusions.end()) {
				register_handler(elem, event, true, static_cast<event_handler_t>(&Window::on_change));
			}
			if(std::find(_exclusions.begin(), _exclusions.end(), std::make_pair(std::string("focus"), elem->GetId())) == _exclusions.end()) {
				register_handler(elem, "focus", true, static_cast<event_handler_t>(&Window::on_focus));
			}
		}
	}
}

void Window::create()
{
	// it turns out with RmlUi you can't load a document in the constructor, bummer
	if(m_rml_docfile.empty()) {
		throw std::exception();
	}
	if(!m_wnd) {
		setup_data_bindings();
		m_wnd = m_gui->load_document(m_rml_docfile, this);
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
		m_gui->tts().enqueue("dialog window closed", TTS::Priority::Top);
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

std::string Window::title() const
{
	Rml::Element *title = m_wnd->GetElementById("title");
	if(title) {
		return title->GetInnerRML();
	}
	return "";
}

void Window::set_title(const std::string &_title)
{
	Rml::Element *title = m_wnd->GetElementById("title");
	if(title) {
		title->SetInnerRML(_title);
	}
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

std::string Window::create_id()
{
	m_auto_id++;
	return str_format("Window-%u", m_auto_id);
}

void Window::register_lazy_update_fn(std::function<void()> _fn)
{
	m_lazy_update_fn.push_back(_fn);
}

void Window::update_after()
{
	for(auto & fn : m_lazy_update_fn) {
		fn();
	}
	m_lazy_update_fn.clear();
}

void Window::register_handler(Rml::Element *_target, const std::string &_event_type, bool _is_targeted, event_handler_t _fn)
{
	register_handler(_target, _event_type, event_handler_info{
		true, _fn, _is_targeted
	});
}

void Window::register_handler(Rml::Element *_target, const std::string &_event_type, const event_handler_info &_handler)
{
	if(_target->GetId().empty()) {
		_target->SetId(create_id());
		PDEBUGF(LOG_V2, LOG_GUI, "Cannot listen for events on an element without an ID, using '%s'\n",
				_target->GetId().c_str());
	}
	PDEBUGF(LOG_V3, LOG_GUI, "Registering handler for event '%s', target <%s id=\"%s\">\n",
			_event_type.c_str(), _target->GetTagName().c_str(), _target->GetId().c_str());
	_target->AddEventListener(_event_type, this);
	get_event_map()[{_target->GetId(),_event_type}] = _handler;
}

void Window::unregister_all_handlers(Rml::Element *_root)
{
	auto &map = get_event_map();
	for(auto entry = map.begin(); entry != map.end();) {
		if(_root->GetId() == entry->first.first || _root->GetElementById(entry->first.first)) {
			PDEBUGF(LOG_V3, LOG_GUI, "Unregistering handler for event '%s', target id='%s'\n",
				entry->first.second.c_str(), entry->first.first.c_str());
			entry = map.erase(entry);
		} else {
			entry++;
		}
	}
}

void Window::register_target_cb(Rml::Element *_target, const std::string &_event_type, TargetCbFn _fn)
{
	assert(_target);
	if(_target->GetId().empty()) {
		_target->SetId(create_id());
		PDEBUGF(LOG_V2, LOG_GUI, "Cannot listen for events on an element without an ID, using '%s'\n",
				_target->GetId().c_str());
	}
	PDEBUGF(LOG_V2, LOG_GUI, "Registering target callback on '%s', type '%s'\n",
					_target->GetId().c_str(), _event_type.c_str());
	register_target_cb(_target, _target->GetId(), _event_type, _fn);
}

void Window::register_target_cb(Rml::Element *_target, const std::string &_elem_id, const std::string &_event_type, TargetCbFn _fn)
{
	assert(_target);
	_target->AddEventListener(_event_type, this);
	auto key = event_map_key_t(_elem_id, _event_type);
	m_target_cb.funcs[key] = _fn;
	m_target_cb.elems[_target].insert(key);
}

void Window::unregister_all_target_cb(Rml::Element *_root)
{
	for(auto entry = m_target_cb.funcs.begin(); entry != m_target_cb.funcs.end();) {
		if(_root->GetId() == entry->first.first || _root->GetElementById(entry->first.first)) {
			PDEBUGF(LOG_V3, LOG_GUI, "Unregistering target callback on '%s', type '%s'\n", entry->first.first.c_str(), entry->first.second.c_str());
			entry = m_target_cb.funcs.erase(entry);
		} else {
			entry++;
		}
	}
	for(auto entry = m_target_cb.elems.begin(); entry != m_target_cb.elems.end();) {
		if(_root == entry->first || _root->GetElementById(entry->first->GetId())) {
			entry = m_target_cb.elems.erase(entry);
		} else {
			entry++;
		}
	}
}

void Window::unregister_target_cb(Rml::Element *_target)
{
	for(auto & fk : m_target_cb.elems[_target]) {
		m_target_cb.funcs.erase(fk);
	}
	m_target_cb.elems.erase(_target);
}

void Window::ProcessEvent(Rml::Event &_event)
{
	Rml::Element *el = _event.GetTargetElement();
	std::string elid = el->GetId();
	std::string fn_elid = elid;
	const std::string &type = _event.GetType();

	if(!m_handlers_enabled) {
		PDEBUGF(LOG_V5, LOG_GUI, "Window: RmlUi Event '%s' on '%s' ignored: event handling disabled\n",
				_event.GetType().c_str(), el->GetId().c_str());
		return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "Window: RmlUi Event '%s' on '%s'", type.c_str(), elid.c_str());

	auto key = event_map_key_t(el->GetId(), type);

	auto target_cb = m_target_cb.funcs.find(key);
	if(target_cb == m_target_cb.funcs.end()) {
		Rml::Element *parent = el->GetParentNode();
		while(parent) {
			target_cb = m_target_cb.funcs.find(event_map_key_t(parent->GetId(), type));
			if(target_cb != m_target_cb.funcs.end()) {
				break;
			}
			parent = parent->GetParentNode();
		}
	}
	if(target_cb != m_target_cb.funcs.end()) {
		PDEBUGF(LOG_V1, LOG_GUI, " (target cb: '%s')\n", target_cb->first.first.c_str());
		if(target_cb->second(_event)) {
			_event.StopImmediatePropagation();
		}
		return;
	}

	event_map_t & evtmap = get_event_map();
	auto hit = evtmap.find(key);

	event_handler_t fn = nullptr;
	if(hit != evtmap.end()) {
		PDEBUGF(LOG_V2, LOG_GUI, " (element entry) ");
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
				fn_elid = parent->GetId();
				fn = hit->second.handler;
				PDEBUGF(LOG_V2, LOG_GUI, " (parent entry) ");
				break;
			}
			parent = parent->GetParentNode();
		}
	}
	if(!fn) {
		hit = evtmap.find(event_map_key_t("*", type));
		fn_elid = "*";
		if(hit != evtmap.end()) {
			PDEBUGF(LOG_V2, LOG_GUI, " (catch-all entry) ");
			fn = hit->second.handler;
		}
	}

	if(fn) {
		if(!hit->second.enabled) {
			PDEBUGF(LOG_V1, LOG_GUI, " ('%s') [DISABLED]\n", fn_elid.c_str());
		} else {
			PDEBUGF(LOG_V1, LOG_GUI, " ('%s')\n", fn_elid.c_str());
			(this->*fn)(_event);
		}
	} else {
		PDEBUGF(LOG_V1, LOG_GUI, " [no fn]\n");
	}
}

void Window::OnAttach(Rml::Element* _element)
{
	if(_element->GetId().empty()) {
		_element->SetId(create_id());
	}
	PDEBUGF(LOG_V2, LOG_GUI, "Element '%s' attached to listener '%s'\n", _element->GetId().c_str(), m_rml_docfile.c_str());
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

Rml::ElementPtr Window::create_button()
{
	return m_wnd->CreateElement("button");
}

Rml::Element * Window::get_button_element(Rml::Event &_ev)
{
	Rml::Element *el = _ev.GetTargetElement();
	while(el) {
		if(el->GetTagName() == "button") {
			return el;
		}
		el = el->GetParentNode();
	};
	return nullptr;
}

Rml::Element * Window::disable(Rml::Element *_el)
{
	return set_disabled(_el, true);
}

Rml::Element * Window::enable(Rml::Element *_el)
{
	return set_disabled(_el, false);
}

Rml::Element * Window::set_disabled(Rml::Element *_el, bool _disabled)
{
	assert(_el);
	_el->SetClass("disabled", _disabled);
	auto *ctrl = dynamic_cast<Rml::ElementFormControl*>(_el);
	if(ctrl) {
		ctrl->SetDisabled(_disabled);
	} else {
		_el->SetClass("enabled", !_disabled);
	}
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
	_el->SetClass("inactive", !_active);
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
	Rml::ScrollIntoViewOptions options;
	options.behavior = Rml::ScrollBehavior::Smooth;
	options.horizontal = Rml::ScrollAlignment::Nearest;
	if(element_relative_bottom > container_height) {
		if(container_height > _element->GetClientHeight()) {
			options.vertical = Rml::ScrollAlignment::End;
		} else {
			options.vertical = Rml::ScrollAlignment::Start;
		}
		_element->ScrollIntoView(options);
	} else if(element_relative_top < 0) {
		options.vertical = Rml::ScrollAlignment::Start;
		_element->ScrollIntoView(options);
	}
	_container->SetScrollLeft(0);
}

void Window::scroll_horizontal_into_view(Rml::Element *_element, Rml::Element *_container)
{
	assert(_element);
	if(!_container) {
		_container = _element->GetParentNode();
	}
	assert(_container);
	auto container_width = _container->GetClientWidth();
	auto container_left = _container->GetAbsoluteLeft();
	auto element_left = _element->GetAbsoluteLeft();
	auto element_relative_left = element_left - container_left;
	auto element_relative_right = element_relative_left + _element->GetClientWidth();
	Rml::ScrollIntoViewOptions options;
	options.behavior = Rml::ScrollBehavior::Smooth;
	options.vertical = Rml::ScrollAlignment::Nearest;
	if(element_relative_right > container_width) {
		if(container_width > _element->GetClientWidth()) {
			options.horizontal = Rml::ScrollAlignment::End;
		} else {
			options.horizontal = Rml::ScrollAlignment::Start;
		}
		_element->ScrollIntoView(options);
	} else if(element_relative_left < 0) {
		options.horizontal = Rml::ScrollAlignment::Start;
		_element->ScrollIntoView(options);
	}
	_container->SetScrollTop(0);
}

void Window::on_cancel(Rml::Event &)
{
	hide();
}

bool Window::would_handle(Rml::Input::KeyIdentifier _key, int _mod)
{
	// TODO create a dynamic table-based system with function pointers to handlers
	// (maybe in another life)
	return _mod == 0 && _key == Rml::Input::KeyIdentifier::KI_ESCAPE;
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

void Window::on_show(Rml::Event &_ev)
{
	Rml::Element *el = _ev.GetTargetElement();
	if(el != m_wnd) {
		return;
	}
	PDEBUGF(LOG_V0, LOG_GUI, "Window SHOW\n");
}

void Window::speak_element(Rml::Element *_el, bool _with_label, bool _describe, TTS::Priority _pri)
{
	assert(_el);

	if(_with_label) {
		std::vector<std::string> breadcrumbs;
		if(_describe) {
			Rml::Element *parent = _el->GetParentNode();
			while(parent) {
				if(parent == m_wnd) {
					if(!title().empty()) {
						breadcrumbs.push_back(title() + ", dialog");
					}
					break;
				} else {
					auto plabel = parent->GetAttribute("aria-label", std::string());
					if(!plabel.empty()) {
						breadcrumbs.push_back(plabel);
					}
				}
				parent = parent->GetParentNode();
			}
		}
		std::reverse(breadcrumbs.begin(), breadcrumbs.end());

		auto label_str = _el->GetAttribute("aria-label", std::string());
		if(!label_str.empty()) {
			auto tag = _el->GetTagName();
			if(tag == "button") {
				label_str += ", button";
				if(_el->IsClassSet("disabled")) {
					label_str += ", disabled";
				} else if(_el->IsClassSet("enabled")) {
					// label_str += ", enabled";
				} else if(_el->IsClassSet("active") || _el->IsClassSet("on")) {
					label_str += ", active";
				} else if(_el->IsClassSet("inactive") || _el->IsClassSet("off")) {
					label_str += ", not active";
				}
			} else if(tag == "spinbutton") {
				label_str += ", spin button, value: " + _el->GetInnerRML();
			} else if(tag == "input") {
				auto type = _el->GetAttribute("type")->Get<std::string>("");
				if(type == "radio") {
					label_str += ", radio button";
					if(_el->IsPseudoClassSet("disabled")) {
						label_str += ", disabled";
					} else if(_el->IsPseudoClassSet("checked")) {
						label_str += ", checked";
					} 
				} else if(type == "text") {
					label_str += ", text input";
					auto input = dynamic_cast<Rml::ElementFormControl*>(_el);
					if(input) {
						if(input->GetValue().empty()) {
							label_str += ", empty";
						} else {
							label_str += ", value: " + input->GetValue();
						}
					}
				} else if(type == "checkbox") {
					label_str += ", check button";
					auto input = dynamic_cast<Rml::ElementFormControl*>(_el);
					if(input) {
						if(input->IsSubmitted()) {
							label_str += ", checked";
						} else {
							label_str += ", not checked";
						}
					}
				} else if(type == "range") {
					bool vertical = _el->GetAttribute("orientation",std::string()) == "vertical";
					if(vertical) {
						label_str += ", vertical slider";
					} else { // horizontal
						label_str += ", horizontal slider";
					}
					auto input = dynamic_cast<Rml::ElementFormControlInput*>(_el);
					if(input) {
						auto value = int(str_parse_real_num(input->GetValue()));
						if(vertical) {
							if(input->GetAttribute("data-top-value", std::string("min")) == "max") {
								value = input->GetAttribute("max", 100) - value;
							}
						} else { // horizontal
							if(input->GetAttribute("data-mid-value", std::string("mid")) == "0") {
								value = value * 2 - input->GetAttribute("max", 100);
							}
						}
						label_str += str_format(", value: %d", value);
					}
				}
			} else if(tag == "select") {
				auto select = dynamic_cast<Rml::ElementFormControlSelect*>(_el);
				label_str += ", drop-down list";
				if(select) {
					auto option = select->GetOption(select->GetSelection());
					if(option) {
						label_str += ", value: " + option->GetInnerRML();
					}
				}
			} else if(tag == "tab") {
				label_str += ", tab button";
				if(_el->IsPseudoClassSet("selected")) {
					label_str += ", selected";
				} 
			}
			breadcrumbs.push_back(label_str);
		}
		if(!breadcrumbs.empty()) {
			m_gui->tts().enqueue(str_implode(breadcrumbs, "\n"), _pri, TTS::BREAK_LINES);
		}
	} else {
		auto tag = _el->GetTagName();
		if(tag == "tabset") {
			auto tabset = dynamic_cast<Rml::ElementTabSet*>(_el);
			auto tabidx = tabset->GetActiveTab();
			Rml::ElementList tabs;
			tabset->GetElementsByTagName(tabs, "tab");
			if(tabidx >= 0 && tabidx < int(tabs.size())) {
				auto label = tabs[tabidx]->GetAttribute("aria-label", std::string());
				if(label.empty()) {
					tabs[tabidx]->GetInnerRML(label);
				}
				m_gui->tts().enqueue(label + " panel shown", _pri);
			}
		} else if(tag == "select") {
			auto select = dynamic_cast<Rml::ElementFormControlSelect*>(_el);
			if(select) {
				auto option = select->GetOption(select->GetSelection());
				if(option) {
					m_gui->tts().enqueue(option->GetInnerRML(), _pri);
				}
			}
		} else if(tag == "input") {
			auto type = _el->GetAttribute("type", std::string());
			auto input = dynamic_cast<Rml::ElementFormControl*>(_el);
			if(!input) {
				return;
			}
			if(type == "checkbox") {
				if(input->IsSubmitted()) {
					m_gui->tts().enqueue("checked", _pri);
				} else {
					m_gui->tts().enqueue("unchecked", _pri);
				}
			} else if(type == "range") {
				bool vertical = input->GetAttribute("orientation",std::string()) == "vertical";
				auto value = int(str_parse_real_num(input->GetValue()));
				if(vertical) {
					if(input->GetAttribute("data-top-value", std::string("min")) == "max") {
						value = input->GetAttribute("max", 100) - value;
					}
				} else {
					if(input->GetAttribute("data-mid-value", std::string("mid")) == "0") {
						value = value * 2 - input->GetAttribute("max", 100);
					}
				}
				m_gui->tts().enqueue(str_format("%d", value), _pri);
			} else if(type != "text" && type != "radio") {
				// radio buttons fire a lot of onchange events....
				// they must be handled manually
				m_gui->tts().enqueue(input->GetValue(), _pri);
			}
		} else if(tag == "tab") {
			if(_el->IsPseudoClassSet("selected")) {
				m_gui->tts().enqueue("selected", _pri);
			}  else {
				m_gui->tts().enqueue("unselected", _pri);
			}
		}
	}
}

void Window::on_focus(Rml::Event &_ev)
{
	assert(_ev.GetId() == Rml::EventId::Focus);

	Rml::Element *el = _ev.GetTargetElement();
	PDEBUGF(LOG_V3, LOG_GUI, "focus on '%s'\n", el->GetId().c_str());
	if(el == m_wnd) {
		if(!title().empty()) {
			m_gui->tts().enqueue(title() + ", dialog", TTS::Priority::High);
		}
	} else {
		speak_element(el, true);
	}
}

void Window::on_change(Rml::Event &_ev)
{
	assert(_ev.GetId() == Rml::EventId::Change || _ev.GetId() == Rml::EventId::Tabchange);

	if(!is_visible()) {
		// idk why sometimes an event is fired when the window is not even shown
		return;
	}
	Rml::Element *el = _ev.GetTargetElement();
	speak_element(el, false);
}

