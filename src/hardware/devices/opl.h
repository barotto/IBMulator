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

#ifndef IBMULATOR_HW_OPL_H
#define IBMULATOR_HW_OPL_H

#include "audio/synth.h"
#include "opl3.h"

#define OPL_SAMPLERATE 49716

class OPL : public SynthChip
{
public:
	enum ChipType {
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

	struct {
		OPLTimer timers[2];
		uint32_t reg_index;
		uint8_t regs[512];
		opl3_chip chip;
	} m_s;

	std::string m_name;
	ChipType m_type = OPL2;

	std::function<void(bool)> m_irqfn;

public:
	OPL();

	void install(ChipType _type, std::string _name, bool _timers);
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
	ChipType type() const { return m_type; }

private:
	void timer(int id);

	bool is_opl3_mode() const { return (m_type==OPL3 && (m_s.regs[0x105] & 1)); }
};

#endif
