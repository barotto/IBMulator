/*
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

#include "ibmulator.h"
#include "machine.h"
#include "opl.h"

#define OPL_THREADED_RENDERING true
#define ARC_SECONDSET 0x100 // second operator set for OPL3

constexpr const char * OPL::ChipNames[];

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
}

void OPL::install(ChipType _type, std::string _name, bool _timers)
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

void OPL::config_changed(int)
{
	OPL3_Reset(&m_s.chip, OPL_SAMPLERATE);
}

void OPL::reset()
{
	memset((void *)m_s.regs, 0, sizeof(m_s.regs));

	m_s.timers[T1].reset();
	m_s.timers[T2].reset();

	m_s.reg_index = 0;

	m_irqfn(false);

	OPL3_Reset(&m_s.chip, OPL_SAMPLERATE);
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

	OPL3_UpdateAfterStateRestored(&m_s.chip, OPL_SAMPLERATE);
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

	OPL3_WriteRegBuffered(&m_s.chip, m_s.reg_index, _val);
}

void OPL::generate(int16_t *_buffer, int _frames, int _stride)
{
	assert(_stride > 0);

	int16_t samples[4];
	if(is_opl3_mode()) {
		// stereo
		for(int i=0; i<_frames; i++,_buffer+=_stride) {
			OPL3_Generate4Ch(&m_s.chip, samples);
			_buffer[0] = samples[0];
			if(_stride >= 2) {
				_buffer[1] = samples[1];
			}
		}
	} else {
		// mono
		for(int i=0; i<_frames; i++,_buffer+=_stride) {
			OPL3_Generate4Ch(&m_s.chip, samples);
			_buffer[0] = samples[0];
			if(m_type == OPL3 && _stride>=2) {
				_buffer[1] = samples[0];
			}
		}
	}
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
