/*
 * Copyright (C) 2016-2024  Marco Bortolin
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

#ifndef IBMULATOR_HW_HDD_H
#define IBMULATOR_HW_HDD_H

#include "storagedev.h"
#include "harddrvfx.h"
#include <memory>

#define HDD_DRIVES_TABLE_SIZE 45
#define HDD_CUSTOM_DRIVE_IDX 47

// The following factors were derived from measurements of a WDL-330P specimen.
// 0.99378882 = average speed = 32.0 / ((921-1)*35/1000.0), 35=avg speed in us/cyl
// 1.6240 = maximum speed in mm/ms
// 0.3328 = acceleration in mm/ms^2
#define HDD_HEAD_SPEED (1.6240 / 0.99378882)
#define HDD_HEAD_ACCEL (0.3328 / 0.99378882)
#define HDD_DISK_RADIUS 32.0


class StorageCtrl;

class HardDiskDrive : public StorageDev
{
protected:
	int m_type;
	uint64_t m_spin_up_duration;
	std::unique_ptr<MediaImage> m_disk;
	bool m_tmp_disk;

	struct {
		uint64_t power_on_time;
		bool dirty;
	} m_s;
	bool m_dirty_restore = false;

	static const MediaGeometry ms_hdd_types[HDD_DRIVES_TABLE_SIZE];
	static const std::map<unsigned, DrivePerformance> ms_hdd_performance;
	static const std::map<int, const DriveIdent> ms_hdd_models;
	static const std::map<uint64_t, int> ms_hdd_sizes;

	HardDriveFX m_fx;

public:
	HardDiskDrive();
	~HardDiskDrive();

	void install(StorageCtrl* _ctrl, uint8_t _id, const char * _ini_section);
	void remove();
	void power_on(uint64_t _time);
	void power_off();
	void config_changed();
	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
	bool is_read_only() const { return false; }
	bool is_dirty(bool _since_restore = false) const {
		return (_since_restore) ? m_dirty_restore : m_s.dirty;
	}
	void commit() const;

	int type() const { return m_type; }
	uint64_t size() const { return m_disk->size(); }

	uint64_t power_up_eta_us() const;

	bool read_sector(int64_t _lba, uint8_t *_buffer, unsigned _len);
	bool write_sector(int64_t _lba, uint8_t *_buffer, unsigned _len);
	void seek(unsigned _from_cyl, unsigned _to_cyl);

	static int64_t get_hdd_type_size(int _hdd_type);

private:
	void get_profile(int _type_id, const char *_section, MediaGeometry &geom_, DrivePerformance &perf_);
	void mount(std::string _imgpath, MediaGeometry _geom, bool _tmp_img);
	void unmount();

	void set_dirty() {
		m_s.dirty = true;
		m_dirty_restore = true;
	}
};

#endif

