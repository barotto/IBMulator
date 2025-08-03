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

#include "ibmulator.h"
#include "new_floppy.h"
#include "gui/gui.h"

const std::map<std::string, FloppyDisk::StdType> NewFloppy::std_enums = {
	{ "FLOPPY_NONE", FloppyDisk::FD_NONE },
	{ "FLOPPY_160K", FloppyDisk::DD_160K },
	{ "FLOPPY_180K", FloppyDisk::DD_180K },
	{ "FLOPPY_320K", FloppyDisk::DD_320K },
	{ "FLOPPY_360K", FloppyDisk::DD_360K },
	{ "FLOPPY_720K", FloppyDisk::DD_720K },
	{ "FLOPPY_1_20", FloppyDisk::HD_1_20 },
	{ "FLOPPY_1_44", FloppyDisk::HD_1_44 },
	// { "FLOPPY_1_68", FloppyDisk::HD_1_68 }, not available in raw floppy controller, so don't present as options
	// { "FLOPPY_1_72", FloppyDisk::HD_1_72 },
	{ "FLOPPY_2_88", FloppyDisk::ED_2_88 }
};

const std::map<FloppyDisk::StdType, std::string> NewFloppy::std_names = {
	{ FloppyDisk::FD_NONE, "FLOPPY_NONE" },
	{ FloppyDisk::DD_160K, "FLOPPY_160K" },
	{ FloppyDisk::DD_180K, "FLOPPY_180K" },
	{ FloppyDisk::DD_320K, "FLOPPY_320K" },
	{ FloppyDisk::DD_360K, "FLOPPY_360K" },
	{ FloppyDisk::DD_720K, "FLOPPY_720K" },
	{ FloppyDisk::HD_1_20, "FLOPPY_1_20" },
	{ FloppyDisk::HD_1_44, "FLOPPY_1_44" },
	// { FloppyDisk::HD_1_68, "FLOPPY_1_68" },
	// { FloppyDisk::HD_1_72, "FLOPPY_1_72" },
	{ FloppyDisk::ED_2_88, "FLOPPY_2_88" }
};

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

void NewFloppy::set_compat_types(std::vector<unsigned> _ctypes,
		const std::vector<std::unique_ptr<FloppyFmt>> &_formats)
{
	assert(m_type_el);
	m_type_el->RemoveAll();
	std::string options;
	// use reverse iterators so bigger floppies are shown first
	auto it = FloppyDisk::std_types.rbegin();
	while(it != FloppyDisk::std_types.rend()) {
		for(auto ctype : _ctypes) {
			if((it->first & FloppyDisk::DENS_MASK) == (ctype & FloppyDisk::DENS_MASK) &&
			   (it->first & FloppyDisk::SIZE_MASK) == (ctype & FloppyDisk::SIZE_MASK)) {
				try {
					m_type_el->Add(it->second.desc, std_names.at(it->first));
				} catch(std::out_of_range &) {}
				break;
			}
		}
		it++;
	}

	assert(m_format_el);
	m_format_el->RemoveAll();
	for(auto &f : _formats) {
		if(f->can_save()) {
			m_format_el->Add(f->description(), f->name());
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
	m_format_el = dynamic_cast<Rml::ElementFormControlSelect*>(get_element("floppy_format"));
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
	if(m_create_clbk) {
		std::string filename = str_trim(m_filename_el->GetValue());
		filename.erase(filename.find_last_not_of(".") + 1);
		if(filename.empty()) {
			return;
		}
		try {
			auto name = m_type_el->GetValue();
			auto type = std_enums.find(name);
			if(type == std_enums.end()) {
				throw std::runtime_error(str_format("Invalid floppy disk type: %s",name).c_str());
			}
			m_create_clbk(m_dest_dir, filename, type->second, m_format_el->GetValue());
		} catch(std::runtime_error &e) {
			m_gui->show_message_box("Error", e.what(), MessageWnd::Type::MSGW_OK,
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
		case Rml::Input::KeyIdentifier::KI_NUMPADENTER:
			if(_ev.GetTargetElement() == m_filename_el) {
				on_create_file(_ev);
				break;
			}
			[[fallthrough]];
		default:
			Window::on_keydown(_ev);
			return;
	}
	_ev.StopImmediatePropagation();
}


