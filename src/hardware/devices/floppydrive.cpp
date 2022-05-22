// license:BSD-3-Clause
// copyright-holders:Nathan Woods, Olivier Galibert, Miodrag Milanovic, Marco Bortolin

// Based on MAME's devices/imagedev/floppy.cpp

#include "ibmulator.h"
#include "machine.h"
#include "floppydrive.h"
#include "floppyfmt.h"
#include "program.h"
#include "floppyctrl.h"

FloppyDrive::FloppyDrive()
{}

void FloppyDrive::install(FloppyCtrl *_ctrl, int _drive_index, Type _drive_type)
{
	assert(_ctrl);
	m_floppyctrl = _ctrl;
	m_drive_index = _drive_index;

	if(m_drive_index == 0) {
		m_drive_config = DISK_A_SECTION;
		m_drive_name = "A";
	} else if(m_drive_index == 1) {
		m_drive_config = DISK_B_SECTION;
		m_drive_name = "B";
	} else {
		assert(false);
	}

	set_type(_drive_type);
	m_drive_index = _drive_index;
	m_disk_changed = false;

	m_index_timer = g_machine.register_timer(
			std::bind(&FloppyDrive::index_timer, this, std::placeholders::_1),
			"FDD " + m_drive_name + " index");
	assert(m_index_timer != NULL_TIMER_HANDLE);

	m_fx_enabled = g_program.config().get_bool(SOUNDFX_SECTION, SOUNDFX_ENABLED);
	if(m_fx_enabled) {
		m_fx.install(m_drive_name);
		m_fx.config_changed();
	}

	PINFOF(LOG_V1, LOG_FDC, "DRV%u: installed as Floppy %s: %s, %u sides, %u tracks, %.0f rpm\n",
			m_drive_index, m_drive_name.c_str(), m_drive_type_desc.c_str(), 
			m_sides, m_tracks, m_rpm);
}

void FloppyDrive::set_type(Type _drive_type)
{
	if(is_motor_on()) {
		PDEBUGF(LOG_FDD, LOG_V0, "%s: changing drive type while in use\n", m_drive_name.c_str());
	}
	if(m_image) {
		if(m_image->is_dirty()) {
			// if a floppy image is present it should have been commited beforehand
			PDEBUGF(LOG_V0, LOG_FDC, "Floppy image is dirty, deleting anyway...\n");
		}
		m_image.reset();
	}

	m_drive_type = _drive_type;

	switch(m_drive_type) {
		default:
		case FDD_NONE:  m_drive_type_desc = "unknown"; break;
		case FDD_525DD: 
			m_drive_type_desc = "5.25\" DD";
			set_rpm(300.0);
			m_tracks = 42;
			m_sides = 2;
			m_dstep_drive = false;
			break;
		case FDD_525HD:
			m_drive_type_desc = "5.25\" HD";
			set_rpm(360.0);
			m_tracks = 84;
			m_sides = 2;
			m_dstep_drive = true;
			break;
		case FDD_350DD:
			m_drive_type_desc = "3.5\" DD";
			set_rpm(300.0);
			m_tracks = 84;
			m_sides = 2;
			m_dstep_drive = false;
			break;
		case FDD_350HD:
			m_drive_type_desc = "3.5\" HD";
			set_rpm(300.0);
			m_tracks = 84;
			m_sides = 2;
			m_dstep_drive = false;
			break;
		case FDD_350ED:
			m_drive_type_desc = "3.5\" ED";
			set_rpm(300.0);
			m_tracks = 84;
			m_sides = 2;
			m_dstep_drive = false;
			break;
	}
}

void FloppyDrive::index_timer(uint64_t)
{
	index_resync();
}

void FloppyDrive::remove()
{
	if(m_image && m_image->is_dirty()) {
		// if a floppy image is present it should have been commited beforehand
		PDEBUGF(LOG_V0, LOG_FDC, "Floppy image is dirty, removing anyway...\n");
	}
	m_image.reset();

	if(m_fx_enabled) {
		m_fx.remove();
	}

	g_machine.unregister_timer(m_index_timer);
	m_index_timer = NULL_TIMER_HANDLE;
}

void FloppyDrive::set_rpm(float _rpm)
{
	m_rpm = _rpm;
	m_rev_time = 60_s / _rpm;
	m_angular_speed = (m_rpm / 60.0) * 2e8;
}

void FloppyDrive::reset(unsigned type)
{
	if(type == MACHINE_POWER_ON) {
		m_s.idx = 0;
		m_s.cyl = 0;
		m_s.ss  = 0;
		m_s.stp = 1;
		if(m_image) {
			m_s.wpt = m_image->is_write_protected();
		} else {
			m_s.wpt = WRITE_PROT;
		}
		m_s.ready_counter = 0;
		m_s.boot_time = 0;
	}

	if(type != DEVICE_SOFT_RESET) {
		// HARD reset and power on
		//  in SOFT reset motor state is unaffected
		m_s.dskchg = (m_image) ? DOOR_CLOSED : DOOR_OPEN;
		m_s.step_time = 0;
		// motor off
		mon_w(MOT_OFF);
		// not ready, will be when motor on and index is synched
		ready_w(DRV_NOT_READY);
		cache_clear();
	}
}

void FloppyDrive::power_off()
{
	mon_w(MOT_OFF);
}

void FloppyDrive::save_state(StateBuf &_state)
{
	_state.write(&m_s, {sizeof(m_s), str_format("FDD%u",m_drive_index).c_str()});

	if(m_image) {
		std::string imgfile = str_format("%s-floppy%u.bin", _state.get_basename().c_str(), m_drive_index);
		try {
			m_image->save_state(imgfile);
		} catch(std::exception &) {
			PERRF(LOG_FDC, "DRV%u: cannot save image %s\n", m_drive_index, imgfile.c_str());
			throw;
		}
	}
}

void FloppyDrive::restore_state(StateBuf &_state)
{
	// before restoring state drive should be removed and re-installed,
	// this will reset the audio channels.

	PINFOF(LOG_V1, LOG_FDC, "DRV%u: restoring state\n", m_drive_index);

	if(g_program.config().get_bool(m_drive_config, DISK_INSERTED)) {
		std::string binpath = str_format("%s-floppy%u.bin", _state.get_basename().c_str(), m_drive_index);
		std::string imgpath = g_program.config().get_string(m_drive_config, DISK_PATH);
		if(!FileSys::file_exists(binpath.c_str())) {
			PERRF(LOG_FDC, "DRV%u: cannot find state image '%s'\n", m_drive_index, binpath.c_str());
			throw std::exception();
		}
		unsigned type = g_program.config().get_int(m_drive_config, DISK_TYPE);
		uint8_t tracks = g_program.config().get_int(m_drive_config, DISK_CYLINDERS);
		uint8_t heads = g_program.config().get_int(m_drive_config, DISK_HEADS);
		FloppyDisk::Properties props{type, tracks, heads};
		if(type & FloppyDisk::TYPE_MASK) {
			auto std_type_props = FloppyDisk::find_std_type(type);
			if(std_type_props.type) {
				props = std_type_props;
			}
		}
		m_image.reset(m_floppyctrl->create_floppy_disk(props));
		try {
			m_image->load_state(imgpath, binpath);
		} catch(std::exception &) {
			PERRF(LOG_FDC, "DRV%u: cannot restore image %s\n", m_drive_index, binpath.c_str());
			m_image.reset();
			throw;
		}
		m_image->set_write_protected(g_program.config().get_bool(m_drive_config, DISK_READONLY));
		m_disk_changed = true;
		m_dstep = m_dstep_drive && m_image->double_step();
	}

	_state.read(&m_s, {sizeof(m_s), str_format("FDD%u",m_drive_index).c_str()});

	if(m_fx_enabled) {
		m_fx.reset();
		if(is_motor_on() && m_image) {
			m_fx.spin(true,false);
		} else {
			m_fx.spin(false,false);
		}
	}
}

void FloppyDrive::recalibrate()
{
	if(m_s.boot_time == 0) {
		if(m_fx_enabled) {
			m_fx.boot(m_image != nullptr);
		}
		m_s.boot_time = g_machine.get_virt_time_ns();
	}
}

bool FloppyDrive::get_cyl_head(int &_cyl, int &_head)
{
	if(!m_image) {
		return false;
	}
	int t, h;
	m_image->get_maximal_geometry(t, h);
	_cyl = m_s.cyl >> m_dstep;
	_head = m_s.ss;
	if(_cyl >= t || _head >= h) {
		return false;
	}
	return true;
}

void FloppyDrive::read_sector(uint8_t _s, uint8_t *buffer, uint32_t bytes)
{
	int cyl, head;
	if(!get_cyl_head(cyl, head)) {
		return;
	}
	m_image->read_sector(cyl, head, _s, buffer, bytes);
}

void FloppyDrive::write_sector(uint8_t _s, const uint8_t *buffer, uint32_t bytes)
{
	int cyl, head;
	if(!get_cyl_head(cyl, head)) {
		return;
	}
	m_image->write_sector(cyl, head, _s, buffer, bytes);
	m_image->set_dirty();
}

bool FloppyDrive::insert_floppy(FloppyDisk *_disk)
{
	assert(_disk);

	if(m_image) {
		PDEBUGF(LOG_V0, LOG_FDC, "insert_floppy(): eject current floppy first\n");
		return false;
	}

	if(!((m_drive_type & FloppyDisk::SIZE_MASK) & (_disk->props().type & FloppyDisk::SIZE_MASK))) {
		PERRF(LOG_FDC, "The floppy disk size is not compatible with this drive!\n");
		return false;
	}

	PINFOF(LOG_V0, LOG_FDC, "Floppy %s: '%s'%s s=%d,tps=%d\n",
			m_drive_name.c_str(),
			_disk->get_image_path().c_str(),
			_disk->is_write_protected()?" WP":"",
			_disk->props().sides,
			_disk->props().tracks);

	std::lock_guard<std::mutex> lock(m_mutex);
	m_image.reset(_disk);
	g_program.config().set_bool(m_drive_config, DISK_INSERTED, true);
	g_program.config().set_string(m_drive_config, DISK_PATH, _disk->get_image_path());
	g_program.config().set_bool(m_drive_config, DISK_READONLY, _disk->is_write_protected());
	g_program.config().set_int(m_drive_config, DISK_TYPE, _disk->props().type);
	g_program.config().set_int(m_drive_config, DISK_CYLINDERS, _disk->props().tracks);
	g_program.config().set_int(m_drive_config, DISK_HEADS, _disk->props().sides);

	m_dstep = m_dstep_drive && m_image->double_step();
	m_disk_changed = true;

	m_s.dskchg = DOOR_OPEN;
	m_s.wpt = m_image->is_write_protected();
	m_s.rev_start_time = (!is_motor_on()) ? TIME_NEVER : g_machine.get_virt_time_ns();
	m_s.rev_count = 0;

	index_resync();

	if(m_fx_enabled) {
		m_fx.snatch();
	}
	if(is_motor_on()) {
		m_s.ready_counter = 2;
		if(m_fx_enabled) {
			m_fx.spin(true, true);
		}
	}

	return true;
}

FloppyDisk* FloppyDrive::eject_floppy(bool _remove)
{
	FloppyDisk *floppy = nullptr;

	if(m_image) {
		std::lock_guard<std::mutex> lock(m_mutex);

		floppy = m_image.release();

		if(m_fx_enabled && is_motor_on()) {
			m_fx.spin(false, true);
		}

		if(!_remove) {
			PINFOF(LOG_V1, LOG_FDC, "Floppy in drive %s ejected\n", m_drive_name.c_str());
			g_program.config().set_bool(m_drive_config, DISK_INSERTED, false);
			m_disk_changed = true;
		}
	}

	m_s.dskchg = DOOR_OPEN;
	m_s.wpt = WRITE_PROT;

	cache_clear();
	ready_w(DRV_NOT_READY);

	return floppy;
}

void FloppyDrive::play_seek_sound(uint8_t _to_cyl)
{
	if(!m_fx_enabled) {
		return;
	}
	if(is_motor_on()) {
		// head sound effect is sampled from a 80 tracks disk
		m_fx.seek(m_s.cyl, _to_cyl, 80);
	} else {
		PDEBUGF(LOG_V1, LOG_AUDIO, "FDD %s seek: motor is off\n", name());
	}
}

void FloppyDrive::mon_w(bool _state)
{
	// motor on, active low
	if(m_s.mon == _state) {
		return;
	}

	m_s.mon = _state;

	if(m_s.mon == MOT_ON) {
		// off -> on
		PDEBUGF(LOG_V1, LOG_FDC, "DRV%u: motor ON\n", m_drive_index);
		if(m_image) {
			m_s.rev_start_time = g_machine.get_virt_time_ns();
			cache_clear();
			m_s.ready_counter = 2;
			index_resync();
			if(m_fx_enabled) {
				m_fx.spin(true, true);
			}
		}
	} else {
		// on -> off
		PDEBUGF(LOG_V1, LOG_FDC, "DRV%u: motor OFF\n", m_drive_index);
		cache_clear();
		m_s.rev_start_time = TIME_NEVER;
		m_s.rev_count = 0;
		g_machine.deactivate_timer(m_index_timer);
		ready_w(DRV_NOT_READY);
		if(m_fx_enabled) {
			m_fx.spin(false, true);
		}
	}
}

uint64_t FloppyDrive::time_next_index()
{
	if(m_s.rev_start_time == TIME_NEVER) {
		return TIME_NEVER;
	}
	return m_s.rev_start_time + m_rev_time;
}

void FloppyDrive::index_resync()
{
	// index pulses at rpm/60 Hz, and stays high for ~2ms at 300rpm

	if(m_s.rev_start_time == TIME_NEVER) {
		if(m_s.idx) {
			m_s.idx = 0;
			m_floppyctrl->fdd_index_pulse(m_drive_index, m_s.idx);
		}
		return;
	}

	uint64_t delta_ns = g_machine.get_virt_time_ns() - m_s.rev_start_time;
	while(delta_ns >= m_rev_time) {
		delta_ns -= m_rev_time;
		m_s.rev_start_time += m_rev_time;
		m_s.rev_count++;
	}
	// head position expressed in cells
	int position = int(ceil(NSEC_TO_SEC(delta_ns) * m_angular_speed));

	int new_idx = position < 2'000'000;

	uint64_t next_evt_ns = TIME_NEVER;
	if(new_idx) {
		const uint64_t index_up_time_ns = SEC_TO_NSEC(2'000'000.0 / m_angular_speed);
		assert(index_up_time_ns > delta_ns);
		next_evt_ns = index_up_time_ns - delta_ns;
	} else {
		next_evt_ns = m_rev_time - delta_ns;
	}
	g_machine.activate_timer(m_index_timer, next_evt_ns, false);

	if(new_idx != m_s.idx) {
		m_s.idx = new_idx;
		if(m_s.idx && m_s.ready==DRV_NOT_READY) {
			m_s.ready_counter--;
			if(!m_s.ready_counter) {
				// Drive spun up
				ready_w(DRV_READY);
			}
		}
		if(new_idx) {
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: disk index rev: %u\n", 
					m_drive_index, m_s.rev_count);
		}
		m_floppyctrl->fdd_index_pulse(m_drive_index, m_s.idx);
	}
}

void FloppyDrive::ready_w(bool _state)
{
	// inverted
	m_s.ready = _state;
	PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: %sready\n", m_drive_index, _state==DRV_NOT_READY?"not ":"");
}

bool FloppyDrive::trk00_r() const
{
	// inverted
	if(m_s.step_time > g_machine.get_virt_time_ns()) {
		// if it's stepping it's not trk 0
		return !(false);
	}
	return !(m_s.cyl == 0);
}

bool FloppyDrive::twosid_r()
{
	int tracks = 0, heads = 0;

	if(m_image) {
		m_image->get_actual_geometry(tracks, heads);
	}

	return heads == 1;
}

void FloppyDrive::step_to(uint8_t cyl, uint64_t _time_to_reach)
{
	if(!is_motor_on()) {
		return;
	}
	if(cyl != m_s.cyl) {
		cache_clear();
		auto now = g_machine.get_virt_time_ns();
		m_s.step_time = now + _time_to_reach;
		if(m_s.boot_time + 500_ms < now) {
			play_seek_sound(cyl);
		}
		if(cyl >= m_tracks) {
			m_s.cyl = m_tracks - 1;
		} else {
			m_s.cyl = cyl;
		}
		// Update disk detection
		if(m_image) {
			m_s.dskchg = DOOR_CLOSED;
		}
	}
}

void FloppyDrive::stp_w(bool state)
{
	// Before spin-up is done, ignore step pulses
	// TODO: There are reports about drives supporting step operation with
	// stopped spindle. Need to check that on real drives.
	// if (ready_counter > 0) return;

	if(m_s.stp != state) {
		cache_clear();
		m_s.stp = state;
		if(m_s.stp == 0) {
			int ocyl = m_s.cyl;
			if(m_s.dir) {
				if(m_s.cyl) {
					m_s.cyl--;
				}
			} else {
				if(m_s.cyl < m_tracks - 1) {
					m_s.cyl++;
				}
			}
			if(ocyl != m_s.cyl) {
				// TODO Do we want a stepper sound?
				PDEBUGF(LOG_V2, LOG_FDD, "%s: stp %d->%d\n", name(), ocyl, m_s.cyl);
			}
			// Update disk detection
			if(m_image) {
				m_s.dskchg = DOOR_CLOSED;
			}
		}
	}
}

// From http://burtleburtle.net/bob/hash/integer.html
uint32_t FloppyDrive::hash32(uint32_t a) const
{
	a = (a+0x7ed55d16) + (a<<12);
	a = (a^0xc761c23c) ^ (a>>19);
	a = (a+0x165667b1) + (a<<5);
	a = (a+0xd3a2646c) ^ (a<<9);
	a = (a+0xfd7046c5) + (a<<3);
	a = (a^0xb55a4f09) ^ (a>>16);
	return a;
}

int FloppyDrive::find_index(uint32_t position, const std::vector<uint32_t> &buf)const
{
	int spos = (buf.size() >> 1)-1;
	int step;
	for(step=1; step<int(buf.size()+1); step<<=1) { }
	step >>= 1;

	for(;;) {
		if(spos >= int(buf.size()) || (spos > 0 && (buf[spos] & FloppyDisk::TIME_MASK) > position)) {
			spos -= step;
			step >>= 1;
		} else if(spos < 0 || (spos < int(buf.size())-1 && (buf[spos+1] & FloppyDisk::TIME_MASK) <= position)) {
			spos += step;
			step >>= 1;
		} else {
			return spos;
		}
	}

	assert(false);
	return 0;
}

uint32_t FloppyDrive::find_position(uint64_t &base, uint64_t when)
{
	base = m_s.rev_start_time;
	int64_t delta_ns = when - base;

	while(delta_ns >= int64_t(m_rev_time)) {
		delta_ns -= m_rev_time;
		base += m_rev_time;
	}
	while(delta_ns < 0) {
		delta_ns += m_rev_time;
		base -= m_rev_time;
	}

	uint32_t res = uint32_t(NSEC_TO_SEC(delta_ns) * m_angular_speed + 0.5);
	if(res >= 200'000'000) {
		// Due to rounding errors in the previous operation,
		// 'res' sometimes overflows 2E+8
		res -= 200'000'000;
		base += m_rev_time;
	}
	return res;
}

bool FloppyDrive::test_track_last_entry_warps(const std::vector<uint32_t> &buf) const
{
	return !((buf[buf.size() - 1]^buf[0]) & FloppyDisk::MG_MASK);
}

uint64_t FloppyDrive::position_to_time(uint64_t base, int position) const
{
	return base + SEC_TO_NSEC(double(position) / m_angular_speed);
}

void FloppyDrive::cache_fill_index(const std::vector<uint32_t> &buf, int &index, uint64_t &base)
{
	int cells = buf.size();

	if(index != 0 || !test_track_last_entry_warps(buf)) {
		m_s.cache_index = index;
		m_s.cache_start_time = position_to_time(base, buf[index] & FloppyDisk::TIME_MASK);
	} else {
		m_s.cache_index = cells - 1;
		m_s.cache_start_time = position_to_time(base - m_rev_time, buf[m_s.cache_index] & FloppyDisk::TIME_MASK);
	}

	m_s.cache_entry = buf[m_s.cache_index];

	index++;
	if(index >= cells) {
		index = test_track_last_entry_warps(buf) ? 1 : 0;
		base += m_rev_time;
	}

	m_s.cache_end_time = position_to_time(base, buf[index] & FloppyDisk::TIME_MASK);
}

void FloppyDrive::cache_clear()
{
	m_s.cache_start_time = m_s.cache_end_time = m_s.cache_weak_start = 0;
	m_s.cache_index = 0;
	m_s.cache_entry = 0;
	m_s.cache_weak = false;
}

void FloppyDrive::cache_fill(uint64_t when)
{
	int cyl, head;
	if(!get_cyl_head(cyl, head)) {
		m_s.cache_start_time = 0;
		m_s.cache_end_time = TIME_NEVER;
		m_s.cache_index = 0;
		m_s.cache_entry = FloppyDisk::MG_N;
		cache_weakness_setup();
		return;
	}

	std::vector<uint32_t> &buf = m_image->get_buffer(cyl, head);
	uint32_t cells = buf.size();
	if(cells <= 1) {
		m_s.cache_start_time = 0;
		m_s.cache_end_time = TIME_NEVER;
		m_s.cache_index = 0;
		m_s.cache_entry = cells == 1 ? buf[0] : FloppyDisk::MG_N;
		cache_weakness_setup();
		return;
	}

	uint64_t base;
	uint32_t position = find_position(base, when);

	int index = find_index(position, buf);

	if(index == -1) {
		// I suspect this should be an abort(), to check...
		m_s.cache_start_time = 0;
		m_s.cache_end_time = TIME_NEVER;
		m_s.cache_index = 0;
		m_s.cache_entry = buf[0];
		cache_weakness_setup();
		return;
	}

	for(;;) {
		cache_fill_index(buf, index, base);
		if(m_s.cache_end_time > when) {
			cache_weakness_setup();
			return;
		}
	}
}

void FloppyDrive::cache_weakness_setup()
{
	uint32_t type = m_s.cache_entry & FloppyDisk::MG_MASK;
	if(type == FloppyDisk::MG_N || type == FloppyDisk::MG_D) {
		m_s.cache_weak = true;
		m_s.cache_weak_start = m_s.cache_start_time;
		return;
	}

	m_s.cache_weak = (m_s.cache_end_time == TIME_NEVER) || 
			(m_s.cache_end_time - m_s.cache_start_time >= m_s.amplifier_freakout_time);
	if(!m_s.cache_weak) {
		m_s.cache_weak_start = TIME_NEVER;
		return;
	}
	m_s.cache_weak_start = m_s.cache_start_time + 16_us;
}

uint64_t FloppyDrive::get_next_transition(uint64_t from_when)
{
	if(!m_image || !is_motor_on()) {
		return TIME_NEVER;
	}

	if((from_when < m_s.cache_start_time) ||
	   (m_s.cache_start_time == 0) ||
	   (m_s.cache_end_time != TIME_NEVER && from_when >= m_s.cache_end_time))
	{
		cache_fill(from_when);
	}

	if(!m_s.cache_weak) {
		return m_s.cache_end_time;
	}

	// Put a flux transition in the middle of a 4us interval with a 50% probability
	uint64_t interval_index;
	if(from_when < m_s.cache_weak_start) {
		interval_index = 0;
	} else {
		interval_index = time_to_cycles(from_when - m_s.cache_weak_start, 250'000);
	}
	uint64_t weak_time = m_s.cache_weak_start + cycles_to_time(interval_index*2+1, 500'000);
	for(;;) {
		if(weak_time >= m_s.cache_end_time) {
			return m_s.cache_end_time;
		}
		if(weak_time > from_when) {
			uint32_t test = hash32(hash32(hash32(hash32(m_s.rev_count) ^ 0x4242) + m_s.cache_index) + interval_index);
			if(test & 1) {
				return weak_time;
			}
		}
		weak_time += 4_us;
		interval_index++;
	}
	assert(false);
	return 0;
}

void FloppyDrive::write_flux(uint64_t start, uint64_t end, unsigned transition_count, const uint64_t *transitions)
{
	int cyl, head;
	if(!get_cyl_head(cyl, head) || !is_motor_on() || wpt_r()) {
		return;
	}

	m_image->set_dirty();

	cache_clear();

	uint64_t base;
	uint32_t start_pos = find_position(base, start);
	uint32_t end_pos   = find_position(base, end);

	std::vector<uint32_t> trans_pos(transition_count);
	for(unsigned i=0; i != transition_count; i++) {
		trans_pos[i] = find_position(base, transitions[i]);
	}

	std::vector<uint32_t> &buf = m_image->get_buffer(cyl, head);

	int index;
	if(!buf.empty()) {
		index = find_index(start_pos, buf);
	} else {
		index = 0;
		buf.push_back(FloppyDisk::MG_N);
	}

	uint32_t cur_mg;
	if((buf[index] & FloppyDisk::TIME_MASK) == start_pos) {
		if(index) {
			cur_mg = buf[index-1];
		} else {
			cur_mg = buf[buf.size() - 1];
		}
	} else {
			cur_mg = buf[index];
	}

	cur_mg &= FloppyDisk::MG_MASK;
	if(cur_mg == FloppyDisk::MG_N || cur_mg == FloppyDisk::MG_D) {
		cur_mg = FloppyDisk::MG_A;
	}

	uint32_t pos = start_pos;
	unsigned ti = 0;
	int cells = buf.size();
	if(transition_count != 0 && trans_pos[0] == pos) {
		cur_mg = cur_mg == FloppyDisk::MG_A ? FloppyDisk::MG_B : FloppyDisk::MG_A;
		ti++;
	}
	while(pos != end_pos) {
		if(int(buf.size()) < cells+10) {
			buf.resize(cells+200);
		}
		uint32_t next_pos;
		if(ti != transition_count) {
			next_pos = trans_pos[ti++];
		} else {
			next_pos = end_pos;
		}
		if(next_pos > pos) {
			write_zone(&buf[0], cells, index, pos, next_pos, cur_mg);
		} else {
			write_zone(&buf[0], cells, index, pos, 200'000'000, cur_mg);
			index = 0;
			write_zone(&buf[0], cells, index, 0, next_pos, cur_mg);
		}
		pos = next_pos;
		cur_mg = cur_mg == FloppyDisk::MG_A ? FloppyDisk::MG_B : FloppyDisk::MG_A;
	}

	buf.resize(cells);
}

void FloppyDrive::write_zone(uint32_t *buf, int &cells, int &index, uint32_t spos, uint32_t epos, uint32_t mg)
{
	cache_clear();
	while(spos < epos) {
		while(index != cells-1 && (buf[index+1] & FloppyDisk::TIME_MASK) <= spos) {
			index++;
		}

		uint32_t ref_start = buf[index] & FloppyDisk::TIME_MASK;
		uint32_t ref_end   = index == cells-1 ? 200'000'000 : buf[index+1] & FloppyDisk::TIME_MASK;
		uint32_t ref_mg    = buf[index] & FloppyDisk::MG_MASK;

		// Can't overwrite a damaged zone
		if(ref_mg == FloppyDisk::MG_D) {
			spos = ref_end;
			continue;
		}

		// If the zone is of the type we want, we don't need to touch it
		if(ref_mg == mg) {
			spos = ref_end;
			continue;
		}

		//  Check the overlaps, act accordingly
		if(spos == ref_start) {
			if(epos >= ref_end) {
				// Full overlap, that cell is dead, we need to see which ones we can extend
				uint32_t prev_mg = index != 0       ? buf[index-1] & FloppyDisk::MG_MASK : ~0;
				uint32_t next_mg = index != cells-1 ? buf[index+1] & FloppyDisk::MG_MASK : ~0;
				if(prev_mg == mg) {
					if(next_mg == mg) {
						// Both match, merge all three in one
						memmove(buf+index, buf+index+2, (cells-index-2)*sizeof(uint32_t));
						cells -= 2;
						index--;
					} else {
						// Previous matches, drop the current cell
						memmove(buf+index, buf+index+1, (cells-index-1)*sizeof(uint32_t));
						cells--;
					}

				} else {
					if(next_mg == mg) {
						// Following matches, extend it
						memmove(buf+index, buf+index+1, (cells-index-1)*sizeof(uint32_t));
						cells--;
						buf[index] = mg | spos;
					} else {
						// None match, convert the current cell
						buf[index] = mg | spos;
						index++;
					}
				}
				spos = ref_end;

			} else {
				// Overlap at the start only
				// Check if we can just extend the previous cell
				if(index != 0 && (buf[index-1] & FloppyDisk::MG_MASK) == mg) {
					buf[index] = ref_mg | epos;
				} else {
					// Otherwise we need to insert a new cell
					if(index != cells-1) {
						memmove(buf+index+1, buf+index, (cells-index)*sizeof(uint32_t));
					}
					cells++;
					buf[index] = mg | spos;
					buf[index+1] = ref_mg | epos;
				}
				spos = epos;
			}

		} else {
			if(epos >= ref_end) {
				// Overlap at the end only
				// If we can't just extend the following cell, we need to insert a new one
				if(index == cells-1 || (buf[index+1] & FloppyDisk::MG_MASK) != mg) {
					if(index != cells-1) {
						memmove(buf+index+2, buf+index+1, (cells-index-1)*sizeof(uint32_t));
					}
					cells++;
				}
				buf[index+1] = mg | spos;
				index++;
				spos = ref_end;

			} else {
				// Full inclusion
				// We need to split the zone in 3
				if(index != cells-1) {
					memmove(buf+index+3, buf+index+1, (cells-index-1)*sizeof(uint32_t));
				}
				cells += 2;
				buf[index+1] = mg | spos;
				buf[index+2] = ref_mg | epos;
				spos = epos;
			}
		}
	}
}

void FloppyDrive::set_write_splice(uint64_t when)
{
	if(m_image && is_motor_on()) {
		m_image->set_dirty();
		uint64_t base;
		int splice_pos = find_position(base, when);
		m_image->set_write_splice_position(m_s.cyl, m_s.ss, splice_pos);
	}
}

uint8_t FloppyDrive::get_data_rate() const
{
	if(!m_image) {
		return FloppyDisk::DRATE_250;
	}

	if(m_drive_type == FDD_525HD &&
		(m_image->props().type & (FloppyDisk::DENS_DD|FloppyDisk::DENS_QD)))
	{
		// 5.25" High Capacity (1.2M) drives operate at 360rpm, always.
		return FloppyDisk::DRATE_300;
	}

	// return the media nominal data rate
	auto &props = m_image->props();
	return props.drate;
}

const FloppyDisk::Properties & FloppyDrive::get_media_props() const
{
	if(m_image) {
		return m_image->props();
	}
	static FloppyDisk::Properties dummy;
	return dummy;
}