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

#ifndef IBMULATOR_GUI_STATEINFO_H
#define IBMULATOR_GUI_STATEINFO_H

#include "../window.h"
#include "state_record.h"
#include <RmlUi/Core.h>


class StateSaveInfo final : public Window
{
private:
	static event_map_t ms_evt_map;
	std::function<void(StateRecord::Info)> m_save_callbk = nullptr;
	std::function<void()> m_cancel_callbk = nullptr;
	Rml::ElementFormControl *m_desc_el = nullptr;
	StateRecord::Info m_state_info;

public:
	StateSaveInfo(GUI * _gui);

	void show() override;

	void set_state(StateRecord::Info _info);
	void set_callbacks(
		std::function<void(StateRecord::Info)> _save_callback,
		std::function<void()> _cancel_callback = nullptr) {
		m_save_callbk = _save_callback;
		m_cancel_callbk = _cancel_callback;
	}

protected:
	void create() override;
	event_map_t & get_event_map() override { return StateSaveInfo::ms_evt_map; }

private:
	void on_save(Rml::Event &);
	void on_cancel(Rml::Event &) override;
	void on_keydown(Rml::Event &_ev) override;
};


#endif
