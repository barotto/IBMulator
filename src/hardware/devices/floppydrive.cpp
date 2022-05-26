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
		m_fx.install(m_drive_name,
			(_drive_type & FloppyDisk::SIZE_MASK) == FloppyDisk::SIZE_5_25 ? FloppyFX::FDD_5_25 : FloppyFX::FDD_3_5);
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
		m_s.boot_time = 1;
		if(m_fx_enabled && m_fx.boot(m_image != nullptr)) {
			m_s.boot_time = g_machine.get_virt_time_ns();
		}
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

uint64_t FloppyDrive::position_to_time(uint64_t base, int position) const
{
	return base + SEC_TO_NSEC(double(position) / m_angular_speed);
}

void FloppyDrive::cache_fill_index(const std::vector<uint32_t> &buf, int &index, uint64_t &base)
{
	int cells = buf.size();

	m_s.cache_index = index;
	m_s.cache_start_time = position_to_time(base, buf[index] & FloppyDisk::TIME_MASK);
	m_s.cache_entry = buf[m_s.cache_index];

	index++;
	if(index >= cells) {
		index = 0;
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
	std::vector<wspan> wspans(1);

	wspans[0].start = find_position(base, start);
	wspans[0].end   = find_position(base, end);

	for(unsigned i=0; i != transition_count; i++) {
		wspans[0].flux_change_positions.push_back(find_position(base, transitions[i]));
	}

	wspan_split_on_wrap(wspans);

	std::vector<uint32_t> &buf = m_image->get_buffer(cyl, head);

	if(buf.empty()) {
		buf.push_back(FloppyDisk::MG_N);
		buf.push_back(FloppyDisk::MG_E | 199'999'999);
	}

	wspan_remove_damaged(wspans, buf);
	wspan_write(wspans, buf);

	cache_clear();
}

void FloppyDrive::wspan_split_on_wrap(std::vector<wspan> &wspans)
{
	int ne = wspans.size();
	for(int i=0; i != ne; i++) {
		if(wspans[i].end < wspans[i].start) {
			wspans.resize(wspans.size()+1);
			auto &ws = wspans[i];
			auto &we = wspans.back();
			we.start = 0;
			we.end = ws.end;
			ws.end = 200000000;
			int start = ws.start;
			unsigned split_index;
			for(split_index = 0; split_index != ws.flux_change_positions.size(); split_index++) {
				if(ws.flux_change_positions[split_index] < start) {
					break;
				}
			}
			if(split_index == 0) {
				std::swap(ws.flux_change_positions, we.flux_change_positions);
			}
			else {
				we.flux_change_positions.resize(ws.flux_change_positions.size() - split_index);
				std::copy(ws.flux_change_positions.begin() + split_index, ws.flux_change_positions.end(), we.flux_change_positions.begin());
				ws.flux_change_positions.erase(ws.flux_change_positions.begin() + split_index, ws.flux_change_positions.end());
			}
		}
	}
}

void FloppyDrive::wspan_remove_damaged(std::vector<wspan> &wspans, const std::vector<uint32_t> &track)
{
	for(size_t pos = 0; pos != track.size(); pos++) {
		if((track[pos] & FloppyDisk::MG_MASK) == FloppyDisk::MG_D) {
			int start = track[pos] & FloppyDisk::TIME_MASK;
			int end = track[pos+1] & FloppyDisk::TIME_MASK;
			int ne = wspans.size();
			for(int i=0; i != ne; i++) {
				// D range outside of span range
				if(wspans[i].start > end || wspans[i].end <= start)
					continue;

				// D range covers span range
				if(wspans[i].start >= start && wspans[i].end-1 <= end) {
					wspans.erase(wspans.begin() + i);
					i --;
					ne --;
					continue;
				}

				// D range covers the start of the span range
				if(wspans[i].start >= start && wspans[i].end-1 > end) {
					wspans[i].start = end+1;
					while(!wspans[i].flux_change_positions.empty() && wspans[i].flux_change_positions[0] <= end) {
						wspans[i].flux_change_positions.erase(wspans[i].flux_change_positions.begin());
					}
					continue;
				}

				// D range covers the end of the span range
				if(wspans[i].start < start && wspans[i].end-1 <= end) {
					wspans[i].end = start;
					while(!wspans[i].flux_change_positions.empty() && wspans[i].flux_change_positions[wspans[i].flux_change_positions.size()-1] >= start) {
						wspans[i].flux_change_positions.erase(wspans[i].flux_change_positions.end()-1);
					}
					continue;
				}

				// D range is inside the span range, need to split
				int id = wspans.size();
				wspans.resize(id+1);
				wspans[id].start = end+1;
				wspans[id].end = wspans[i].end;
				wspans[id].flux_change_positions = wspans[i].flux_change_positions;
				wspans[i].end = start;
				while(!wspans[i].flux_change_positions.empty() && wspans[i].flux_change_positions[wspans[i].flux_change_positions.size()-1] >= start) {
					wspans[i].flux_change_positions.erase(wspans[i].flux_change_positions.end()-1);
				}
				while(!wspans[id].flux_change_positions.empty() && wspans[id].flux_change_positions[0] <= end) {
					wspans[id].flux_change_positions.erase(wspans[id].flux_change_positions.begin());
				}
			}
		}
	}
}

void FloppyDrive::wspan_write(const std::vector<wspan> &wspans, std::vector<uint32_t> &track)
{
	for(const auto &ws : wspans) {
		unsigned si, ei;
		for(si = 0; si != track.size(); si++) {
			if(int(track[si] & FloppyDisk::TIME_MASK) >= ws.start) {
				break;
			}
		}
		for(ei = si; ei != track.size(); ei++) {
			if(int(track[ei] & FloppyDisk::TIME_MASK) >= ws.end) {
				break;
			}
		}

		// Reduce neutral zone at the start, if there's one
		if(si != track.size() && (track[si] & FloppyDisk::MG_MASK) == FloppyDisk::MG_E) {
			// Neutral zone is over the whole range, split it and adapt si/ei
			if(si == ei) {
				track.insert(track.begin() + si, FloppyDisk::MG_E | (ws.start-1));
				track.insert(track.begin() + si + 1, (track[si-1] & FloppyDisk::MG_MASK) | ws.end);
				si = ei = si+1;
			} else {
				// Reduce the zone size
				track[si] = FloppyDisk::MG_E | (ws.start-1);
				si ++;
			}
		}

		// Check for a neutral zone at the end and reduce it if needed
		if(ei != track.size() && (track[ei] & FloppyDisk::MG_MASK) == FloppyDisk::MG_E) {
			track[ei-1] = FloppyDisk::MG_N | ws.end;
			ei --;
		}

		// Clear the covered zone
		track.erase(track.begin() + si, track.begin() + ei);

		// Insert the flux changes
		for(auto f : ws.flux_change_positions) {
			track.insert(track.begin() + si, FloppyDisk::MG_F | f);
			si ++;
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