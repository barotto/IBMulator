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

#include "ibmulator.h"
#include "gui/gui.h"
#include "shader_parameters.h"
#include "program.h"

event_map_t ShaderParameters::ms_evt_map = {
	GUI_EVT("close", "click", Window::on_cancel),
	GUI_EVT("cancel", "click", Window::on_cancel),
	GUI_EVT("search", "keydown", ShaderParameters::on_search),
	GUI_EVT("reset_all", "click", ShaderParameters::on_reset_all),
	GUI_EVT("*", "mousedown", ShaderParameters::on_mousedown),
	GUI_EVT("*", "click", ShaderParameters::on_click),
	GUI_EVT("*", "keydown", ShaderParameters::on_keydown),
	GUI_EVT("class:value", "focus", ShaderParameters::on_value_focus),
	GUI_EVT("class:value", "blur", ShaderParameters::on_value_focus),
	GUI_EVT("class:value", "keydown", ShaderParameters::on_value_keydown),
	GUI_EVT("save", "click", ShaderParameters::on_save)
};

ShaderParameters::ShaderParameters(GUI *_gui, ScreenRenderer *_renderer)
:
Window(_gui, "shader_parameters.rml"),
m_renderer(_renderer),
m_params(*_renderer->get_shader_params())
{
}

void ShaderParameters::show()
{
	Window::show();
}

void ShaderParameters::close()
{
	if(m_save_info) {
		m_save_info->close();
		m_save_info.reset(nullptr);
	}
	Window::close();
}

void ShaderParameters::create()
{
	Window::create();

	auto container = get_element("params_cnt");
	
	if(m_params.empty()) {
		container->SetInnerRML("This shader has no parameters.");
		Window::set_disabled(get_element("search"), true);
		//get_element("search_cnt")->SetClass("invisible", true);
		Window::set_disabled(get_element("save"), true);
	} else {
		for(size_t idx=0; idx<m_params.size(); idx++) {
			Rml::ElementPtr child = m_wnd->CreateElement("div");
			child->SetClassNames("entry");
			child->SetId(m_params[idx].name);
			std::string inner;
			inner += "<div class=\"desc\">" + m_params[idx].desc + "</div>";
			if(m_params[idx].used) {
				if(m_params[idx].step != 0.0) {
					inner += "<button id=\"" + m_params[idx].name + "__dec\" class=\"romshell decrease\"><span>-</span></button>";
				}
				//inner += "<div class=\"value\" id=\"" + m_params[idx].name + "__value\">" + str_format("%g", m_params[idx].value) + "</div>";
				inner += "<input type=\"text\" class=\"value\" id=\"" + m_params[idx].name + "__value\" value=\"\" >"+ m_params[idx].get_value_str() +"</input>";
				if(m_params[idx].step != 0.0) {
					inner += "<button id=\"" + m_params[idx].name + "__inc\" class=\"romshell increase\"><span>+</span></button>";
				}
				m_params_map[m_params[idx].name] = idx;
			}
			child->SetInnerRML(inner);
			container->AppendChild(std::move(child));
			if(m_params[idx].used && m_params[idx].step != 0.0) {
				auto *inc = container->GetElementById(m_params[idx].name + "__inc");
				auto *dec = container->GetElementById(m_params[idx].name + "__dec");
				Window::set_disabled(inc, m_params[idx].value >= m_params[idx].max);
				Window::set_disabled(dec, m_params[idx].value <= m_params[idx].min);
			}
		}
	}

	m_tools.search = dynamic_cast<Rml::ElementFormControlInput*>(get_element("search"));

	m_save_info = std::make_unique<ShaderSaveInfo>(m_gui);
	m_save_info->create();
	m_save_info->set_modal(true);

	m_click_timer = m_gui->timers().register_timer([this](uint64_t){
		if(m_click_name.empty()) {
			m_gui->timers().deactivate_timer(m_click_timer);
		} else {
			if(m_click_inc) {
				increase_value(m_click_name);
			} else {
				decrease_value(m_click_name);
			}
		}
	}, "Shader Parameter click");
}

void ShaderParameters::update()
{
	Window::update();
	if(m_do_search) {
		auto search_for = str_trim(m_tools.search->GetValue());
		if(search_for == m_cur_search) {
			return;
		}
		m_cur_search = search_for;
		if(search_for.empty()) {
			for(auto & p : m_params) {
				get_element(p.name)->SetClass("d-none", false);
			}
			return;
		}
		for(auto & p : m_params) {
			if(str_find_ci(p.desc, search_for) != p.desc.end()) {
				get_element(p.name)->SetClass("d-none", false);
			} else {
				get_element(p.name)->SetClass("d-none", true);
			}
		}
		m_do_search = false;
	}
}

void ShaderParameters::update_value(ScreenRenderer::ShaderParam &_param, float _new_value)
{
	auto *cnt = get_element(_param.name);

	if(_new_value != _param.value) {
		m_renderer->set_shader_param(_param.name, _new_value);
		_param.value = _new_value;
		auto *vel = cnt->GetElementById(_param.name + "__value");
		assert(vel);
		if(m_wnd->GetFocusLeafNode() != vel) {
			vel->SetInnerRML(_param.get_value_str());
		}
	}
	
	auto *reset = cnt->GetElementById(_param.name + "__rst");
	if(!reset && (_new_value != _param.prev_value)) {
		Rml::ElementPtr btn = create_button();
		btn->SetClassNames("romshell reset");
		btn->SetId(_param.name + "__rst");
		btn->SetInnerRML("<span>R</span>");
		if(_param.step != 0) {
			cnt->InsertBefore(std::move(btn), cnt->GetElementById(_param.name + "__dec"));
		} else {
			cnt->InsertBefore(std::move(btn), cnt->GetElementById(_param.name + "__value"));
		}
		m_modified.insert(_param.name);
	} else if(reset && (_new_value == _param.prev_value)) {
		get_element(_param.name)->RemoveChild(reset);
		m_modified.erase(_param.name);
	}
	if(m_modified.empty()) {
		get_element("reset_all")->SetClass("invisible", true);
	} else {
		get_element("reset_all")->SetClass("invisible", false);
	}
	if(_param.step != 0.0) {
		auto *inc = cnt->GetElementById(_param.name + "__inc");
		auto *dec = cnt->GetElementById(_param.name + "__dec");
		Window::set_disabled(inc, _param.value >= _param.max);
		Window::set_disabled(dec, _param.value <= _param.min);
	}
}

void ShaderParameters::reset_value(std::string _name)
{
	auto &param = m_params[m_params_map[_name]];
	update_value(param, param.prev_value);
}

void ShaderParameters::ShaderParameters::on_reset_all(Rml::Event &)
{
	for(auto & param : m_params) {
		if(param.value != param.prev_value) {
			update_value(param, param.prev_value);
		}
	}
}

void ShaderParameters::increase_value(std::string _name)
{
	auto &param = m_params[m_params_map[_name]];
	float value = param.value + param.step;
	value = std::min(value, param.max);
	update_value(param, value);
}

void ShaderParameters::decrease_value(std::string _name)
{
	auto &param = m_params[m_params_map[_name]];
	float value = param.value - param.step;
	value = std::max(value, param.min);
	update_value(param, value);
}

void ShaderParameters::ShaderParameters::on_search(Rml::Event &_ev)
{
	Window::on_keydown(_ev);
	m_do_search = true;
}

void ShaderParameters::on_value_focus(Rml::Event &_ev)
{
	auto *el = _ev.GetTargetElement();
	auto &id = el->GetId();

	assert(id.size() > 7);
	auto name = id.substr(0, id.size()-7);
	auto input = dynamic_cast<Rml::ElementFormControlInput*>(el);
	if(!input) {
		assert(false);
		return;
	}
	auto &param = m_params[m_params_map[name]];
	if(_ev.GetId() == Rml::EventId::Focus) {
		el->SetInnerRML("");
		input->SetValue(param.get_value_str());
		m_focus_value = param.value;
	} else {
		el->SetInnerRML(param.get_value_str());
		input->SetValue("");
	}
	_ev.StopImmediatePropagation();
}

void ShaderParameters::on_value_keydown(Rml::Event &_ev)
{
	auto key = get_key_identifier(_ev);
	auto *el = _ev.GetTargetElement();
	auto &id = el->GetId();

	if(key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER) {
		assert(id.size() > 7);
		auto name = id.substr(0, id.size()-7);
		auto &param = m_params[m_params_map[name]];
		auto input = dynamic_cast<Rml::ElementFormControlInput*>(el);
		if(!input) {
			assert(false);
			return;
		}
		float value = 0.0;
		try {
			value = str_parse_real_num(input->GetValue());
			value = std::min(value, param.max);
			value = std::max(value, param.min);
			update_value(param, value);
		} catch(...) {}
		input = dynamic_cast<Rml::ElementFormControlInput*>(el);
		if(!input) {
			assert(false);
			return;
		}
		input->SetValue(param.get_value_str());
		_ev.StopImmediatePropagation();
	} else if(key == Rml::Input::KI_ESCAPE) {
		el->GetParentNode()->Focus();
		_ev.StopImmediatePropagation();
	}
}

void ShaderParameters::on_save(Rml::Event &)
{
	if(Window::is_disabled(get_element("save"))) {
		return;
	}
	m_save_info->set_callbacks(
		[=]()
		{
			if(m_save_info->values.name.empty()) {
				return;
			}
			auto *src_preset = m_renderer->get_vga_shader_preset();
			auto src_preset_path = src_preset->get_path();
			auto assets_dir = g_program.config().get_assets_shaders_path();
			auto user_dir = g_program.config().get_users_shaders_path();

			try {
				FileSys::create_dir(user_dir.c_str());
			} catch(std::exception &) {
				m_gui->show_error_message_box("Cannot create the user's shaders directory.");
				return;
			}

			std::string reference;
			if(src_preset_path.find(assets_dir) == 0) {
				reference = src_preset_path.substr(assets_dir.size());
			} else if(src_preset_path.find(user_dir) == 0) {
				reference = src_preset_path.substr(user_dir.size());
			} else {
				// ????
				return;
			}


			if(FileSys::is_absolute(m_save_info->values.name.c_str(), m_save_info->values.name.size())) {
				m_gui->show_error_message_box("Cannot use absolute paths.");
				return;
			}

			std::string preset_path = user_dir + m_save_info->values.name;
			std::string preset_dir,preset_base,preset_ext;
			if(!FileSys::get_path_parts(preset_path.c_str(), preset_dir,preset_base,preset_ext)) {
				m_gui->show_error_message_box("The destination directory is not valid.");
				return;
			}

			preset_dir += FS_SEP;
			if(preset_dir.find(user_dir) != 0) {
				m_gui->show_error_message_box("Cannot save the preset outside the shaders directory.");
				return;
			}

			std::string preset_name = preset_base + ".slangp";
			preset_path = preset_dir + preset_name;

			if(reference == preset_name) {
				m_gui->show_error_message_box("Cannot use the same name as the reference preset.");
				return;
			}

			PINFOF(LOG_V0, LOG_GUI, "Saving shader preset '%s'\n", preset_path.c_str());
			auto file = FileSys::make_file(preset_path.c_str(), "wb");
			if(!file) {
				m_gui->show_error_message_box("Cannot create the preset file.");
				return;
			}
			try {
				ShaderPreset::write_reference(file.get(), reference);
			} catch(std::runtime_error &e) {
				m_gui->show_error_message_box(e.what());
				return;
			}
			for(auto & param : m_params) {
				if(!m_save_info->values.save_all && m_modified.find(param.name) == m_modified.end()) {
					continue;
				}
				try {
					if(m_save_info->values.add_comments) {
						ShaderPreset::write_comment(file.get(), param.desc);
					}
					ShaderPreset::write_parameter(file.get(), param.name, param.value);
				} catch(std::runtime_error &e) {
					m_gui->show_error_message_box(e.what());
					return;
				}
			}
		}
	);
	m_save_info->set_shader_path(m_renderer->get_vga_shader_preset()->get_path());
	m_save_info->show();
}

void ShaderParameters::on_mousedown(Rml::Event &_ev)
{
	auto *el = get_button_element(_ev);
	if(!el) {
		return;
	}
	auto &id = el->GetId();
	bool is_inc = id.find("__inc") != id.npos;
	bool is_dec = id.find("__dec") != id.npos;
	if(is_inc || is_dec) {
		assert(id.size() > 5);
		m_click_name = id.substr(0, id.size()-5);
		m_click_inc = is_inc;
		m_gui->timers().activate_timer(m_click_timer, 500_ms, 50_ms, true);
		if(is_inc) {
			increase_value(m_click_name);
		} else if(is_dec) {
			decrease_value(m_click_name);
		}
		_ev.StopImmediatePropagation();
	}
}

void ShaderParameters::on_click(Rml::Event &_ev)
{
	m_gui->timers().deactivate_timer(m_click_timer);
	m_click_name.clear();

	auto &id = _ev.GetTargetElement()->GetId();
	bool is_rst = id.find("__rst") != id.npos;
	if(is_rst) {
		assert(id.size() > 5);
		auto name = id.substr(0, id.size()-5);
		reset_value(name);
		_ev.StopImmediatePropagation();
	}
}

void ShaderParameters::on_keydown(Rml::Event &_ev)
{
	auto key = get_key_identifier(_ev);
	auto &id = _ev.GetTargetElement()->GetId();
	bool is_inc = id.find("__inc") != id.npos;
	bool is_dec = id.find("__dec") != id.npos;
	if((is_inc || is_dec) && (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER)) {
		assert(id.size() > 5);
		auto name = id.substr(0, id.size()-5);
		if(is_inc) {
			increase_value(name);
		} else {
			decrease_value(name);
		}
		_ev.StopImmediatePropagation();
		return;
	}
	Window::on_keydown(_ev);
}