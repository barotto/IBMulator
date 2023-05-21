/*
 * Copyright (C) 2002-2015  The DOSBox Team
 * Copyright (C) 2016-2023  Marco Bortolin
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

#define _USE_MATH_DEFINES
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "ibmulator.h"
#include "machine.h"
#include "opl.h"

#define OPL_THREADED_RENDERING true

constexpr const char * OPL::ChipNames[];

#define FL05 0.5
#define FL2  2.0

#define FIXEDPT      0x10000    // fixed-point calculations using 16+16
#define FIXEDPT_LFO  0x1000000  // fixed-point calculations using 8+24

#define WAVEPREC  1024  // waveform precision (10 bits)

#define INTFREQU  (14318180.0 / 288.0) // clocking of the chip

#define OF_TYPE_ATT         0
#define OF_TYPE_DEC         1
#define OF_TYPE_REL         2
#define OF_TYPE_SUS         3
#define OF_TYPE_SUS_NOKEEP  4
#define OF_TYPE_OFF         5

#define ARC_CONTROL      0x00
#define ARC_TVS_KSR_MUL  0x20
#define ARC_KSL_OUTLEV   0x40
#define ARC_ATTR_DECR    0x60
#define ARC_SUSL_RELR    0x80
#define ARC_FREQ_NUM     0xa0
#define ARC_KON_BNUM     0xb0
#define ARC_PERC_MODE    0xbd
#define ARC_FEEDBACK     0xc0
#define ARC_WAVE_SEL     0xe0

#define ARC_SECONDSET  0x100 // second operator set for OPL3
#define OPL3_MODE      0x105 // OPL3 mode register

#define OP_ACT_OFF     0x00
#define OP_ACT_NORMAL  0x01 // regular channel activated (bitmasked)
#define OP_ACT_PERC    0x02 // percussion channel activated (bitmasked)

#define BLOCKBUF_SIZE  512

// vibrato constants
#define VIBTAB_SIZE  8
#define VIBFAC       70/50000 // no braces, integer mul/div

// tremolo constants and table
#define TREMTAB_SIZE  53
#define TREM_FREQ     3.7 // tremolo at 3.7hz

static int16_t wavtable[WAVEPREC*3]; // wave form table

// vibrato/tremolo tables
static int32_t vib_table[VIBTAB_SIZE];
static int32_t trem_table[TREMTAB_SIZE*2];

static int32_t vibval_const[BLOCKBUF_SIZE];
static int32_t tremval_const[BLOCKBUF_SIZE];

// vibrato value tables (used per-operator)
static int32_t vibval_var1[BLOCKBUF_SIZE];
static int32_t vibval_var2[BLOCKBUF_SIZE];

// vibrato/trmolo value table pointers
static int32_t *vibval1, *vibval2, *vibval3, *vibval4;
static int32_t *tremval1, *tremval2, *tremval3, *tremval4;

// key scale level lookup table
static const double kslmul[4] = {
	0.0, 0.5, 0.25, 1.0 // -> 0, 3, 1.5, 6 dB/oct
};

// frequency multiplicator lookup table
static const double frqmul_tab[16] = {
	0.5,1,2,3,4,5,6,7,8,9,10,10,12,12,15,15
};

// key scale levels
static uint8_t kslev[8][16];

// map a channel number to the register offset of the modulator (=register base)
static const uint8_t modulatorbase[9] = {
	0,1,2,
	8,9,10,
	16,17,18
};

// map a register base to a modulator operator number or operator number
static const uint8_t regbase2modop[44] = {
	0,1,2,0,1,2,0,0,3,4,5,3,4,5,0,0,6,7,8,6,7,8,                  // first set
	18,19,20,18,19,20,0,0,21,22,23,21,22,23,0,0,24,25,26,24,25,26 // second set
};
static const uint8_t regbase2op[44] = {
	0,1,2,9,10,11,0,0,3,4,5,12,13,14,0,0,6,7,8,15,16,17,          // first set
	18,19,20,27,28,29,0,0,21,22,23,30,31,32,0,0,24,25,26,33,34,35 // second set
};

// start of the waveform
static uint32_t waveform[8] = {
	WAVEPREC,
	WAVEPREC>>1,
	WAVEPREC,
	(WAVEPREC*3)>>2,
	0,
	0,
	(WAVEPREC*5)>>2,
	WAVEPREC<<1
};

// length of the waveform as mask
static uint32_t wavemask[8] = {
	WAVEPREC-1,
	WAVEPREC-1,
	(WAVEPREC>>1)-1,
	(WAVEPREC>>1)-1,
	WAVEPREC-1,
	((WAVEPREC*3)>>2)-1,
	WAVEPREC>>1,
	WAVEPREC-1
};

// where the first entry resides
static uint32_t wavestart[8] = {
	0,
	WAVEPREC>>1,
	0,
	WAVEPREC>>2,
	0,
	0,
	0,
	WAVEPREC>>3
};

// envelope generator function constants
static double attackconst[4] = {
	(1.0/2.82624),
	(1.0/2.25280),
	(1.0/1.88416),
	(1.0/1.59744)
};
static double decrelconst[4] = {
	(1.0/39.28064),
	(1.0/31.41608),
	(1.0/26.17344),
	(1.0/22.44608)
};


/*******************************************************************************
 * OPL Chip
 */

OPL::OPL()
: m_irqfn([](bool){})
{
	memset(&m_s, 0, sizeof(m_s));

	m_s.timers[T1].index = NULL_TIMER_ID;
	m_s.timers[T1].id = T1;
	m_s.timers[T1].increment = 80;
	m_s.timers[T2].index = NULL_TIMER_ID;
	m_s.timers[T2].id = T2;
	m_s.timers[T2].increment = 320;

	init_tables();
}

void OPL::install(ChipTypes _type, std::string _name, bool _timers)
{
	m_name = _name;
	m_type = _type;
	if(_timers) {
		std::string timername = _name + " T1";
		m_s.timers[T1].index = g_machine.register_timer(
				std::bind(&OPL::timer,this,T1),
				timername.c_str());
		assert(m_s.timers[T1].index != NULL_TIMER_ID);

		timername = _name + " T2";
		m_s.timers[T2].index = g_machine.register_timer(
				std::bind(&OPL::timer,this,T2),
				timername.c_str());
		assert(m_s.timers[T2].index != NULL_TIMER_ID);
	}
}

void OPL::remove()
{
	g_machine.unregister_timer(m_s.timers[T1].index);
	g_machine.unregister_timer(m_s.timers[T2].index);
}

void OPL::config_changed(int _samplerate)
{
	m_samplerate = _samplerate;
	m_generator_add = (uint32_t)(INTFREQU * FIXEDPT/_samplerate);
	// vibrato at ~6.1 ?? (opl3 docs say 6.1, opl4 docs say 6.0, y8950 docs say 6.4)
	m_vibtab_add = static_cast<uint32_t>(VIBTAB_SIZE * FIXEDPT_LFO/8192 * INTFREQU/_samplerate);
	// tremolo at 3.7hz
	m_tremtab_add = (uint32_t)((double)TREMTAB_SIZE * TREM_FREQ * FIXEDPT_LFO/(double)_samplerate);
}

void OPL::reset()
{
	memset((void *)m_s.regs, 0, sizeof(m_s.regs));
	memset((void *)m_s.op, 0, sizeof(Operator)*OPL_OPERATORS);
	memset((void *)m_s.wave_sel, 0, sizeof(m_s.wave_sel));

	for(int i=0; i<OPL_OPERATORS; i++) {
		m_s.op[i].op_state = OF_TYPE_OFF;
		m_s.op[i].act_state = OP_ACT_OFF;
		m_s.op[i].amp = 0.0;
		m_s.op[i].step_amp = 0.0;
		m_s.op[i].vol = 0.0;
		m_s.op[i].tcount = 0;
		m_s.op[i].tinc = 0;
		m_s.op[i].toff = 0;
		m_s.op[i].cur_wmask = wavemask[0];
		m_s.op[i].cur_wform = waveform[0];
		m_s.op[i].freq_high = 0;

		m_s.op[i].generator_pos = 0;
		m_s.op[i].cur_env_step = 0;
		m_s.op[i].env_step_a = 0;
		m_s.op[i].env_step_d = 0;
		m_s.op[i].env_step_r = 0;
		m_s.op[i].step_skip_pos_a = 0;
		m_s.op[i].env_step_skip_a = 0;

		m_s.op[i].is_4op = false;
		m_s.op[i].is_4op_attached = false;
		m_s.op[i].left_pan = 1;
		m_s.op[i].right_pan = 1;

		m_s.op[i].type = m_type;
		m_s.op[i].recipsamp = 1.0 / m_samplerate;
	}

	m_s.timers[T1].reset();
	m_s.timers[T2].reset();

	m_s.reg_index = 0;
	m_s.vibtab_pos = 0;
	m_s.tremtab_pos = 0;

	m_irqfn(false);
}

void OPL::save_state(StateBuf &_state)
{
	_state.write(&m_s, {sizeof(m_s), name()});
}

void OPL::restore_state(StateBuf &_state)
{
	int t1 = m_s.timers[T1].index;
	int t2 = m_s.timers[T2].index;
	_state.read(&m_s, {sizeof(m_s), name()});
	m_s.timers[T1].index = t1;
	m_s.timers[T2].index = t2;
}

void OPL::timer(int id)
{
	bool overflow = m_s.timers[id].timeout();
	if(overflow) {
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s T: T%u overflow\n", name(), id+1);
		m_irqfn(true);
	}
}

uint8_t OPL::read(unsigned _port)
{
	// base + 0..3
	assert(_port<=3);

	uint8_t status = m_s.timers[T1].overflow | m_s.timers[T2].overflow;

	PDEBUGF(LOG_V2, LOG_AUDIO, "%s Tn: %u -> T1:%u, T2:%u\n", name(), _port, m_s.timers[T1].overflow, m_s.timers[T2].overflow);

	if(status) {
		status |= 0x80;
	}
	if(m_type == OPL3) {
		// opl3-detection routines require ret&6 to be zero
		if(_port == 0) {
			return status;
		}
		return 0x00;
	} else {
		// opl2-detection routines require ret&6 to be 6
		if(_port == 0) {
			return status|0x6;
		}
		return 0xff;
	}
}

void OPL::write_timers(int _index, uint8_t _value)
{
	if(m_s.timers[T1].index == NULL_TIMER_ID) {
		return;
	}
	switch(_index) {
	case 0x02: // timer 1 preset value
		m_s.timers[T1].value = _value;
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s Tn: T1 <- 0x%02X\n", name(), _value);
		break;
	case 0x03: // timer 2 preset value
		m_s.timers[T2].value = _value;
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s Tn: T2 <- 0x%02X\n", name(), _value);
		break;
	case 0x04:
		// IRQ reset, timer mask/start
		if(_value & 0x80) {
			// bit 7 - Resets the flags for timers 1 & 2.
			// If set, all other bits are ignored.
			m_irqfn(false);
			m_s.timers[T1].clear();
			m_s.timers[T2].clear();
			PDEBUGF(LOG_V2, LOG_AUDIO, "%s Tn: T1,T2 clear\n", name());
		} else {
			// timer 1 control
			m_s.timers[T1].masked = _value & 0x40;
			m_s.timers[T1].toggle(_value & 1);
			if(m_s.timers[T1].masked) {
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s Tn: T1 masked\n", name());
				m_s.timers[T1].clear();
			}
			// timer 2 control
			m_s.timers[T2].masked = _value & 0x20;
			m_s.timers[T2].toggle(_value & 2);
			if(m_s.timers[T2].masked) {
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s Tn: T2 masked\n", name());
				m_s.timers[T2].clear();
			}
		}
		break;
	default:
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s: invalid timer port: %d\n", name(), _index);
		break;
	}
}

void OPL::write(unsigned _port, uint8_t _val)
{
	// if OPL_THREADED_RENDERING is true this function is called by the Mixer thread

	// base + 0..3
	assert(_port<=3);

	if(_port == 0 || _port == 2) {
		// Address port
		m_s.reg_index = _val;
		if(m_type == OPL3 && (_port == 2)) {
			// possibly second set
			if(is_opl3_mode() || (m_s.reg_index == 5)) {
				m_s.reg_index |= ARC_SECONDSET;
			}
		}
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s: %u <- i 0x%02x\n", name(), _port, m_s.reg_index);
		return;
	}

	uint32_t second_set = m_s.reg_index & ARC_SECONDSET;
	if((_port == 1 && second_set) || (_port == 3 && !second_set)) {
		PDEBUGF(LOG_V3, LOG_AUDIO,
			"%s: invalid data port %u for register index %03Xh\n",
			name(), _port, m_s.reg_index);
	}

	m_s.regs[m_s.reg_index] = _val;
	PDEBUGF(LOG_V2, LOG_AUDIO, "%s: %u <- v 0x%02x\n", name(), _port, _val);

	switch(m_s.reg_index & 0xf0) {
	case ARC_CONTROL:
		// here we check for the second set registers, too:
		switch (m_s.reg_index) {
		case 0x02: // timer 1 preset value
		case 0x03: // timer 2 preset value
		case 0x04: // IRQ reset, timer mask/start
#if (OPL_THREADED_RENDERING)
			PDEBUGF(LOG_V2, LOG_AUDIO, "%s: Timer command ignored\n", name());
#else
			// timers are operated by the Machine thread
			write_timers(m_s.reg_index, _val);
#endif
			break;
		case ARC_SECONDSET|0x04:
			// 4op enable/disable switches for each possible channel
			m_s.op[0].is_4op = (_val&1)>0;
			m_s.op[3].is_4op_attached = m_s.op[0].is_4op;
			m_s.op[1].is_4op = (_val&2)>0;
			m_s.op[4].is_4op_attached = m_s.op[1].is_4op;
			m_s.op[2].is_4op = (_val&4)>0;
			m_s.op[5].is_4op_attached = m_s.op[2].is_4op;
			m_s.op[18].is_4op = (_val&8)>0;
			m_s.op[21].is_4op_attached = m_s.op[18].is_4op;
			m_s.op[19].is_4op = (_val&16)>0;
			m_s.op[22].is_4op_attached = m_s.op[19].is_4op;
			m_s.op[20].is_4op = (_val&32)>0;
			m_s.op[23].is_4op_attached = m_s.op[20].is_4op;
			break;
		case ARC_SECONDSET|0x05:
			PDEBUGF(LOG_V2, LOG_AUDIO, "%s: OPL3 mode %s\n", name(), (_val&1)?"enabled":"disabled");
			break;
		case 0x08:
			// CSW, note select
			break;
		default:
			break;
		}
		break;
	case ARC_TVS_KSR_MUL:
	case ARC_TVS_KSR_MUL+0x10: {
		// tremolo/vibrato/sustain keeping enabled; key scale rate; frequency multiplication
		int num = m_s.reg_index&7;
		unsigned base = (m_s.reg_index-ARC_TVS_KSR_MUL)&0xff;
		if((num<6) && (base<22)) {
			unsigned modop = regbase2modop[second_set?(base+22):base];
			unsigned regbase = base+second_set;
			unsigned chanbase = second_set?(modop-18+ARC_SECONDSET):modop;

			// change tremolo/vibrato and sustain keeping of this operator
			Operator* op_ptr = &m_s.op[modop+((num<3) ? 0 : 9)];
			op_ptr->change_keepsustain(m_s.regs,regbase);
			op_ptr->change_vibrato(m_s.regs,regbase);

			// change frequency calculations of this operator as
			// key scale rate and frequency multiplicator can be changed
			if(m_type == OPL3) {
				if(is_opl3_mode() && (m_s.op[modop].is_4op_attached)) {
					// operator uses frequency of channel
					op_ptr->change_frequency(m_s.regs,chanbase-3,regbase);
				} else {
					op_ptr->change_frequency(m_s.regs,chanbase,regbase);
				}
			} else {
				op_ptr->change_frequency(m_s.regs,chanbase,base);
			}
		}
		break;
	}
	case ARC_KSL_OUTLEV:
	case ARC_KSL_OUTLEV+0x10: {
		// key scale level; output rate
		int num = m_s.reg_index&7;
		unsigned base = (m_s.reg_index-ARC_KSL_OUTLEV)&0xff;
		if((num<6) && (base<22)) {
			unsigned modop = regbase2modop[second_set?(base+22):base];
			unsigned chanbase = second_set?(modop-18+ARC_SECONDSET):modop;

			// change frequency calculations of this operator as
			// key scale level and output rate can be changed
			Operator* op_ptr = &m_s.op[modop+((num<3) ? 0 : 9)];
			if(m_type == OPL3) {
				unsigned regbase = base+second_set;
				if(is_opl3_mode() && (m_s.op[modop].is_4op_attached)) {
					// operator uses frequency of channel
					op_ptr->change_frequency(m_s.regs,chanbase-3,regbase);
				} else {
					op_ptr->change_frequency(m_s.regs,chanbase,regbase);
				}
			} else {
				op_ptr->change_frequency(m_s.regs,chanbase,base);
			}
		}
		break;
	}
	case ARC_ATTR_DECR:
	case ARC_ATTR_DECR+0x10: {
		// attack/decay rates
		int num = m_s.reg_index&7;
		unsigned base = (m_s.reg_index-ARC_ATTR_DECR)&0xff;
		if((num<6) && (base<22)) {
			unsigned regbase = base+second_set;

			// change attack rate and decay rate of this operator
			Operator* op_ptr = &m_s.op[regbase2op[second_set?(base+22):base]];
			op_ptr->change_attackrate(m_s.regs,regbase);
			op_ptr->change_decayrate(m_s.regs,regbase);
		}
		break;
	}
	case ARC_SUSL_RELR:
	case ARC_SUSL_RELR+0x10: {
		// sustain level; release rate
		int num = m_s.reg_index&7;
		unsigned base = (m_s.reg_index-ARC_SUSL_RELR)&0xff;
		if((num<6) && (base<22)) {
			unsigned regbase = base+second_set;

			// change sustain level and release rate of this operator
			Operator* op_ptr = &m_s.op[regbase2op[second_set?(base+22):base]];
			op_ptr->change_releaserate(m_s.regs,regbase);
			op_ptr->change_sustainlevel(m_s.regs,regbase);
		}
		break;
	}
	case ARC_FREQ_NUM: {
		// 0xa0-0xa8 low8 frequency
		unsigned base = (m_s.reg_index-ARC_FREQ_NUM)&0xff;
		if(base<9) {
			int opbase = second_set?(base+18):base;
			if(m_type == OPL3) {
				if(is_opl3_mode() && m_s.op[opbase].is_4op_attached) {
					break;
				}
			}
			// regbase of modulator:
			int modbase = modulatorbase[base]+second_set;

			unsigned chanbase = base+second_set;

			m_s.op[opbase].change_frequency(m_s.regs,chanbase,modbase);
			m_s.op[opbase+9].change_frequency(m_s.regs,chanbase,modbase+3);
			if(m_type == OPL3) {
				// for 4op channels all four operators are modified to the frequency of the channel
				if(is_opl3_mode() && m_s.op[opbase].is_4op) {
					if(UNLIKELY(opbase > 20)) {
						// if opbase is > 20 there must be a bug in either reset() or ARC_CONTROL
						PERRF(LOG_AUDIO, "OPL3: Invalid opbase: %u\n", opbase);
						assert(false);
						break;
					}
					m_s.op[opbase+3].change_frequency(m_s.regs,chanbase,modbase+8);
					m_s.op[opbase+3+9].change_frequency(m_s.regs,chanbase,modbase+3+8);
				}
			}
		}
		break;
	}
	case ARC_KON_BNUM: {
		if(m_s.reg_index == ARC_PERC_MODE) {
			if(m_type == OPL3) {
				if(second_set) {
					return;
				}
			}
			if((_val&0x30) == 0x30) { // BassDrum active
				m_s.op[6].enable(m_s.wave_sel,16,OP_ACT_PERC);
				m_s.op[6].change_frequency(m_s.regs,6,16);
				m_s.op[6+9].enable(m_s.wave_sel,16+3,OP_ACT_PERC);
				m_s.op[6+9].change_frequency(m_s.regs,6,16+3);
			} else {
				m_s.op[6].disable(OP_ACT_PERC);
				m_s.op[6+9].disable(OP_ACT_PERC);
			}
			if((_val&0x28) == 0x28) { // Snare active
				m_s.op[16].enable(m_s.wave_sel,17+3,OP_ACT_PERC);
				m_s.op[16].change_frequency(m_s.regs,7,17+3);
			} else {
				m_s.op[16].disable(OP_ACT_PERC);
			}
			if((_val&0x24) == 0x24) { // TomTom active
				m_s.op[8].enable(m_s.wave_sel,18,OP_ACT_PERC);
				m_s.op[8].change_frequency(m_s.regs,8,18);
			} else {
				m_s.op[8].disable(OP_ACT_PERC);
			}
			if((_val&0x22) == 0x22) { // Cymbal active
				m_s.op[8+9].enable(m_s.wave_sel,18+3,OP_ACT_PERC);
				m_s.op[8+9].change_frequency(m_s.regs,8,18+3);
			} else {
				m_s.op[8+9].disable(OP_ACT_PERC);
			}
			if((_val&0x21) == 0x21) { // Hihat active
				m_s.op[7].enable(m_s.wave_sel,17,OP_ACT_PERC);
				m_s.op[7].change_frequency(m_s.regs,7,17);
			} else {
				m_s.op[7].disable(OP_ACT_PERC);
			}

			break;
		}
		// regular 0xb0-0xb8
		unsigned base = (m_s.reg_index-ARC_KON_BNUM)&0xff;
		if(base<9) {
			int opbase = second_set?(base+18):base;
			if(m_type == OPL3) {
				if(is_opl3_mode() && m_s.op[opbase].is_4op_attached) {
					break;
				}
			}
			// regbase of modulator:
			int modbase = modulatorbase[base]+second_set;

			if(_val&32) {
				// operator switched on
				m_s.op[opbase].enable(m_s.wave_sel,modbase,OP_ACT_NORMAL);     // modulator (if 2op)
				m_s.op[opbase+9].enable(m_s.wave_sel,modbase+3,OP_ACT_NORMAL); // carrier (if 2op)
				if(m_type == OPL3) {
					// for 4op channels all four operators are switched on
					if(is_opl3_mode() && m_s.op[opbase].is_4op) {
						if(UNLIKELY(opbase > 20)) {
							PERRF(LOG_AUDIO, "OPL3: Invalid opbase: %u\n", opbase);
							assert(false);
							break;
						}
						// turn on chan+3 operators as well
						m_s.op[opbase+3].enable(m_s.wave_sel,modbase+8,OP_ACT_NORMAL);
						m_s.op[opbase+3+9].enable(m_s.wave_sel,modbase+3+8,OP_ACT_NORMAL);
					}
				}
			} else {
				// operator switched off
				m_s.op[opbase].disable(OP_ACT_NORMAL);
				m_s.op[opbase+9].disable(OP_ACT_NORMAL);
				if(m_type == OPL3) {
					// for 4op channels all four operators are switched off
					if(is_opl3_mode() && m_s.op[opbase].is_4op) {
						if(UNLIKELY(opbase > 20)) {
							PERRF(LOG_AUDIO, "OPL3: Invalid opbase: %u\n", opbase);
							assert(false);
							break;
						}
						// turn off chan+3 operators as well
						m_s.op[opbase+3].disable(OP_ACT_NORMAL);
						m_s.op[opbase+3+9].disable(OP_ACT_NORMAL);
					}
				}
			}

			unsigned chanbase = base+second_set;

			// change frequency calculations of modulator and carrier (2op) as
			// the frequency of the channel has changed
			m_s.op[opbase].change_frequency(m_s.regs,chanbase,modbase);
			m_s.op[opbase+9].change_frequency(m_s.regs,chanbase,modbase+3);
			if(m_type == OPL3) {
				// for 4op channels all four operators are modified to the frequency of the channel
				if(is_opl3_mode() && m_s.op[opbase].is_4op) {
					if(UNLIKELY(opbase > 20)) {
						PERRF(LOG_AUDIO, "OPL3: Invalid opbase: %u\n", opbase);
						assert(false);
						break;
					}
					// change frequency calculations of chan+3 operators as well
					m_s.op[opbase+3].change_frequency(m_s.regs,chanbase,modbase+8);
					m_s.op[opbase+3+9].change_frequency(m_s.regs,chanbase,modbase+3+8);
				}
			}
		}
		break;
	}
	case ARC_FEEDBACK: {
		// 0xc0-0xc8 feedback/modulation type (AM/FM)
		unsigned base = (m_s.reg_index-ARC_FEEDBACK)&0xff;
		if(base<9) {
			int opbase = second_set?(base+18):base;
			unsigned chanbase = base+second_set;
			m_s.op[opbase].change_feedback(m_s.regs,chanbase);
			if(m_type == OPL3) {
				// OPL3 panning
				m_s.op[opbase].left_pan = ((_val&0x10)>>4);
				m_s.op[opbase].right_pan = ((_val&0x20)>>5);
			}
		}
		break;
	}
	case ARC_WAVE_SEL:
	case ARC_WAVE_SEL+0x10: {
		int num = m_s.reg_index&7;
		unsigned base = (m_s.reg_index-ARC_WAVE_SEL)&0xff;
		if((num<6) && (base<22)) {
			if(m_type == OPL3) {
				int wselbase = second_set?(base+22):base; // for easier mapping onto wave_sel[]
				// change waveform
				if(is_opl3_mode()) {
					m_s.wave_sel[wselbase] = _val&7; // opl3 mode enabled, all waveforms accessible
				} else {
					m_s.wave_sel[wselbase] = _val&3;
				}
				Operator* op_ptr = &m_s.op[regbase2modop[wselbase]+((num<3) ? 0 : 9)];
				op_ptr->change_waveform(m_s.wave_sel,wselbase);
			} else {
				if(m_s.regs[0x01]&0x20) {
					// wave selection enabled, change waveform
					m_s.wave_sel[base] = _val&3;
					Operator* op_ptr = &m_s.op[regbase2modop[base]+((num<3) ? 0 : 9)];
					op_ptr->change_waveform(m_s.wave_sel,base);
				}
			}
		}
		break;
	}
	default:
		break;
	}
}

inline void clipit16(int32_t ival, int16_t* outval)
{
	if(ival<32768) {
		if(ival>-32769) {
			*outval=(int16_t)ival;
		} else {
			*outval = -32768;
		}
	} else {
		*outval = 32767;
	}
}

// be careful with this
// uses cptr and chanval, outputs into outbufl(/outbufr)
// for OPL3 check if OPL3-mode is enabled (which uses stereo panning)
#define CHANVAL_OUT \
	if(is_opl3_mode()) {    \
		outbufl[i] += chanval*cptr[0].left_pan;  \
		outbufr[i] += chanval*cptr[0].right_pan; \
	} else {                                     \
		outbufl[i] += chanval;                   \
	}

void OPL::generate(int16_t *_buffer, int _frames, int _stride)
{
	assert(_stride > 0);
	
	int i, endframes;
	Operator* cptr;

	// output buffers, left and right channel for OPL3 stereo
	int32_t outbufl[BLOCKBUF_SIZE];
	int32_t outbufr[BLOCKBUF_SIZE];

	// vibrato/tremolo lookup tables (global, to possibly be used by all operators)
	int32_t vib_lut[BLOCKBUF_SIZE];
	int32_t trem_lut[BLOCKBUF_SIZE];

	for(int curfrm=0; curfrm<_frames; curfrm+=endframes) {
		endframes = _frames - curfrm;
		if(endframes > BLOCKBUF_SIZE) {
			endframes = BLOCKBUF_SIZE;
		}

		memset((void*)&outbufl, 0, endframes*sizeof(int32_t));
		// clear second output buffer (opl3 stereo)
		if(is_opl3_mode()) {
			memset((void*)&outbufr, 0, endframes*sizeof(int32_t));
		}

		// calculate vibrato/tremolo lookup tables
		// 14cents/7cents switching
		int32_t vib_tshift = ((m_s.regs[ARC_PERC_MODE]&0x40)==0) ? 1 : 0;
		for(i=0; i<endframes; i++) {
			// cycle through vibrato table
			m_s.vibtab_pos += m_vibtab_add;
			if(m_s.vibtab_pos/FIXEDPT_LFO>=VIBTAB_SIZE) {
				m_s.vibtab_pos -= VIBTAB_SIZE*FIXEDPT_LFO;
			}
			// 14cents (14/100 of a semitone) or 7cents
			vib_lut[i] = vib_table[m_s.vibtab_pos/FIXEDPT_LFO] >> vib_tshift;

			// cycle through tremolo table
			m_s.tremtab_pos += m_tremtab_add;
			if(m_s.tremtab_pos/FIXEDPT_LFO>=TREMTAB_SIZE) {
				m_s.tremtab_pos-=TREMTAB_SIZE*FIXEDPT_LFO;
			}
			if(m_s.regs[ARC_PERC_MODE]&0x80) {
				trem_lut[i] = trem_table[m_s.tremtab_pos/FIXEDPT_LFO];
			} else {
				trem_lut[i] = trem_table[TREMTAB_SIZE+m_s.tremtab_pos/FIXEDPT_LFO];
			}
		}

		if(m_s.regs[ARC_PERC_MODE]&0x20) {
			//BassDrum
			cptr = &m_s.op[6];
			if(m_s.regs[ARC_FEEDBACK+6]&1) {
				// additive synthesis
				if(cptr[9].op_state != OF_TYPE_OFF) {
					if(cptr[9].vibrato) {
						vibval1 = vibval_var1;
						for(i=0; i<endframes; i++) {
							vibval1[i] = (int32_t)((vib_lut[i]*cptr[9].freq_high/8)*FIXEDPT*VIBFAC);
						}
					} else {
						vibval1 = vibval_const;
					}
					if(cptr[9].tremolo) {
						tremval1 = trem_lut; // tremolo enabled, use table
					} else {
						tremval1 = tremval_const;
					}

					// calculate channel output
					for(i=0; i<endframes; i++) {
						cptr[9].advance(vibval1[i],m_generator_add);
						cptr[9].exec();
						cptr[9].output(0,tremval1[i]);

						int32_t chanval = cptr[9].cval*2;
						CHANVAL_OUT
					}
				}
			} else {
				// frequency modulation
				if((cptr[9].op_state != OF_TYPE_OFF) || (cptr[0].op_state != OF_TYPE_OFF)) {
					if((cptr[0].vibrato) && (cptr[0].op_state != OF_TYPE_OFF)) {
						vibval1 = vibval_var1;
						for(i=0; i<endframes; i++) {
							vibval1[i] = (int32_t)((vib_lut[i]*cptr[0].freq_high/8)*FIXEDPT*VIBFAC);
						}
					} else {
						vibval1 = vibval_const;
					}
					if((cptr[9].vibrato) && (cptr[9].op_state != OF_TYPE_OFF)) {
						vibval2 = vibval_var2;
						for(i=0; i<endframes; i++) {
							vibval2[i] = (int32_t)((vib_lut[i]*cptr[9].freq_high/8)*FIXEDPT*VIBFAC);
						}
					} else {
						vibval2 = vibval_const;
					}
					if(cptr[0].tremolo) {
						tremval1 = trem_lut; // tremolo enabled, use table
					} else {
						tremval1 = tremval_const;
					}
					if(cptr[9].tremolo) {
						tremval2 = trem_lut; // tremolo enabled, use table
					} else {
						tremval2 = tremval_const;
					}

					// calculate channel output
					for(i=0; i<endframes; i++) {
						cptr[0].advance(vibval1[i], m_generator_add);
						cptr[0].exec();
						cptr[0].output((cptr[0].lastcval+cptr[0].cval)*cptr[0].mfbi/2, tremval1[i]);

						cptr[9].advance(vibval2[i], m_generator_add);
						cptr[9].exec();
						cptr[9].output(cptr[0].cval*FIXEDPT, tremval2[i]);

						int32_t chanval = cptr[9].cval*2;
						CHANVAL_OUT
					}
				}
			}

			//TomTom (j=8)
			if(m_s.op[8].op_state != OF_TYPE_OFF) {
				cptr = &m_s.op[8];
				if(cptr[0].vibrato) {
					vibval3 = vibval_var1;
					for(i=0; i<endframes; i++) {
						vibval3[i] = (int32_t)((vib_lut[i]*cptr[0].freq_high/8)*FIXEDPT*VIBFAC);
					}
				} else {
					vibval3 = vibval_const;
				}

				if(cptr[0].tremolo) {
					tremval3 = trem_lut; // tremolo enabled, use table
				} else {
					tremval3 = tremval_const;
				}

				// calculate channel output
				for(i=0; i<endframes; i++) {
					cptr[0].advance(vibval3[i], m_generator_add);
					cptr[0].exec();
					cptr[0].output(0, tremval3[i]);

					int32_t chanval = cptr[0].cval*2;
					CHANVAL_OUT
				}
			}

			//Snare/Hihat (j=7), Cymbal (j=8)
			if((m_s.op[7].op_state != OF_TYPE_OFF) || (m_s.op[16].op_state != OF_TYPE_OFF) ||
				(m_s.op[17].op_state != OF_TYPE_OFF)) {
				cptr = &m_s.op[7];
				if((cptr[0].vibrato) && (cptr[0].op_state != OF_TYPE_OFF)) {
					vibval1 = vibval_var1;
					for(i=0; i<endframes; i++) {
						vibval1[i] = (int32_t)((vib_lut[i]*cptr[0].freq_high/8)*FIXEDPT*VIBFAC);
					}
				} else {
					vibval1 = vibval_const;
				}
				if((cptr[9].vibrato) && (cptr[9].op_state == OF_TYPE_OFF)) {
					vibval2 = vibval_var2;
					for(i=0; i<endframes; i++) {
						vibval2[i] = (int32_t)((vib_lut[i]*cptr[9].freq_high/8)*FIXEDPT*VIBFAC);
					}
				} else {
					vibval2 = vibval_const;
				}

				if(cptr[0].tremolo) {
					tremval1 = trem_lut; // tremolo enabled, use table
				} else {
					tremval1 = tremval_const;
				}
				if(cptr[9].tremolo) {
					tremval2 = trem_lut; // tremolo enabled, use table
				} else {
					tremval2 = tremval_const;
				}

				cptr = &m_s.op[8];
				if((cptr[9].vibrato) && (cptr[9].op_state == OF_TYPE_OFF)) {
					vibval4 = vibval_var2;
					for(i=0; i<endframes; i++) {
						vibval4[i] = (int32_t)((vib_lut[i]*cptr[9].freq_high/8)*FIXEDPT*VIBFAC);
					}
				} else {
					vibval4 = vibval_const;
				}

				if(cptr[9].tremolo) {
					tremval4 = trem_lut; // tremolo enabled, use table
				} else {
					tremval4 = tremval_const;
				}

				// calculate channel output
				for(i=0; i<endframes; i++) {
					advance_drums(
						&m_s.op[7],vibval1[i],
						&m_s.op[7+9],vibval2[i],
						&m_s.op[8+9],vibval4[i]);

					m_s.op[7].exec();   // Hihat
					m_s.op[7].output(0,tremval1[i]);

					m_s.op[7+9].exec(); // Snare
					m_s.op[7+9].output(0,tremval2[i]);

					m_s.op[8+9].exec(); // Cymbal
					m_s.op[8+9].output(0,tremval4[i]);

					int32_t chanval = (m_s.op[7].cval + m_s.op[7+9].cval + m_s.op[8+9].cval)*2;
					CHANVAL_OUT
				}
			}
		}

		unsigned max_channel = OPL_CHANNELS;

		if(!is_opl3_mode()) {
			max_channel = OPL_CHANNELS/2;
		}

		for(int cur_ch=max_channel-1; cur_ch>=0; cur_ch--) {
			// skip drum/percussion operators
			if((m_s.regs[ARC_PERC_MODE]&0x20) && (cur_ch >= 6) && (cur_ch < 9)) {
				continue;
			}

			unsigned k = cur_ch;
			if(m_type==OPL3) {
				if(cur_ch < 9) {
					cptr = &m_s.op[cur_ch];
				} else {
					cptr = &m_s.op[cur_ch+9]; // second set is operator18-operator35
					k += (-9+256);            // second set uses registers 0x100 onwards
				}
				// check if this operator is part of a 4-op
				if(is_opl3_mode() && cptr->is_4op_attached) {
					continue;
				}
			} else {
				cptr = &m_s.op[cur_ch];
			}

			// check for FM/AM
			if(m_s.regs[ARC_FEEDBACK+k]&1) {

				if(is_opl3_mode() && cptr->is_4op) {
					if(m_s.regs[ARC_FEEDBACK+k+3]&1) {
						// AM-AM-style synthesis (op1[fb] + (op2 * op3) + op4)
						if(cptr[0].op_state != OF_TYPE_OFF) {
							if(cptr[0].vibrato) {
								vibval1 = vibval_var1;
								for(i=0; i<endframes; i++) {
									vibval1[i] = (int32_t)((vib_lut[i]*cptr[0].freq_high/8)*FIXEDPT*VIBFAC);
								}
							} else {
								vibval1 = vibval_const;
							}
							if(cptr[0].tremolo) {
								tremval1 = trem_lut; // tremolo enabled, use table
							} else {
								tremval1 = tremval_const;
							}

							// calculate channel output
							for(i=0; i<endframes; i++) {
								cptr[0].advance(vibval1[i],m_generator_add);
								cptr[0].exec();
								cptr[0].output((cptr[0].lastcval+cptr[0].cval)*cptr[0].mfbi/2,tremval1[i]);

								int32_t chanval = cptr[0].cval;
								CHANVAL_OUT
							}
						}

						if((cptr[3].op_state != OF_TYPE_OFF) || (cptr[9].op_state != OF_TYPE_OFF)) {
							if((cptr[9].vibrato) && (cptr[9].op_state != OF_TYPE_OFF)) {
								vibval1 = vibval_var1;
								for(i=0; i<endframes; i++) {
									vibval1[i] = (int32_t)((vib_lut[i]*cptr[9].freq_high/8)*FIXEDPT*VIBFAC);
								}
							} else {
								vibval1 = vibval_const;
							}
							if(cptr[9].tremolo) {
								tremval1 = trem_lut; // tremolo enabled, use table
							} else {
								tremval1 = tremval_const;
							}
							if(cptr[3].tremolo) {
								tremval2 = trem_lut; // tremolo enabled, use table
							} else {
								tremval2 = tremval_const;
							}

							// calculate channel output
							for(i=0; i<endframes; i++) {
								cptr[9].advance(vibval1[i],m_generator_add);
								cptr[9].exec();
								cptr[9].output(0,tremval1[i]);

								cptr[3].advance(0,m_generator_add);
								cptr[3].exec();
								cptr[3].output(cptr[9].cval*FIXEDPT,tremval2[i]);

								int32_t chanval = cptr[3].cval;
								CHANVAL_OUT
							}
						}

						if(cptr[3+9].op_state != OF_TYPE_OFF) {
							if(cptr[3+9].tremolo) {
								tremval1 = trem_lut; // tremolo enabled, use table
							} else {
								tremval1 = tremval_const;
							}

							// calculate channel output
							for(i=0; i<endframes; i++) {
								cptr[3+9].advance(0,m_generator_add);
								cptr[3+9].exec();
								cptr[3+9].output(0,tremval1[i]);

								int32_t chanval = cptr[3+9].cval;
								CHANVAL_OUT
							}
						}
					} else {
						// AM-FM-style synthesis (op1[fb] + (op2 * op3 * op4))
						if(cptr[0].op_state != OF_TYPE_OFF) {
							if(cptr[0].vibrato) {
								vibval1 = vibval_var1;
								for(i=0; i<endframes; i++) {
									vibval1[i] = (int32_t)((vib_lut[i]*cptr[0].freq_high/8)*FIXEDPT*VIBFAC);
								}
							} else {
								vibval1 = vibval_const;
							}
							if(cptr[0].tremolo) {
								tremval1 = trem_lut; // tremolo enabled, use table
							} else {
								tremval1 = tremval_const;
							}

							// calculate channel output
							for(i=0; i<endframes; i++) {
								cptr[0].advance(vibval1[i],m_generator_add);
								cptr[0].exec();
								cptr[0].output((cptr[0].lastcval+cptr[0].cval)*cptr[0].mfbi/2,tremval1[i]);
								int32_t chanval = cptr[0].cval;
								CHANVAL_OUT
							}
						}

						if((cptr[9].op_state != OF_TYPE_OFF) || (cptr[3].op_state != OF_TYPE_OFF) || (cptr[3+9].op_state != OF_TYPE_OFF)) {
							if((cptr[9].vibrato) && (cptr[9].op_state != OF_TYPE_OFF)) {
								vibval1 = vibval_var1;
								for(i=0; i<endframes; i++) {
									vibval1[i] = (int32_t)((vib_lut[i]*cptr[9].freq_high/8)*FIXEDPT*VIBFAC);
								}
							} else {
								vibval1 = vibval_const;
							}
							if(cptr[9].tremolo) {
								tremval1 = trem_lut; // tremolo enabled, use table
							} else {
								tremval1 = tremval_const;
							}
							if(cptr[3].tremolo) {
								tremval2 = trem_lut; // tremolo enabled, use table
							} else {
								tremval2 = tremval_const;
							}
							if(cptr[3+9].tremolo) {
								tremval3 = trem_lut; // tremolo enabled, use table
							} else {
								tremval3 = tremval_const;
							}

							// calculate channel output
							for(i=0; i<endframes; i++) {
								cptr[9].advance(vibval1[i],m_generator_add);
								cptr[9].exec();
								cptr[9].output(0,tremval1[i]);

								cptr[3].advance(0,m_generator_add);
								cptr[3].exec();
								cptr[3].output(cptr[9].cval*FIXEDPT,tremval2[i]);

								cptr[3+9].advance(0,m_generator_add);
								cptr[3+9].exec();
								cptr[3+9].output(cptr[3].cval*FIXEDPT,tremval3[i]);

								int32_t chanval = cptr[3+9].cval;
								CHANVAL_OUT
							}
						}
					}
					continue;
				} //if OPL3

				// 2op additive synthesis
				if((cptr[9].op_state == OF_TYPE_OFF) && (cptr[0].op_state == OF_TYPE_OFF)) {
					continue;
				}
				if((cptr[0].vibrato) && (cptr[0].op_state != OF_TYPE_OFF)) {
					vibval1 = vibval_var1;
					for(i=0; i<endframes; i++) {
						vibval1[i] = (int32_t)((vib_lut[i]*cptr[0].freq_high/8)*FIXEDPT*VIBFAC);
					}
				} else {
					vibval1 = vibval_const;
				}
				if((cptr[9].vibrato) && (cptr[9].op_state != OF_TYPE_OFF)) {
					vibval2 = vibval_var2;
					for(i=0; i<endframes; i++) {
						vibval2[i] = (int32_t)((vib_lut[i]*cptr[9].freq_high/8)*FIXEDPT*VIBFAC);
					}
				} else {
					vibval2 = vibval_const;
				}
				if(cptr[0].tremolo) {
					tremval1 = trem_lut; // tremolo enabled, use table
				} else {
					tremval1 = tremval_const;
				}
				if(cptr[9].tremolo) {
					tremval2 = trem_lut; // tremolo enabled, use table
				} else {
					tremval2 = tremval_const;
				}

				// calculate channel output
				for(i=0; i<endframes; i++) {
					// carrier1
					cptr[0].advance(vibval1[i],m_generator_add);
					cptr[0].exec();
					cptr[0].output((cptr[0].lastcval+cptr[0].cval)*cptr[0].mfbi/2,tremval1[i]);

					// carrier2
					cptr[9].advance(vibval2[i],m_generator_add);
					cptr[9].exec();
					cptr[9].output(0,tremval2[i]);

					int32_t chanval = cptr[9].cval + cptr[0].cval;
					CHANVAL_OUT
				}

			} else { // if(m_s.regs[ARC_FEEDBACK+k]&1)

				if(is_opl3_mode() && cptr->is_4op) {
					if(m_s.regs[ARC_FEEDBACK+k+3]&1) {
						// FM-AM-style synthesis ((op1[fb] * op2) + (op3 * op4))
						if((cptr[0].op_state != OF_TYPE_OFF) || (cptr[9].op_state != OF_TYPE_OFF)) {
							if((cptr[0].vibrato) && (cptr[0].op_state != OF_TYPE_OFF)) {
								vibval1 = vibval_var1;
								for(i=0; i<endframes; i++) {
									vibval1[i] = (int32_t)((vib_lut[i]*cptr[0].freq_high/8)*FIXEDPT*VIBFAC);
								}
							} else {
								vibval1 = vibval_const;
							}
							if((cptr[9].vibrato) && (cptr[9].op_state != OF_TYPE_OFF)) {
								vibval2 = vibval_var2;
								for(i=0; i<endframes; i++) {
									vibval2[i] = (int32_t)((vib_lut[i]*cptr[9].freq_high/8)*FIXEDPT*VIBFAC);
								}
							} else {
								vibval2 = vibval_const;
							}
							if(cptr[0].tremolo) {
								tremval1 = trem_lut; // tremolo enabled, use table
							} else {
								tremval1 = tremval_const;
							}
							if(cptr[9].tremolo) {
								tremval2 = trem_lut; // tremolo enabled, use table
							} else {
								tremval2 = tremval_const;
							}

							// calculate channel output
							for(i=0; i<endframes; i++) {
								cptr[0].advance(vibval1[i],m_generator_add);
								cptr[0].exec();
								cptr[0].output((cptr[0].lastcval+cptr[0].cval)*cptr[0].mfbi/2,tremval1[i]);

								cptr[9].advance(vibval2[i],m_generator_add);
								cptr[9].exec();
								cptr[9].output(cptr[0].cval*FIXEDPT,tremval2[i]);

								int32_t chanval = cptr[9].cval;
								CHANVAL_OUT
							}
						}

						if((cptr[3].op_state != OF_TYPE_OFF) || (cptr[3+9].op_state != OF_TYPE_OFF)) {
							if(cptr[3].tremolo) {
								tremval1 = trem_lut; // tremolo enabled, use table
							} else {
								tremval1 = tremval_const;
							}
							if(cptr[3+9].tremolo) {
								tremval2 = trem_lut; // tremolo enabled, use table
							} else {
								tremval2 = tremval_const;
							}

							// calculate channel output
							for(i=0; i<endframes; i++) {
								cptr[3].advance(0,m_generator_add);
								cptr[3].exec();
								cptr[3].output(0,tremval1[i]);

								cptr[3+9].advance(0,m_generator_add);
								cptr[3+9].exec();
								cptr[3+9].output(cptr[3].cval*FIXEDPT,tremval2[i]);

								int32_t chanval = cptr[3+9].cval;
								CHANVAL_OUT
							}
						}

					} else {
						// FM-FM-style synthesis (op1[fb] * op2 * op3 * op4)
						if((cptr[0].op_state != OF_TYPE_OFF) || (cptr[9].op_state != OF_TYPE_OFF) ||
							(cptr[3].op_state != OF_TYPE_OFF) || (cptr[3+9].op_state != OF_TYPE_OFF)) {
							if((cptr[0].vibrato) && (cptr[0].op_state != OF_TYPE_OFF)) {
								vibval1 = vibval_var1;
								for(i=0; i<endframes; i++) {
									vibval1[i] = (int32_t)((vib_lut[i]*cptr[0].freq_high/8)*FIXEDPT*VIBFAC);
								}
							} else {
								vibval1 = vibval_const;
							}
							if((cptr[9].vibrato) && (cptr[9].op_state != OF_TYPE_OFF)) {
								vibval2 = vibval_var2;
								for(i=0; i<endframes; i++) {
									vibval2[i] = (int32_t)((vib_lut[i]*cptr[9].freq_high/8)*FIXEDPT*VIBFAC);
								}
							} else {
								vibval2 = vibval_const;
							}
							if(cptr[0].tremolo) {
								tremval1 = trem_lut; // tremolo enabled, use table
							} else {
								tremval1 = tremval_const;
							}
							if(cptr[9].tremolo) {
								tremval2 = trem_lut; // tremolo enabled, use table
							} else {
								tremval2 = tremval_const;
							}
							if(cptr[3].tremolo) {
								tremval3 = trem_lut; // tremolo enabled, use table
							} else {
								tremval3 = tremval_const;
							}
							if(cptr[3+9].tremolo) {
								tremval4 = trem_lut; // tremolo enabled, use table
							} else {
								tremval4 = tremval_const;
							}

							// calculate channel output
							for(i=0; i<endframes; i++) {
								cptr[0].advance(vibval1[i],m_generator_add);
								cptr[0].exec();
								cptr[0].output((cptr[0].lastcval+cptr[0].cval)*cptr[0].mfbi/2,tremval1[i]);

								cptr[9].advance(vibval2[i],m_generator_add);
								cptr[9].exec();
								cptr[9].output(cptr[0].cval*FIXEDPT,tremval2[i]);

								cptr[3].advance(0,m_generator_add);
								cptr[3].exec();
								cptr[3].output(cptr[9].cval*FIXEDPT,tremval3[i]);

								cptr[3+9].advance(0,m_generator_add);
								cptr[3+9].exec();
								cptr[3+9].output(cptr[3].cval*FIXEDPT,tremval4[i]);

								int32_t chanval = cptr[3+9].cval;
								CHANVAL_OUT
							}
						}
					}
					continue;
				} //if OPL3

				// 2op frequency modulation
				if((cptr[9].op_state == OF_TYPE_OFF) && (cptr[0].op_state == OF_TYPE_OFF)) continue;
				if((cptr[0].vibrato) && (cptr[0].op_state != OF_TYPE_OFF)) {
					vibval1 = vibval_var1;
					for(i=0; i<endframes; i++) {
						vibval1[i] = (int32_t)((vib_lut[i]*cptr[0].freq_high/8)*FIXEDPT*VIBFAC);
					}
				} else {
					vibval1 = vibval_const;
				}
				if((cptr[9].vibrato) && (cptr[9].op_state != OF_TYPE_OFF)) {
					vibval2 = vibval_var2;
					for(i=0; i<endframes; i++) {
						vibval2[i] = (int32_t)((vib_lut[i]*cptr[9].freq_high/8)*FIXEDPT*VIBFAC);
					}
				} else {
					vibval2 = vibval_const;
				}
				if(cptr[0].tremolo) {
					tremval1 = trem_lut; // tremolo enabled, use table
				} else {
					tremval1 = tremval_const;
				}
				if(cptr[9].tremolo) {
					tremval2 = trem_lut; // tremolo enabled, use table
				} else {
					tremval2 = tremval_const;
				}

				// calculate channel output
				for(i=0; i<endframes; i++) {
					// modulator
					cptr[0].advance(vibval1[i],m_generator_add);
					cptr[0].exec();
					cptr[0].output((cptr[0].lastcval+cptr[0].cval)*cptr[0].mfbi/2,tremval1[i]);

					// carrier
					cptr[9].advance(vibval2[i],m_generator_add);
					cptr[9].exec();
					cptr[9].output(cptr[0].cval*FIXEDPT,tremval2[i]);

					int32_t chanval = cptr[9].cval;
					CHANVAL_OUT
				}
			}
		}
		
		// convert to 16bit samples
		if(is_opl3_mode()) {
			// stereo
			for(i=0; i<endframes; i++,_buffer+=_stride) {
				clipit16(outbufl[i], _buffer);
				if(_stride >= 2) {
					clipit16(outbufr[i], _buffer+1);
				}
			}
		} else {
			// mono
			for(i=0; i<endframes; i++,_buffer+=_stride) {
				clipit16(outbufl[i], _buffer);
				if(m_type == OPL3 && _stride>=2) {
					clipit16(outbufl[i], _buffer+1);
				}
			}
		}
	} // for samples_to_process
}

bool OPL::is_silent()
{
	for(int i=0xb0; i<0xb9; i++) {
		if((m_s.regs[i] & 0x20) || (m_s.regs[i+ARC_SECONDSET] & 0x20)) {
			return false;
		}
	}
	return true;
}

void OPL::init_tables()
{
	static bool tables_initialized = false;
	if(tables_initialized) {
		return;
	}
	tables_initialized = true;

	int i,j;

	// create vibrato table
	vib_table[0] = 8;
	vib_table[1] = 4;
	vib_table[2] = 0;
	vib_table[3] = -4;
	for(i=4; i<VIBTAB_SIZE; i++) {
		vib_table[i] = vib_table[i-4]*-1;
	}

	for(i=0; i<BLOCKBUF_SIZE; i++) {
		vibval_const[i] = 0;
	}

	// create tremolo table
	int32_t trem_table_int[TREMTAB_SIZE];
	for(i=0; i<14; i++) {
		trem_table_int[i] = i-13;    // upwards (13 to 26 -> -0.5/6 to 0)
	}
	for(i=14; i<41; i++) {
		trem_table_int[i] = -i+14;   // downwards (26 to 0 -> 0 to -1/6)
	}
	for(i=41; i<53; i++) {
		trem_table_int[i] = i-40-26; // upwards (1 to 12 -> -1/6 to -0.5/6)
	}

	for(i=0; i<TREMTAB_SIZE; i++) {
		// 0.0 .. -26/26*4.8/6 == [0.0 .. -0.8], 4/53 steps == [1 .. 0.57]
		double trem_val1 = (double)(((double)trem_table_int[i])*4.8/26.0/6.0);             // 4.8db
		double trem_val2 = (double)((double)((int32_t)(trem_table_int[i]/4))*1.2/6.0/6.0); // 1.2db (larger stepping)

		trem_table[i] = (int32_t)(pow(FL2,trem_val1)*FIXEDPT);
		trem_table[TREMTAB_SIZE+i] = (int32_t)(pow(FL2,trem_val2)*FIXEDPT);
	}

	for(i=0; i<BLOCKBUF_SIZE; i++) {
		tremval_const[i] = FIXEDPT;
	}

	// create waveform tables
	for(i=0; i<(WAVEPREC>>1); i++) {
		wavtable[(i<<1)  +WAVEPREC] = (int16_t)(16384*sin((double)((i<<1)  )*M_PI*2/WAVEPREC));
		wavtable[(i<<1)+1+WAVEPREC] = (int16_t)(16384*sin((double)((i<<1)+1)*M_PI*2/WAVEPREC));
		wavtable[i]                 = wavtable[(i<<1) + WAVEPREC];
		// alternative: (zero-less)
		/*
		wavtable[(i<<1)  +WAVEPREC] = (int16_t)(16384*sin((double)((i<<2)+1)*PI/WAVEPREC));
		wavtable[(i<<1)+1+WAVEPREC] = (int16_t)(16384*sin((double)((i<<2)+3)*PI/WAVEPREC));
		wavtable[i]                 = wavtable[(i<<1)-1+WAVEPREC];
		*/
	}
	for(i=0; i<(WAVEPREC>>3); i++) {
		wavtable[i+(WAVEPREC<<1)]		= wavtable[i+(WAVEPREC>>3)]-16384;
		wavtable[i+((WAVEPREC*17)>>3)]	= wavtable[i+(WAVEPREC>>2)]+16384;
	}

	// key scale level table verified ([table in book]*8/3)
	kslev[7][0] = 0;  kslev[7][1] = 24; kslev[7][2] = 32; kslev[7][3] = 37;
	kslev[7][4] = 40; kslev[7][5] = 43; kslev[7][6] = 45; kslev[7][7] = 47;
	kslev[7][8] = 48;
	for(i=9; i<16; i++) {
		kslev[7][i] = (uint8_t)(i+41);
	}
	for(j=6; j>=0; j--) {
		for(i=0; i<16; i++) {
			int oct = (int)kslev[j+1][i]-8;
			if(oct < 0) oct = 0;
			kslev[j][i] = (uint8_t)oct;
		}
	}
}

void OPL::advance_drums(Operator* op_pt1, int32_t vib1,
		Operator* op_pt2, int32_t vib2, Operator* op_pt3, int32_t vib3)
{
	uint32_t c1 = op_pt1->tcount/FIXEDPT;
	uint32_t c3 = op_pt3->tcount/FIXEDPT;
	uint32_t phasebit = (((c1 & 0x88) ^ ((c1<<5) & 0x80)) | ((c3 ^ (c3<<2)) & 0x20)) ? 0x02 : 0x00;
	uint32_t noisebit = rand()&1;
	uint32_t snare_phase_bit = (((unsigned)((op_pt1->tcount/FIXEDPT) / 0x100))&1);

	//Hihat
	uint32_t inttm = ((phasebit<<8) | (0x34<<(phasebit ^ (noisebit<<1)))) * FIXEDPT;
	op_pt1->advance(vib1, m_generator_add, &inttm);

	//Snare
	inttm = (((1+snare_phase_bit) ^ noisebit)<<8) * FIXEDPT;
	op_pt2->advance(vib2, m_generator_add, &inttm);

	//Cymbal
	inttm = ((1+phasebit)<<8) * FIXEDPT;
	op_pt3->advance(vib3, m_generator_add, &inttm);
}


/*******************************************************************************
 * Operator
 */

void OPL::Operator::advance(int32_t vib, uint32_t generator_add, uint32_t *pos)
{
	// waveform position
	if(pos==nullptr) {
		wfpos = tcount;
	} else {
		wfpos = *pos;
	}

	// advance waveform time
	tcount += tinc;
	tcount += (int32_t)(tinc)*vib/FIXEDPT;
	generator_pos += generator_add;
}

// output level is sustained, mode changes only when operator is turned off (->release)
// or when the keep-sustained bit is turned off (->sustain_nokeep)
void OPL::Operator::output(int32_t modulator, int32_t trem)
{
	if(op_state != OF_TYPE_OFF) {
		lastcval = cval;
		uint32_t i = (uint32_t)((wfpos+modulator)/FIXEDPT);

		// wform: -16384 to 16383 (0x4000)
		// trem :  32768 to 65535 (0x10000)
		// step_amp: 0.0 to 1.0
		// vol  : 1/2^14 to 1/2^29 (/0x4000; /1../0x8000)
		int16_t wform = (&wavtable[cur_wform])[i&cur_wmask];
		cval = (int32_t)(step_amp * vol * wform * trem/16.0);
	}
}


// no action, operator is off
void OPL::Operator::off()
{
}

// output level is sustained, mode changes only when operator is turned off (->release)
// or when the keep-sustained bit is turned off (->sustain_nokeep)
void OPL::Operator::sustain()
{
	uint32_t num_steps_add = generator_pos/FIXEDPT; // number of (standardized) samples
	for(uint32_t ct=0; ct<num_steps_add; ct++) {
		cur_env_step++;
	}
	generator_pos -= num_steps_add*FIXEDPT;
}

// operator in release mode, if output level reaches zero the operator is turned off
void OPL::Operator::release()
{
	// ??? boundary?
	if(amp > 0.00000001) {
		// release phase
		amp *= releasemul;
	}

	uint32_t num_steps_add = generator_pos/FIXEDPT; // number of (standardized) samples
	for(uint32_t ct=0; ct<num_steps_add; ct++) {
		cur_env_step++; // sample counter
		if((cur_env_step & env_step_r)==0) {
			if(amp <= 0.00000001) {
				// release phase finished, turn off this operator
				amp = 0.0;
				if(op_state == OF_TYPE_REL) {
					op_state = OF_TYPE_OFF;
				}
			}
			step_amp = amp;
		}
	}
	generator_pos -= num_steps_add*FIXEDPT;
}

// operator in decay mode, if sustain level is reached the output level is either
// kept (sustain level keep enabled) or the operator is switched into release mode
void OPL::Operator::decay()
{
	if(amp > sustain_level) {
		// decay phase
		amp *= decaymul;
	}

	uint32_t num_steps_add = generator_pos/FIXEDPT; // number of (standardized) samples
	for(uint32_t ct=0; ct<num_steps_add; ct++) {
		cur_env_step++;
		if((cur_env_step & env_step_d)==0) {
			if(amp <= sustain_level) {
				// decay phase finished, sustain level reached
				if(sus_keep) {
					// keep sustain level (until turned off)
					op_state = OF_TYPE_SUS;
					amp = sustain_level;
				} else {
					// next: release phase
					op_state = OF_TYPE_SUS_NOKEEP;
				}
			}
			step_amp = amp;
		}
	}
	generator_pos -= num_steps_add*FIXEDPT;
}

// operator in attack mode, if full output level is reached,
// the operator is switched into decay mode
void OPL::Operator::attack()
{
	amp = ((a3*amp + a2)*amp + a1)*amp + a0;

	uint32_t num_steps_add = generator_pos/FIXEDPT;	 // number of (standardized) samples
	for(uint32_t ct=0; ct<num_steps_add; ct++) {
		cur_env_step++; // next sample
		if((cur_env_step & env_step_a)==0) { // check if next step already reached
			if(amp > 1.0) {
				// attack phase finished, next: decay
				op_state = OF_TYPE_DEC;
				amp = 1.0;
				step_amp = 1.0;
			}
			step_skip_pos_a <<= 1;
			if(step_skip_pos_a==0) step_skip_pos_a = 1;
			if(step_skip_pos_a & env_step_skip_a) { // check if required to skip next step
				step_amp = amp;
			}
		}
	}
	generator_pos -= num_steps_add*FIXEDPT;
}

void OPL::Operator::change_attackrate(uint8_t *regs, unsigned regbase)
{
	int attackrate = regs[ARC_ATTR_DECR+regbase]>>4;
	if(attackrate) {
		double f = (double)(pow(FL2,(double)attackrate+(toff>>2)-1)*attackconst[toff&3]*recipsamp);
		// attack rate coefficients
		a0 = 0.0377 * f;
		a1 = 10.73 * f + 1;
		a2 = -17.57 * f;
		a3 = 7.42 * f;

		int step_skip = attackrate*4 + toff;
		int steps = step_skip >> 2;
		env_step_a = (1<<(steps<=12?12-steps:0))-1;

		int step_num = (step_skip<=48)?(4-(step_skip&3)):0;
		static uint8_t step_skip_mask[5] = {0xff, 0xfe, 0xee, 0xba, 0xaa};
		env_step_skip_a = step_skip_mask[step_num];

		if(step_skip>=(type==OPL3?60:62)) {
			a0 = (double)(2.0); // something that triggers an immediate transition to amp:=1.0
			a1 = (double)(0.0);
			a2 = (double)(0.0);
			a3 = (double)(0.0);
		}
	} else {
		// attack disabled
		a0 = 0.0;
		a1 = 1.0;
		a2 = 0.0;
		a3 = 0.0;
		env_step_a = 0;
		env_step_skip_a = 0;
	}
}

void OPL::Operator::change_decayrate(uint8_t *regs, unsigned regbase)
{
	int decayrate = regs[ARC_ATTR_DECR+regbase]&15;
	// decaymul should be 1.0 when decayrate==0
	if(decayrate) {
		double f = (double)(-7.4493*decrelconst[toff&3]*recipsamp);
		decaymul = (double)(pow(FL2,f*pow(FL2,(double)(decayrate+(toff>>2)))));
		int steps = (decayrate*4 + toff) >> 2;
		env_step_d = (1<<(steps<=12?12-steps:0))-1;
	} else {
		decaymul = 1.0;
		env_step_d = 0;
	}
}

void OPL::Operator::change_releaserate(uint8_t *regs, unsigned regbase)
{
	int releaserate = regs[ARC_SUSL_RELR+regbase]&15;
	// releasemul should be 1.0 when releaserate==0
	if(releaserate) {
		double f = (double)(-7.4493*decrelconst[toff&3]*recipsamp);
		releasemul = (double)(pow(FL2,f*pow(FL2,(double)(releaserate+(toff>>2)))));
		int steps = (releaserate*4 + toff) >> 2;
		env_step_r = (1<<(steps<=12?12-steps:0))-1;
	} else {
		releasemul = 1.0;
		env_step_r = 0;
	}
}

void OPL::Operator::change_sustainlevel(uint8_t *regs, unsigned regbase)
{
	int sustainlevel = regs[ARC_SUSL_RELR+regbase]>>4;
	// sustainlevel should be 0.0 when sustainlevel==15 (max)
	if(sustainlevel<15) {
		sustain_level = (double)(pow(FL2,(double)sustainlevel * (-FL05)));
	} else {
		sustain_level = 0.0;
	}
}

void OPL::Operator::change_waveform(uint8_t *wave_sel, unsigned regbase)
{
	if(type == OPL3) {
		if(regbase>=ARC_SECONDSET) {
			regbase -= (ARC_SECONDSET-22); // second set starts at 22
		}
	}
	// waveform selection
	cur_wmask = wavemask[wave_sel[regbase]];
	cur_wform = waveform[wave_sel[regbase]];
	// (might need to be adapted to waveform type here...)
}

void OPL::Operator::change_keepsustain(uint8_t *regs, unsigned regbase)
{
	sus_keep = (regs[ARC_TVS_KSR_MUL+regbase]&0x20)>0;
	if(op_state==OF_TYPE_SUS) {
		if(!sus_keep) op_state = OF_TYPE_SUS_NOKEEP;
	} else if(op_state==OF_TYPE_SUS_NOKEEP) {
		if(sus_keep) op_state = OF_TYPE_SUS;
	}
}

// enable/disable vibrato/tremolo LFO effects
void OPL::Operator::change_vibrato(uint8_t *regs, unsigned regbase)
{
	vibrato = (regs[ARC_TVS_KSR_MUL+regbase]&0x40)!=0;
	tremolo = (regs[ARC_TVS_KSR_MUL+regbase]&0x80)!=0;
}

// change amount of self-feedback
void OPL::Operator::change_feedback(uint8_t *regs, unsigned chanbase)
{
	int feedback = regs[ARC_FEEDBACK+chanbase]&14;
	if(feedback) {
		mfbi = (int32_t)(pow(FL2,(double)((feedback>>1)+8)));
	} else {
		mfbi = 0;
	}
}

void OPL::Operator::change_frequency(uint8_t *regs, unsigned chanbase, unsigned regbase)
{
	// frequency
	uint32_t frn = ((((uint32_t)regs[ARC_KON_BNUM+chanbase])&3)<<8) + (uint32_t)regs[ARC_FREQ_NUM+chanbase];
	// block number/octave
	uint32_t oct = ((((uint32_t)regs[ARC_KON_BNUM+chanbase])>>2)&7);
	freq_high = (int32_t)((frn>>7)&7);

	// keysplit
	uint32_t note_sel = (regs[8]>>6)&1;
	toff = ((frn>>9)&(note_sel^1)) | ((frn>>8)&note_sel);
	toff += (oct<<1);

	// envelope scaling (KSR)
	if(!(regs[ARC_TVS_KSR_MUL+regbase]&0x10)) {
		toff >>= 2;
	}

	// 20+a0+b0:
	double frqmul = frqmul_tab[regs[ARC_TVS_KSR_MUL+regbase]&15] * INTFREQU/WAVEPREC * (double)FIXEDPT * recipsamp;
	tinc = (uint32_t)((((double)(frn<<oct))*frqmul));
	// 40+a0+b0:
	double vol_in = (double)((double)(regs[ARC_KSL_OUTLEV+regbase]&63) +
	                kslmul[regs[ARC_KSL_OUTLEV+regbase]>>6]*kslev[oct][frn>>6]);
	vol = (double)(pow(FL2,(double)(vol_in * -0.125 - 14)));

	// operator frequency changed, care about features that depend on it
	change_attackrate(regs, regbase);
	change_decayrate(regs, regbase);
	change_releaserate(regs, regbase);
}

void OPL::Operator::enable(uint8_t *wave_sel, unsigned regbase, uint32_t act_type)
{
	// check if this is really an off-on transition
	if(act_state == OP_ACT_OFF) {
		int wselbase = regbase;
		if(wselbase>=ARC_SECONDSET) {
			wselbase -= (ARC_SECONDSET-22); // second set starts at 22
		}

		tcount = wavestart[wave_sel[wselbase]]*FIXEDPT;

		// start with attack mode
		op_state = OF_TYPE_ATT;
		act_state |= act_type;
	}
}

void OPL::Operator::disable(uint32_t act_type)
{
	// check if this is really an on-off transition
	if(act_state != OP_ACT_OFF) {
		act_state &= (~act_type);
		if(act_state == OP_ACT_OFF) {
			if(op_state != OF_TYPE_OFF) op_state = OF_TYPE_REL;
		}
	}
}

void OPL::Operator::exec()
{
	switch(op_state) {
		case OF_TYPE_ATT: return attack();
		case OF_TYPE_DEC: return decay();
		case OF_TYPE_REL: return release();
		case OF_TYPE_SUS: return sustain();
		case OF_TYPE_SUS_NOKEEP: return release();
		case OF_TYPE_OFF: return off();
	}
}


/*******************************************************************************
 * Timer
 */

void OPL::OPLTimer::reset()
{
	value = 0;
	overflow = 0;
	masked = false;
	toggle(false);
}

void OPL::OPLTimer::toggle(bool _start)
{
	if(_start) {
		uint32_t time = (256 - value) * increment;
		assert(time);
		g_machine.activate_timer(index, uint64_t(time)*1_us, false);
		PDEBUGF(LOG_V2, LOG_AUDIO, "OPLTimer: T%u start, time=%uus\n", id+1, time);
	} else {
		g_machine.deactivate_timer(index);
	}
}

void OPL::OPLTimer::clear()
{
	overflow = 0;
}

bool OPL::OPLTimer::timeout()
{
	// reloads the preset value
	// DOSBox doesn't do this!
	toggle(true);
	if(masked) {
		return false;
	} else {
		bool ovr = overflow;
		overflow = 0x40 >> id;
		return !ovr;
	}
}


