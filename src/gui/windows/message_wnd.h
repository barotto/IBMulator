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

#ifndef IBMULATOR_GUI_MESSAGEBOX_H
#define IBMULATOR_GUI_MESSAGEBOX_H

#include "../window.h"
#include <RmlUi/Core.h>

class MessageWnd : public Window
{
public:

	enum class Type {
		MSGW_OK,
		MSGW_YES_NO
	};

private:

	static event_map_t ms_evt_map;
	std::function<void()> m_action1_clbk = nullptr;
	std::function<void()> m_action2_clbk = nullptr;
	Type m_type = Type::MSGW_OK;

public:

	MessageWnd(GUI * _gui);
	virtual ~MessageWnd();

	void create();
	void show();

	void set_type(Type);
	void set_callbacks(
		std::function<void()> _action1,
		std::function<void()> _action2) {
		m_action1_clbk = _action1;
		m_action2_clbk = _action2;
	}
	void set_title(const std::string &);
	void set_message(const std::string &);

	event_map_t & get_event_map() { return MessageWnd::ms_evt_map; }

private:

	void on_action(Rml::Event &);
	void on_keydown(Rml::Event &_ev);
	
	Rml::ElementPtr create_button(std::string _label, std::string _id, Rml::ElementDocument *_doc) const;
};



#endif
