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

#include "ibmulator.h"
#include "new_floppy.h"
#include "gui/gui.h"
#include <regex>

event_map_t NewFloppy::ms_evt_map = {
	GUI_EVT( "cancel",      "click",   NewFloppy::on_cancel ),
	GUI_EVT( "close",       "click",   NewFloppy::on_cancel ),
	GUI_EVT( "create_file", "click",   NewFloppy::on_create_file ),
	GUI_EVT( "filename",    "keydown", NewFloppy::on_keydown ),
	GUI_EVT( "dirinfo",     "click",   NewFloppy::on_destdir ),
	GUI_EVT( "*",           "keydown", Window::on_keydown )
};

NewFloppy::NewFloppy(GUI *_gui)
:
Window(_gui, "new_floppy.rml")
{
}

NewFloppy::~NewFloppy()
{
}

void NewFloppy::set_compat_sizes(std::vector<uint64_t> _sizes)
{
	m_compat_sizes = _sizes;
	assert(m_type_el);
	m_type_el->RemoveAll();
	std::string options;
	std::sort(_sizes.begin(), _sizes.end(), std::greater<uint64_t>());
	for(auto size : _sizes) {
		switch(size) {
			case FLOPPY_160K_BYTES: m_type_el->Add("5.25\" 160K", "FLOPPY_160K"); break;
			case FLOPPY_180K_BYTES: m_type_el->Add("5.25\" 180K", "FLOPPY_180K"); break;
			case FLOPPY_320K_BYTES: m_type_el->Add("5.25\" 320K", "FLOPPY_320K"); break;
			case FLOPPY_360K_BYTES: m_type_el->Add("5.25\" 360K", "FLOPPY_360K"); break;
			case FLOPPY_1_2_BYTES : m_type_el->Add("5.25\" 1.2M", "FLOPPY_1_2" ); break;
			case FLOPPY_720K_BYTES: m_type_el->Add("3.5\" 720K" , "FLOPPY_720K"); break;
			case FLOPPY_1_44_BYTES: m_type_el->Add("3.5\" 1.44M", "FLOPPY_1_44"); break;
			case FLOPPY_2_88_BYTES: m_type_el->Add("3.5\" 2.88M", "FLOPPY_2_88"); break;
			default: continue;
		}
	}
}

void NewFloppy::show()
{
	auto heredir = get_element("heredir");
	auto mediadir = get_element("mediadir");
	if(m_cwd.empty()) {
		heredir->SetAttribute("disabled", true);
		mediadir->SetAttribute("checked", true);
	} else {
		heredir->RemoveAttribute("disabled");
	}
	if(m_media_dir.empty()) {
		mediadir->SetAttribute("disabled", true);
		heredir->SetAttribute("checked", true);
	} else {
		mediadir->RemoveAttribute("disabled");
	}
	if(m_cwd == m_media_dir) {
		heredir->SetAttribute("checked", true);
		get_element("dirinfo")->SetClass("d-none", true);
	} else {
		get_element("dirinfo")->SetClass("d-none", false);
	}
	if(mediadir->GetAttribute("checked")) {
		m_dest_dir = m_media_dir;
	} else {
		m_dest_dir = m_cwd;
	}

	Window::show();
	m_filename_el->Focus();
}

void NewFloppy::create()
{
	Window::create();
	m_filename_el = dynamic_cast<Rml::ElementFormControlInput*>(get_element("filename"));
	m_type_el = dynamic_cast<Rml::ElementFormControlSelect*>(get_element("floppy_type"));
	m_formatted_el = dynamic_cast<Rml::ElementFormControl*>(get_element("formatted"));
	m_create_el = get_element("create_file");
	get_element("heredir")->SetAttribute("checked", true);
}

void NewFloppy::on_destdir(Rml::Event &_ev)
{
	std::string value = Window::get_form_input_value(_ev);
	if(value == "here") {
		m_dest_dir = m_cwd;
	} else if(value == "media") {
		m_dest_dir = m_media_dir;
	}
}

void NewFloppy::on_create_file(Rml::Event &)
{
	std::string filename = str_trim(m_filename_el->GetValue());
	filename.erase(filename.find_last_not_of(".") + 1);

	if(filename.empty()) {
		return;
	}
	if(m_create_clbk) {
		try {
			std::map<std::string, FloppyDiskType> types = {
				{ "FLOPPY_160K" , FLOPPY_160K },
				{ "FLOPPY_180K" , FLOPPY_180K },
				{ "FLOPPY_320K" , FLOPPY_320K },
				{ "FLOPPY_360K" , FLOPPY_360K },
				{ "FLOPPY_1_2"  , FLOPPY_1_2  },
				{ "FLOPPY_720K" , FLOPPY_720K },
				{ "FLOPPY_1_44" , FLOPPY_1_44 },
				{ "FLOPPY_2_88" , FLOPPY_2_88 }
			};
			m_create_clbk(m_dest_dir, filename, types[m_type_el->GetValue()],
					m_formatted_el->GetAttribute("checked"));
		} catch(std::runtime_error &e) {
			m_gui->show_message_box("Error",
					str_to_html(e.what()), MessageWnd::Type::MSGW_OK,
			[=](){
				m_filename_el->Focus();
			});
			return;
		}
	}
	hide();
}

void NewFloppy::on_cancel(Rml::Event &_ev)
{
	if(m_cancel_callbk) {
		m_cancel_callbk();
	}
	Window::on_cancel(_ev);
}

void NewFloppy::on_keydown(Rml::Event &_ev)
{
	switch(get_key_identifier(_ev)) {
		case Rml::Input::KeyIdentifier::KI_RETURN:
			if(_ev.GetTargetElement() == m_filename_el) {
				on_create_file(_ev);
			}
			break;
		default:
			Window::on_keydown(_ev);
			break;
	}
}


