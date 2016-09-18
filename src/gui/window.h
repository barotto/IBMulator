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

#ifndef IBMULATOR_GUI_WINDOW_H
#define IBMULATOR_GUI_WINDOW_H


#include <Rocket/Core/EventListener.h>
#include <functional>

class GUI;
class Window;

typedef void (RC::EventListener::*event_handler_t)(RC::Event &);
typedef std::pair<RC::String,RC::String> event_map_key_t;
typedef std::map<event_map_key_t, event_handler_t> event_map_t;
#define GUI_EVT(id, type, fn) { {id, type}, static_cast<event_handler_t>(&fn) }

class Window : public RC::EventListener
{
protected:
	GUI * m_gui;
	RC::ElementDocument * m_wnd;
	static event_map_t ms_event_map;
	bool m_evts_added;

public:
	Window(GUI * _gui, const char *_rml);
	virtual ~Window();

	void show();
	void hide();
	void close();
	bool is_visible();

	virtual void config_changed() {}
	virtual void update();

protected:
	void ProcessEvent(RC::Event &);
	virtual event_map_t & get_event_map() { return ms_event_map; }
	RC::Element * get_element(const RC::String &_id);
	void add_events();
	void remove_events();
};

#endif
