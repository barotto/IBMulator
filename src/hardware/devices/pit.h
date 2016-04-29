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

class PIC;

#define PIT_CLK_TIME (uint64_t(838))
#define PIT_FREQ (1193317)

class PIT : public IODevice
{
	IODEVICE(PIT, "8254 PIT")

private:
	struct {
		PIT_82C54 timer;
		bool      speaker_data_on;
		uint64_t  pit_time;
		uint64_t  pit_ticks;
	} m_s;

	int m_systimer;
	uint32_t m_crnt_emulated_ticks;
	std::atomic<uint64_t> m_mt_pit_ticks;
	PCSpeaker *m_pcspeaker;

public:
	PIT(Devices* _dev);
	~PIT();

	void install();
	void remove();
	void reset(unsigned type);
	void config_changed();
	uint16_t read(uint16_t _address, unsigned _io_len);
	void write(uint16_t _address, uint16_t _value, unsigned _io_len);

	const PIT_82C54 & get_timer() const {
		return m_s.timer;
	}
	uint64_t get_pit_ticks_mt() {
		return m_mt_pit_ticks;
	}

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

private:
	void handle_systimer(uint64_t);
	void update_emulation(uint64_t _pit_time);
	void update_systimer(uint64_t _cpu_time);
	void irq0_handler(bool value, uint32_t);
	void speaker_handler(bool value, uint32_t _cycles);
	void set_OUT_handlers();
};

#endif
