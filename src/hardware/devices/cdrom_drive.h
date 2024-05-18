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

#ifndef IBMULATOR_HW_CDROMDRIVE_H
#define IBMULATOR_HW_CDROMDRIVE_H

#include "timers.h"
#include "storagedev.h"
#include "cdrom_disc.h"
#include "cdrom_fx.h"
#include "cdrom_events.h"

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
	enum AudioStatus {
		AUDIO_PLAYING = 0x11,      // Play operation in progress
		AUDIO_PAUSED = 0x12,       // Play operation paused
		AUDIO_SUCCESS_STOP = 0x13, // Play operation successfully completed
		AUDIO_ERROR_STOP = 0x14,   // Play operation stopped due to error
		AUDIO_NO_STATUS = 0x15     // No current audio status to return
	};

private:
	struct {
		DiscState disc;
		bool disc_changed; // for the controller
		bool disc_loaded; // has the PVD been read? (timed)
		bool door_locked;
		int cur_speed_x;
		uint64_t speed_change_time;
		uint8_t timeout_mult;
		struct Audio {
			int64_t start_sector;
			int64_t end_sector;
			uint32_t played_pcm_frames;
			uint32_t total_redbook_frames;
			uint32_t total_pcm_frames;
			uint64_t seek_delay_ns;
			bool is_playing;
			bool is_paused;
			bool completed; // true after last audio play is stopped
			bool error; // true if last audio play error
			bool head_pos_valid;
			bool sotc; // Stop On Track Crossing
			uint8_t port0_ch;
			uint8_t port0_vol;
			uint8_t port1_ch;
			uint8_t port1_vol;

			void to_start_state() {
				is_playing = true;
				is_paused = false;
				completed = false;
				error = false;
			}
			void to_stop_state(bool _error = false) {
				is_playing = false;
				is_paused = false;
				completed = true;
				error = _error;
			}
		} audio;
	} m_s;
	std::unique_ptr<CdRomDisc> m_disc;
	TimerID m_disc_timer = NULL_TIMER_ID;
	int m_max_speed_x = 1;

	struct {
		// all times in ns
		uint64_t open_door;
		uint64_t close_door;
		uint64_t spin_up;
		uint64_t spin_down;
		uint64_t read_toc;
		uint64_t to_idle;
		uint64_t to_max_speed;
	} m_durations;

	struct Audio {
		std::shared_ptr<MixerChannel> channel;
		CdRomDisc::TrackIterator track;
		std::mutex player_mutex;
		std::mutex channel_mutex;
	} m_audio;

	std::map<uintptr_t, CdRomEvents::ActivityCbFn> m_activity_cb;
	CdRomFX m_fx;

public:

	CdRomDrive();

	void install(StorageCtrl *_ctrl, uint8_t _id, const char *_ini_section);
	void remove();
	void config_changed();
	void power_on(uint64_t);
	void power_off();

	void set_durations(uint64_t _open_door_us, uint64_t _close_door_us);

	int max_speed_x() const { return m_max_speed_x; }
	int cur_speed_x() const { return m_s.cur_speed_x; }
	int max_speed_kb() const { return m_max_speed_x * 176; }
	int cur_speed_kb() const { return m_s.cur_speed_x * 176; }

	void save_state(StateBuf &);
	void restore_state(StateBuf &);

	void insert_medium(CdRomDisc *_disc, std::string _path);
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
	void set_sotc(bool);
	void set_audio_port(uint8_t _port, uint8_t _ch, uint8_t _vol);
	std::pair<uint8_t,uint8_t> get_audio_port(uint8_t _port);

	bool read_sector(int64_t _lba, uint8_t *_buffer, unsigned _len);
	void seek(unsigned _from_cyl, unsigned _to_cyl);
	uint32_t transfer_time_us(int64_t _xfer_amount);
	uint32_t rotational_latency_us();

	bool check_play_audio(int64_t &_start_lba_, int64_t _end_lba, uint8_t &sense_, uint8_t &asc_);
	void play_audio(int64_t _start_lba, int64_t _end_lba, uint64_t _play_delay);
	AudioStatus get_audio_status(bool _reset, int64_t *curr_lba_ = nullptr);
	bool pause_resume_audio(bool _resume);
	void stop_audio(bool _error = false, bool _audio_lock = true);
	void lock_audio() { m_audio.player_mutex.lock(); }
	void unlock_audio() { m_audio.player_mutex.unlock(); }

	bool read_toc(uint8_t *buf_, size_t _bufsize, size_t &length_, bool _msf, unsigned _start_track, unsigned _format);
	bool read_sub_channel(uint8_t *buf_, size_t _bufsize, size_t &length_, bool _msf, bool _subq, unsigned _format,
			int64_t _curr_lba, uint8_t &sense_, uint8_t &asc_);

	void register_activity_cb(uintptr_t _handler, CdRomEvents::ActivityCbFn _cb) {
		m_activity_cb[_handler] = _cb;
	}
	void unregister_activity_cb(uintptr_t _handler) {
		m_activity_cb.erase(_handler);
	}
	void signal_activity(CdRomEvents::EventType what, uint64_t led_duration);

private:
	int64_t curr_audio_lba() const;
	bool start_audio_track(int64_t _start_lba, int64_t _end_lba, bool _seek);
	void start_audio(bool _audio_lock);
	bool is_motor_on() const;
	void activate_timer(uint64_t _nsecs, const char *_reason);
	void deactivate_timer(const char *_reason);
	void timer_handler(uint64_t);
	void insert_disc(CdRomDisc *_disc, std::string _path);
	void remove_disc();
	void remove_medium();
	void update_disc_state();
	uint64_t do_close_door(bool _force);
	void create_audio_samples(uint64_t _time_span_ns, bool _first_upd);
	void update_volumes();
};

#endif

