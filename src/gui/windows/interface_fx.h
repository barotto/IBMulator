/*
 * Copyright (C) 2015-2023  Marco Bortolin
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

#ifndef IBMULATOR_GUI_INTERFACE_FX_H
#define IBMULATOR_GUI_INTERFACE_FX_H

#include "gui/guifx.h"

class GUIDrivesFX : public GUIFX
{
public:
	enum SampleType {
		FLOPPY_INSERT,
		FLOPPY_EJECT
	};
	enum FDDType {
		FDD_5_25,
		FDD_3_5
	};

private:
	std::vector<AudioBuffer> m_buffers[2];
	const static SoundFX::samples_t ms_samples[2];
	std::atomic<int> m_event;

public:
	GUIDrivesFX() : GUIFX() {}
	void init(Mixer *);
	void use_floppy(FDDType _fdd_type, SampleType _how);
	bool create_sound_samples(uint64_t _time_span_us, bool, bool);
};

class GUISystemFX : public GUIFX
{
private:
	enum SampleType {
		POWER_UP,
		POWER_DOWN,
		POWER_ON
	};
	std::atomic<bool> m_power_on = false;
	std::atomic<bool> m_change_state = false;
	std::vector<AudioBuffer> m_buffers;
	const static SoundFX::samples_t ms_samples;

public:
	GUISystemFX() : GUIFX() {}

	void init(Mixer *_mixer);
	void update(bool _power_on, bool _change_state);
	bool create_sound_samples(uint64_t _time_span_us, bool, bool);
};

#endif
