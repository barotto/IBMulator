/*
 * Copyright (C) 2024  Marco Bortolin
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
 * This is the CD-ROM noise simulator
 */

#ifndef IBMULATOR_HW_CDROMFX_H
#define IBMULATOR_HW_CDROMFX_H

#include "drivefx.h"


class CdRomFX : public DriveFX
{
public:
	enum SampleType {
		CD_SPIN = 0,
		CD_SPIN_UP,
		CD_SPIN_DOWN,
		CD_SEEK_STEP,
		CD_SEEK_OUT,
		CD_SEEK_IN
	};
private:
	static std::vector<AudioBuffer> ms_buffers;
	const static SoundFX::samples_t ms_samples;

public:
	CdRomFX() : DriveFX() {}

	void install(const std::string &_drive);

	uint64_t duration_us(SampleType _sample) const;

	bool create_seek_samples(uint64_t _time_span_ns, bool _prebuf, bool _first_upd);
	bool create_spin_samples(uint64_t _time_span_ns, bool _prebuf, bool _first_upd);
};

#endif
