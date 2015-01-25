/*
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ibmulator.h"
#include "gui.h"
#include "machine.h"
#include "program.h"
#include <sys/stat.h>

#include <Rocket/Core.h>
#include "hardware/devices/floppy.h"

event_map_t Interface::ms_evt_map = {
	GUI_EVT( "power",     "click", Interface::on_power ),
	GUI_EVT( "pause",     "click", Interface::on_pause ),
	GUI_EVT( "save",      "click", Interface::on_save ),
	GUI_EVT( "restore",   "click", Interface::on_restore ),
	GUI_EVT( "exit",      "click", Interface::on_exit ),
	GUI_EVT( "fdd_select","click", Interface::on_fdd_select ),
	GUI_EVT( "fdd_eject", "click", Interface::on_fdd_eject ),
	GUI_EVT( "fdd_mount", "click", Interface::on_fdd_mount )
};

Interface::Interface(Machine *_machine, GUI * _gui)
:
Window(_gui, "interface.rml")
{
	ASSERT(m_wnd);
	m_machine = _machine;

	m_wnd->AddEventListener("click", this, false);

	m_sysunit = get_element("sysunit");

	m_buttons.power = get_element("power");
	m_buttons.pause = get_element("pause");
	m_buttons.fdd_select = get_element("fdd_select");

	if(g_program.config().get_string(DRIVES_SECTION,DRIVES_FDD_B).compare("none") == 0) {
		m_buttons.fdd_select->SetProperty("visibility", "hidden");
		m_drive_b = false;
	} else {
		m_drive_b = true;
	}
	m_warning = get_element("warning");

	m_status.fdd_led = get_element("fdd_led");
	m_status.hdd_led = get_element("hdd_led");
	m_status.fdd_disk = get_element("fdd_disk");

	m_leds.power = false;
	m_leds.pause = false;
	m_leds.fdd = false;
	m_leds.hdd = false;
	m_curr_drive = 0;

	m_floppy_present = g_program.config().get_bool(DISK_A_SECTION,DISK_INSERTED);
	m_sysunit->SetClass("disk", m_floppy_present);
	if(m_floppy_present) {
		update_floppy_disk(g_program.config().get_file(DISK_A_SECTION,DISK_PATH, false));
	}
	m_floppy_changed = g_floppy.get_disk_changed(0);

	m_gui_mode = g_program.config().get_enum(GUI_SECTION, GUI_MODE, GUI::ms_gui_modes);

	m_fs = new FileSelect(_gui);
	m_fs->set_select_callbk(std::bind(&Interface::on_floppy_mount, this,
			std::placeholders::_1, std::placeholders::_2));
	m_fs->set_cancel_callbk(NULL);
}

Interface::~Interface()
{
	delete m_fs;
}

void Interface::update_floppy_disk(std::string _filename)
{
	size_t pos = _filename.rfind(FS_SEP);
	if(pos!=string::npos) {
		_filename = _filename.substr(pos+1);
	}
	m_status.fdd_disk->SetInnerRML(_filename.c_str());
}

void Interface::on_floppy_mount(std::string _img_path, bool _write_protect)
{
	struct stat sb;
	if((stat(_img_path.c_str(), &sb) != 0) || S_ISDIR(sb.st_mode)) {
		PERRF(LOG_GUI, "unable to read '%s'\n", _img_path.c_str());
		return;
	}

	if(m_drive_b) {
		//check if the same image file is already mounted on drive A
		const char *section = m_curr_drive?DISK_A_SECTION:DISK_B_SECTION;
		if(g_program.config().get_bool(section,DISK_INSERTED)) {
			std::string other = g_program.config().get_file(section,DISK_PATH, false);
			struct stat other_s;
			if(stat(other.c_str(), &other_s) == 0) {
				if(other_s.st_dev == sb.st_dev && other_s.st_ino == sb.st_ino) {
					PERRF(LOG_GUI, "can't mount '%s' on drive %s because is already mounted on drive %s\n",
							_img_path.c_str(), m_curr_drive?"A":"B", m_curr_drive?"B":"A");
					return;
				}
			}
		}
	}

	uint type;
	switch(sb.st_size) {
		case 160*1024:
			type = FLOPPY_160K;
			break;
		case 180*1024:
			type = FLOPPY_180K;
			break;
		case 320*1024:
			type = FLOPPY_320K;
			break;
		case 360*1024:
			type = FLOPPY_360K;
			break;
		case 1200*1024:
			type = FLOPPY_1_2;
			break;
		case 720*1024:
			type = FLOPPY_720K;
			break;
		case 1440*1024:
			type = FLOPPY_1_44;
			break;
		default:
			PERRF(LOG_GUI, "unable to determine the type of '%s'\n", _img_path.c_str());
			return;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "mounting '%s' on floppy %s %s\n", _img_path.c_str(),
			m_curr_drive?"B":"A", _write_protect?"(write protected)":"");
	m_machine->cmd_insert_media(m_curr_drive, type, _img_path, _write_protect);
	m_fs->hide();
}

void Interface::update_size(uint _width, uint _height)
{
	if(m_gui_mode==GUI_MODE_NORMAL) {
		char buf[10];
		snprintf(buf, 10, "%upx", _width);
		m_sysunit->SetProperty("width", buf);
		snprintf(buf, 10, "%upx", _height);
		m_sysunit->SetProperty("height", buf);
	}
}

void Interface::update()
{
	bool motor = g_floppy.get_motor_enable(m_curr_drive);
	if(motor && m_leds.fdd==false) {
		m_leds.fdd = true;
		m_status.fdd_led->SetClass("active", true);
	} else if(!motor && m_leds.fdd==true) {
		m_leds.fdd = false;
		m_status.fdd_led->SetClass("active", false);
	}
	/* TODO
	bool act = g_harddrive.get_access_activity();
	if(act && m_leds.hdd==false) {
		m_leds.hdd = true;
		m_status.hdd_led->SetClass("active", true);
	} else if(!act && m_leds.hdd==true) {
		m_leds.hdd = false;
		m_status.hdd_led->SetClass("active", false);
	}
	*/

	bool present = g_floppy.is_media_present(m_curr_drive);
	bool changed = g_floppy.get_disk_changed(m_curr_drive);
	if(present && (m_floppy_present==false || m_floppy_changed!=changed)) {
		m_floppy_changed = changed;
		m_floppy_present = true;
		m_sysunit->SetClass("disk", true);
		const char *section = m_curr_drive?DISK_B_SECTION:DISK_A_SECTION;
		update_floppy_disk(g_program.config().get_file(section,DISK_PATH,false));
	} else if(!present && m_floppy_present==true) {
		m_floppy_present = false;
		m_sysunit->SetClass("disk", false);
		m_status.fdd_disk->SetInnerRML("");
	}
	if(m_machine->is_on() && m_leds.power==false) {
		m_leds.power = true;
		m_buttons.power->SetClass("active", true);
	} else if(!m_machine->is_on() && m_leds.power==true) {
		m_leds.power = false;
		m_buttons.power->SetClass("active", false);
	}
	if(m_machine->is_paused() && m_leds.pause==false) {
		m_leds.pause = true;
		m_buttons.pause->SetClass("resume", true);
	} else if(!m_machine->is_paused() && m_leds.pause==true){
		m_leds.pause = false;
		m_buttons.pause->SetClass("resume", false);
	}
}

void Interface::on_power(RC::Event &)
{
	m_machine->cmd_switch_power();
	m_machine->cmd_resume();
}

void Interface::on_pause(RC::Event &)
{
	if(m_machine->is_paused()) {
		m_machine->cmd_resume();
	} else {
		m_machine->cmd_pause();
	}
}

void Interface::on_save(RC::Event &)
{
	//TODO file select window to choose the destination
	g_program.save_state("");
}

void Interface::on_restore(RC::Event &)
{
	//TODO file select window to choose the source
	g_program.restore_state("");
}

void Interface::on_exit(RC::Event &)
{
	g_program.stop();
}

void Interface::on_fdd_select(RC::Event &)
{
	m_status.fdd_disk->SetInnerRML("");
	if(m_curr_drive == 0) {
		m_curr_drive = 1;
		m_floppy_changed = g_floppy.get_disk_changed(1);
		m_buttons.fdd_select->SetClass("a", false);
		m_buttons.fdd_select->SetClass("b", true);
		if(g_program.config().get_bool(DISK_B_SECTION,DISK_INSERTED)) {
			update_floppy_disk(g_program.config().get_file(DISK_B_SECTION,DISK_PATH, false));
		}
	} else {
		m_curr_drive = 0;
		m_floppy_changed = g_floppy.get_disk_changed(0);
		m_buttons.fdd_select->SetClass("a", true);
		m_buttons.fdd_select->SetClass("b", false);
		if(g_program.config().get_bool(DISK_A_SECTION,DISK_INSERTED)) {
			update_floppy_disk(g_program.config().get_file(DISK_A_SECTION,DISK_PATH, false));
		}
	}
}

void Interface::on_fdd_eject(RC::Event &)
{
	m_machine->cmd_eject_media(m_curr_drive);
}

void Interface::on_fdd_mount(RC::Event &)
{
	std::string floppy_dir;

	if(m_curr_drive==0) {
		floppy_dir = g_program.config().get_file(DISK_A_SECTION, DISK_PATH, false);
	} else {
		floppy_dir = g_program.config().get_file(DISK_B_SECTION, DISK_PATH, false);
	}

	if(!floppy_dir.empty()) {
		size_t pos = floppy_dir.rfind(FS_SEP);
		if(pos == string::npos) {
			floppy_dir = "";
		} else {
			floppy_dir = floppy_dir.substr(0,pos);
		}
	}
	if(floppy_dir.empty()) {
		floppy_dir = g_program.config().get_file(PROGRAM_SECTION, PROGRAM_MEDIA_DIR, false);
		if(floppy_dir.empty()) {
			floppy_dir = g_program.config().get_cfg_home();
		}
	}
	try {
		m_fs->set_current_dir(floppy_dir);
	} catch(std::exception &e) {
		return;
	}
	m_fs->show();
}

void Interface::show_warning(bool _show)
{
	if(_show) {
		m_warning->SetProperty("visibility", "visible");
	} else {
		m_warning->SetProperty("visibility", "hidden");
	}
}
