/*
 * Copyright (C) 2015-2024  Marco Bortolin
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
#include "interface.h"
#include "message_wnd.h"
#include "program.h"
#include "gui/gui.h"
#include "utils.h"
#include <sys/stat.h>
#include "stb/stb.h"

#include "tinyfiledialogs/tinyfiledialogs.h"

#include "hardware/devices/floppyctrl.h"
#include "hardware/devices/storagectrl_ata.h"
using namespace std::placeholders;

#define CDROM_LED_BLINK_TIME 250_ms

Interface::Interface(Machine *_machine, GUI *_gui, Mixer *_mixer, const char *_rml)
:
Window(_gui, _rml),
m_machine(_machine),
m_mixer(_mixer)
{
	_machine->register_floppy_loader_state_cb(
		std::bind(&Interface::floppy_activity_cb, this, _1, _2, nullptr)
	);
}

Interface::~Interface()
{
}

void Interface::close()
{
	remove_drives();

	if(m_fs) {
		m_fs->close();
		m_fs.reset(nullptr);
	}
	if(m_state_save) {
		m_state_save->close();
		m_state_save.reset(nullptr);
	}
	if(m_state_load) {
		m_state_load->close();
		m_state_load.reset(nullptr);
	}
	if(m_state_save_info) {
		m_state_save_info->close();
		m_state_save_info.reset(nullptr);
	}

	Window::close();
}

void Interface::create()
{
	Window::create();

	m_buttons.power = get_element("power");
	m_status.hdd_led = get_element("hdd_led");

	m_speed = get_element("speed");
	m_speed_value = get_element("speed_value");
	m_message = get_element("message");

	m_leds.power = false;

	auto mode = g_program.config().get_string(DIALOGS_SECTION, DIALOGS_FILE_MODE, "grid");
	auto order = g_program.config().get_string(DIALOGS_SECTION, DIALOGS_FILE_ORDER, "name");
	int zoom = g_program.config().get_int(DIALOGS_SECTION, DIALOGS_FILE_ZOOM, 2);
	m_fs = std::make_unique<FileSelect>(m_gui);
	m_fs->create(mode, order, zoom);
	m_fs->set_cancel_callbk(nullptr);
	std::string home_dir = g_program.config().get_file(PROGRAM_SECTION, PROGRAM_MEDIA_DIR, FILE_TYPE_USER);
	std::string cfg_home = g_program.config().get_cfg_home();
	if(home_dir.empty()) {
		home_dir = cfg_home;
	}
	try {
		m_fs->set_home(home_dir);
	} catch(std::runtime_error &) {
		// if still not valid then give up
		m_fs->set_home(cfg_home);
	}

	mode = g_program.config().get_string(DIALOGS_SECTION, DIALOGS_SAVE_MODE, "grid");
	order = g_program.config().get_string(DIALOGS_SECTION, DIALOGS_SAVE_ORDER, "date");
	zoom = g_program.config().get_int(DIALOGS_SECTION, DIALOGS_SAVE_ZOOM, 1);

	m_state_save = std::make_unique<StateSave>(m_gui);
	m_state_save->create(mode, order, zoom);
	m_state_save->set_modal(true);

	m_state_save_info = std::make_unique<StateSaveInfo>(m_gui);
	m_state_save_info->create();
	m_state_save_info->set_modal(true);

	m_state_load = std::make_unique<StateLoad>(m_gui);
	m_state_load->create(mode, order, zoom);
	m_state_load->set_modal(true);

	m_audio_enabled = g_program.config().get_bool(SOUNDFX_SECTION, SOUNDFX_ENABLED);
	if(m_audio_enabled) {
		m_drives_audio.init(m_mixer);
		m_system_audio.init(m_mixer);
	}
}

void Interface::UIDrive::hide()
{
	uidrive_el->SetClass("d-none", true);
}

void Interface::UIDrive::show()
{
	uidrive_el->SetClass("d-none", false);
}

void Interface::UIDrive::set_led(bool _active)
{
	activity_led->SetClass("active", _active);
	if(activity_led_bloom) {
		activity_led_bloom->SetClass("active", _active);
	}
}

void Interface::set_hdd_active(bool _active)
{
	m_leds.hdd = _active;
	m_status.hdd_led->SetClass("active", _active);
}

Interface::UIDriveBlock * Interface::create_uidrive_block(Rml::Element *_uidrive_block_el)
{
	return &m_drive_blocks.emplace_back(_uidrive_block_el);
}

Interface::UIDrive * Interface::UIDriveBlock::create_uidrive(Interface::MachineDrive *_drive,
		Rml::Element *uidrive_el, Rml::Element *activity_led, Rml::Element *activity_led_bloom,
		Rml::Element *medium_string, Rml::Element *drive_select)
{
	UIDrive *uidrive = &uidrives.emplace_back(this, _drive, uidrive_el,
			activity_led, activity_led_bloom, medium_string, drive_select);
	_drive->add_uidrive(uidrive);
	if(curr_drive == uidrives.end()) {
		curr_drive = uidrives.begin();
		uidrive->show();
	} else {
		uidrive->hide();
	}
	block_el->SetClass("many_drives", (uidrives.size() > 1));

	return uidrive;
}

void Interface::MachineDrive::add_uidrive(UIDrive *_uidrive)
{
	uidrives.push_back(_uidrive);
}

void Interface::UIDriveBlock::change_drive()
{
	if(uidrives.size() < 2) {
		return;
	}
	curr_drive->hide();
	curr_drive = std::next(curr_drive);
	if(curr_drive == uidrives.end()) {
		curr_drive = uidrives.begin();
	}
	curr_drive->show();
}

void Interface::UIDriveBlock::change_drive(std::list<UIDrive>::iterator _to_drive)
{
	if(uidrives.size() < 2) {
		return;
	}
	if(curr_drive == _to_drive) {
		return;
	}
	std::list<UIDrive>::iterator uidrive = uidrives.begin();
	for(; uidrive != uidrives.end(); uidrive++) {
		if(uidrive->drive == _to_drive->drive) {
			curr_drive->hide();
			curr_drive = uidrive;
			curr_drive->show();
			break;
		}
	}
}

void Interface::UIDriveBlock::change_drive(std::list<MachineDrive>::iterator _to_drive)
{
	if(uidrives.size() < 2) {
		return;
	}
	if(curr_drive->drive == &*_to_drive) {
		return;
	}
	std::list<UIDrive>::iterator uidrive = uidrives.begin();
	for(; uidrive != uidrives.end(); uidrive++) {
		if(uidrive->drive == &*_to_drive) {
			curr_drive->hide();
			curr_drive = uidrive;
			curr_drive->show();
			break;
		}
	}
}

Interface::MachineDrive * Interface::config_floppy(int _id)
{
	std::lock_guard<std::mutex> lock(m_drives_mutex);

	FloppyCtrl *floppy_ctrl = m_machine->devices().device<FloppyCtrl>();
	if(!floppy_ctrl) {
		PDEBUGF(LOG_V0, LOG_GUI, "Interface::config_floppy(): floppy controller not installed.\n");
		return nullptr;
	}
	if(floppy_ctrl->drive_type(_id) == FloppyDrive::FDD_NONE) {
		return nullptr;
	}

	MachineDrive *drive = &m_drives.emplace_back();
	drive->floppy_ctrl = floppy_ctrl;
	drive->drive_id = _id;
	drive->label = _id ==  0 ? "A" : "B";
	if((floppy_ctrl->drive_type(_id) & FloppyDisk::SIZE_MASK) == FloppyDisk::SIZE_5_25) {
		drive->drive_type = GUIDrivesFX::FDD_5_25;
	} else {
		drive->drive_type = GUIDrivesFX::FDD_3_5;
	}

	floppy_ctrl->register_activity_cb(_id, std::bind(&Interface::floppy_activity_cb, this,
			_1, _2, drive));

	return &m_drives.back();
}

Interface::MachineDrive * Interface::config_cdrom(int _num)
{
	std::lock_guard<std::mutex> lock(m_drives_mutex);

	StorageCtrl *ata = m_machine->devices().device<StorageCtrl_ATA>();
	if(!ata) {
		return nullptr;
	}
	int num = 0;
	for(int id = 0; id < ata->installed_devices(); id++) {
		StorageDev *dev = ata->get_device(id);
		if(dev->category() == StorageDev::DEV_CDROM) {
			if(num == _num) {
				MachineDrive *drive = &m_drives.emplace_back();
				drive->storage_ctrl = ata;
				drive->drive_id = id;
				drive->drive_type = GUIDrivesFX::CDROM;
				drive->cdrom = dynamic_cast<CdRomDrive*>(dev);
				if(m_audio_enabled) {
					drive->cdrom->set_durations(
						m_drives_audio.duration_us(GUIDrivesFX::CDROM, GUIDrivesFX::EJECT),
						m_drives_audio.duration_us(GUIDrivesFX::CDROM, GUIDrivesFX::INSERT)
					);
				}
				assert(drive->cdrom);
				if(_num == 0) {
					drive->label = "CD";
				} else {
					drive->label = str_format("CD%d", _num+1);
				}
				drive->cdrom->register_activity_cb(uintptr_t(this), std::bind(&Interface::cdrom_activity_cb, this,
						std::placeholders::_1, std::placeholders::_2, drive));
				drive->led_on_timer = m_gui->timers().register_timer(std::bind(&Interface::cdrom_led_timer, this, _1, drive),
						str_format("CD-ROM %d LED", _num));
				return &m_drives.back();
			}
			num++;
		}
	}
	return nullptr;
}

void Interface::floppy_activity_cb(FloppyEvents::EventType _what, uint8_t _drive_idx, MachineDrive *_drive)
{
	assert(!m_drives.empty());
	// FDDs: LED turns on when the drive's motor is on
	// 1. the drive sets activity according to motor, 
	// 2. in the update(): if led_activity then LED on, otherwise off
	std::lock_guard<std::mutex> lock(m_drives_mutex);
	MachineDrive *drive = _drive;
	if(!drive) {
		for(auto &d : m_drives) {
			if(d.floppy_ctrl && d.drive_id == _drive_idx) {
				drive = &d;
				break;
			}
		}
	}
	if(drive) {
		switch(_what) {
			case FloppyEvents::EVENT_MOTOR_ON:
				drive->led_activity = 1;
				break;
			case FloppyEvents::EVENT_MOTOR_OFF:
				drive->led_activity = 0;
				break;
			default:
				break;
		}
		drive->floppy_event_type = _what;
		drive->event = true;
	}
}

void Interface::cdrom_activity_cb(CdRomEvents::EventType _what, uint64_t _duration, MachineDrive *_drive)
{
	assert(!m_drives.empty());
	// CD-ROMs: blinking LED with a 0.5s cycle
	// 1. the drive sets led_activity when a sector is read
	// 2. in the update(): LED on, timer is started with a 0.25s timeout 
	// first timer timeout: LED off, timer restart with 0.25s sec
	// second timeout: led_activity reduced by 0.5s, check if there's led_activity, if true repeat
	std::lock_guard<std::mutex> lock(m_drives_mutex);
	if(int64_t(_duration) > _drive->led_activity) {
		_drive->led_activity += _duration;
	} else if(_what == CdRomEvents::POWER_OFF) {
		_drive->led_activity = 0;
	}
	_drive->event = true;
	if(_drive->cd_event_type == CdRomEvents::MEDIUM || _drive->cd_event_type == CdRomEvents::MEDIUM_LOADING)
	{
		// EVENT_MEDIUM* have the lowest priority
		_drive->cd_event_type = _what;
	}
	// this is the Machine thread, don't play sounds here 
}

std::vector<const char*> Interface::MachineDrive::compatible_file_extensions()
{
	if(floppy_ctrl) {
		return floppy_ctrl->get_compatible_file_extensions();
	} else {
		return CdRomDisc::get_compatible_file_extensions();
	}
}

std::vector<unsigned> Interface::MachineDrive::compatible_file_types()
{
	std::vector<unsigned> filetypes;
	switch(drive_type) {
		case GUIDrivesFX::FDD_3_5:
		case GUIDrivesFX::FDD_5_25: {
			FloppyDrive::Type floppy_drive = floppy_ctrl->drive_type(drive_id);
			unsigned dens_drive = (floppy_drive & FloppyDisk::DENS_MASK) >> FloppyDisk::DENS_SHIFT;
			unsigned dens_mask = FloppyDisk::DENS_MASK >> FloppyDisk::DENS_SHIFT;
			unsigned dens_cur = 1;
			while(dens_cur & dens_mask) {
				if(dens_drive & dens_cur) {
					unsigned type_value = (floppy_drive & FloppyDisk::SIZE_MASK) | (dens_cur << FloppyDisk::DENS_SHIFT);
					type_value |= FileSelect::FILE_FLOPPY_DISK;
					filetypes.push_back(type_value);
				}
				dens_cur <<= 1;
			}
			break;
		}
		case GUIDrivesFX::CDROM:
			filetypes.push_back(FileSelect::FILE_OPTICAL_DISC);
			break;
		default:
			break;
	}
	return filetypes;
}

void Interface::config_changing()
{
	std::lock_guard<std::mutex> lock(m_drives_mutex);

	for(auto &block : m_drive_blocks) {
		if(block.block_el) {
			block.block_el->SetInnerRML("");
		}
	}
	m_drive_blocks.clear();

	remove_drives();
}

void Interface::remove_drives()
{
	for(auto &drive : m_drives) {
		if(drive.led_on_timer != NULL_TIMER_ID) {
			m_gui->timers().unregister_timer(drive.led_on_timer);
		}
	}
	m_drives.clear();
	m_curr_drive = m_drives.end();
}

void Interface::config_changed(bool _startup)
{
	m_fs->hide();

	MachineDrive *drive_a = config_floppy(0);
	MachineDrive *drive_b = config_floppy(1);
	MachineDrive *drive_d = config_cdrom(0);

	m_curr_drive = m_drives.begin();

	auto insert_medium = [this](MachineDrive *_drive, const char *_cfg_section) {
		std::string diskpath = g_program.config().find_media(_cfg_section, DISK_PATH);
		if(!diskpath.empty() && g_program.config().get_bool(_cfg_section, DISK_INSERTED)) {
			g_program.config().set_bool(_cfg_section, DISK_INSERTED, false);
			if(!FileSys::file_exists(diskpath.c_str())) {
				PERRF(LOG_GUI, "The image specified in [%s] doesn't exist.\n", _cfg_section);
				return;
			}
			if(FileSys::is_directory(diskpath.c_str())) {
				PERRF(LOG_GUI, "The image specified in [%s] cannot be a directory.\n", _cfg_section);
				return;
			}
			// don't play audio samples here
			if(_drive->cdrom) {
				m_machine->cmd_insert_cdrom(_drive->cdrom, diskpath, nullptr);
			} else {
				bool wp = g_program.config().get_bool(_cfg_section, DISK_READONLY);
				m_machine->cmd_insert_floppy(_drive->drive_id, diskpath, wp, nullptr);
			}
		}
	};

	if(_startup) {
		// media are inserted by the GUI
		if(drive_a) {
			insert_medium(drive_a, DISK_A_SECTION);
		}
		if(drive_b) {
			insert_medium(drive_b, DISK_B_SECTION);
		}
		if(drive_d) {
			insert_medium(drive_d, DISK_CD_SECTION);
		}
	}

	set_hdd_active(false);
	m_hdd = m_machine->devices().device<StorageCtrl>();

	set_video_brightness(g_program.config().get_real(DISPLAY_SECTION, DISPLAY_BRIGHTNESS));
	set_video_contrast(g_program.config().get_real(DISPLAY_SECTION, DISPLAY_CONTRAST));
	set_video_saturation(g_program.config().get_real(DISPLAY_SECTION, DISPLAY_SATURATION));

	bool is_mono = g_program.config().get_enum(DISPLAY_SECTION, DISPLAY_TYPE, {
			{ "color",     false },
			{ "mono",       true },
			{ "monochrome", true }
	}, false);
	m_screen->set_monochrome(is_mono);
	PINFOF(LOG_V0, LOG_GUI, "Installed a %s monitor\n", is_mono?"monochrome":"color");
}

void Interface::UIDrive::set_medium_string(std::string _filename)
{
	if(!_filename.empty()) {
		size_t pos = _filename.rfind(FS_SEP);
		if(pos!=std::string::npos) {
			_filename = _filename.substr(pos+1);
		}
	}
	medium_string->SetInnerRML(_filename.c_str());
}

void Interface::UIDrive::update_medium_status()
{
	if(drive->drive_type == GUIDrivesFX::CDROM) {
		if(drive->cd_event_type == CdRomEvents::MEDIUM_LOADING) {
			set_medium_string("Loading...");
		} else if(g_program.config().get_bool(DISK_CD_SECTION, DISK_INSERTED)) {
			set_medium_string(g_program.config().get_file(DISK_CD_SECTION, DISK_PATH, FILE_TYPE_USER));
		} else {
			set_medium_string("");
		}
		assert(drive->cdrom);
		uidrive_el->SetClass("door_open", drive->cdrom->is_door_open());
	} else {
		switch(drive->floppy_event_type) {
			case FloppyEvents::EVENT_DISK_LOADING:
				uidrive_el->SetClass("with_medium", false);
				set_medium_string("Loading...");
				break;
			case FloppyEvents::EVENT_DISK_SAVING:
				uidrive_el->SetClass("with_medium", false);
				set_medium_string("Saving...");
				break;
			default:
				const char *section = drive->drive_id ? DISK_B_SECTION : DISK_A_SECTION;
				if(g_program.config().get_bool(section, DISK_INSERTED)) {
					uidrive_el->SetClass("with_medium", true);
					set_medium_string(g_program.config().get_file(section, DISK_PATH, FILE_TYPE_USER));
				} else {
					uidrive_el->SetClass("with_medium", false);
					set_medium_string("");
				}
				break;
		}
	}
}

Rml::ElementPtr Interface::create_uidrive_el(MachineDrive *_drive, UIDriveBlock *_block)
{
	/*
	 <div class="uidrive">
		<div class="drive_led"></div>
		<div class="drive_background"></div>
		<div class="drive_led_bloom"></div>
		<div class="drive_select"></div>
		<div class="drive_medium"></div>
		<div class="drive_mount"></div>
		<div class="drive_eject"></div>
	</div>
	 */
	Rml::ElementPtr uidrive_el = m_wnd->CreateElement("div");
	uidrive_el->SetClassNames("uidrive");
	switch(_drive->drive_type) {
		case GUIDrivesFX::FDD_5_25:
			uidrive_el->SetClass("fdd_5_25", true);
			break;
		case GUIDrivesFX::FDD_3_5:
			uidrive_el->SetClass("fdd_3_5", true);
			break;
		case GUIDrivesFX::CDROM:
			uidrive_el->SetClass("cdrom", true);
			break;
		default:
			break;
	}

	Rml::ElementPtr drive_led = m_wnd->CreateElement("div");
	drive_led->SetClassNames("drive_led");
	Rml::ElementPtr drive_background = m_wnd->CreateElement("div");
	drive_background->SetClassNames("drive_background");
	Rml::ElementPtr drive_led_bloom = m_wnd->CreateElement("div");
	drive_led_bloom->SetClassNames("drive_led_bloom");
	Rml::ElementPtr drive_select = m_wnd->CreateElement("div");
	drive_select->SetClassNames(str_format("drive_select %s", _drive->label.c_str()));
	Rml::ElementPtr drive_medium = m_wnd->CreateElement("div");
	drive_medium->SetClassNames("drive_medium");
	Rml::ElementPtr drive_mount = m_wnd->CreateElement("div");
	drive_mount->SetClassNames("drive_mount");
	Rml::ElementPtr drive_eject = m_wnd->CreateElement("div");
	drive_eject->SetClassNames("drive_eject");

	_block->create_uidrive(
		_drive,
		uidrive_el.get(),
		drive_led.get(),
		drive_led_bloom.get(),
		drive_medium.get(),
		drive_select.get()
	);

	register_target_cb(drive_select.get(), "click",
		std::bind(&Interface::on_drive_select, this, _1, _block));
	register_target_cb(drive_mount.get(), "click",
		std::bind(&Interface::on_medium_mount, this, _1, _drive));
	register_target_cb(drive_medium.get(), "click",
		std::bind(&Interface::on_medium_mount, this, _1, _drive));
	register_target_cb(drive_eject.get(), "click",
		std::bind(&Interface::on_medium_button, this, _1, _drive));

	uidrive_el->AppendChild(std::move(drive_led));
	uidrive_el->AppendChild(std::move(drive_background));
	uidrive_el->AppendChild(std::move(drive_led_bloom));
	uidrive_el->AppendChild(std::move(drive_select));
	uidrive_el->AppendChild(std::move(drive_mount));
	uidrive_el->AppendChild(std::move(drive_medium));
	uidrive_el->AppendChild(std::move(drive_eject));

	return uidrive_el;
}

void Interface::on_medium_select(std::string _img_path, bool _write_protect, MachineDrive *_drive)
{
	// called after the user selects an image to mount from the file selector

	m_fs->hide();

	struct stat sb;
	if((FileSys::stat(_img_path.c_str(), &sb) != 0) || S_ISDIR(sb.st_mode)) {
		PERRF(LOG_GUI, "Unable to read '%s'\n", _img_path.c_str());
		return;
	}

	const char *drive_section;
	if(_drive->drive_type == GUIDrivesFX::CDROM) {
		drive_section = DISK_CD_SECTION;
	} else {
		drive_section = _drive->drive_id ? DISK_B_SECTION : DISK_A_SECTION;
	}
	if(g_program.config().get_bool(drive_section, DISK_INSERTED)) {
		if(FileSys::is_same_file(_img_path.c_str(),
				g_program.config().get_file(drive_section, DISK_PATH, FILE_TYPE_USER).c_str())) {
			PINFOF(LOG_V0, LOG_GUI, "The selected floppy image is already mounted\n");
			return;
		}
	}

	if(_drive->floppy_ctrl && _drive->floppy_ctrl->drive_type(1) != FloppyDrive::FDD_NONE) {
		// check if the same image file is already mounted on drive A
		const char *other_section = _drive->drive_id ? DISK_A_SECTION : DISK_B_SECTION;
		if(g_program.config().get_bool(other_section, DISK_INSERTED)) {
			if(FileSys::is_same_file(_img_path.c_str(),
					g_program.config().get_file(other_section, DISK_PATH, FILE_TYPE_USER).c_str())) {
					PERRF(LOG_GUI, "Can't mount '%s' on drive %s because it's already mounted on drive %s\n",
							_img_path.c_str(), _drive->drive_id?"B":"A", _drive->drive_id?"A":"B");
					return;
			}
		}
	}

	if(_drive->drive_type == GUIDrivesFX::CDROM) {
		PDEBUGF(LOG_V1, LOG_GUI, "Mounting '%s' on CD-ROM drive %s\n", _img_path.c_str(), _drive->label.c_str());
		m_machine->cmd_insert_cdrom(_drive->cdrom, _img_path, [](bool){});
	} else {
		PDEBUGF(LOG_V1, LOG_GUI, "Mounting '%s' on floppy %s %s\n", _img_path.c_str(),
				_drive->drive_id?"B":"A", _write_protect?"(write protected)":"");
		m_machine->cmd_eject_floppy(_drive->drive_id, nullptr);
		m_machine->cmd_insert_floppy(_drive->drive_id, _img_path, _write_protect, [=](bool result) {
			// Machine thread here
			// "insert" audio sample plays only when floppy is confirmed inserted
			if(result && m_audio_enabled) {
				m_drives_audio.use_drive(_drive->drive_type, GUIDrivesFX::INSERT);
			}
			floppy_activity_cb(FloppyEvents::EVENT_MEDIUM, -1, _drive);
		});
	}
}

void Interface::cdrom_led_timer(uint64_t, MachineDrive *_drive)
{
	// first timer timeout: LED off, timer restart with 0.25 sec
	if(_drive->led_on) {
		for(auto uidrive : _drive->uidrives) {
			uidrive->set_led(false);
		}
		m_gui->timers().activate_timer(_drive->led_on_timer, CDROM_LED_BLINK_TIME, false);
		_drive->led_on = false;
	} else {
		// second timeout: check the activity, if positive repeat
		std::lock_guard<std::mutex> lock(m_drives_mutex);
		_drive->led_activity -= CDROM_LED_BLINK_TIME * 2;
		if(_drive->led_activity > 0) {
			m_gui->timers().activate_timer(_drive->led_on_timer, CDROM_LED_BLINK_TIME, false);
			for(auto uidrive : _drive->uidrives) {
				uidrive->set_led(true);
			}
			_drive->led_on = true;
		} else {
			_drive->led_activity = 0;
		}
	}
}

void Interface::update()
{
	{ // removable media drives
	std::lock_guard<std::mutex> lock(m_drives_mutex);
	for(auto &drive : m_drives) {
		if(drive.drive_type == GUIDrivesFX::CDROM) {
			// LED on, a timer is started with a 0.25s timeout,
			if(drive.led_activity && !m_gui->timers().is_timer_active(drive.led_on_timer)) {
				for(auto uidrive : drive.uidrives) {
					uidrive->set_led(true);
				}
				m_gui->timers().activate_timer(drive.led_on_timer, CDROM_LED_BLINK_TIME, false);
				drive.led_on = true;
			}
		} else {
			// floppy drive, LED on/off by motor activity
			if(drive.led_on != drive.led_activity) {
				for(auto uidrive : drive.uidrives) {
					uidrive->set_led(drive.led_activity);
				}
				drive.led_on = drive.led_activity;
			}
		}
		if(drive.event) {
			// medium and drive events
			for(auto uidrive : drive.uidrives) {
				if(m_audio_enabled) {
					switch(drive.cd_event_type) {
						case CdRomEvents::DOOR_CLOSING:
							m_drives_audio.use_drive(GUIDrivesFX::CDROM, GUIDrivesFX::INSERT);
							break;
						case CdRomEvents::DOOR_OPENING:
							m_drives_audio.use_drive(GUIDrivesFX::CDROM, GUIDrivesFX::EJECT);
							break;
						default:
							break;
					}
				}
				uidrive->update_medium_status();
			}
			drive.event = false;
			drive.cd_event_type = CdRomEvents::MEDIUM;
			drive.floppy_event_type = FloppyEvents::EVENT_MEDIUM;
		}
	}
	} // lock_guard

	if(m_hdd) {
		// Hard Disc Drive
		bool hdd_busy = m_hdd->is_busy();
		if(hdd_busy && m_leds.hdd==false) {
			set_hdd_active(true);
		} else if(!hdd_busy && m_leds.hdd==true) {
			set_hdd_active(false);
		}
	}

	// Power LED
	if(m_machine->is_on() && m_leds.power==false) {
		m_leds.power = true;
		m_screen->params.poweron = true;
		m_screen->params.updated = true;
		m_buttons.power->SetClass("active", true);
	} else if(!m_machine->is_on() && m_leds.power==true) {
		m_leds.power = false;
		m_screen->params.poweron = false;
		m_screen->params.updated = true;
		m_buttons.power->SetClass("active", false);
	}

	// Speed indicator
	if(m_machine->is_on()) {
		if(m_machine->is_paused()) {
			m_speed->SetClass("warning", false);
			m_speed->SetClass("slow", false);
			m_speed->SetClass("paused", true);
			m_speed_value->SetInnerRML("paused");
			m_speed->SetProperty("visibility", "visible");
		} else {
			m_speed->SetClass("paused", false);
			int vtime_ratio_1000 = round(m_machine->get_bench().cavg_vtime_ratio * 1000.0);
			static std::string str(10,0);
			m_speed_value->SetInnerRML(str_format(str, "%d%%", vtime_ratio_1000/10));
			if(m_machine->cycles_factor() != 1.0) {
				m_speed->SetClass("warning", false);
				if(m_machine->get_bench().load > 1.0) {
					m_speed->SetClass("slow", true);
				} else {
					m_speed->SetClass("slow", false);
				}
				m_speed->SetProperty("visibility", "visible");
			} else {
				if(m_machine->get_bench().is_stressed()) {
					m_speed->SetClass("warning", true);
					m_speed->SetProperty("visibility", "visible");
				} else {
					m_speed->SetProperty("visibility", "hidden");
				}
			}
		}
	} else {
		m_speed->SetProperty("visibility", "hidden");
	}

	// Child windows
	m_fs->update();
	m_state_load->update();
	m_state_save->update();
}

void Interface::on_power(Rml::Event &)
{
	switch_power();
}

void Interface::show_message(const char* _mex)
{
	std::string str(_mex);
	str_replace_all(str, "\n", "<br />");

	m_message->SetInnerRML(str);
	if(str.empty()) {
		m_message->SetProperty("visibility", "hidden");
	} else {
		m_message->SetProperty("visibility", "visible");
	}
}

bool Interface::on_drive_select(Rml::Event &, UIDriveBlock *_block)
{
	assert(_block);
	_block->change_drive();

	for(auto &block : m_drive_blocks) {
		block.change_drive(_block->curr_drive);
	}

	std::list<MachineDrive>::iterator drive = m_drives.begin();
	for(; drive != m_drives.end(); drive++) {
		if(&*drive == _block->curr_drive->drive) {
			m_curr_drive = drive;
		}
	}

	return true;
}

bool Interface::on_medium_button(Rml::Event &, MachineDrive *_drive)
{
	switch(_drive->drive_type) {
		case GUIDrivesFX::FDD_5_25:
		case GUIDrivesFX::FDD_3_5:
			if(!g_program.config().get_bool(
					_drive->drive_id == 0 ? DISK_A_SECTION : DISK_B_SECTION, DISK_INSERTED)) {
				return true;
			}
			m_machine->cmd_eject_floppy(_drive->drive_id, [=](bool) {
				floppy_activity_cb(FloppyEvents::EVENT_DISK_EJECTED, -1, _drive);
			});
			// "eject" audio sample plays now
			if(m_audio_enabled) {
				m_drives_audio.use_drive(_drive->drive_type, GUIDrivesFX::EJECT);
			}
			break;
		case GUIDrivesFX::CDROM:
			// door open fx will play in the update()
			m_machine->cmd_toggle_cdrom_door(_drive->cdrom);
			break;
		default:
			break;
	}
	return true;
}

bool Interface::on_medium_mount(Rml::Event &, MachineDrive *_drive)
{
	std::string media_dir;

	if(_drive->drive_type == GUIDrivesFX::CDROM) {
		media_dir = g_program.config().find_media(DISK_CD_SECTION, DISK_PATH);
	} else {
		if(_drive->drive_id == 0) {
			if(g_program.config().get_bool(DISK_A_SECTION, DISK_INSERTED)) {
				media_dir = g_program.config().find_media(DISK_A_SECTION, DISK_PATH);
			}
		} else {
			if(g_program.config().get_bool(DISK_B_SECTION, DISK_INSERTED)) {
				media_dir = g_program.config().find_media(DISK_B_SECTION, DISK_PATH);
			}
		}
	}

	if(!media_dir.empty()) {
		size_t pos = media_dir.rfind(FS_SEP);
		if(pos == std::string::npos) {
			media_dir = "";
		} else {
			media_dir = media_dir.substr(0,pos);
		}
	}
	if(media_dir.empty()) {
		// the file select dialog contains a valid media/home directory
		// the file select dialog always exists, even when native dialogs are set
		if(m_fs->is_current_dir_valid()) {
			media_dir = m_fs->get_current_dir();
		} else {
			media_dir = m_fs->get_home();
		}
	}

	if(g_program.config().get_string(DIALOGS_SECTION, DIALOGS_FILE_TYPE, "custom") == "native") {
		media_dir += "/";
		auto filter_patterns = _drive->compatible_file_extensions();
		std::vector<std::string> patt;
		for(auto e : filter_patterns) {
			patt.push_back(str_format("*%s", e));
		}
		filter_patterns.clear();
		for(auto &p : patt) {
			filter_patterns.push_back(p.c_str());
		}
		if(m_gui->is_fullscreen()) {
			// native dialogs don't play well when the application they're called by
			// is rendered fullscreen.
			// the user will have to switch back to fullscreen, can't auto do it.
			m_gui->toggle_fullscreen();
		}
#ifdef _WIN32
		tinyfd_winUtf8 = 1;
#endif
		const char *openfile = tinyfd_openFileDialog(
			str_format("Image for drive %s", _drive->label.c_str()).c_str(),
			media_dir.c_str(),
			filter_patterns.size(),
			&filter_patterns[0],
			std::string("Disc image (" + str_implode(filter_patterns) + ")").c_str(),
			0 // aAllowMultipleSelects
		);
		if(openfile) {
			on_medium_select(openfile, false, _drive);
		}
	} else {
		if(_drive->drive_type == GUIDrivesFX::CDROM) {
			m_fs->set_features(nullptr, nullptr, false);
			m_fs->set_compat_types(
				_drive->compatible_file_types(),
				_drive->compatible_file_extensions()
			);
		} else {
			m_fs->set_features(
				std::bind(&Interface::create_new_floppy_image, this, _1, _2, _3, _4),
				get_filesel_info,
				true
			);
			m_fs->set_compat_types(
				_drive->compatible_file_types(),
				_drive->compatible_file_extensions(),
				_drive->floppy_ctrl->get_compatible_formats(),
				!_drive->floppy_ctrl->can_use_any_floppy()
			);
		}
		try {
			if(m_fs->get_current_dir() != media_dir) {
				m_fs->set_current_dir(media_dir);
			}
		} catch(...) {
			m_fs->set_current_dir(m_fs->get_home());
		}
		m_fs->set_title(str_format("Image for drive %s", _drive->label.c_str()));
		m_fs->set_select_callbk(std::bind(&Interface::on_medium_select, this, _1, _2, _drive));
		m_fs->reload();
		m_fs->show();
	}

	return true;
}

void Interface::reset_savestate_dialogs(std::string _dir)
{
	Rml::ReleaseTextures();
	Rml::ReleaseCompiledGeometry();
	StateDialog::set_current_dir(_dir);
	m_state_save->set_dirty();
	m_state_load->set_dirty();
}

void Interface::show_state_dialog(bool _save)
{
	try {
		std::string dir = g_program.config().get_file(CAPTURE_SECTION, CAPTURE_DIR, FILE_TYPE_USER);
		if(dir.empty()) {
			throw std::runtime_error("Capture directory not set!");
		}
		if(dir != StateDialog::current_dir()) {
			reset_savestate_dialogs(dir);
		}
	} catch(std::runtime_error &e) {
		PERRF(LOG_GUI, "%s\n", e.what());
		return;
	}

	bool machine_was_paused = m_machine->is_paused();
	bool input_was_grabbed = m_gui->is_input_grabbed();
	auto dialog_delete = [=](StateRecord::Info _info) {
		m_gui->show_message_box(
			"Delete State",
			str_format("Do you want to delete slot %s?", _info.name.c_str()),
			MessageWnd::Type::MSGW_YES_NO,
			[=]()
			{
				PDEBUGF(LOG_V0, LOG_GUI, "Delete record: %s\n", _info.name.c_str());
				try {
					g_program.delete_state(_info);
				} catch(std::runtime_error &e) {
					PERRF(LOG_GUI, "Error deleting state record: %s\n", e.what());
				} catch(std::out_of_range &) {
					PDEBUGF(LOG_V0, LOG_GUI, "StateDialog: invalid state id!\n");
				}
				reset_savestate_dialogs("");
			}
		);
	};
	auto dialog_end = [=]() {
		m_state_save->hide();
		m_state_load->hide();
		if(!machine_was_paused) {
			m_machine->cmd_resume(false);
		}
		m_gui->grab_input(input_was_grabbed);
	};
	m_machine->cmd_pause(false);
	m_gui->grab_input(false);
	if(_save) {
		m_state_save->update();
		m_state_save->set_callbacks(
			// save
			[=](StateRecord::Info _info)
			{
				if(_info.name == QUICKSAVE_RECORD) {
					save_state({QUICKSAVE_RECORD, QUICKSAVE_DESC, "", 0, 0});
					dialog_end();
				} else {
					m_state_save_info->set_callbacks(
						[=](StateRecord::Info _info)
						{
							save_state(_info);
							m_state_save_info->hide();
							dialog_end();
						}
					);
					m_state_save_info->set_state(_info);
					m_state_save_info->show();
				}
			},
			// delete
			dialog_delete,
			// cancel
			dialog_end
		);
		m_state_save->show();
	} else {
		m_state_load->update();
		m_state_load->set_callbacks(
			[=](StateRecord::Info _info)
			{
				m_gui->restore_state(_info);
				m_state_save->set_selection(_info.name);
				m_state_load->hide();
				m_gui->grab_input(input_was_grabbed);
			},
			// delete
			dialog_delete,
			// cancel
			dialog_end
		);
		m_state_load->show();
	}
}

void Interface::on_save_state(Rml::Event &)
{
	if(m_machine->is_on()) {
		show_state_dialog(true);
	} else {
		m_gui->show_message("The machine must be on");
	}
}

void Interface::on_load_state(Rml::Event &)
{
	show_state_dialog(false);
}

void Interface::on_sound(Rml::Event &)
{
	m_gui->toggle_mixer_control();
}

void Interface::on_printer(Rml::Event &)
{
	m_gui->toggle_printer_control();
}

void Interface::on_dblclick(Rml::Event &)
{
	m_gui->toggle_fullscreen();
}

void Interface::drive_medium_mount()
{
	if(m_curr_drive != m_drives.end()) {
		Rml::Event e;
		on_medium_mount(e, &*m_curr_drive);
	}
}

void Interface::drive_medium_eject()
{
	if(m_curr_drive != m_drives.end()) {
		Rml::Event e;
		on_medium_button(e, &*m_curr_drive);
	}
}

void Interface::drive_select()
{
	if(m_drives.size() < 2) {
		return;
	}
	if(m_curr_drive != m_drives.end()) {
		m_curr_drive = std::next(m_curr_drive);
		if(m_curr_drive == m_drives.end()) {
			m_curr_drive = m_drives.begin();
		}
	}
	for(auto &block : m_drive_blocks) {
		block.change_drive(m_curr_drive);
	}
	m_gui->show_message(str_format("Drive %s selected", m_curr_drive->label.c_str()));
}

void Interface::save_state(StateRecord::Info _info)
{
	PDEBUGF(LOG_V0, LOG_GUI, "Saving %s: %s\n", _info.name.c_str(), _info.user_desc.c_str());
	g_program.save_state(_info, [this](StateRecord::Info _info) {
		reset_savestate_dialogs("");
		m_state_save->set_selection(_info.name);
		m_state_load->set_selection(_info.name);
		m_gui->show_message("State saved");
	}, nullptr);
}

void Interface::render_screen()
{
	m_screen->render();
}

void Interface::switch_power()
{
	if(m_audio_enabled) {
		m_system_audio.update(!m_machine->is_on(), true);
	}
	m_machine->cmd_switch_power();
	m_machine->cmd_resume();
}

void Interface::set_audio_volume(float _volume)
{
	m_mixer->set_volume_cat(MixerChannel::AUDIOCARD, _volume);
}

void Interface::set_video_brightness(float _level)
{
	m_screen->set_brightness(_level);
}

void Interface::set_video_contrast(float _level)
{
	m_screen->set_contrast(_level);
}

void Interface::set_video_saturation(float _level)
{
	m_screen->set_saturation(_level);
}

void Interface::set_ambient_light(float _level)
{
	m_screen->set_ambient(_level);
}

void Interface::sig_state_restored()
{
	if(m_audio_enabled) {
		m_system_audio.update(true, false);
	}
}

void Interface::save_framebuffer(std::string _screenfile, std::string _palfile)
{
	SDL_Surface * surface = SDL_CreateRGBSurface(
		0,
		m_screen->display()->mode().xres,
		m_screen->display()->mode().yres,
		32,
		PALETTE_RMASK,
		PALETTE_GMASK,
		PALETTE_BMASK,
		PALETTE_AMASK
	);
	if(!surface) {
		PERRF(LOG_GUI, "error creating buffer surface\n");
		throw std::exception();
	}
	SDL_Surface * palette = nullptr;
	if(!_palfile.empty()) {
		palette = SDL_CreateRGBSurface(
			0,         //flags (unused)
			16, 16,    //w x h
			32,        //bit depth
			PALETTE_RMASK,
			PALETTE_GMASK,
			PALETTE_BMASK,
			PALETTE_AMASK
		);
		if(!palette) {
			SDL_FreeSurface(surface);
			PERRF(LOG_GUI, "error creating palette surface\n");
			throw std::exception();
		}
	}
	m_screen->display()->lock();
		SDL_LockSurface(surface);
		m_screen->display()->copy_screen((uint8_t*)surface->pixels);
		SDL_UnlockSurface(surface);
		if(palette) {
			SDL_LockSurface(palette);
			for(uint i=0; i<256; i++) {
				((uint32_t*)palette->pixels)[i] = m_screen->display()->get_color(i);
			}
			SDL_UnlockSurface(palette);
		}
	m_screen->display()->unlock();

	stbi_write_png_compression_level = 9;
	int result = stbi_write_png(_screenfile.c_str(), surface->w, surface->h,
			surface->format->BytesPerPixel, surface->pixels, surface->pitch);

	SDL_FreeSurface(surface);
	if(result<0) {
		PERRF(LOG_GUI, "error saving surface to PNG\n");
		if(palette) {
			SDL_FreeSurface(palette);
		}
		throw std::exception();
	}
	if(palette) {
		result = stbi_write_png(_palfile.c_str(), palette->w, palette->h,
				palette->format->BytesPerPixel, palette->pixels, palette->pitch);
		SDL_FreeSurface(palette);
		if(result < 0) {
			PERRF(LOG_GUI, "error saving palette to PNG\n");
			throw std::exception();
		}
	}
}

SDL_Surface * Interface::copy_framebuffer()
{
	m_screen->display()->lock();
	SDL_Surface * surface = SDL_CreateRGBSurface(
		0,
		m_screen->display()->mode().xres,
		m_screen->display()->mode().yres,
		32,
		PALETTE_RMASK,
		PALETTE_GMASK,
		PALETTE_BMASK,
		PALETTE_AMASK
	);
	if(surface) {
		SDL_LockSurface(surface);
		m_screen->display()->copy_screen((uint8_t*)surface->pixels);
		SDL_UnlockSurface(surface);
	}
	m_screen->display()->unlock();

	if(!surface) {
		PERRF(LOG_GUI, "Error creating buffer surface\n");
		throw std::exception();
	}
	return surface;
}

std::string Interface::get_filesel_info(std::string _filepath)
{
	std::unique_ptr<FloppyFmt> format(FloppyFmt::find(_filepath));

	if(!format) {
		return "Invalid floppy image format";
	}

	std::string info = std::string("File: ") + str_to_html(FileSys::get_basename(_filepath.c_str())) + "<br />";
	info += format->get_preview_string(_filepath);

	return info;
}

std::string Interface::create_new_floppy_image(std::string _dir, std::string _file, FloppyDisk::StdType _type, std::string _format)
{
	PDEBUGF(LOG_V1, LOG_GUI, "New floppy image: %s, %s, %d, %s\n", _dir.c_str(), _file.c_str(), _type, _format.c_str());

	if(_file.empty()) {
		throw std::runtime_error("Empty file name.");
	}
	if(!FileSys::is_directory(_dir.c_str())) {
		throw std::runtime_error("Invalid destination directory.");
	}
	if(!FileSys::is_dir_writeable(_dir.c_str())) {
		throw std::runtime_error("You don't have permission to write to the destination directory.");
	}
#ifdef _WIN32
	const std::regex invalid_chars("[<>:\"\\/\\\\|?*]");
#else
	const std::regex invalid_chars("[\\/]");
#endif
	if(std::regex_search(_file, invalid_chars)) {
		throw std::runtime_error("Invalid characters used in the file name.");
	}

	std::string base, ext;
	FileSys::get_file_parts(_file.c_str(), base, ext);
#ifdef _WIN32
	const std::vector<const char*> invalid_names = {
		"CON", "PRN", "AUX", "NUL",
		"COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8","COM9",
		"LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
	};
	if(std::find(invalid_names.begin(), invalid_names.end(), base) != invalid_names.end()) {
		throw std::runtime_error("Invalid file name.");
	}
#endif

	std::unique_ptr<FloppyFmt> format(FloppyFmt::find_by_name(_format));
	if(!format) {
		throw std::runtime_error("Invalid image format.");
	}
	ext = str_to_lower(ext);
	if(!format->has_file_extension(ext)) {
		_file += format->default_file_extension();
	}

	if(_file.size() > 255) {
		throw std::runtime_error("File name too long.");
	}

	std::string path = _dir + FS_SEP + _file;

	if(FileSys::file_exists(path.c_str())) {
		throw std::runtime_error(str_format("The file \"%s\" already exists.", _file.c_str()));
	}

	FloppyCtrl::create_new_floppy_image(path, FloppyDrive::FDD_NONE, _type, _format);

	return _file;
}