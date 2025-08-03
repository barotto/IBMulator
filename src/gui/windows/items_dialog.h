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

#ifndef IBMULATOR_GUI_ITEMSDIALOG_H
#define IBMULATOR_GUI_ITEMSDIALOG_H

#include "../window.h"

class GUI;


class ItemsDialog : public Window
{
protected:
	std::string m_entries_el_id;
	Rml::Element *m_entries_el = nullptr;
	std::string m_entries_cont_el_id;
	Rml::Element *m_entries_cont_el = nullptr;

	Rml::Element *m_selected_entry = nullptr;

	std::string m_mode;
	int m_min_zoom = 0;
	int m_max_zoom = 0;
	int m_zoom;

	bool m_moving_selection = false;

public:
	ItemsDialog(GUI *_gui, const char *_rml,
		std::string _mode, int _zoom,
		const std::string &_entries_el, const std::string &_entries_cont_el);

	bool would_handle(Rml::Input::KeyIdentifier, int) override;
	void on_keydown(Rml::Event &) override;
	virtual void on_keyup(Rml::Event &);

protected:
	void create() override;

	virtual Rml::Element* get_entry(Rml::Element *target_el);
	virtual Rml::Element* get_entry(Rml::Event &_ev);
	virtual void entry_select(Rml::Element *_entry_el);
	virtual void entry_deselect();

	virtual void set_zoom(int _amount);
	virtual void set_mode(std::string _mode);
	virtual std::string get_mode();

	void move_selection(Rml::Input::KeyIdentifier _id);
};


#endif
