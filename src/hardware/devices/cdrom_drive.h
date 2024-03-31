/*
 * Copyright (C) 2017-2024  Marco Bortolin
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

#ifndef IBMULATOR_HW_CDROMDRIVE_H
#define IBMULATOR_HW_CDROMDRIVE_H

#include "timers.h"
#include "storagedev.h"
#include "cdrom_disc.h"
#include "cdrom_fx.h"

class CdRomDrive : public StorageDev
{
public:
	enum DiscState {
		DISC_NO_DISC,      // tray closed, no disc present
		DISC_DOOR_OPEN,    // the tray is open
		DISC_DOOR_CLOSING, // the tray is closing
		DISC_SPINNING_UP,  // the disc is spinning up
		DISC_READY,        // the disc is ready to be accessed (rotating)
		DISC_IDLE,         // the disc is inserted but not rotating
		DISC_EJECTING,     // the disc is spinning down before the tray opens
	};
	enum DiscType {
		TYPE_CDROM_DATA,
		TYPE_CDDA_AUDIO,
		TYPE_CDROM_DATA_AUDIO
	};
	enum EventType {
		EVENT_MEDIUM,       // GUI should update (lowest priority)
		EVENT_DOOR_OPENING, // door is opening (play fx, LED blinking)
		EVENT_DOOR_CLOSING, // door is closing (play fx, LED blinking)
		EVENT_SPINNING_UP,  // disc is spinning up (LED blinking)
		EVENT_READ,         // data is being read (LED blinking)
		EVENT_POWER_OFF     // unit is turned off (LED off)
	};
	using ActivityCbFn = std::function<void(EventType what, uint64_t led_duration)>;

private:
	struct {
		DiscState disc;
		bool disc_changed; // for the controller
		bool disc_loaded; // has the PVD been read?
		bool door_locked;
		uint8_t timeout_mult;
	} m_s;
	std::shared_ptr<CdRomDisc> m_disc;
	std::string m_ini_section;
	TimerID m_disc_timer = NULL_TIMER_ID;
	int m_max_speed_x = 1;
	int m_cur_speed_x = 1;

	struct {
		// all times in ns
		uint64_t open_door;
		uint64_t close_door;
		uint64_t spin_up;
		uint64_t spin_down;
		uint64_t read_toc;
		uint64_t to_idle;
	} m_durations;

	std::map<uintptr_t, ActivityCbFn> m_activity_cb;
	CdRomFX m_fx;

public:

	CdRomDrive();

	void install(StorageCtrl *_ctrl, uint8_t _id);
	void remove();
	void config_changed(const char *_section);
	void power_on(uint64_t);
	void power_off();

	void set_durations(uint64_t _open_door_us, uint64_t _close_door_us);

	int max_speed_x() const { return m_max_speed_x; }
	int cur_speed_x() const { return m_cur_speed_x; }
	int max_speed_kb() const { return m_max_speed_x * 176; }
	int cur_speed_kb() const { return m_cur_speed_x * 176; }

	void save_state(StateBuf &);
	void restore_state(StateBuf &);

	bool insert_medium(const std::string &);
	bool is_medium_present();
	bool has_medium_changed(bool _reset = false);

	void open_door();
	uint64_t close_door(bool _force = false);
	void toggle_door_button();
	bool is_door_open();
	void spin_up();
	void spin_down();
	uint64_t time_to_ready_us();
	DiscState disc_state();
	uint8_t disc_type();
	bool is_disc_accessible();
	bool is_door_locked() const { return m_s.door_locked; }
	void lock_door(bool _lock) { m_s.door_locked = _lock; }
	uint8_t timeout_mult() const { return m_s.timeout_mult; }
	void set_timeout_mult(uint8_t);

	bool read_sector(int64_t _lba, uint8_t *_buffer, unsigned _len);
	void seek(unsigned _from_cyl, unsigned _to_cyl);
	uint32_t transfer_time_us(int64_t _xfer_amount);
	uint32_t rotational_latency_us();

	bool get_audio_status(bool &playing_, bool &pause_);
	bool stop_audio();

	bool read_toc(uint8_t *buf_, size_t _bufsize, size_t &length_, bool _msf, unsigned _start_track, unsigned _format);
	bool read_sub_channel(uint8_t *buf_, size_t _bufsize, size_t &length_, bool _msf, bool _subq, unsigned _format);

	void register_activity_cb(uintptr_t _handler, ActivityCbFn _cb) {
		m_activity_cb[_handler] = _cb;
	}
	void unregister_activity_cb(uintptr_t _handler) {
		m_activity_cb.erase(_handler);
	}

private:
	bool is_motor_on() const;
	void timer_handler(uint64_t);
	void remove_medium();
	void update_disc_state();
	uint64_t do_close_door(bool _force);
	void signal_activity(EventType what, uint64_t led_duration);
};

#endif

