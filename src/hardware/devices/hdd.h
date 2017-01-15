/*
 * Copyright (C) 2016  Marco Bortolin
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


class HardDiskDrive : public StorageDev
{
protected:
	int m_type;
	uint64_t m_spin_up_duration;
	std::string m_imgpath;
	std::unique_ptr<MediaImage> m_disk;
	bool m_save_on_close;
	bool m_read_only;
	bool m_tmp_disk;

	static const MediaGeometry ms_hdd_types[HDD_DRIVES_TABLE_SIZE];
	static const std::map<uint, DrivePerformance> ms_hdd_performance;
	static const std::map<int, const DriveIdent> ms_hdd_models;

	HardDriveFX m_fx;

public:
	HardDiskDrive();
	~HardDiskDrive();

	void install();
	void remove();
	void power_on(uint64_t _time);
	void power_off();
	void config_changed(const char *_section);
	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

	int type() const { return m_type; }
	uint64_t size() const { return m_disk->hd_size; }

	uint64_t power_up_eta_us() const;

	void read_sector(int64_t _lba, uint8_t *_buffer, unsigned _len);
	void write_sector(int64_t _lba, uint8_t *_buffer, unsigned _len);
	void seek(unsigned _from_cyl, unsigned _to_cyl);

private:
	void get_profile(int _type_id, const char *_section, MediaGeometry &geom_, DrivePerformance &perf_);
	void mount(std::string _imgpath, MediaGeometry _geom, bool _read_only);
	void unmount(bool _save, bool _read_only);
};

#endif

