// license:BSD-3-Clause
// copyright-holders:Nathan Woods, Olivier Galibert, Miodrag Milanovic, Marco Bortolin

// Based on MAME's devices/imagedev/floppy.h

#ifndef IBMULATOR_HW_FLOPPYDRIVE_H
#define IBMULATOR_HW_FLOPPYDRIVE_H

#include "floppydisk.h"
#include "floppyfx.h"
#include "floppyevents.h"

class FloppyCtrl;

class FloppyDrive
{
public:
	enum Type {
		FDD_NONE  = 0x00,
		FDD_525DD = FloppyDisk::SIZE_5_25 | FloppyDisk::DENS_DD | FloppyDisk::DENS_QD,
		FDD_525HD = FloppyDisk::SIZE_5_25 | FloppyDisk::DENS_DD | FloppyDisk::DENS_QD | FloppyDisk::DENS_HD,
		FDD_350DD = FloppyDisk::SIZE_3_5  | FloppyDisk::DENS_DD,
		FDD_350HD = FloppyDisk::SIZE_3_5  | FloppyDisk::DENS_DD | FloppyDisk::DENS_HD,
		FDD_350ED = FloppyDisk::SIZE_3_5  | FloppyDisk::DENS_DD | FloppyDisk::DENS_HD | FloppyDisk::DENS_ED
	};

protected:
	Type m_drive_type = FDD_NONE;
	std::string m_drive_type_desc;
	unsigned m_drive_index = 0;
	std::string m_drive_name;
	std::string m_drive_config;
	FloppyCtrl *m_floppyctrl = nullptr;

	TimerID m_index_timer = NULL_TIMER_ID;

	std::unique_ptr<FloppyDisk> m_image;

	FloppyFX m_fx;
	bool m_fx_enabled = false;
	bool m_disk_changed = false; // for GUI use
	std::mutex m_mutex;  // for GUI access

	// Physical characteristics
	int m_tracks = 0; // addressable tracks
	int m_sides = 0;  // number of heads
	float m_rpm = 0.f; // rotation per minute => gives index pulse frequency
	double m_angular_speed = 0.0; // angular speed in cells per second, where a full circle is 2e8 cells
	uint64_t m_rev_time = 0; // time of 1 disk revolution in ns 
	bool m_dstep_drive = false;
	bool m_dstep = false;

	enum : bool {
		MOT_ON  = 0,
		MOT_OFF = 1,
		DRV_READY = 0,
		DRV_NOT_READY = 1,
		DOOR_OPEN = 0,
		DOOR_CLOSED = 1,
		WRITE_PROT = 1,
		WRITE_NOT_PROT = 0
	};
	struct {
		// input lines
		bool dir; // direction (inv)
		bool stp; // step (inv)
		bool mon; // motor on (inv)
		bool ss;  // side select

		// output lines
		bool idx;    // index pulse
		bool wpt;    // write protect
		bool dskchg; // disk changed (inv)
		bool ready;  // drive ready (inv)

		int cyl; // current head cylinder position
		uint64_t step_time; // drive is being stepped and cyl will be reached at this point in time

		uint64_t rev_start_time;
		uint32_t rev_count;

		// Current floppy zone cache
		uint64_t cache_start_time;
		uint64_t cache_end_time;
		uint64_t cache_weak_start;
		uint64_t amplifier_freakout_time;
		int cache_index;
		uint32_t cache_entry;
		bool cache_weak;

		int  ready_counter;

		uint64_t spin_boot_time; // boot time or event (for SoundFX)
		uint64_t seek_boot_time; // boot time or event (for SoundFX)
	} m_s;

	FloppyEvents::ActivityCbFn m_activity_cb;

public:
	FloppyDrive();
	virtual ~FloppyDrive() {}

	void install(FloppyCtrl *_ctrl, int _drive_index, Type _drive_type);
	void remove();
	void reset(unsigned _type);
	void power_off();

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

	Type type() const { return m_drive_type; }
	const char *name() const { return m_drive_name.c_str(); }
	const char *description() const { return m_drive_type_desc.c_str(); }

	bool insert_floppy(FloppyDisk *_floppy);
	FloppyDisk* eject_floppy(bool _remove);
	bool is_media_present() const { return m_image != nullptr; }
	std::string get_media_path() const;
	bool is_media_dirty(bool _since_restore) const {
		return (m_image && m_image->is_dirty(_since_restore));
	}
	bool can_media_be_committed() const {
		return (m_image && m_image->can_be_committed());
	}
	bool is_double_step_media() const { return m_dstep; }
	bool is_motor_on() const { return m_s.mon == MOT_ON; }

	int get_cyl() const { return m_s.cyl; }

	int tracks() const { return m_tracks; }
	int sides() const { return m_sides; }

	bool wpt_r() const { return m_s.wpt; }
	bool dskchg_r() const { return m_s.dskchg; } // inverted
	bool trk00_r() const; // inverted
	bool idx_r() const { return m_s.idx; }
	bool mon_r() const { return m_s.mon; } // inverted
	bool ss_r() const { return m_s.ss; }
	bool ready_r() const { return m_s.ready; }  // inverted
	bool twosid_r();

	void dir_w(bool state) { m_s.dir = state; } // inverted
	void stp_w(bool state); // inverted
	void mon_w(bool state); // inverted
	void ss_w(bool state) { if (m_sides > 1) m_s.ss = state; }
	void ready_w(bool state); // inverted

	void step_to(uint8_t cyl, uint64_t _step_time);

	void index_resync();
	uint64_t time_next_index();
	uint64_t get_next_transition(uint64_t from_when_ns);
	void write_flux(uint64_t start_ns, uint64_t end_ns, unsigned transition_count, const uint64_t *transitions_ns);
	void set_write_splice(uint64_t when_ns);

	inline bool has_disk_changed() {
		std::lock_guard<std::mutex> lock(m_mutex);
		bool changed = m_disk_changed;
		m_disk_changed = false;
		return changed;
	}

	uint8_t get_data_rate() const;
	const FloppyDisk::Properties & get_media_props() const;

	void read_sector(uint8_t _s, uint8_t *buffer, uint32_t bytes);
	void write_sector(uint8_t _s, const uint8_t *buffer, uint32_t bytes);

	void register_activity_cb(FloppyEvents::ActivityCbFn _cb) {
		m_activity_cb = _cb;
	}

protected:
	void set_type(Type _drive_type);
	void set_rpm(float _rpm);

	void index_timer(uint64_t);

	uint32_t find_position(uint64_t &base_ns, uint64_t when_ns);
	uint64_t position_to_time(uint64_t base_ns, int position) const;

	bool get_cyl_head(int &_cyl, int &_head);

	// Temporary structure storing a write span
	struct wspan {
		int start, end;
		std::vector<int> flux_change_positions;
	};

	static void wspan_split_on_wrap(std::vector<wspan> &wspans);
	static void wspan_remove_damaged(std::vector<wspan> &wspans, const std::vector<uint32_t> &track);
	static void wspan_write(const std::vector<wspan> &wspans, std::vector<uint32_t> &track);

	uint32_t hash32(uint32_t val) const;

	void cache_clear();
	void cache_fill_index(const std::vector<uint32_t> &buf, int &index, uint64_t &base_ns);
	void cache_fill(uint64_t when_ns);
	void cache_weakness_setup();

	void play_seek_sound(uint8_t _to_cyl);
};


#endif
