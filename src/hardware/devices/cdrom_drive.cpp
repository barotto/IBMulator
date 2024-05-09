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

/*
 * Portions of code
 * Copyright (C) 2020-2023  The DOSBox Staging Team
 * Copyright (C) 2002-2021  The DOSBox Team
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

void CdRomDrive::install(StorageCtrl *_ctrl, uint8_t _id, const char *_ini_section)
{
	StorageDev::install(_ctrl, _id, _ini_section);

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

	using namespace std::placeholders;
	m_audio.channel = g_mixer.register_channel(
		std::bind(&CdRomDrive::create_audio_samples, this, _1, _2, _3),
		"CD Audio", MixerChannel::AUDIOCARD, MixerChannel::AudioType::DAC);
	m_audio.channel->set_disable_timeout(EFFECTS_MIN_DUR_NS);
	m_audio.channel->set_features(
		MixerChannel::HasVolume |
		MixerChannel::HasBalance |
		MixerChannel::HasReverb |
		MixerChannel::HasChorus |
		MixerChannel::HasFilter |
		MixerChannel::HasCrossfeed
	);
	m_audio.channel->register_config_map({
		{ MixerChannel::ConfigParameter::Volume,    { _ini_section, CDROM_VOLUME }},
		{ MixerChannel::ConfigParameter::Balance,   { _ini_section, CDROM_BALANCE }},
		{ MixerChannel::ConfigParameter::Reverb,    { _ini_section, CDROM_REVERB }},
		{ MixerChannel::ConfigParameter::Chorus,    { _ini_section, CDROM_CHORUS }},
		{ MixerChannel::ConfigParameter::Crossfeed, { _ini_section, CDROM_CROSSFEED }},
		{ MixerChannel::ConfigParameter::Filter,    { _ini_section, CDROM_FILTERS }},
	});
	m_audio.channel->set_in_spec({AUDIO_FORMAT_S16, 2u, 44100.0});

	// Some programs have 2 different mono audio tracks encoded in the L/R channels
	// and use per channel volume to disable one of the two (eg. grolier encyclopedia)
	// Volumes are set by the guest using the sub adjustment.
	// m_audio.channel->add_autoval_cb(MixerChannel::ConfigParameter::Volume, std::bind(&CdRomDrive::update_volumes, this));
}

void CdRomDrive::power_on(uint64_t)
{
	m_cur_speed_x = m_max_speed_x;
	m_s.disc_changed = false;
	set_timeout_mult(0);
	set_audio_port(0, 1, 0xff);
	set_audio_port(1, 2, 0xff);
	update_volumes();
	set_sotc(false);
	lock_door(false);

	if(m_disc) {
		m_s.disc = DISC_DOOR_CLOSING;
		update_disc_state();
	} else if(m_s.disc == DISC_DOOR_OPEN) {
		close_door();
	}
}

void CdRomDrive::power_off()
{
	stop_audio();

	signal_activity(CdRomEvents::POWER_OFF, 0);

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

	std::lock_guard<std::mutex> lock(m_audio.player_mutex);

	_state.write(&m_s, {sizeof(m_s), str_format("CDROM%u", m_drive_index).c_str()});
}

void CdRomDrive::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_HDD, "%s: restoring state\n", name());

	remove_disc();

	_state.read(&m_s, {sizeof(m_s), str_format("CDROM%u", m_drive_index).c_str()});

	if(g_program.config().get_bool(m_ini_section, DISK_INSERTED)) {
		std::string diskpath = g_program.config().find_media(m_ini_section, DISK_PATH);
		if(diskpath.empty()) {
			PERRF(LOG_GUI, "A CD-ROM disc is inserted but the image path is not set.\n");
			throw std::runtime_error("cannot restore CD-ROM state");
		}
		CdRomDisc *disc = CdRomLoader::load_cdrom(diskpath);
		if(!disc) {
			// error log messages are printed by the loader
			throw std::runtime_error("cannot restore CD-ROM state");
		}
		insert_disc(disc, diskpath);
	}

	switch(m_s.disc) {
		case DISC_NO_DISC:
		case DISC_DOOR_OPEN:
		case DISC_EJECTING:
			if(m_disc) {
				// this is a bug
				PERRF(LOG_HDD, "CD-ROM: Invalid disc state on restore: %u\n", m_s.disc);
				throw std::runtime_error("invalid state");
			}
			break;
		case DISC_DOOR_CLOSING:
			break;
		case DISC_SPINNING_UP:
		case DISC_READY:
		case DISC_IDLE:
			if(!m_disc) {
				// this is a bug
				PERRF(LOG_HDD, "CD-ROM: Invalid disc state on restore: %u\n", m_s.disc);
				throw std::runtime_error("invalid state");
			} else if(m_s.disc == DISC_SPINNING_UP || m_s.disc == DISC_READY) {
				if(m_fx_enabled) {
					m_fx.spin(true, false);
				}
			}
			break;
	}
	if(m_fx_enabled) {
		m_fx.clear_seek_events();
	}

	update_volumes();

	if(m_s.audio.is_playing) {
		auto curr_sector = curr_audio_lba();
		m_audio.track = m_disc->get_track(curr_sector);
		if(m_audio.track == m_disc->tracks().end() || !m_audio.track->is_audio()) {
			PERRF(LOG_GUI, "Invalid audio track at sector %lld\n", curr_sector);
			throw std::runtime_error("cannot restore CD-ROM state");
		}
		auto byte_offset = m_audio.track->sector_to_byte(curr_sector);
		if(byte_offset < 0 ||
		   !m_audio.track->file->seek(byte_offset, false)
		) {
			PERRF(LOG_HDD, "CD-ROM: failed to seek track %u to sector %lld, byte offset: %lld\n",
					m_audio.track->number, curr_sector, byte_offset);
			throw std::runtime_error("cannot restore CD-ROM state");
		}
		m_audio.channel->enable(true);
	}
}

void CdRomDrive::config_changed()
{
	// Program thread (Startup) and Machine thread (restore state)

	// At program launch, the Program interface is responsible for media insertions
	// At restore state, media is inserted in the restore_state() function

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
}

void CdRomDrive::insert_disc(CdRomDisc *_disc, std::string _path)
{
	assert(!m_disc);
	assert(m_path.empty());

	m_path = _path;
	m_disc.reset(_disc);

	auto geometry = m_disc->geometry();
	set_geometry(geometry, BYTES_PER_RAW_REDBOOK_FRAME, 0);
	m_sectors = m_disc->sectors();
	m_disk_radius = double(geometry.cylinders) * CdRomDisc::track_width_mm;
}

void CdRomDrive::insert_medium(CdRomDisc *_disc, std::string _path)
{
	assert(_disc);

	remove_medium();

	insert_disc(_disc, _path);
	m_s.disc_changed = true;

	if(g_machine.is_on()) {
		// somebody will play sound fx for this
		do_close_door(true);
	} else {
		PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: disc is inserted and IDLE.\n");
		m_s.disc = DISC_IDLE;
		signal_activity(CdRomEvents::MEDIUM, 0);
	}

	g_program.config().set_bool(m_ini_section, DISK_INSERTED, true);
	g_program.config().set_string(m_ini_section, DISK_PATH, _path);
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
		signal_activity(CdRomEvents::MEDIUM, 0);
	} else {
		if(m_s.door_locked) {
			PINFOF(LOG_V0, LOG_HDD, "CD-ROM: cannot open: the door is soft-locked.\n");
			return;
		}

		stop_audio();

		if(is_motor_on()) {
			m_s.disc = DISC_EJECTING;
			signal_activity(CdRomEvents::MEDIUM, m_durations.spin_down);
			activate_timer(m_durations.spin_down, "to state DISC_DOOR_OPEN");
			if(m_fx_enabled) {
				m_fx.spin(false, true);
			}
		} else {
			m_s.disc = DISC_EJECTING;
			deactivate_timer("open door");
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
			PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: close_door(): the door is NOT open: forcing it open...\n");
			m_s.disc = DISC_DOOR_OPEN;
		} else {
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: close_door(): the door is not open.\n");
			return 0;
		}
	}

	signal_activity(CdRomEvents::DOOR_CLOSING, m_durations.close_door);

	m_s.disc = DISC_DOOR_CLOSING;
	PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: door open -> door closing \n");

	activate_timer(m_durations.close_door, "to state DISC_SPINNING_UP");

	return m_durations.close_door;
}

void CdRomDrive::signal_activity(CdRomEvents::EventType _what, uint64_t _led_duration)
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

void CdRomDrive::remove_disc()
{
	CdRomDisc * disc = m_disc.release();
	if(disc) {
		g_machine.cmd_dispose_cdrom(disc);
	}
	m_path.clear();
}

void CdRomDrive::remove_medium()
{
	remove_disc();

	m_s.disc = DISC_NO_DISC;
	m_s.disc_changed = true;
	m_s.disc_loaded = false;

	m_s.audio.completed = false;

	deactivate_timer("remove medium");
	g_program.config().set_bool(m_ini_section, DISK_INSERTED, false);
	signal_activity(CdRomEvents::MEDIUM, 0);
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

void CdRomDrive::set_sotc(bool _sotc)
{
	m_s.audio.sotc = _sotc;
	PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: Stop On Track Crossing (SOTC): %d\n", _sotc);
}

void CdRomDrive::set_audio_port(uint8_t _port, uint8_t _ch, uint8_t _vol)
{
	// Machine thread

	std::lock_guard<std::mutex> lock(m_audio.player_mutex);

	PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: Audio port %u: channel=%u, volume=%u\n", _port, _ch, _vol);
	if(_port == 0) {
		m_s.audio.port0_ch = _ch;
		m_s.audio.port0_vol = _vol;
	} else {
		m_s.audio.port1_ch = _ch;
		m_s.audio.port1_vol = _vol;
	}
	update_volumes();
}

std::pair<uint8_t,uint8_t> CdRomDrive::get_audio_port(uint8_t _port)
{
	if(_port == 0) {
		return std::make_pair(m_s.audio.port0_ch, m_s.audio.port0_vol);
	} else {
		return std::make_pair(m_s.audio.port1_ch, m_s.audio.port1_vol);
	}
}

void CdRomDrive::update_volumes()
{
	// Machine and Mixer threads

	std::lock_guard<std::mutex> lock(m_audio.channel_mutex);

	float left = float(m_s.audio.port0_vol) / 255.f;
	float right = float(m_s.audio.port1_vol) / 255.f;
	if(m_audio.channel->volume_sub_left() != left || m_audio.channel->volume_sub_right() != right) {
		PDEBUGF(LOG_V1, LOG_AUDIO, "CD-ROM: audio volume L:%.3f - R:%.3f\n", left, right);
	}
	m_audio.channel->set_volume_sub(left, right);
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
			activate_timer(m_durations.to_idle, "to state DISC_IDLE");
			break;
		case DISC_SPINNING_UP: // valid state for sector read in the future
			break;
		default:
			break;
	}

	if(!is_medium_present()) {
		PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: cannot read from medium: not present.\n");
		return false;
	}

	// duration is not relevant
	signal_activity(CdRomEvents::READ_DATA, 1);
	m_s.audio.completed = false;
	m_s.audio.head_pos_valid = false;

	try {
		m_disc->read_sector(_buffer, _lba, _bytes);
	} catch(std::exception &e) {
		PERRF(LOG_HDD, "CD-ROM: cannot read from medium: %s\n", e.what());
		throw;
	}

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
			activate_timer(m_durations.to_idle, "to state DISC_IDLE");
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
	m_s.audio.completed = false;
	m_s.audio.head_pos_valid = false;
}

void CdRomDrive::activate_timer(uint64_t _nsecs, const char *_reason)
{
	if(g_machine.is_timer_active(m_disc_timer)) {
		PDEBUGF(LOG_V3, LOG_HDD, "CD-ROM: timer cancelled, ETA: %llu ns\n", g_machine.get_timer_eta(m_disc_timer));
	}
	PDEBUGF(LOG_V3, LOG_HDD, "CD-ROM: new timer set: %s, ETA: %llu ns\n", _reason, _nsecs);
	g_machine.activate_timer(m_disc_timer, _nsecs, false);
}

void CdRomDrive::deactivate_timer(const char *_reason)
{
	if(g_machine.is_timer_active(m_disc_timer)) {
		PDEBUGF(LOG_V3, LOG_HDD, "CD-ROM: timer cancelled: %s\n", _reason);
		g_machine.deactivate_timer(m_disc_timer);
	}
}

void CdRomDrive::timer_handler(uint64_t)
{
	update_disc_state();
}

void CdRomDrive::update_disc_state()
{
	switch(m_s.disc) {
		case DISC_NO_DISC:
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: NO_DISC: INVALID DISC STATE\n");
			break;
		case DISC_DOOR_OPEN:
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: DISC_DOOR_OPEN: INVALID DISC STATE\n");
			break;
		case DISC_DOOR_CLOSING:
			if(m_disc) {
				PDEBUGF(LOG_V2, LOG_HDD, "CD-ROM: state: door closed -> spinning up & reading TOC\n");
				m_s.disc = DISC_SPINNING_UP;
				m_s.disc_loaded = true; // keep it here
				if(m_fx_enabled) {
					m_fx.spin(true, true);
				}
				uint64_t next_event = m_durations.spin_up + m_durations.read_toc; // next event is READY
				signal_activity(CdRomEvents::SPINNING_UP, next_event);
				activate_timer(next_event, "to state DISC_SPINNING_UP");
			} else {
				PDEBUGF(LOG_V2, LOG_HDD, "CD-ROM: state: door closed -> no disc\n");
				m_s.disc = DISC_NO_DISC;
			}
			break;
		case DISC_SPINNING_UP: {
			PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: state: disc spinned up -> ready\n");
			m_s.disc = DISC_READY;
			if(m_s.audio.seek_delay_ns) {
				activate_timer(m_s.audio.seek_delay_ns, "to audio start");
			} else {
				activate_timer(m_durations.to_idle, "to state DISC_IDLE");
			}
			break;
		}
		case DISC_READY:
			if(m_s.audio.seek_delay_ns) {
				PDEBUGF(LOG_V2, LOG_HDD, "CD-ROM: state: starting audio ...\n");
				start_audio(true);
				activate_timer(m_durations.to_idle, "state polling");
			} else if(!m_s.audio.is_playing) {
				PDEBUGF(LOG_V2, LOG_HDD, "CD-ROM: state: ready -> idle\n");
				m_s.disc = DISC_IDLE;
				if(m_fx_enabled) {
					m_fx.spin(false, true);
				}
			} else {
				activate_timer(m_durations.to_idle, "state polling");
			}
			break;
		case DISC_IDLE:
			PDEBUGF(LOG_V2, LOG_HDD, "CD-ROM: state: idle -> ready\n");
			m_s.disc = DISC_READY;
			activate_timer(m_durations.to_idle, "to state DISC_IDLE");
			break;
		case DISC_EJECTING:
			signal_activity(CdRomEvents::DOOR_OPENING, m_durations.open_door);
			remove_medium();
			m_s.disc = DISC_DOOR_OPEN;
			break;
	}
}

void CdRomDrive::spin_up()
{
	switch(m_s.disc) {
		case DISC_NO_DISC:
			PDEBUGF(LOG_V2, LOG_HDD, "CD-ROM: spin up: no disc!\n");
			break;
		case DISC_DOOR_OPEN:
			PDEBUGF(LOG_V2, LOG_HDD, "CD-ROM: spin up: door is open!\n");
			break;
		case DISC_DOOR_CLOSING:
			PDEBUGF(LOG_V2, LOG_HDD, "CD-ROM: spin up: door is closing!\n");
			break;
		case DISC_READY:
			if(!m_s.audio.is_playing) {
				PDEBUGF(LOG_V2, LOG_HDD, "CD-ROM: spin up: disc already spinning.\n");
				// reset IDLE timer
				activate_timer(m_durations.to_idle, "to state DISC_IDLE");
			}
			break;
		case DISC_SPINNING_UP:
			PDEBUGF(LOG_V2, LOG_HDD, "CD-ROM: spin up: already spinning up!\n");
			break;
		case DISC_IDLE:
			PDEBUGF(LOG_V2, LOG_HDD, "CD-ROM: spin up: idle -> spinning up...\n");
			m_s.disc = DISC_SPINNING_UP;
			if(m_fx_enabled) {
				m_fx.spin(true, true);
			}
			activate_timer(m_durations.spin_up, "to state DISC_READY");
			signal_activity(CdRomEvents::SPINNING_UP, m_durations.spin_up);
			break;
		case DISC_EJECTING:
			PDEBUGF(LOG_V2, LOG_HDD, "CD-ROM: spin up: the disc is getting ejected!\n");
			break;
	}
}

void CdRomDrive::spin_down()
{
	switch(m_s.disc) {
		case DISC_NO_DISC:
			PDEBUGF(LOG_V2, LOG_HDD, "CD-ROM: spin down: no disc!\n");
			break;
		case DISC_DOOR_OPEN:
			PDEBUGF(LOG_V2, LOG_HDD, "CD-ROM: spin down: door is open!\n");
			break;
		case DISC_DOOR_CLOSING:
			PDEBUGF(LOG_V2, LOG_HDD, "CD-ROM: spin down: door is closing!\n");
			break;
		case DISC_SPINNING_UP:
		case DISC_READY:
			PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: spinning down...\n");
			stop_audio();
			m_s.disc = DISC_IDLE;
			if(m_fx_enabled) {
				m_fx.spin(false, true);
			}
			deactivate_timer("spin down");
			break;
		case DISC_IDLE:
			PDEBUGF(LOG_V2, LOG_HDD, "CD-ROM: spin down: already idle!\n");
			break;
		case DISC_EJECTING:
			PDEBUGF(LOG_V2, LOG_HDD, "CD-ROM: spin down: the disc is already spinning down!\n");
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

bool CdRomDrive::check_play_audio(int64_t &_start_lba_, int64_t _end_lba, uint8_t &sense_, uint8_t &asc_)
{
	// check track validity
	auto track = m_disc->get_track(_start_lba_);
	if(track == m_disc->tracks().end() || !track->file || track->is_data()) {
		PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: play_audio_check(): invalid start track.\n");
		sense_ = 0x05;
		asc_ = 0x64; // ILLEGAL MODE FOR THIS TRACK
		return false;
	}

	// If the request falls into the pregap, which is prior to the track's
	// actual start but not so earlier that it falls into the prior track's
	// audio, then we simply skip the pre-gap (beacuse we can't negatively
	// seek into the track) and instead start playback at the actual track
	// start.
	if(_start_lba_ < track->start) {
		PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: play_audio_check(): start LBA (%lld) is in track %d pregap, moving to sector %lld.\n",
				_start_lba_, track->number, track->start);
		_start_lba_ = track->start;
	}

	// If the Starting MSF address is greater than the Ending MSF address,
	// the command shall be terminated with CHECK CONDITION status.
	// The sense key shall be set to ILLEGAL REQUEST.
	if(_start_lba_ > _end_lba) {
		PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: play_audio_check(): invalid start/end LBA sectors: %lld < %lld.\n",
				_start_lba_, _end_lba);
		sense_ = 0x05;
		asc_ = 0x24; // ASC_INVALID_FIELD_IN_CMD_PACKET
		return false;
	}

	if(_start_lba_ > max_lba() || _end_lba > sectors()) {
		PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: play_audio_check(): start (%lld), end (%lld) LBA sectors out-of-range.\n",
				_start_lba_, _end_lba);
		sense_ = 0x05;
		asc_ = 0x33; // ASC_LOGICAL_BLOCK_OOR
		return false;
	}

	return true;
}

bool CdRomDrive::start_audio_track(int64_t _start_lba, int64_t _end_lba, bool _do_seek)
{
	// Machine and Mixer threads
	// lock to be acquired beforehand

	if(_start_lba > _end_lba) {
		PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: start_audio_track_play: invalid starting point!\n");
		return false;
	}

	m_s.audio.start_sector = _start_lba;
	m_s.audio.end_sector = _end_lba;

	m_s.audio.total_redbook_frames = _end_lba - _start_lba;
	m_s.audio.total_pcm_frames = m_s.audio.total_redbook_frames * PCM_FRAMES_PER_REDBOOK_FRAME;

	if(!m_s.audio.total_redbook_frames) {
		PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: start_audio_track_play: nothing to play.\n");
		return false;
	}

	if(_do_seek) {
		m_audio.track = m_disc->get_track(_start_lba);
		if(m_audio.track == m_disc->tracks().end() || !m_audio.track->is_audio()) { 
			return false;
		}

		m_s.audio.played_pcm_frames = 0;

		PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: start_audio_track_play: track=%u, from=%lld, to=%lld, frames=%u\n",
				m_audio.track->number, m_s.audio.start_sector, m_s.audio.end_sector, m_s.audio.total_redbook_frames);

		int64_t byte_offset = m_audio.track->sector_to_byte(m_s.audio.start_sector);

		if(byte_offset < 0 ||
		   !m_audio.track->file->seek(byte_offset, static_cast<bool>(m_s.audio.seek_delay_ns))
		) {
			PERRF(LOG_HDD, "CD-ROM: failed to seek track %u to sector: %lld, byte offset: %lld\n",
					m_audio.track->number, m_s.audio.start_sector, byte_offset);
			return false;
		}
	}

	m_s.audio.to_start_state();

	return true;
}

void CdRomDrive::play_audio(int64_t _start_lba, int64_t _end_lba, uint64_t _seek_delay_us)
{
	// Machine thread, called from the disc controller
	// a call to play_audio_check() shall be done beforehand to check values

	std::lock_guard<std::mutex> lock(m_audio.player_mutex);

	bool do_seek = false;
	if(_seek_delay_us) {
		// a seek stops audio play 
		stop_audio(false, false);
		do_seek = true;
	} else {
		// head is already on desired position
		// seek audio file only if not currently playing
		do_seek = !m_s.audio.is_playing;
	}

	m_s.audio.seek_delay_ns = US_TO_NS(_seek_delay_us);

	PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: play_audio: start: %s (%lld), end: %s (%lld), seek: %llu ns\n",
			TMSF(_start_lba).to_string().c_str(), _start_lba,
			TMSF(_end_lba).to_string().c_str(), _end_lba,
			m_s.audio.seek_delay_ns);

	if(!start_audio_track(_start_lba, _end_lba, do_seek)) {
		stop_audio(true, false);
		return;
	}

	switch(m_s.disc) {
		case DISC_READY:
			if(m_s.audio.seek_delay_ns) {
				PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: play_audio: seeking to sector %lld, ETA: %llu ns\n",
						_start_lba, m_s.audio.seek_delay_ns);
				activate_timer(m_s.audio.seek_delay_ns, "to audio start");
			} else {
				start_audio(false);
			}
			break;
		case DISC_SPINNING_UP:
			PDEBUGF(LOG_V2, LOG_HDD, "CD-ROM: audio will be started when disc becomes ready and seek is completed (if req.)\n");
			break;
		default:
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: play_audio(): invalid disc state: %d\n", m_s.disc);
			break;
	}
}

bool CdRomDrive::pause_resume_audio(bool _resume)
{
	// Machine thread

	if(m_s.audio.is_paused) {
		// audio is playing but paused
		if(_resume) {
			std::lock_guard<std::mutex> lock(m_audio.player_mutex);
			m_s.audio.is_paused = false;
			PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: audio unpaused.\n");
			m_audio.channel->enable(true);
		} else {
			// It shall not be considered an error to request a PAUSE when a pause is already in effect
			// or to request a RESUME when a play operation is in progress.
			stop_audio();
		}
		return true;
	} else if(m_s.audio.is_playing || m_s.audio.seek_delay_ns) {
		if(!_resume) {
			std::lock_guard<std::mutex> lock(m_audio.player_mutex);
			m_s.audio.is_paused = true;
			PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: audio paused.\n");
		}
		return true;
	} else {
		PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: no active audio play operation.\n");
		return false;
	}
}

void CdRomDrive::start_audio(bool _audio_lock)
{
	// Machine thread

	if(_audio_lock) {
		m_audio.player_mutex.lock();
	}

	m_s.audio.to_start_state();
	m_s.audio.seek_delay_ns = 0;
	m_s.audio.head_pos_valid = true;

	m_audio.channel->enable(true);

	PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: audio started.\n");

	if(_audio_lock) {
		m_audio.player_mutex.unlock();
	}
}

void CdRomDrive::stop_audio(bool _error, bool _audio_lock)
{
	// Machine thread

	if(_audio_lock) {
		m_audio.player_mutex.lock();
	}

	if(m_s.audio.is_playing) {
		m_s.audio.to_stop_state(_error);
		m_s.audio.seek_delay_ns = 0;

		PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: audio stopped.\n");
		// if mixer channel is active it will be stopped by the mixer
	}

	if(_audio_lock) {
		m_audio.player_mutex.unlock();
	}
}

int64_t CdRomDrive::curr_audio_lba() const
{
	double fraction_played = static_cast<double>(m_s.audio.played_pcm_frames) / m_s.audio.total_pcm_frames;
	int64_t played_redbook_frames = static_cast<int64_t>(ceil(fraction_played * m_s.audio.total_redbook_frames));
	int64_t curr_lba = m_s.audio.start_sector + played_redbook_frames;

	return curr_lba;
}

CdRomDrive::AudioStatus CdRomDrive::get_audio_status(bool _reset, int64_t *curr_lba_)
{
	// audio lock shall be acquired

	if(curr_lba_) {
		if(m_s.audio.head_pos_valid) {
			*curr_lba_ = curr_audio_lba();
		} else {
			*curr_lba_ = -1;
		}
	}

	if(m_s.audio.is_playing) {
		// play operation active
		// Mixer channel might not be enabled yet if drive is seeking
		if(m_s.audio.is_paused) {
			return AUDIO_PAUSED;
		}
		return AUDIO_PLAYING;
	}
	if(m_s.audio.completed) {
		if(_reset) {
			m_s.audio.completed = false;
		}
		if(m_s.audio.error) {
			return AUDIO_ERROR_STOP;
		}
		return AUDIO_SUCCESS_STOP;
	}
	return AUDIO_NO_STATUS;
}

bool CdRomDrive::create_audio_samples(uint64_t _time_span_ns, bool _prebuf, bool _first_upd)
{
	// Mixer thread

	UNUSED(_prebuf);

	std::lock_guard<std::mutex> lock(m_audio.player_mutex);

	static uint64_t prev_mtime_ns = 0;
	static double gen_frames_rem = .0;

	uint64_t cur_mtime_ns = g_machine.get_virt_time_ns_mt();
	uint64_t elapsed_ns = 0;

	if(_first_upd) {
		elapsed_ns = _time_span_ns;
	} else {
		assert(cur_mtime_ns >= prev_mtime_ns);
		elapsed_ns = cur_mtime_ns - prev_mtime_ns;
	}
	prev_mtime_ns = cur_mtime_ns;

	int req_frames = static_cast<int>(m_audio.channel->in_spec().ns_to_frames(elapsed_ns) + gen_frames_rem);
	int gen_frames = 0;

	int64_t curr_lba = curr_audio_lba();
	if(curr_lba >= m_s.audio.end_sector) {
		m_s.audio.to_stop_state();
	}

	bool active = true;
	if(!m_s.audio.is_playing || m_s.audio.is_paused) {
		PDEBUGF(LOG_V2, LOG_MIXER, "CD-ROM: audio paused, creating silence.\n");
		gen_frames = req_frames;
		m_audio.channel->in().fill_frames_silence(req_frames);
		active = !m_audio.channel->check_disable_time(cur_mtime_ns);
	} else if(req_frames) {
		static AudioBuffer buff({ AUDIO_FORMAT_S16, REDBOOK_CHANNELS, REDBOOK_PCM_FRAMES_PER_SECOND });
		buff.resize_frames(req_frames);

		gen_frames = m_audio.track->file->decode(buff.data(), req_frames);
		if(gen_frames == CdRomDisc::DECODE_EOF) {
			// EOF, this track has come to an end
			if(!m_s.audio.sotc) {
				// proceed to the next
				uint8_t sense, asc;
				if(
				   !check_play_audio(curr_lba, m_s.audio.end_sector, sense, asc) ||
				   !start_audio_track(curr_lba, m_s.audio.end_sector, true)
				)
				{
					gen_frames = CdRomDisc::DECODE_ERROR;
				} else {
					// try again
					gen_frames = m_audio.track->file->decode(buff.data(), req_frames);
					if(gen_frames == CdRomDisc::DECODE_EOF) {
						PDEBUGF(LOG_V0, LOG_MIXER, "CD-ROM: unexpected EOF\n");
						m_s.audio.to_stop_state(true);
					}
				}
			} else {
				m_s.audio.to_stop_state();
			}
		}
		if(gen_frames == CdRomDisc::DECODE_ERROR) {
			// decoding error
			PDEBUGF(LOG_V0, LOG_MIXER, "CD-ROM: audio decoding error, stopping.\n");
			m_s.audio.to_stop_state(true);
		} else if(gen_frames == CdRomDisc::DECODE_NOT_READY) {
			// data not ready / seek in progress
			PDEBUGF(LOG_V1, LOG_MIXER, "CD-ROM: data not ready.\n");
		}
		if(gen_frames > 0) {
			buff.resize_frames(gen_frames);
			if(m_s.audio.port0_ch != 1 || m_s.audio.port1_ch != 2) {
				int16_t *data = &(buff.at<int16_t>(0));
				for(size_t i=0; i<buff.frames(); i++) {
					int16_t ch[2] = { data[i*2 + 0], data[i*2 + 1] };
					if(m_s.audio.port0_ch == 0) {
						data[i*2 + 0] = 0;
					} else if(m_s.audio.port0_ch <= 2) {
						data[i*2 + 0] = ch[m_s.audio.port0_ch - 1];
					}
					if(m_s.audio.port1_ch == 0) {
						data[i*2 + 1] = 0;
					} else if(m_s.audio.port1_ch <= 2) {
						data[i*2 + 1] = ch[m_s.audio.port1_ch - 1];
					}
				}
			}
			m_audio.channel->in().add_frames(buff);
			m_s.audio.played_pcm_frames += gen_frames;
		} else {
			gen_frames = req_frames;
			m_audio.channel->in().fill_frames_silence(req_frames);
		}

		m_audio.channel->set_disable_time(cur_mtime_ns);

		signal_activity(CdRomEvents::READ_DATA, 1);
	}

	gen_frames_rem = req_frames - double(gen_frames);

	m_audio.channel->input_finish();

	unsigned needed_frames = round(m_audio.channel->in_spec().ns_to_frames(_time_span_ns));
	PDEBUGF(LOG_V2, LOG_MIXER, "CD-ROM: mix time: %04llu ns, frames: %d, machine time: %llu ns, gen.frames: %d, curr.LBA: %lld\n",
			_time_span_ns, needed_frames, elapsed_ns, gen_frames, curr_audio_lba());

	return active;
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
	if(!m_disc->get_tracks_info(first, last, leadOut)) {
		PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: read_toc: failed to get tracks info.\n");
		return false;
	}

	std::vector<uint8_t> buf(4);
	buf.reserve(_bufsize);

	switch(_format) {
		case 0: // Read TOC
		{
			buf[2] = first;
			buf[3] = last;

			for(unsigned track = first; track <= last; track++) {
				if(track < _start_track) {
					continue;
				}

				uint8_t attr;
				TMSF start;
				if(!m_disc->get_track_info(track, start, attr)) {
					PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: read_toc: unable to read track %u information.\n", track);
					attr = 0x40;
					start.min = 0;
					start.sec = 0;
					start.fr = 0;
				} else {
					PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: read_toc: Track %u (attr=0x%02x %s)\n",
							track, attr, start.to_string().c_str());
				}

				buf.push_back(0x00);               // entry+0 RESERVED
				buf.push_back(0x10 | (attr >> 4)); // entry+1 ADR (1) | CONTROL
				buf.push_back(track);              // entry+2 TRACK
				buf.push_back(0x00);               // entry+3 RESERVED
				if(_msf) {
					buf.push_back(0x00);
					buf.push_back(start.min);
					buf.push_back(start.sec);
					buf.push_back(start.fr);
				} else {
					uint32_t sec = start.to_frames();
					buf.push_back( (uint8_t)(sec >> 24u) );
					buf.push_back( (uint8_t)(sec >> 16u) );
					buf.push_back( (uint8_t)(sec >> 8u) );
					buf.push_back( (uint8_t)(sec >> 0u) );
				}
			}

			// Lead-out
			buf.push_back(0x00);
			buf.push_back(0x14); // ADR (1) | CONTROL (4)
			buf.push_back(0xAA); // TRACK (Lead-out track number is defined as 0AAh)
			buf.push_back(0x00);
			if(_msf) {
				buf.push_back(0x00);
				buf.push_back(leadOut.min);
				buf.push_back(leadOut.sec);
				buf.push_back(leadOut.fr);
			} else {
				uint32_t sec = leadOut.to_frames();
				buf.push_back( (uint8_t)(sec >> 24u) );
				buf.push_back( (uint8_t)(sec >> 16u) );
				buf.push_back( (uint8_t)(sec >> 8u) );
				buf.push_back( (uint8_t)(sec >> 0u) );
			}
			PDEBUGF(LOG_V2, LOG_HDD,
				"CD-ROM: read_toc: lead-out => MSF %s, logical sector %lld\n",
				leadOut.to_string().c_str(), leadOut.to_frames());

			break;
		}
		case 1: // Read multisession info
		{
			uint8_t attr;
			TMSF start;

			buf[2] = 1u; // first complete session
			buf[3] = 1u; // last complete session 

			if (!m_disc->get_track_info(first, start, attr)) {
				PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: read_toc: unable to read track %u information.\n", first);
				attr = 0x40;
				start.min = 0;
				start.sec = 0;
				start.fr = 0;
			}

			PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: read_toc: Track %u (attr=0x%02x %s)\n",
					first, attr, start.to_string().c_str());

			buf.push_back(0x00);               // entry+0 RESERVED
			buf.push_back(0x10 | (attr >> 4)); // entry+1 ADR (1) | CONTROL
			buf.push_back(first);              // entry+2 TRACK
			buf.push_back(0x00);               // entry+3 RESERVED

			// then, start address of first track in session
			if(_msf) {
				buf.push_back(0x00);
				buf.push_back(start.min);
				buf.push_back(start.sec);
				buf.push_back(start.fr);
			} else {
				uint32_t sec = start.to_frames();
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
						TMSF msf(blocks);
						buf.push_back(0x00);
						buf.push_back(msf.min);
						buf.push_back(msf.sec);
						buf.push_back(msf.fr);
					} else {
						buf.push_back( (uint8_t)((blocks >> 24)) );
						buf.push_back( (uint8_t)((blocks >> 16)) );
						buf.push_back( (uint8_t)((blocks >> 8)) );
						buf.push_back( (uint8_t)((blocks >> 0)) );
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
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: invalid TOC format requested.\n");
			assert(false);
			return false;
	}

	length_ = buf.size();
	buf[0] = ((length_-2) >> 8) & 0xff;
	buf[1] = (length_-2) & 0xff;

	if(length_ > _bufsize) {
		PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: read_toc: TOC exceeds available buffer size: %u > %u bytes\n", length_, _bufsize);
		length_ = _bufsize;
	}
	std::memcpy(buf_, &buf[0], length_);

	return true;
}

bool CdRomDrive::read_sub_channel(uint8_t *buf_, size_t _bufsize, size_t &length_,
	bool _msf, bool _subq, unsigned _format, int64_t _abs_lba, uint8_t &sense_, uint8_t &asc_)
{
	// audio lock shall be acquired

	if(!m_disc || !m_disc->sectors()) {
		PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: read_sub_channel: NO REFERENCE POSITION FOUND.\n");
		sense_ = 0x02;
		asc_ = 0x06;
		return false;
	}

	auto curr_audio_status = get_audio_status(true);

	std::vector<uint8_t> buf{
		0, // 0 reserved
		curr_audio_status, // 1 audio status
		0, // 2 data len MSB
		0, // 3 data len LSB
	};

	static const std::map<AudioStatus, const char*> s_audio_status_str{
		{ AUDIO_PLAYING, "Play operation in progress" },
		{ AUDIO_PAUSED, "Play operation paused" },
		{ AUDIO_SUCCESS_STOP, "Play operation successfully completed" },
		{ AUDIO_ERROR_STOP, "Play operation stopped due to error" },
		{ AUDIO_NO_STATUS, "No current audio status to return" }
	};

	PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: read_sub_channel: audio status: %s (0x%02x)\n",
			s_audio_status_str.find(curr_audio_status)->second, curr_audio_status);

	// When the sub Q bit is Zero, only the Sub-Channel data header is returned.
	if(_subq) {
		buf.reserve(24);
		buf.push_back(_format); // 4
		if(_format == 1) {
			// Current Position
			if(_abs_lba > m_disc->sectors()) {
				PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: read_sub_channel: LOGICAL BLOCK ADDRESS OUT OF RANGE.\n");
				sense_ = 0x05;
				asc_ = 0x21;
				return false;
			}
			if(_abs_lba == m_disc->sectors()) {
				_abs_lba = m_disc->sectors() - 1;
			}
			auto track = m_disc->get_track(_abs_lba);
			if(track == m_disc->tracks().end()) {
				PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: read_sub_channel: ILLEGAL MODE FOR THIS TRACK OR INCOMPATIBLE MEDIUM.\n");
				sense_ = 0x05;
				asc_ = 0x64;
				return false;
			}
			int64_t rel_lba = _abs_lba - track->start;
			TMSF abs_msf(_abs_lba);
			TMSF rel_msf(rel_lba, 0);
			buf.push_back(0x10 | (track->attr >> 4)); // 5 ADR / Control
			buf.push_back(track->number); // 6
			buf.push_back(1); // 7
			if(_msf) {
				// absolute
				buf.push_back(0x00);        // 8
				buf.push_back(abs_msf.min); // 9
				buf.push_back(abs_msf.sec); // 10
				buf.push_back(abs_msf.fr);  // 11
				// relative
				buf.push_back(0x00);        // 12
				buf.push_back(rel_msf.min); // 13
				buf.push_back(rel_msf.sec); // 14
				buf.push_back(rel_msf.fr);  // 15
			} else {
				// absolute
				buf.push_back( (uint8_t)(_abs_lba >> 24u) ); // 8
				buf.push_back( (uint8_t)(_abs_lba >> 16u) ); // 9
				buf.push_back( (uint8_t)(_abs_lba >> 8u) );  // 10
				buf.push_back( (uint8_t)(_abs_lba >> 0u) );  // 11
				// relative
				buf.push_back( (uint8_t)(rel_lba >> 24u) ); // 12
				buf.push_back( (uint8_t)(rel_lba >> 16u) ); // 13
				buf.push_back( (uint8_t)(rel_lba >> 8u) );  // 14
				buf.push_back( (uint8_t)(rel_lba >> 0u) );  // 15
			}
			PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: read_sub_channel: "
				"position at %s (absolute sector %lld), "
				"track %u at %s (relative sector %lld)\n",
				abs_msf.to_string().c_str(), _abs_lba,
				track->number, rel_msf.to_string().c_str(), rel_lba);
		} else if(_format == 2) {
			// UPC
			buf.resize(24);
			if(!m_disc->MCN().empty()) {
				// TODO untested
				buf[8] = 0x80; // MCVal=1 (UPC valid)
				for(int i=0; i<14; i++) {
					if(i < m_disc->MCN().size()) {
						buf[9+i] = m_disc->MCN()[i];
					} else {
						buf[9+i] = 0;
					}
				}
			} else {
				buf[8] = 0; // MCVal=0 (no UPC)
			}
		} else if(_format == 3) {
			// ISRC (not implemented)
			buf.resize(24);
			buf[5] = 0x14; // ADR / Control
			buf[6] = 0x01;
			buf[8] = 0; // TCVal=0 (no ISRC)
		} else {
			// invalid format
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: invalid sub channel data format: %u\n", _format);
		}
	}

	length_ = buf.size();
	buf[2] = ((length_-4) >> 8) & 0xff;
	buf[3] = (length_-4) & 0xff;

	if(length_ > _bufsize) {
		PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: read_sub_channel: data exceeds available buffer size: %u > %u bytes\n", length_, _bufsize);
		length_ = _bufsize;
	}
	std::memcpy(buf_, &buf[0], length_);

	return true;
}