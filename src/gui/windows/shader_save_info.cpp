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
#include "shader_save_info.h"
#include "filesys.h"
#include "gui.h"
#include "program.h"

event_map_t ShaderSaveInfo::ms_evt_map = {
	GUI_EVT( "cancel", "click", ShaderSaveInfo::on_cancel ),
	GUI_EVT( "close", "click", ShaderSaveInfo::on_cancel ),
	GUI_EVT( "save", "click", ShaderSaveInfo::on_save ),
	GUI_EVT( "shader_name", "keydown", ShaderSaveInfo::on_keydown ),
	GUI_EVT( "*", "keydown", Window::on_keydown )
};

ShaderSaveInfo::ShaderSaveInfo(GUI *_gui)
:
Window(_gui, "shader_save_info.rml")
{
}

void ShaderSaveInfo::create()
{
	Window::create();
	m_el.name = dynamic_cast<Rml::ElementFormControl*>(get_element("shader_name"));
	m_el.save_all = dynamic_cast<Rml::ElementFormControl*>(get_element("save_all"));
	m_el.add_comments = dynamic_cast<Rml::ElementFormControl*>(get_element("add_comments"));
	
	//TODO let user pick the dir? maybe not.
	auto *dir = get_element("directory");
	dir->SetInnerRML(g_program.config().get_users_shaders_path());
	Window::set_disabled(dir, true);
}

void ShaderSaveInfo::show()
{
	m_el.name->SetValue(values.name);
	if(values.save_all) {
		m_el.save_all->SetAttribute("checked", true);
	}
	if(values.add_comments) {
		m_el.add_comments->SetAttribute("checked", true);
	}

	Window::show();
	m_el.name->Focus();
}

void ShaderSaveInfo::set_shader_path(std::string _path)
{
	std::string dir,base,ext;
	FileSys::get_path_parts(_path.c_str(), dir, base, ext);
	values.name = base + ext;
}

void ShaderSaveInfo::on_save(Rml::Event &)
{
	values.name = m_el.name->GetValue();
	values.save_all = m_el.save_all->GetAttribute("checked") != nullptr;
	values.add_comments = m_el.add_comments->GetAttribute("checked") != nullptr;

	if(m_save_callbk) {
		m_save_callbk();
	}

	hide();
}

void ShaderSaveInfo::on_cancel(Rml::Event &_ev)
{
	if(m_cancel_callbk) {
		m_cancel_callbk();
	}
	Window::on_cancel(_ev);
}

void ShaderSaveInfo::on_keydown(Rml::Event &_ev)
{
	switch(get_key_identifier(_ev)) {
		case Rml::Input::KeyIdentifier::KI_RETURN:
		case Rml::Input::KeyIdentifier::KI_NUMPADENTER:
			if(_ev.GetTargetElement() == m_el.name) {
				on_save(_ev);
				break;
			}
			[[fallthrough]];
		default:
			Window::on_keydown(_ev);
			return;
	}
	_ev.StopImmediatePropagation();
}


