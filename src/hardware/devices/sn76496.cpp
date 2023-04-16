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

		m_s.LastRegister = r;
		m_s.Register[r] = (m_s.Register[r] & 0x3f0) | (_value & 0x0f);
		switch (r) {
			case 0:	/* tone 0 : frequency */
			case 2:	/* tone 1 : frequency */
			case 4:	/* tone 2 : frequency */
				m_s.Period[c] = m_s.UpdateStep * m_s.Register[r];
				if(m_s.Period[c] == 0) {
					m_s.Period[c] = 0x3fe;
				}
				if(r == 4) {
					/* update noise shift frequency */
					if((m_s.Register[6] & 0x03) == 0x03) {
						m_s.Period[3] = 2 * m_s.Period[2];
					}
				}
				break;
			case 1:	/* tone 0 : volume */
			case 3:	/* tone 1 : volume */
			case 5:	/* tone 2 : volume */
			case 7:	/* noise  : volume */
				m_s.Volume[c] = m_s.VolTable[_value & 0x0f];
				break;
			case 6:	/* noise  : frequency, mode */
				{
					int n = m_s.Register[6];
					m_s.NoiseFB = (n & 4) ? SN76496_FB_WNOISE : SN76496_FB_PNOISE;
					n &= 3;
					/* N/512,N/1024,N/2048,Tone #3 output */
					if(n == 3) {
						m_s.Period[3] = 2 * m_s.Period[2];
					} else {
						m_s.Period[3] = (m_s.UpdateStep << (5+n));
					}
					/* reset noise shifter */
					// m_s.RNG = SN76496_NG_PRESET;
					// m_s.Output[3] = m_s.RNG & 1;
				}
				break;
		}
	}
	else
	{
		int r = m_s.LastRegister;
		int c = r/2;

		switch (r) {
			case 0:	/* tone 0 : frequency */
			case 2:	/* tone 1 : frequency */
			case 4:	/* tone 2 : frequency */
				m_s.Register[r] = (m_s.Register[r] & 0x0f) | ((_value & 0x3f) << 4);
				m_s.Period[c] = m_s.UpdateStep * m_s.Register[r];
				if(m_s.Period[c] == 0) {
					m_s.Period[c] = 0x3fe;
				}
				if(r == 4) {
					/* update noise shift frequency */
					if((m_s.Register[6] & 0x03) == 0x03) {
						m_s.Period[3] = 2 * m_s.Period[2];
					}
				}
				break;
		}
	}
}

void SN76496::generate(int16_t *_buffer, int _samples, int _stride)
{
	int i;
	int stride = _stride;
	if(stride <= 0) {
		stride = 1;
	}

	/* If the volume is 0, increase the counter */
	for(i = 0; i < 4; i++) {
		if(m_s.Volume[i] == 0) {
			/* note that I do count += samples, NOT count = samples + 1. You might think */
			/* it's the same since the volume is 0, but doing the latter could cause */
			/* interferencies when the program is rapidly modulating the volume. */
			if(m_s.Count[i] <= _samples * SN76496_STEP) {
				m_s.Count[i] += _samples * SN76496_STEP;
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
			if(m_s.Output[i]) {
				vol[i] += m_s.Count[i];
			}
			m_s.Count[i] -= SN76496_STEP;
			/* m_s.Period[i] is the half period of the square wave. Here, in each */
			/* loop I add m_s.Period[i] twice, so that at the end of the loop the */
			/* square wave is in the same status (0 or 1) it was at the start. */
			/* vol[i] is also incremented by m_s.Period[i], since the wave has been 1 */
			/* exactly half of the time, regardless of the initial position. */
			/* If we exit the loop in the middle, m_s.Output[i] has to be inverted */
			/* and vol[i] incremented only if the exit status of the square */
			/* wave is 1. */
			while(m_s.Count[i] <= 0) {
				m_s.Count[i] += m_s.Period[i];
				if(m_s.Count[i] > 0) {
					m_s.Output[i] ^= 1;
					if(m_s.Output[i]) {
						vol[i] += m_s.Period[i];
					}
					break;
				}
				m_s.Count[i] += m_s.Period[i];
				vol[i] += m_s.Period[i];
			}
			if(m_s.Output[i]) {
				vol[i] -= m_s.Count[i];
			}
		}

		left = SN76496_STEP;
		do {
			int nextevent;

			if(m_s.Count[3] < left) {
				nextevent = m_s.Count[3];
			} else {
				nextevent = left;
			}

			if(m_s.Output[3]) {
				vol[3] += m_s.Count[3];
			}
			m_s.Count[3] -= nextevent;
			if(m_s.Count[3] <= 0) {
				if(m_s.RNG & 1) {
					m_s.RNG ^= m_s.NoiseFB;
				}
				m_s.RNG >>= 1;
				m_s.Output[3] = m_s.RNG & 1;
				m_s.Count[3] += m_s.Period[3];
				if(m_s.Output[3]) {
					vol[3] += m_s.Period[3];
				}
			}
			if(m_s.Output[3]) {
				vol[3] -= m_s.Count[3];
			}

			left -= nextevent;
		} while(left > 0);

		out = vol[0] * m_s.Volume[0] + vol[1] * m_s.Volume[1] +
		      vol[2] * m_s.Volume[2] + vol[3] * m_s.Volume[3];

		if(out > SN76496_MAX_OUTPUT * SN76496_STEP) {
			out = SN76496_MAX_OUTPUT * SN76496_STEP;
		}

		*_buffer = int16_t(out / SN76496_STEP);
		_buffer += stride;

		count--;
	}
}

void SN76496::set_gain(int _gain)
{
	int i;
	double out;

	_gain &= 0xff;

	/* increase max output basing on gain (0.2 dB per step) */
	out = (int)(SN76496_MAX_OUTPUT / 3);
	while(_gain-- > 0) {
		out *= 1.023292992;	/* = (10 ^ (0.2/20)) */
	}

	/* build volume table (2dB per step) */
	for(i = 0; i < 15; i++)	{
		/* limit volume to avoid clipping */
		if(out > (int)(SN76496_MAX_OUTPUT / 3)) {
			m_s.VolTable[i] = (int)(SN76496_MAX_OUTPUT / 3);
		} else {
			m_s.VolTable[i] = (int)out;
		}
		out /= 1.258925412;	/* = 10 ^ (2/20) = 2dB */
	}

	m_s.VolTable[15] = 0;
}

void SN76496::reset()
{
	int i;
	for(i = 0; i < 4; i++) {
		m_s.Volume[i] = 0;
	}
	m_s.LastRegister = 0;
	for(i = 0; i < 8; i+=2) {
		m_s.Register[i] = 0;
		m_s.Register[i + 1] = 0x0f;	/* volume = 0 */
	}
	for(i = 0; i < 4; i++) {
		m_s.Output[i] = 0;
		m_s.Period[i] = m_s.Count[i] = m_s.UpdateStep;
	}
	m_s.RNG = SN76496_NG_PRESET;
	m_s.Output[3] = m_s.RNG & 1;
	set_gain(0x1);
}

void SN76496::install(int _clock)
{
	m_s.Clock = _clock;
}

void SN76496::config_changed(int _rate)
{
	m_s.SampleRate = _rate;
	/* the base clock for the tone generators is the chip clock divided by 16; */
	/* for the noise generator, it is clock / 256. */
	/* Here we calculate the number of steps which happen during one sample */
	/* at the given sample rate. No. of events = sample rate / (clock/16). */
	/* SN76496_STEP is a multiplier used to turn the fraction into a fixed point */
	/* number. */
	m_s.UpdateStep = (unsigned int)(((double)SN76496_STEP * m_s.SampleRate * 16) / m_s.Clock);
}

bool SN76496::is_silent()
{
	return (m_s.Volume[0]==0) && (m_s.Volume[1]==0) && (m_s.Volume[2]==0) && (m_s.Volume[3]==0);
}

void SN76496::save_state(StateBuf &_state)
{
	_state.write(&m_s, {sizeof(m_s), name()});
}

void SN76496::restore_state(StateBuf &_state)
{
	_state.read(&m_s, {sizeof(m_s), name()});
}
