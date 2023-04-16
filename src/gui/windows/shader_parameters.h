/*
 * Copyright (C) 2023  Marco Bortolin
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

#ifndef IBMULATOR_SHADER_PARAMETERS_H
#define IBMULATOR_SHADER_PARAMETERS_H

#include "../window.h"
#include <RmlUi/Core.h>
#include "shader_save_info.h"

class ShaderParameters : public Window
{
private:
	static event_map_t ms_evt_map;

	ScreenRenderer * m_renderer;
	ScreenRenderer::ShaderParamsList m_params;
	using ShaderParamsMap = std::map<std::string, size_t>;
	ShaderParamsMap m_params_map;
	
	std::unique_ptr<ShaderSaveInfo> m_save_info;
	
	struct {
		Rml::ElementFormControlInput *search;
	} m_tools = {};
	std::string m_cur_search;
	bool m_do_search = false;
	TimerID m_click_timer = NULL_TIMER_ID;
	std::string m_click_name;
	bool m_click_inc = false;
	float m_focus_value = 0.0;
	std::set<std::string> m_modified;
	
public:
	ShaderParameters(GUI *_gui, ScreenRenderer *_renderer);

	void show();
	void create();
	void update();
	void close();

	void on_search(Rml::Event &_ev);
	void on_reset_all(Rml::Event &_ev);
	void on_keydown(Rml::Event &_ev);
	void on_click(Rml::Event &_ev);
	void on_mousedown(Rml::Event &_ev);
	void on_value_focus(Rml::Event &_ev);
	void on_value_keydown(Rml::Event &_ev);
	void on_save(Rml::Event &_ev);
	event_map_t & get_event_map() { return ShaderParameters::ms_evt_map; }

private:
	void increase_value(std::string _name);
	void decrease_value(std::string _name);
	void reset_value(std::string _name);
	void update_value(ScreenRenderer::ShaderParam &, float _new_value);
};



#endif
