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

/*
	Based of sn76496.c of the M.A.M.E. project
*/

#ifndef IBMULATOR_SN76496
#define IBMULATOR_SN76496

#include "audio/synth.h"

class SN76496 : public SynthChip
{
private:
	struct {
		int  Clock;
		int  SampleRate;
		uint UpdateStep;
		int  VolTable[16]; // volume table
		int  Register[8];  // registers
		int  LastRegister; // last register written
		int  Volume[4];    // volume of voice 0-2 and noise
		uint RNG;          // noise generator
		int  NoiseFB;      // noise feedback mask
		int  Period[4];
		int  Count[4];
		int  Output[4];
	} m_s;

public:
	SN76496();

	void write(uint16_t _value);
	void reset();
	void install(int _clock);
	void remove() {}
	void config_changed(int _rate);
	void generate(int16_t *_buffer, int _samples, int _stride);
	void set_gain(int _gain);
	bool is_silent();
	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
	const char *name() { return "SN76496"; }
};

#endif
