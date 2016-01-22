/*
 * Copyright (C) 2015, 2016  Marco Bortolin
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

struct SN76496
{
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


	void write(uint16_t _value);
	void generate_samples(int16_t *_buffer, int _samples);
	void reset(int _clock, int _rate);
	void set_gain(int _gain);
	bool is_silent();
};

#endif
