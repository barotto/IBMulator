/*
 * Copyright (C) 2015-2020  Marco Bortolin
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
 * This is the HDD noise simulator
 */

#ifndef IBMULATOR_HW_HARDDRVFX_H
#define IBMULATOR_HW_HARDDRVFX_H

#include "drivefx.h"


class HardDriveFX : public DriveFX
{
protected:
	enum SampleType {
		HDD_SPIN_UP = 0,
		HDD_SPIN_DOWN,
		HDD_SPIN,
		HDD_SEEK,
		HDD_SEEK_LONG
	};
	std::vector<AudioBuffer> m_buffers;
	const static SoundFX::samples_t ms_samples;

public:
	HardDriveFX();
	~HardDriveFX();

	void install(const std::string &_name);
	uint64_t spin_up_time_us() const;
	void config_changed() {}

	bool create_seek_samples(uint64_t _time_span_ns, bool _prebuf, bool _first_upd);
	bool create_spin_samples(uint64_t _time_span_ns, bool _prebuf, bool _first_upd);
};

#endif
