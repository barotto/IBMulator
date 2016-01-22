/*
 * Copyright (c) 2001-2014  The Bochs Project
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

#ifndef IBMULATOR_HW_PIT_H
#define IBMULATOR_HW_PIT_H

#include "hardware/iodevice.h"
#include "pit82c54.h"
#include "pcspeaker.h"

class PIT;
extern PIT g_pit;

class PIT : public IODevice
{
private:

	struct {
		PIT_82C54 timer;
		bool speaker_data_on;
		uint64_t  last_nsec;
		uint32_t  last_next_event_time;
		uint64_t  total_ticks;
		uint64_t  total_nsec;
		double dticks_amount;
	} m_s;

	int m_timer_handle;
	uint32_t m_crnt_emulated_ticks;
	uint64_t m_crnt_start_time;
	static void irq0_handler(bool value, uint32_t);
	static void speaker_handler(bool value, uint32_t _cycles);

public:

	PIT();
	virtual ~PIT();

	void init();
	void reset(unsigned type);
	uint16_t read(uint16_t _address, unsigned _io_len);
	void write(uint16_t _address, uint16_t _value, unsigned _io_len);
	const char* get_name() { return "PIT"; }

	void handle_timer();
	bool periodic(uint64_t _time, uint64_t _nsec_delta);

	const PIT_82C54 & get_timer() const {
		return m_s.timer;
	}

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
};

#endif
