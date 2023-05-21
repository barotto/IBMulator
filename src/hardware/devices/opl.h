/*
 * Copyright (C) 2002-2015  The DOSBox Team
 * Copyright (C) 2016-2022  Marco Bortolin
 *
 * This file is part of IBMulator.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Originally based on ADLIBEMU.C, an AdLib/OPL2 emulation library by Ken Silverman
 * Copyright (C) 1998-2001 Ken Silverman
 * Ken Silverman's official web site: "http://www.advsys.net/ken"
 */
#ifndef IBMULATOR_HW_OPL_H
#define IBMULATOR_HW_OPL_H

#include "audio/synth.h"

#define OPL_CHANNELS   18
#define OPL_OPERATORS  (OPL_CHANNELS*2)

class OPL : public SynthChip
{
public:
	enum ChipTypes {
		OPL2, OPL3
	};
	constexpr static const char * ChipNames[] = {
		"YM3812", // OPL2
		"YMF262"  // OPL3
	};

private:
	enum TIMERS {
		T1, T2
	};

	struct OPLTimer {
		int id;
		int increment;
		TimerID index;
		bool masked;
		uint8_t value;
		uint8_t overflow;

		void reset();
		void toggle(bool _value);
		void clear();
		bool timeout();
	};

	/* operator struct definition
		 For OPL2 all 9 channels consist of two operators each, carrier and modulator.
		 Channel x has operators x as modulator and operators (9+x) as carrier.
		 For OPL3 all 18 channels consist either of two operators (2op mode) or four
		 operators (4op mode) which is determined through register4 of the second
		 register set.
		 Only the channels 0,1,2 (first set) and 9,10,11 (second set) can act as
		 4op channels. The two additional operators for a channel y come from the
		 2op channel y+3 so the operators y, (9+y), y+3, (9+y)+3 make up a 4op
		 channel.
	*/
	struct Operator
	{
		int32_t  cval, lastcval;       // current output/last output (used for feedback)
		uint32_t tcount, wfpos, tinc;  // time (position in waveform) and time increment
		double   amp, step_amp;        // and amplification (envelope)
		double   vol;                  // volume
		double   sustain_level;        // sustain level
		int32_t  mfbi;                 // feedback amount
		double   a0, a1, a2, a3;       // attack rate function coefficients
		double   decaymul, releasemul; // decay/release rate functions
		uint32_t op_state;             // current state of operator (attack/decay/sustain/release/off)
		uint32_t toff;
		int32_t  freq_high;            // highest three bits of the frequency, used for vibrato calculations
		int      cur_wform;            // index of start of selected waveform
		uint32_t cur_wmask;            // mask for selected waveform
		uint32_t act_state;            // activity state (regular, percussion)
		bool     sus_keep;             // keep sustain level when decay finished
		bool     vibrato, tremolo;     // vibrato/tremolo enable bits

		// variables used to provide non-continuous envelopes
		uint32_t generator_pos;        // for non-standard sample rates we need to determine how many samples have passed
		int      cur_env_step;         // current (standardized) sample position
		int      env_step_a,           // number of std samples of one step (for attack/decay/release mode)
		         env_step_d,
		         env_step_r;
		uint8_t  step_skip_pos_a;      // position of 8-cyclic step skipping (always 2^x to check against mask)
		int      env_step_skip_a;      // bitmask that determines if a step is skipped (respective bit is zero then)

		bool     is_4op, is_4op_attached;// OPL3 base of a 4op channel/part of a 4op channel
		int32_t  left_pan, right_pan;    // OPL3 stereo panning amount

		ChipTypes type;
		double    recipsamp;           // inverse of sampling rate

		void enable(uint8_t *wave_sel, unsigned regbase, uint32_t act_type);
		void disable(uint32_t act_type);

		void advance(int32_t vib, uint32_t generator_add, uint32_t *pos=nullptr);
		void output(int32_t modulator, int32_t trem);

		void attack();
		void decay();
		void release();
		void sustain();
		void off();
		void exec();

		void change_frequency(uint8_t *regs, unsigned chanbase, unsigned regbase);
		void change_attackrate(uint8_t *regs, unsigned regbase);
		void change_decayrate(uint8_t *regs, unsigned regbase);
		void change_releaserate(uint8_t *regs, unsigned regbase);
		void change_sustainlevel(uint8_t *regs, unsigned regbase);
		void change_waveform(uint8_t *wave_sel, unsigned regbase);
		void change_keepsustain(uint8_t *regs, unsigned regbase);
		void change_vibrato(uint8_t *regs, unsigned regbase);
		void change_feedback(uint8_t *regs, unsigned chanbase);
	};

	struct {
		Operator op[OPL_OPERATORS];
		OPLTimer timers[2];
		uint32_t reg_index;
		uint8_t  regs[512];    // register set (including second set for OPL3)
		uint8_t  wave_sel[44]; // waveform selection
		uint32_t vibtab_pos;   // vibrato counter
		uint32_t tremtab_pos;  // tremolo counter
	} m_s;

	std::string m_name;
	ChipTypes   m_type = OPL2;
	int         m_samplerate = 49716;
	uint32_t    m_generator_add = 0;
	uint32_t    m_vibtab_add = 0;
	uint32_t    m_tremtab_add = 0;

	std::function<void(bool)> m_irqfn;

public:
	OPL();
	~OPL() {}
	
	void install(ChipTypes _type, std::string _name, bool _timers);
	void remove();
	void config_changed(int _samplerate);
	uint8_t read(unsigned _port);
	void write(unsigned _port, uint8_t _value);
	void write_timers(int _index, uint8_t _value);
	void reset();
	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
	void generate(int16_t *_buffer, int _frames, int _stride);
	bool is_silent();
	const char *name() { return m_name.c_str(); }
	void set_IRQ_callback(std::function<void(bool)> _fn) {
		m_irqfn = _fn;
	}

private:
	void timer(int id);
	static void init_tables();
	void advance_drums(Operator* op_pt1, int32_t vib1,
	                   Operator* op_pt2, int32_t vib2,
	                   Operator* op_pt3, int32_t vib3);
	bool is_opl3_mode() const { return (m_type==OPL3 && (m_s.regs[0x105] & 1)); }
};

#endif
