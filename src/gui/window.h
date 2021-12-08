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

#ifndef IBMULATOR_GUI_WINDOW_H
#define IBMULATOR_GUI_WINDOW_H


#include <RmlUi/Core.h>
#include <functional>


class GUI;
class Window;

typedef void (Rml::EventListener::*event_handler_t)(Rml::Event &);
typedef std::pair<std::string,std::string> event_map_key_t;
typedef std::map<event_map_key_t, event_handler_t> event_map_t;
#define GUI_EVT(id, type, fn) { {id, type}, static_cast<event_handler_t>(&fn) }

class Window : public Rml::EventListener
{
	friend class GUI;
protected:
	GUI *m_gui;
	std::string m_rml_docfile;
	Rml::ElementDocument *m_wnd = nullptr;
	static event_map_t ms_event_map;
	bool m_evts_added = false;
	Rml::ModalFlag m_modal = Rml::ModalFlag::None;

public:
	Window(GUI * _gui, const char *_rml);
	virtual ~Window() {}

	virtual void create();
	virtual void show();
	virtual void hide();
	virtual void close();
	virtual void focus();

	bool is_visible();
	bool is_loaded() const { return m_wnd; }

	void set_modal(bool _modal) {
		m_modal = _modal ? Rml::ModalFlag::Modal : Rml::ModalFlag::None;
	}

	virtual void config_changed() {}
	virtual void update();

	Rml::ElementDocument *document() { return m_wnd; }

	virtual void on_cancel(Rml::Event &);
	virtual void on_keydown(Rml::Event &);

protected:
	void ProcessEvent(Rml::Event &);
	virtual event_map_t & get_event_map() { return ms_event_map; }
	Rml::Element * get_element(const std::string &_id);
	void add_events();

	static Rml::Input::KeyIdentifier get_key_identifier(Rml::Event &);
	static std::string get_form_input_value(Rml::Event &);
	static Rml::Element * disable(Rml::Element *);
	static Rml::Element * enable(Rml::Element *);
	static Rml::Element * set_disabled(Rml::Element *_el, bool _disabled);
	static bool is_disabled(Rml::Element *);
	static Rml::Element * set_active(Rml::Element *_elm, bool _active);
	static bool is_active(Rml::Element *);
	static void scroll_vertical_into_view(Rml::Element *_element, Rml::Element *_container = nullptr);
};

#endif
