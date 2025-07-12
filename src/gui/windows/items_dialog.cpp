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
#include "items_dialog.h"
#include "gui.h"


ItemsDialog::ItemsDialog(GUI * _gui, const char *_rml)
:
Window(_gui, _rml)
{
}

ItemsDialog::~ItemsDialog()
{
}

void ItemsDialog::create(std::string _mode, int _zoom,
		const std::string &_entries_el, const std::string &_entries_cont_el)
{
	m_entries_el = get_element(_entries_el);
	m_entries_cont_el = get_element(_entries_cont_el);

	set_mode(_mode);

	m_zoom = _zoom;
	set_zoom(0);
}

void ItemsDialog::set_mode(std::string _mode)
{
	if(_mode != "grid" && _mode != "list") {
		_mode = "grid";
	}

	auto e = get_element(str_format("mode_%s",_mode.c_str()));
	if(e) {
		e->SetAttribute("checked", true);
	}

	m_entries_el->SetClass("list", false);
	m_entries_el->SetClass("grid", false);
	m_entries_el->SetClass(_mode, true);
}

std::string ItemsDialog::get_mode()
{
	if(m_entries_el->IsClassSet("list")) {
		return "list";
	}
	return "grid";
}

void ItemsDialog::set_zoom(int _amount)
{
	m_entries_el->SetClass(str_format("zoom-%d", m_zoom), false);
	m_zoom += _amount;
	m_zoom = std::min(m_max_zoom, m_zoom);
	m_zoom = std::max(m_min_zoom, m_zoom);
	m_entries_el->SetClass(str_format("zoom-%d", m_zoom), true);
}

Rml::Element* ItemsDialog::get_entry(Rml::Element *target_el)
{
	Rml::Element *entry_el = nullptr;
	entry_el = target_el;
	while(entry_el && entry_el->GetId().empty()) {
		entry_el = entry_el->GetParentNode();
	}
	return entry_el;
}

Rml::Element* ItemsDialog::get_entry(Rml::Event &_ev)
{
	return get_entry(_ev.GetTargetElement());
}

void ItemsDialog::entry_select(Rml::Element *_entry_el)
{
	entry_deselect();

	m_selected_entry = _entry_el;
	m_selected_entry->SetClass("selected", true);
	m_selected_entry->SetClass("hover", true);

	scroll_vertical_into_view(_entry_el, m_entries_cont_el);
}

void ItemsDialog::entry_deselect()
{
	if(m_selected_entry) {
		m_selected_entry->SetClass("selected", false);
		m_selected_entry->SetClass("hover", false);
	}
	m_selected_entry = nullptr;
}

bool ItemsDialog::would_handle(Rml::Input::KeyIdentifier _key, int _mod)
{
	return (
		( _mod == 0 && _key == Rml::Input::KeyIdentifier::KI_OEM_MINUS ) ||
		( _mod == 0 && _key == Rml::Input::KeyIdentifier::KI_SUBTRACT ) ||
		( _mod == 0 && _key == Rml::Input::KeyIdentifier::KI_OEM_PLUS ) ||
		( _mod == 0 && _key == Rml::Input::KeyIdentifier::KI_ADD ) ||
		( _mod == Rml::Input::KM_CTRL && _key == Rml::Input::KeyIdentifier::KI_1 ) ||
		( _mod == Rml::Input::KM_CTRL && _key == Rml::Input::KeyIdentifier::KI_2 ) ||
		( _mod == 0 && _key == Rml::Input::KeyIdentifier::KI_LEFT ) ||
		( _mod == 0 && _key == Rml::Input::KeyIdentifier::KI_RIGHT ) ||
		( _mod == 0 && _key == Rml::Input::KeyIdentifier::KI_UP ) ||
		( _mod == 0 && _key == Rml::Input::KeyIdentifier::KI_DOWN ) ||
		( _mod == 0 && _key == Rml::Input::KeyIdentifier::KI_NEXT ) ||
		( _mod == 0 && _key == Rml::Input::KeyIdentifier::KI_PRIOR ) ||
		( _mod == 0 && _key == Rml::Input::KeyIdentifier::KI_END ) ||
		( _mod == 0 && _key == Rml::Input::KeyIdentifier::KI_HOME ) ||
		Window::would_handle(_key, _mod)
	);
}

void ItemsDialog::on_keydown(Rml::Event &_ev)
{
	auto id = get_key_identifier(_ev);
	switch(id) {
		case Rml::Input::KeyIdentifier::KI_OEM_MINUS:
		case Rml::Input::KeyIdentifier::KI_SUBTRACT:
			set_zoom(-1);
			break;
		case Rml::Input::KeyIdentifier::KI_OEM_PLUS:
		case Rml::Input::KeyIdentifier::KI_ADD:
			set_zoom(1);
			break;
		case Rml::Input::KeyIdentifier::KI_1:
		case Rml::Input::KeyIdentifier::KI_2:
			if(_ev.GetParameter<bool>("ctrl_key", false)) {
				if(id == Rml::Input::KeyIdentifier::KI_1) {
					set_mode("grid");
				} else {
					set_mode("list");
				}
				break;
			} else {
				Window::on_keydown(_ev);
				return;
			}
		case Rml::Input::KeyIdentifier::KI_LEFT:
		case Rml::Input::KeyIdentifier::KI_RIGHT:
		case Rml::Input::KeyIdentifier::KI_UP:
		case Rml::Input::KeyIdentifier::KI_DOWN:
		case Rml::Input::KeyIdentifier::KI_NEXT:
		case Rml::Input::KeyIdentifier::KI_PRIOR:
		case Rml::Input::KeyIdentifier::KI_END:
		case Rml::Input::KeyIdentifier::KI_HOME:
			if(!_ev.GetParameter<bool>("alt_key", false)) {
				move_selection(id);
				break;
			}
			[[fallthrough]];
		default:
			Window::on_keydown(_ev);
			return;
	}
	_ev.StopImmediatePropagation();
}

void ItemsDialog::on_keyup(Rml::Event &)
{
	m_moving_selection = false;
}

void ItemsDialog::move_selection(Rml::Input::KeyIdentifier _key_id)
{
	Rml::Element *entry = nullptr;
	Rml::Element *start_entry = m_selected_entry;
	int entryidx = -1;
	bool is_grid = get_mode() == "grid";
	bool is_list = !is_grid;

	if(_key_id == Rml::Input::KeyIdentifier::KI_HOME)
	{
		entry = m_entries_el->GetFirstChild();
	}
	else if(_key_id == Rml::Input::KeyIdentifier::KI_END)
	{
		entry = m_entries_el->GetLastChild();
	} 
	else if(!start_entry)
	{
		if(_key_id == Rml::Input::KeyIdentifier::KI_DOWN ||
		  (is_list && _key_id == Rml::Input::KeyIdentifier::KI_NEXT) ||
		  (is_grid && _key_id == Rml::Input::KeyIdentifier::KI_RIGHT))
		{
			std::tie(entry,entryidx) = get_first_visible_element(m_entries_el, m_entries_cont_el);
		}
		else if(_key_id == Rml::Input::KeyIdentifier::KI_UP || 
		       (is_list && _key_id == Rml::Input::KeyIdentifier::KI_PRIOR) ||
		       (is_grid && _key_id == Rml::Input::KeyIdentifier::KI_LEFT))
		{
			std::tie(entry,entryidx) = get_last_visible_element(m_entries_el, m_entries_cont_el);
		}
	}
	
	bool scroll_top = false;
	bool scroll_bottom = false;
	if(!entry && start_entry) {
		if(entryidx < 0) {
			for(; entryidx < m_entries_el->GetNumChildren(); entryidx++) {
				if(m_entries_el->GetChild(entryidx) == start_entry) {
					break;
				}
			}
		}
		switch(_key_id) {
			case Rml::Input::KeyIdentifier::KI_UP:
				entryidx--;
				if(is_grid) {
					// find upper element
					float x = start_entry->GetAbsoluteLeft();
					for(; entryidx >= 0; entryidx--) {
						auto child = m_entries_el->GetChild(entryidx);
						assert(child);
						if(child->GetAbsoluteLeft() == x) {
							break;
						}
					}
				}
				break;
			case Rml::Input::KeyIdentifier::KI_DOWN:
				entryidx++;
				if(is_grid) {
					// find lower element
					float x = start_entry->GetAbsoluteLeft();
					float y = start_entry->GetAbsoluteTop();
					for(; entryidx < m_entries_el->GetNumChildren(); entryidx++) {
						auto child = m_entries_el->GetChild(entryidx);
						assert(child);
						if(child->GetAbsoluteLeft() == x) {
							break;
						}
					}
					if(entryidx == m_entries_el->GetNumChildren()) {
						int lastidx = m_entries_el->GetNumChildren() - 1;
						auto child = m_entries_el->GetChild(lastidx);
						if(child && child->GetAbsoluteTop() != y) {
							entryidx = lastidx;
						}
					}
				}
				break;
			case Rml::Input::KeyIdentifier::KI_LEFT:
				if(is_grid) {
					entryidx--;
				}
				break;
			case Rml::Input::KeyIdentifier::KI_RIGHT:
				if(is_grid) {
					entryidx++;
				}
				break;
			case Rml::Input::KeyIdentifier::KI_NEXT:
				if(is_list) {
					std::tie(entry,entryidx) = get_last_visible_element(m_entries_el, m_entries_cont_el);
					if(entryidx < m_entries_el->GetNumChildren() - 1) {
						entryidx++;
					}
					scroll_top = true;
				}
				break;
			case Rml::Input::KeyIdentifier::KI_PRIOR:
				if(is_list) {
					std::tie(entry,entryidx) = get_first_visible_element(m_entries_el, m_entries_cont_el);
					if(entryidx > 0) {
						entryidx--;
					}
					scroll_bottom = true;
				}
				break;
			default:
				return;
		}
		if(entryidx < 0 || entryidx >= m_entries_el->GetNumChildren()) {
			return;
		}
		entry = m_entries_el->GetChild(entryidx);
	}

	if(entry) {
		if(entry != m_selected_entry) {
			m_moving_selection = true;
			entry_select(entry);
			if(scroll_top) {
				entry->ScrollIntoView(true);
			} else if(scroll_bottom) {
				entry->ScrollIntoView(false);
			}
		}
		m_entries_el->Focus();
	}
}