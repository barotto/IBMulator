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

#ifndef IBMULATOR_GUI_STATESAVE_H
#define IBMULATOR_GUI_STATESAVE_H

#include "state_dialog.h"


class StateSave final : public StateDialog
{
private:
	static event_map_t ms_evt_map;

public:
	StateSave(GUI * _gui, std::string _mode, std::string _order, int _zoom);

	bool would_handle(Rml::Input::KeyIdentifier _key, int _mod) override;

	void entry_select(Rml::Element *_entry) override;
	void entry_select(std::string _name, Rml::Element *_entry, bool _tts_append);

	void action_on_record(std::string _rec_name) override;

	void on_keydown(Rml::Event &_ev) override;

protected:
	void create() override;
	event_map_t & get_event_map() override { return StateSave::ms_evt_map; }
	void speak_entry(const StateRecord* _sr, Rml::Element *_entry_el, bool _append) override;

private:
	void on_entry(Rml::Event &);
	void on_new_save(Rml::Event &);
};


#endif
