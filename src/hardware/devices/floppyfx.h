/*
 * Copyright (C) 2015, 2016  Marco Bortolin
 *
 * This file is part of IBMulator
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
 * This is the FDD noise simulator
 */

#ifndef IBMULATOR_HW_FLOPPYFX_H
#define IBMULATOR_HW_FLOPPYFX_H

#include "drivefx.h"


class FloppyFX : public DriveFX
{
private:
	std::atomic<bool> m_snatch;
	enum SampleType {
		FDD_SPIN = 0,
		FDD_SPIN_UP,
		FDD_SPIN_DOWN,
		FDD_SEEK_STEP,
		FDD_SEEK_UP,
		FDD_SEEK_DOWN,
		FDD_SNATCH,
		FDD_BOOT,
		FDD_BOOT_DISK
	};
	static std::vector<AudioBuffer> ms_buffers;
	const static SoundFX::samples_t ms_samples;
	uint64_t m_booting;
	uint64_t m_spin_time;

public:
	FloppyFX();
	~FloppyFX();

	void init(const std::string &_drive);
	void config_changed();
	void spin(bool _spinning, bool _change_state);
	void snatch(bool _value=true) { m_snatch = _value; }
	void boot(bool _wdisk);

	bool create_seek_samples(uint64_t _time_span_us, bool _prebuf, bool _first_upd);
	bool create_spin_samples(uint64_t _time_span_us, bool _prebuf, bool _first_upd);
};

#endif
