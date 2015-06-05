/*
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
	Based on tandy_sound.cpp of the DOSBox project
	(which was based on sn76496.c of the M.A.M.E. project)
*/

#include "ibmulator.h"
#include "sn76496.h"

#define SN76496_MAX_OUTPUT 0x7fff
#define SN76496_STEP 0x10000

/* Formulas for noise generator */
/* bit0 = output */

/* noise feedback for white noise mode (verified on real SN76489 by John Kortink) */
#define SN76496_FB_WNOISE 0x14002	/* (16bits) bit16 = bit0(out) ^ bit2 ^ bit15 */

/* noise feedback for periodic noise mode */
//#define SN76496_FB_PNOISE 0x10000 /* 16bit rorate */
#define SN76496_FB_PNOISE 0x08000   /* JH 981127 - fixes Do Run Run */

/*
0x08000 is definitely wrong. The Master System conversion of Marble Madness
uses periodic noise as a baseline. With a 15-bit rotate, the bassline is
out of tune.
The 16-bit rotate has been confirmed against a real PAL Sega Master System 2.
Hope that helps the System E stuff, more news on the PSG as and when!
*/

/* noise generator start preset (for periodic noise) */
#define SN76496_NG_PRESET 0x0f35


void SN76496::write(uint16_t _value)
{
	/* update the output buffer before changing the registers */

	if(_value & 0x80)
	{
		int r = (_value & 0x70) >> 4;
		int c = r/2;

		LastRegister = r;
		Register[r] = (Register[r] & 0x3f0) | (_value & 0x0f);
		switch (r) {
			case 0:	/* tone 0 : frequency */
			case 2:	/* tone 1 : frequency */
			case 4:	/* tone 2 : frequency */
				Period[c] = UpdateStep * Register[r];
				if(Period[c] == 0) {
					Period[c] = 0x3fe;
				}
				if(r == 4) {
					/* update noise shift frequency */
					if((Register[6] & 0x03) == 0x03) {
						Period[3] = 2 * Period[2];
					}
				}
				break;
			case 1:	/* tone 0 : volume */
			case 3:	/* tone 1 : volume */
			case 5:	/* tone 2 : volume */
			case 7:	/* noise  : volume */
				Volume[c] = VolTable[_value & 0x0f];
				break;
			case 6:	/* noise  : frequency, mode */
				{
					int n = Register[6];
					NoiseFB = (n & 4) ? SN76496_FB_WNOISE : SN76496_FB_PNOISE;
					n &= 3;
					/* N/512,N/1024,N/2048,Tone #3 output */
					if(n == 3) {
						Period[3] = 2 * Period[2];
					} else {
						Period[3] = (UpdateStep << (5+n));
					}
					/* reset noise shifter */
					// RNG = SN76496_NG_PRESET;
					// Output[3] = RNG & 1;
				}
				break;
		}
	}
	else
	{
		int r = LastRegister;
		int c = r/2;

		switch (r) {
			case 0:	/* tone 0 : frequency */
			case 2:	/* tone 1 : frequency */
			case 4:	/* tone 2 : frequency */
				Register[r] = (Register[r] & 0x0f) | ((_value & 0x3f) << 4);
				Period[c] = UpdateStep * Register[r];
				if(Period[c] == 0) {
					Period[c] = 0x3fe;
				}
				if(r == 4) {
					/* update noise shift frequency */
					if((Register[6] & 0x03) == 0x03) {
						Period[3] = 2 * Period[2];
					}
				}
				break;
		}
	}
}

void SN76496::generate_samples(int16_t *_buffer, int _samples)
{
	int i;

	/* If the volume is 0, increase the counter */
	for(i = 0; i < 4; i++) {
		if(Volume[i] == 0) {
			/* note that I do count += samples, NOT count = samples + 1. You might think */
			/* it's the same since the volume is 0, but doing the latter could cause */
			/* interferencies when the program is rapidly modulating the volume. */
			if(Count[i] <= _samples * SN76496_STEP) {
				Count[i] += _samples * SN76496_STEP;
			}
		}
	}

	int count = _samples;
	while(count) {
		int vol[4];
		unsigned int out;
		int left;

		/* vol[] keeps track of how long each square wave stays */
		/* in the 1 position during the sample period. */
		vol[0] = vol[1] = vol[2] = vol[3] = 0;

		for(i = 0; i < 3; i++) {
			if(Output[i]) {
				vol[i] += Count[i];
			}
			Count[i] -= SN76496_STEP;
			/* Period[i] is the half period of the square wave. Here, in each */
			/* loop I add Period[i] twice, so that at the end of the loop the */
			/* square wave is in the same status (0 or 1) it was at the start. */
			/* vol[i] is also incremented by Period[i], since the wave has been 1 */
			/* exactly half of the time, regardless of the initial position. */
			/* If we exit the loop in the middle, Output[i] has to be inverted */
			/* and vol[i] incremented only if the exit status of the square */
			/* wave is 1. */
			while(Count[i] <= 0) {
				Count[i] += Period[i];
				if(Count[i] > 0) {
					Output[i] ^= 1;
					if(Output[i]) {
						vol[i] += Period[i];
					}
					break;
				}
				Count[i] += Period[i];
				vol[i] += Period[i];
			}
			if(Output[i]) {
				vol[i] -= Count[i];
			}
		}

		left = SN76496_STEP;
		do {
			int nextevent;

			if(Count[3] < left) {
				nextevent = Count[3];
			} else {
				nextevent = left;
			}

			if(Output[3]) {
				vol[3] += Count[3];
			}
			Count[3] -= nextevent;
			if(Count[3] <= 0) {
				if(RNG & 1) {
					RNG ^= NoiseFB;
				}
				RNG >>= 1;
				Output[3] = RNG & 1;
				Count[3] += Period[3];
				if(Output[3]) {
					vol[3] += Period[3];
				}
			}
			if(Output[3]) {
				vol[3] -= Count[3];
			}

			left -= nextevent;
		} while(left > 0);

		out = vol[0] * Volume[0] + vol[1] * Volume[1] +
		      vol[2] * Volume[2] + vol[3] * Volume[3];

		if(out > SN76496_MAX_OUTPUT * SN76496_STEP) {
			out = SN76496_MAX_OUTPUT * SN76496_STEP;
		}

		*(_buffer++) = int16_t(out / SN76496_STEP);

		count--;
	}
}

void SN76496::set_gain(int _gain)
{
	int i;
	double out;

	_gain &= 0xff;

	/* increase max output basing on gain (0.2 dB per step) */
	out = SN76496_MAX_OUTPUT / 3;
	while(_gain-- > 0) {
		out *= 1.023292992;	/* = (10 ^ (0.2/20)) */
	}

	/* build volume table (2dB per step) */
	for(i = 0; i < 15; i++)	{
		/* limit volume to avoid clipping */
		if(out > SN76496_MAX_OUTPUT / 3) {
			VolTable[i] = SN76496_MAX_OUTPUT / 3;
		} else {
			VolTable[i] = (int)out;
		}
		out /= 1.258925412;	/* = 10 ^ (2/20) = 2dB */
	}

	VolTable[15] = 0;
}

void SN76496::reset(int _clock, int _rate)
{
	int i;
	SampleRate = _rate;
	/* the base clock for the tone generators is the chip clock divided by 16; */
	/* for the noise generator, it is clock / 256. */
	/* Here we calculate the number of steps which happen during one sample */
	/* at the given sample rate. No. of events = sample rate / (clock/16). */
	/* SN76496_STEP is a multiplier used to turn the fraction into a fixed point */
	/* number. */
	UpdateStep = (unsigned int)(((double)SN76496_STEP * SampleRate * 16) / _clock);
	for(i = 0; i < 4; i++) {
		Volume[i] = 0;
	}
	LastRegister = 0;
	for(i = 0; i < 8; i+=2) {
		Register[i] = 0;
		Register[i + 1] = 0x0f;	/* volume = 0 */
	}

	for(i = 0; i < 4; i++) {
		Output[i] = 0;
		Period[i] = Count[i] = UpdateStep;
	}
	RNG = SN76496_NG_PRESET;
	Output[3] = RNG & 1;
	set_gain(0x1);
}

bool SN76496::is_silent()
{
	return (Volume[0]==0) && (Volume[1]==0) && (Volume[2]==0) && (Volume[3]==0);
}
