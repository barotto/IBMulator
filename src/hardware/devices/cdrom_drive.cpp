/*
 * Copyright (C) 2024  Marco Bortolin
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
#include "filesys.h"
#include "cdrom_drive.h"
#include "hdd.h"
#include "program.h"
#include "storagectrl.h"
#include <cstring>

CdRomDrive::CdRomDrive()
:
StorageDev(DEV_CDROM)
{
	m_ident = DriveIdent{
		"IBMLTR", // Vendor name
		"CD-ROM", // Product id
		"1.0",    // Product revision
		"IBMULATOR CD-ROM DRIVE", // Model name
		"1",      // Serial number
		"1.0"     // Firmware revision
	};
	memset(&m_s, 0, sizeof(m_s));
	m_max_speed_x = 1;
	m_cur_speed_x = 1;
}

void CdRomDrive::install(StorageCtrl *_ctrl, uint8_t _id)
{
	StorageDev::install(_ctrl, _id);

	m_disc_timer = g_machine.register_timer(
		std::bind(&CdRomDrive::timer_handler, this, std::placeholders::_1),
		"CD-ROM disc"
	);

	m_durations.spin_up = 1500_ms;
	m_durations.spin_down = 1500_ms;
	m_durations.open_door = 1500_ms;
	m_durations.close_door = 1400_ms;
	m_durations.read_toc = 1_s; // made up
	m_durations.to_idle = 30_s;

	if(m_fx_enabled) {
		m_fx.install(m_name);
		m_durations.spin_up = US_TO_NS(m_fx.duration_us(CdRomFX::CD_SPIN_UP));
		m_durations.spin_down = US_TO_NS(m_fx.duration_us(CdRomFX::CD_SPIN_DOWN));
	}
}

void CdRomDrive::power_on(uint64_t)
{
	m_s.door_locked = false;
	if(m_disc) {
		m_s.disc = DISC_DOOR_CLOSING;
		update_disc_state();
	} else if(m_s.disc == DISC_DOOR_OPEN) {
		close_door();
	}
}

void CdRomDrive::power_off()
{
	signal_activity(EVENT_POWER_OFF, 0);

	if(m_fx_enabled) {
		m_fx.clear_seek_events();
		if(is_motor_on()) {
			m_fx.spin(false, true);
		}
	}
}

void CdRomDrive::set_durations(uint64_t _open_door_us, uint64_t _close_door_us)
{
	m_durations.open_door = US_TO_NS(_open_door_us);
	m_durations.close_door = US_TO_NS(_close_door_us);
}

void CdRomDrive::remove()
{
	g_machine.unregister_timer(m_disc_timer);

	if(m_fx_enabled) {
		m_fx.remove();
	}
}

void CdRomDrive::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_HDD, "%s: saving state\n", name());

	_state.write(&m_s, {sizeof(m_s), str_format("CDROM%u", m_drive_index).c_str()});
}

void CdRomDrive::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_HDD, "%s: restoring state\n", name());

	_state.read(&m_s, {sizeof(m_s), str_format("CDROM%u", m_drive_index).c_str()});

	switch(m_s.disc) {
		case DISC_NO_DISC:
		case DISC_DOOR_OPEN:
		case DISC_EJECTING:
			if(m_disc) {
				PERRF(LOG_HDD, "CD-ROM: Invalid disc state on restore: %u\n", m_s.disc);
				remove_medium();
			}
			break;
		case DISC_DOOR_CLOSING:
			break;
		case DISC_SPINNING_UP:
		case DISC_READY:
		case DISC_IDLE:
			if(!m_disc) {
				PERRF(LOG_HDD, "CD-ROM: Invalid disc state on restore: %u\n", m_s.disc);
			} else if(m_s.disc == DISC_SPINNING_UP || m_s.disc == DISC_READY) {
				if(m_fx_enabled) {
					m_fx.spin(true, true);
				}
			}
			break;
	}
	if(m_fx_enabled) {
		m_fx.clear_seek_events();
	}
}

void CdRomDrive::config_changed(const char *_section)
{
	// Program thread (Startup) and Machine thread (restore state)

	m_ini_section = _section;
	m_activity_cb.clear();

	m_max_speed_x = g_program.config().get_int_or_bool(DRIVES_SECTION, DRIVES_CDROM);
	m_max_speed_x = std::max(m_max_speed_x, 1);
	m_max_speed_x = std::min(m_max_speed_x, 72);
	m_cur_speed_x = m_max_speed_x;

	memset(m_ident.model, 0, sizeof(m_ident.model));
	snprintf(m_ident.model, sizeof(m_ident.model), "IBMULATOR %uX CD-ROM DRIVE", uint8_t(m_max_speed_x));

	// CAV only
	m_performance.rot_speed = (m_max_speed_x * 60 * 150 * 1024) / (CdRomDisc::sectors_per_track * BYTES_PER_MODE1_DATA);
	double seek_max_ms;
	double seek_third_ms;

	// Average access times from variuous Mitsumi CD-ROM drives

	// FX001D 1x 530-200rpm, 1/3 280, max 450 (outer)
	//  avg. latency 1/3 30000/420rpm = 71.428
	//  avg. latency max 150
	// seek 1/3 ms = 280 - 72 = 208;
	// seek max ms = 450 - 150 = 300;

	// FX001D 2x 1060-400 1/3 250, max 390
	//  avg. latency 1/3 30000/840rpm = 35.714
	//  avg. latency max 75
	// seek 1/3 ms = 250 - 36 = 214;
	// seek max ms = 390 - 75 = 315;

	// FX14x IDE 12x-16x 6360-3200rpm, 1/3 120, max 250
	//  avg. latency inner 4.72, outer 9.375ms
	// seek 1/3 ms = 120 - 5 = 115;
	// seek max ms = 250 - 10 = 240;

	// FX24x IDE 12x-24x 6360-4800rpm, 1/3 90, max 160
	//  avg. latency inner 4.72, outer 6.25ms
	// seek 1/3 ms = 90 - 5 = 85;
	// seek max ms = 160 - 6 = 154;

	if(m_max_speed_x < 4) {
		seek_third_ms = 200;
		seek_max_ms = 300;
	} else if(m_max_speed_x >= 4 && m_max_speed_x <= 16) {
		seek_third_ms = 115;
		seek_max_ms = 240;
	} else {
		seek_third_ms = 85;
		seek_max_ms = 154;
	}

	m_performance.seek_max_ms = seek_max_ms;
	m_performance.seek_trk_ms = 0;
	m_performance.seek_third_ms = seek_third_ms;
	m_performance.interleave = 1;

	MediaGeometry geometry;
	geometry.heads = 1;
	geometry.spt = CdRomDisc::sectors_per_track;
	geometry.cylinders = CdRomDisc::max_tracks;
	m_performance.update(geometry, BYTES_PER_RAW_REDBOOK_FRAME, 0);

	m_sector_data = BYTES_PER_MODE1_DATA;
	m_head_speed_factor = HDD_HEAD_SPEED;
	m_head_accel_factor = HDD_HEAD_ACCEL;

	PINFOF(LOG_V0, LOG_HDD,  "Installed %s\n", name());
	PINFOF(LOG_V0, LOG_HDD,  "  Interface: %s\n", m_controller->name());
	PINFOF(LOG_V1, LOG_HDD,  "  Model name: %s\n", m_ident.model);
	PINFOF(LOG_V1, LOG_HDD,  "  Speed: %dX (%d KB/s)\n", m_max_speed_x, m_max_speed_x*150);
	PDEBUGF(LOG_V1, LOG_HDD, "  Rotational speed: %g RPM\n", m_performance.rot_speed);
	PINFOF(LOG_V1, LOG_HDD,  "  Full stroke seek time: %g ms\n", m_performance.seek_max_ms);
	PINFOF(LOG_V1, LOG_HDD,  "  1/3 stroke seek time: %g ms\n", m_performance.seek_third_ms);
	PDEBUGF(LOG_V1, LOG_HDD, "    seek overhead time: %g us\n", m_performance.seek_overhead_us);
	PDEBUGF(LOG_V1, LOG_HDD, "    seek avgspeed time: %g us/cyl\n", m_performance.seek_avgspeed_us);
	PDEBUGF(LOG_V1, LOG_HDD, "  Track read time (rot.lat.): %g us\n", m_performance.trk_read_us);
	PDEBUGF(LOG_V1, LOG_HDD, "  Sector read time: %g us\n", m_performance.sec_read_us);
	// double check values:
	double read_speed_bytes_sec = (1e6 / m_performance.sec_read_us) * BYTES_PER_MODE1_DATA;
	double read_speed_factor = read_speed_bytes_sec / (150.0*1024.0);
	PDEBUGF(LOG_V1, LOG_HDD, "  Read speed (raw): %g bytes per us\n", m_performance.bytes_per_us);
	PDEBUGF(LOG_V1, LOG_HDD, "  Read speed (net): %.1f bytes per sec (%.2fX)\n", read_speed_bytes_sec, read_speed_factor);

	set_timeout_mult(0);

	if(g_program.config().get_bool(_section, DISK_INSERTED)) {
		if(insert_medium(g_program.config().get_string(_section, DISK_PATH))) {
			m_s.disc = DISC_IDLE;
			m_s.disc_changed = true;
			g_machine.deactivate_timer(m_disc_timer);
		}
	} else {
		remove_medium();
	}
}

bool CdRomDrive::insert_medium(const std::string &_path)
{
	remove_medium();

	PINFOF(LOG_V0, LOG_HDD, "CD-ROM: loading image '%s'...\n", _path.c_str());

	auto path = g_program.config().find_media(_path);
	if(FileSys::file_exists(path.c_str())) {
		m_disc = std::make_shared<CdRomDisc_Image>();
		try {
			m_disc->load(path);
		} catch(std::runtime_error &e) {
			PERRF(LOG_HDD, "CD-ROM: %s\n", e.what());
			remove_medium();
			return false;
		}
		m_path = path;
	} else {
		PERRF(LOG_HDD, "CD-ROM: cannot find image file '%s'.\n");
		return false;
	}

	uint8_t first, last;
	TMSF leadOut;
	if(!m_disc->get_audio_tracks(first, last, leadOut)) {
		PERRF(LOG_HDD, "CD-ROM: cannot get tracks information.\n");
		remove_medium();
		return false;
	}

	double sectors = leadOut.to_frames() - REDBOOK_FRAME_PADDING;

	MediaGeometry geometry;
	geometry.heads = 1;
	geometry.spt = CdRomDisc::sectors_per_track;
	geometry.cylinders = std::ceil(sectors / CdRomDisc::sectors_per_track);
	set_geometry(geometry, BYTES_PER_RAW_REDBOOK_FRAME, 0);

	m_sectors = sectors; // make it precise
	m_disk_radius = double(geometry.cylinders) * CdRomDisc::track_width_mm;

	PINFOF(LOG_V1, LOG_HDD,  "CD-ROM:   tracks: %u, sectors: %lld\n", m_disc->tracks_count(), m_sectors);
	PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM:   C/S: %u/%u, radius: %.1f mm\n",
			m_geometry.cylinders, m_geometry.spt, m_disk_radius);

	if(g_machine.is_on()) {
		// somebody will play sound fx for this
		do_close_door(true);
	} else {
		PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: disc is inserted and IDLE.\n");
		m_s.disc = DISC_IDLE;
		signal_activity(EVENT_MEDIUM, 0);
	}

	g_program.config().set_bool(m_ini_section, DISK_INSERTED, true);
	g_program.config().set_string(m_ini_section, DISK_PATH, _path);

	return true;
}

bool CdRomDrive::is_motor_on() const
{
	return (m_s.disc == DISC_READY || m_s.disc == DISC_SPINNING_UP);
}

bool CdRomDrive::is_door_open()
{
	return m_s.disc == DISC_DOOR_OPEN;
}

void CdRomDrive::open_door()
{
	if(is_door_open()) {
		return;
	}
	if(!g_machine.is_on()) {
		remove_medium();
		signal_activity(EVENT_MEDIUM, 0);
	} else {
		if(m_s.door_locked) {
			PINFOF(LOG_V0, LOG_HDD, "CD-ROM: cannot open: the door is soft-locked.\n");
			return;
		}

		if(is_motor_on()) {
			m_s.disc = DISC_EJECTING;
			signal_activity(EVENT_MEDIUM, m_durations.spin_down);
			g_machine.activate_timer(m_disc_timer, m_durations.spin_down, false);
			if(m_fx_enabled) {
				m_fx.spin(false, true);
			}
		} else {
			m_s.disc = DISC_EJECTING;
			g_machine.deactivate_timer(m_disc_timer);
			update_disc_state();
		}
	}
}

uint64_t CdRomDrive::close_door(bool _force)
{
	if(!g_machine.is_on()) {
		return 0;
	} else {
		return do_close_door(_force);
	}
}

uint64_t CdRomDrive::do_close_door(bool _force)
{
	if(m_s.disc != DISC_DOOR_OPEN) {
		if(_force) {
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: close_door(): the door is NOT open: forcing it open...\n");
			m_s.disc = DISC_DOOR_OPEN;
		} else {
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: close_door(): the door is not open.\n");
			return 0;
		}
	}

	signal_activity(EVENT_DOOR_CLOSING, m_durations.close_door);

	m_s.disc = DISC_DOOR_CLOSING;
	PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: door open -> door closing \n");
	g_machine.activate_timer(m_disc_timer, m_durations.close_door, false);

	return m_durations.close_door;
}

void CdRomDrive::signal_activity(EventType _what, uint64_t _led_duration)
{
	for(auto cb : m_activity_cb) {
		cb.second(_what, _led_duration);
	}
}

void CdRomDrive::toggle_door_button()
{
	if(m_s.disc == CdRomDrive::DiscState::DISC_DOOR_OPEN) {
		close_door();
	} else {
		open_door();
	}
}

void CdRomDrive::remove_medium()
{
	m_disc.reset();
	m_path.clear();
	m_s.disc = DISC_NO_DISC;
	m_s.disc_changed = true;
	m_s.disc_loaded = false;
	g_machine.deactivate_timer(m_disc_timer);
	g_program.config().set_bool(m_ini_section, DISK_INSERTED, false);
	signal_activity(EVENT_MEDIUM, 0);
}

bool CdRomDrive::is_medium_present()
{
	return m_disc != nullptr;
}

bool CdRomDrive::has_medium_changed(bool _reset)
{
	if(m_s.disc_changed) {
		m_s.disc_changed = !_reset;
		return true;
	}
	return false;
}

bool CdRomDrive::is_disc_accessible()
{
	return is_medium_present() && m_s.disc_loaded;
}

void CdRomDrive::set_timeout_mult(uint8_t _mult)
{
	m_s.timeout_mult = _mult & 0xF;
	if(m_s.timeout_mult == 0) {
		m_durations.to_idle = SEC_TO_NSEC(g_program.config().get_int_or_default(DRIVES_SECTION, DRIVES_CDROM_IDLE));
	} else if(m_s.timeout_mult <= 0x9) {
		m_durations.to_idle = MSEC_TO_NSEC(125 * pow(2,m_s.timeout_mult-1));
	} else {
		m_durations.to_idle = MSEC_TO_NSEC(1000 * pow(2,m_s.timeout_mult-0xA));
	}
	PINFOF(LOG_V1, LOG_HDD, "CD-ROM: idle timeout: %gs\n", NSEC_TO_SEC(m_durations.to_idle));
}

uint8_t CdRomDrive::disc_type()
{
	// according to table 59 of SFF-8020i, mode select command
	if(m_s.disc_loaded) {
		return m_disc->type();
	} else {
		return m_s.disc == CdRomDrive::DISC_DOOR_OPEN ? 0x71 : 0x70;
	}
}

uint32_t CdRomDrive::transfer_time_us(int64_t _xfer_amount)
{
	// head already at the correct lba, seek and rotational latency already accounted for.
	return m_performance.sec_xfer_us * _xfer_amount;
}

uint32_t CdRomDrive::rotational_latency_us()
{
	return m_performance.avg_rot_lat_us;
}

bool CdRomDrive::read_sector(int64_t _lba, uint8_t *_buffer, unsigned _bytes)
{
	assert(_buffer);

	// READ family of commands

	switch(m_s.disc) {
		case DISC_NO_DISC:
		case DISC_DOOR_OPEN:
		case DISC_DOOR_CLOSING:
		case DISC_EJECTING:
			// the user did something?
			return false;
		case DISC_IDLE:
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: read_sector while disc is IDLE!\n");
			return false;
		case DISC_READY:
			// next state: IDLE
			g_machine.activate_timer(m_disc_timer, m_durations.to_idle, false);
			break;
		case DISC_SPINNING_UP: // valid state for sector read in the future
			break;
		default:
			break;
	}

	if(!m_disc) {
		assert(false);
		PERRF(LOG_HDD, "CD-ROM: cannot read from medium: not present.\n");
		return false;
	}

	try {
		m_disc->read_sector(_buffer, _lba, _bytes);
	} catch(std::exception &e) {
		PERRF(LOG_HDD, "CD-ROM: cannot read from medium: %s\n", e.what());
		return false;
	}

	// duration is not relevant
	signal_activity(EVENT_READ, 1);

	return true;
}

void CdRomDrive::seek(unsigned _from_track, unsigned _to_track)
{
	unsigned delay_us = 0;
	switch(m_s.disc) {
		case DISC_NO_DISC:
		case DISC_DOOR_OPEN:
		case DISC_EJECTING:
		case DISC_DOOR_CLOSING:
			// the user did something?
			return;
		case DISC_IDLE:
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: seek while disc is IDLE!\n");
			return;
		case DISC_READY:
			// next state: IDLE
			g_machine.activate_timer(m_disc_timer, m_durations.to_idle, false);
			break;
		case DISC_SPINNING_UP: // valid state for sector read in the future
			// delay the sample to not overlap with the spin-up sound
			delay_us = NSEC_TO_USEC(m_durations.spin_up);
			break;
		default:
			break;
	}
	if(m_fx_enabled) {
		uint64_t time = g_machine.get_virt_time_us() + delay_us;
		m_fx.seek(time, _from_track, _to_track, CdRomDisc::max_tracks);
	}
}

void CdRomDrive::timer_handler(uint64_t)
{
	update_disc_state();
}

void CdRomDrive::update_disc_state()
{
	uint64_t next_event = 0;
	switch(m_s.disc) {
		case DISC_NO_DISC:
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: NO_DISC: INVALID DISC STATE\n");
			break;
		case DISC_DOOR_OPEN:
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: DISC_DOOR_OPEN: INVALID DISC STATE\n");
			break;
		case DISC_DOOR_CLOSING:
			if(m_disc) {
				PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: state: door closed -> spinning up & reading TOC\n");
				m_s.disc = DISC_SPINNING_UP;
				m_s.disc_loaded = true; // keep it here
				next_event = m_durations.spin_up + m_durations.read_toc; // next event is READY
				if(m_fx_enabled) {
					m_fx.spin(true, true);
				}
				signal_activity(EVENT_SPINNING_UP, next_event);
			} else {
				PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: state: door closed -> no disc\n");
				m_s.disc = DISC_NO_DISC;
			}
			break;
		case DISC_SPINNING_UP:
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: state: disc spinned up -> ready\n");
			m_s.disc = DISC_READY;
			next_event = m_durations.to_idle; // next event is IDLE
			break;
		case DISC_READY:
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: state: ready -> idle\n");
			m_s.disc = DISC_IDLE;
			if(m_fx_enabled) {
				m_fx.spin(false, true);
			}
			break;
		case DISC_IDLE:
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: state: idle -> ready\n");
			m_s.disc = DISC_READY;
			next_event = m_durations.to_idle; // next event is IDLE
			break;
		case DISC_EJECTING:
			signal_activity(EVENT_DOOR_OPENING, m_durations.open_door);
			remove_medium();
			m_s.disc = DISC_DOOR_OPEN;
			break;
	}
	if(next_event) {
		g_machine.activate_timer(m_disc_timer, next_event, false);
	}
}

void CdRomDrive::spin_up()
{
	switch(m_s.disc) {
		case DISC_NO_DISC:
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: spin up: no disc!\n");
			break;
		case DISC_DOOR_OPEN:
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: spin up: door is open!\n");
			break;
		case DISC_DOOR_CLOSING:
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: spin up: door is closing!\n");
			break;
		case DISC_READY:
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: spin up: disc already spinning.\n");
			// reset IDLE timer
			// next state: IDLE
			g_machine.activate_timer(m_disc_timer, m_durations.to_idle, false);
			break;
		case DISC_SPINNING_UP:
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: spin up: already spinning up!\n");
			break;
		case DISC_IDLE:
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: spin up: idle -> spinning up...\n");
			m_s.disc = DISC_SPINNING_UP;
			if(m_fx_enabled) {
				m_fx.spin(true, true);
			}
			// next state: DISC_READY
			g_machine.activate_timer(m_disc_timer, m_durations.spin_up, false);
			signal_activity(EVENT_SPINNING_UP, m_durations.spin_up);
			break;
		case DISC_EJECTING:
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: spin up: the disc is getting ejected!\n");
			break;
	}
}

void CdRomDrive::spin_down()
{
	switch(m_s.disc) {
		case DISC_NO_DISC:
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: spin down: no disc!\n");
			break;
		case DISC_DOOR_OPEN:
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: spin down: door is open!\n");
			break;
		case DISC_DOOR_CLOSING:
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: spin down: door is closing!\n");
			break;
		case DISC_SPINNING_UP:
		case DISC_READY:
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: spinning down...\n");
			m_s.disc = DISC_IDLE;
			if(m_fx_enabled) {
				m_fx.spin(false, true);
			}
			g_machine.deactivate_timer(m_disc_timer);
			break;
		case DISC_IDLE:
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: spin down: already idle\n");
			break;
		case DISC_EJECTING:
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: spin down: the disc is already spinning down!\n");
			break;
	}
}

uint64_t CdRomDrive::time_to_ready_us()
{
	if(m_s.disc == DISC_SPINNING_UP) {
		return NSEC_TO_USEC(g_machine.get_timer_eta(m_disc_timer));
	} else if(m_s.disc == DISC_READY) {
		return 0;
	} else {
		return TIME_NEVER;
	}
}

CdRomDrive::DiscState CdRomDrive::disc_state()
{
	return m_s.disc;
}

bool CdRomDrive::get_audio_status(bool &_playing, bool &_pause)
{
	// TODO
	_playing = false;
	_pause = false;
	return true;
}

bool CdRomDrive::stop_audio()
{
	// TODO
	return true;
}

bool CdRomDrive::read_toc(uint8_t *buf_, size_t _bufsize, size_t &length_, bool _msf, unsigned _start_track, unsigned _format)
{
	assert(buf_);

	if(!m_disc) {
		PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: read_toc: no disc in the drive!\n");
		return false;
	}

	uint8_t first, last;
	TMSF leadOut;
	if(!m_disc->get_audio_tracks(first, last, leadOut)) {
		PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: read_toc: failed to get track info\n");
		return false;
	}

	std::vector<uint8_t> buf(4);
	buf.reserve(_bufsize);

	switch(_format) {
		case 0: // Read TOC
		{
			buf[2] = first;
			buf[3] = last;

			for (unsigned track = first; track <= last; track++) {
				uint8_t attr;
				TMSF start;

				if(!m_disc->get_audio_track_info(track, start, attr)) {
					PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: read_toc: unable to read track %u information\n", track);
					attr = 0x41; // ADR=1 CONTROL=4
					start.min = 0;
					start.sec = 0;
					start.fr = 0;
				}

				if(track < _start_track) {
					continue;
				}

				PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: read_toc: playing Track %u (attr=0x%02x %02u:%02u:%02u)\n",
						first, attr, start.min, start.sec, start.fr);

				buf.push_back(0x00);               // entry+0 RESERVED
				buf.push_back((attr >> 4) | 0x10); // entry+1 ADR=1 CONTROL=4 (DATA)
				buf.push_back(track);              // entry+2 TRACK
				buf.push_back(0x00);               // entry+3 RESERVED
				if(_msf) {
					buf.push_back(0x00);
					buf.push_back(start.min);
					buf.push_back(start.sec);
					buf.push_back(start.fr);
				} else {
					uint32_t sec = start.to_frames() - 150u;
					buf.push_back( (uint8_t)(sec >> 24u) );
					buf.push_back( (uint8_t)(sec >> 16u) );
					buf.push_back( (uint8_t)(sec >> 8u) );
					buf.push_back( (uint8_t)(sec >> 0u) );
				}
			}

			buf.push_back(0x00);
			buf.push_back(0x14);
			buf.push_back(0xAA); // TRACK
			buf.push_back(0x00);
			if(_msf) {
				buf.push_back(0x00);
				buf.push_back(leadOut.min);
				buf.push_back(leadOut.sec);
				buf.push_back(leadOut.fr);
			} else {
				uint32_t sec = leadOut.to_frames() - 150u;
				buf.push_back( (uint8_t)(sec >> 24u) );
				buf.push_back( (uint8_t)(sec >> 16u) );
				buf.push_back( (uint8_t)(sec >> 8u) );
				buf.push_back( (uint8_t)(sec >> 0u) );
			}

			break;
		}
		case 1: // Read multisession info
		{
			uint8_t attr;
			TMSF start;

			buf[2] = 1u; // first complete session
			buf[3] = 1u; // last complete session 

			if (!m_disc->get_audio_track_info(first, start, attr)) {
				PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: read_toc: unable to read track %u information\n", first);
				attr = 0x41; // ADR=1 CONTROL=4
				start.min = 0;
				start.sec = 0;
				start.fr = 0;
			}

			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: read_toc: playing Track %u (attr=0x%02x %02u:%02u:%02u)\n", first, attr, start.min,
				start.sec, start.fr);

			buf.push_back(0x00);               // entry+0 RESERVED
			buf.push_back((attr >> 4) | 0x10); // entry+1 ADR=1 CONTROL=4 (DATA)
			buf.push_back(first);              // entry+2 TRACK
			buf.push_back(0x00);               // entry+3 RESERVED

			// then, start address of first track in session
			if(_msf) {
				buf.push_back(0x00 );
				buf.push_back(start.min);
				buf.push_back(start.sec);
				buf.push_back(start.fr);
			} else {
				uint32_t sec = start.to_frames() - 150u;
				buf.push_back( (uint8_t)(sec >> 24u) );
				buf.push_back( (uint8_t)(sec >> 16u) );
				buf.push_back( (uint8_t)(sec >> 8u) );
				buf.push_back( (uint8_t)(sec >> 0u) );
			}
			break;
		}
		case 2: // Raw TOC - emulate a single session only (ported from qemu)
		{
			buf[2] = 1;
			buf[3] = 1;
			for (int i = 0; i < 4; i++) {
				buf.push_back(1);
				buf.push_back(0x14);
				buf.push_back(0);
				if (i < 3) {
					buf.push_back(0xa0 + i);
				} else {
					buf.push_back(1);
				}
				buf.push_back(0);
				buf.push_back(0);
				buf.push_back(0);
				if (i < 2) {
					buf.push_back(0);
					buf.push_back(1);
					buf.push_back(0);
					buf.push_back(0);
				} else if (i == 2) {
					uint32_t blocks = sectors();
					if(_msf) {
						buf.push_back(0); // reserved
						buf.push_back( (uint8_t)(((blocks + 150) / 75) / 60) ); // minute
						buf.push_back( (uint8_t)(((blocks + 150) / 75) % 60) ); // second
						buf.push_back( (uint8_t)((blocks + 150) % 75) ); // frame;
					} else {
						buf.push_back( (blocks >> 24) & 0xff );
						buf.push_back( (blocks >> 16) & 0xff );
						buf.push_back( (blocks >> 8) & 0xff );
						buf.push_back( (blocks >> 0) & 0xff );
					}
				} else {
					buf.push_back(0);
					buf.push_back(0);
					buf.push_back(0);
					buf.push_back(0);
				}
			}

			break;
		}
		default:
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: invalid TOC format requested\n");
			assert(false);
			return false;
	}

	length_ = buf.size();
	buf[0] = ((length_-2) >> 8) & 0xff;
	buf[1] = (length_-2) & 0xff;

	if(length_ > _bufsize) {
		PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: read_toc: TOC exceeds available buffer size: %u > %u bytes\n", length_, _bufsize);
		length_ = _bufsize;
	}
	std::memcpy(buf_, &buf[0], length_);

	return true;
}

bool CdRomDrive::read_sub_channel(uint8_t *buf_, size_t _bufsize, size_t &length_, bool _msf, bool _subq, unsigned _format)
{
	if(!m_disc) {
		PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: read_sub_channel: no disc in the drive!\n");
		return false;
	}

	uint8_t audio_status = 0;
	bool playing, pause;
	if(!get_audio_status(playing, pause)) {
		playing = pause = false;
	}
	if(playing) {
		audio_status = pause ? 0x12 : 0x11;
	} else {
		audio_status = 0x13;
	}

	std::vector<uint8_t> buf{
		0, // 0 reserved
		audio_status, // 1 audio status
		0, // 2 data len MSB
		0, // 3 data len LSB
	};

	// When the sub Q bit is Zero, only the Sub-Channel data header is returned.
	if(_subq) {
		buf.reserve(24);
		buf.push_back(_format); // 4
		if(_format == 1) {
			// Current Position
			uint8_t attr, track, index;
			TMSF rel, abs;
			if(!m_disc->get_audio_sub(attr, track, index, rel, abs)) {
				PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: read_sub_channel: unable to read current position.\n");
				return false;
			}
			buf.push_back((attr >> 4) | 0x10); // 5 ADR / Control
			buf.push_back(track); // 6
			buf.push_back(index); // 7
			if(_msf) {
				buf.push_back(0x00);    // 8
				buf.push_back(abs.min); // 9
				buf.push_back(abs.sec); // 10
				buf.push_back(abs.fr);  // 11
				buf.push_back(0x00);    // 12
				buf.push_back(rel.min); // 13
				buf.push_back(rel.sec); // 14
				buf.push_back(rel.fr);  // 15
			} else {
				uint32_t sec;
				sec = abs.to_frames() - 150u;
				buf.push_back((uint8_t)(sec >> 24u)); // 8
				buf.push_back((uint8_t)(sec >> 16u)); // 9
				buf.push_back((uint8_t)(sec >> 8u));  // 10
				buf.push_back((uint8_t)(sec >> 0u));  // 11
				sec = rel.to_frames() - 150u;
				buf.push_back((uint8_t)(sec >> 24u)); // 12
				buf.push_back((uint8_t)(sec >> 16u)); // 13
				buf.push_back((uint8_t)(sec >> 8u));  // 14
				buf.push_back((uint8_t)(sec >> 0u));  // 15
			}
		} else {
			// UPC or ISRC (not implemented)
			buf.resize(24);
			if(_format == 3) {
				// ISRC
				buf[5] = 0x14; // ADR / Control
				buf[6] = 0x01;
			}
			buf[8] = 0; // MCVal=0 (no UPC) / TCVal=0 (no ISRC)
		}
	}

	length_ = buf.size();
	buf[2] = ((length_-4) >> 8) & 0xff;
	buf[3] = (length_-4) & 0xff;

	if(length_ > _bufsize) {
		PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: read_sub_channel: data exceeds available buffer size: %u > %u bytes\n", length_, _bufsize);
		length_ = _bufsize;
	}
	std::memcpy(buf_, &buf[0], length_);

	return true;
}