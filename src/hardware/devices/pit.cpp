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

#include "ibmulator.h"
#include "program.h"
#include "machine.h"
#include "hardware/devices.h"
#include "pit.h"
#include "pic.h"
#include "mixer.h"
#include <cstring>

PIT g_pit;

//1.1931816666MHz Clock
#define TICKS_PER_SECOND (1193182.0)
#define CYCLE_TIME (1000000000.0/TICKS_PER_SECOND) //838.095110386 ns


PIT::PIT()
{
	m_timer_handle = NULL_TIMER_HANDLE;
}

PIT::~PIT()
{
}

void PIT::init(void)
{
	g_devices.register_read_handler(this, 0x0040, 1);
	g_devices.register_read_handler(this, 0x0041, 1);
	g_devices.register_read_handler(this, 0x0042, 1);
	g_devices.register_read_handler(this, 0x0043, 1);

	g_devices.register_write_handler(this, 0x0040, 1);
	g_devices.register_write_handler(this, 0x0041, 1);
	g_devices.register_write_handler(this, 0x0042, 1);
	g_devices.register_write_handler(this, 0x0043, 1);

	//System Control Port B
	g_devices.register_read_handler(this, 0x0061, 1);
	g_devices.register_write_handler(this, 0x0061, 1);

	g_machine.register_irq(0, "8254 PIT");

	m_timer_handle = g_machine.register_timer_ns(
		std::bind(&PIT::handle_timer, this),
		100560u, //nsec
		true, //continuous
		true, //active
		get_name() //name
	);

	m_s.timer.init();
	m_s.timer.set_OUT_handler(0, &PIT::irq0_handler);
	m_s.timer.set_OUT_handler(2, &PIT::speaker_handler);
}

void PIT::reset(unsigned type)
{
	if(type == MACHINE_POWER_ON) {
		m_s.speaker_data_on = false;

		uint64_t my_time_nsec = g_machine.get_virt_time_ns();

		PDEBUGF(LOG_V2, LOG_PIT, "RESETting timer.\n");
		g_machine.deactivate_timer(m_timer_handle);
		PDEBUGF(LOG_V2, LOG_PIT, "deactivated timer.\n");
		if(m_s.timer.get_next_event_time()) {
			g_machine.activate_timer_ns(
				m_timer_handle,
				std::max(uint64_t(1u), uint64_t(CYCLE_TIME*m_s.timer.get_next_event_time())),
				false
			);
			PDEBUGF(LOG_V2, LOG_PIT, "activated timer.\n");
		}
		m_s.last_next_event_time = m_s.timer.get_next_event_time();
		m_s.last_nsec = my_time_nsec;

		m_s.total_ticks = 0;
		m_s.total_nsec = 0;

		PDEBUGF(LOG_V2, LOG_PIT, " s.last_nsec = %llu\n", m_s.last_nsec);
		PDEBUGF(LOG_V2, LOG_PIT, " next event time = %x\n", m_s.timer.get_next_event_time());
		PDEBUGF(LOG_V2, LOG_PIT, " last next event time = %d\n", m_s.last_next_event_time);
	}

	m_s.timer.reset(type);
}

void PIT::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_PIT, "saving state\n");

	StateHeader h;
	h.name = get_name();
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);
}

void PIT::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_PIT, "restoring state\n");

	StateHeader h;
	h.name = get_name();
	h.data_size = sizeof(m_s);
	_state.read(&m_s,h);

	m_s.timer.set_OUT_handler(0, &PIT::irq0_handler);
	m_s.timer.set_OUT_handler(2, &PIT::speaker_handler);
}

void PIT::handle_timer()
{
	uint64_t my_time_ns = g_machine.get_virt_time_ns();
	uint64_t time_passed = my_time_ns - m_s.last_nsec;

	//PDEBUGF(LOG_V2, LOG_PIT, "entering timer handler\n");

	if(time_passed) {
		periodic(my_time_ns, time_passed);
	}
	m_s.last_nsec = m_s.last_nsec + time_passed;
	if(time_passed || (m_s.last_next_event_time != m_s.timer.get_next_event_time())) {
		//PDEBUGF(LOG_V2, LOG_PIT, "RESETting timer\n");
		g_machine.deactivate_timer(m_timer_handle);
		//PDEBUGF(LOG_V2, LOG_PIT, "deactivated timer\n");
		if(m_s.timer.get_next_event_time()) {
			g_machine.activate_timer_ns(m_timer_handle,
				std::max(uint64_t(1u), uint64_t(CYCLE_TIME*m_s.timer.get_next_event_time())),
				false
			);
			//PDEBUGF(LOG_V2, LOG_PIT, "activated timer\n");
		}
		m_s.last_next_event_time = m_s.timer.get_next_event_time();
	}
	/*
	PDEBUGF(LOG_V2, LOG_PIT, " s.last_usec = %llu\n", m_s.last_usec);
	PDEBUGF(LOG_V2, LOG_PIT, " next event time = %x\n", m_s.timer.get_next_event_time());
	PDEBUGF(LOG_V2, LOG_PIT, " last next event time = %d\n", m_s.last_next_event_time);
	*/
}

uint16_t PIT::read(uint16_t address, unsigned /*io_len*/)
{
	uint8_t value = 0;

	handle_timer();

	switch (address) {
		case 0x40: /* timer 0 - system ticks */
			value = m_s.timer.read(0);
			break;
		case 0x41: /* timer 1 read */
			value = m_s.timer.read(1);
			break;
		case 0x42: /* timer 2 read */
			value = m_s.timer.read(2);
			break;
		case 0x43: /* timer 1 read */
			value = m_s.timer.read(3);
			break;
		case 0x61: {
			/* AT, port 61h */
			uint16_t refresh_clock_div2 = ((g_machine.get_virt_time_ns() / 15085) & 1);
			value = (m_s.timer.read_OUT(2)  << 5) |
				  (refresh_clock_div2     << 4) |
				  (m_s.speaker_data_on    << 1) |
				  (m_s.timer.read_GATE(2) ? 1 : 0);
			break;
		}
	}

	PDEBUGF(LOG_V2, LOG_PIT, "read from port 0x%04x, value = 0x%02x\n", address, value);
	return value;
}

void PIT::write(uint16_t _address, uint16_t _value, unsigned /*io_len*/)
{
	uint8_t  value;
	uint64_t my_time_nsec = g_machine.get_virt_time_ns();
	uint64_t time_passed = my_time_nsec - m_s.last_nsec;

	if(time_passed) {
		periodic(my_time_nsec, time_passed);
	}
	m_s.last_nsec = m_s.last_nsec + time_passed;

	value = (uint8_t)_value;

	PDEBUGF(LOG_V2, LOG_PIT, "write to port 0x%04x, value = 0x%02x\n", _address, value);

	switch (_address) {
		case 0x40: /* timer 0: write count register */
			m_s.timer.write(0, value);
			break;
		case 0x41: /* timer 1: write count register */
			m_s.timer.write(1, value);
			break;
		case 0x42: /* timer 2: write count register */
			m_s.timer.write(2, value);
			break;

		case 0x43: /* timer 0-2 mode control */
			m_s.timer.write(3, value);
			break;

		case 0x61:
			m_s.timer.set_GATE(2, value & 0x01);
			bool speaker_data_on = (value >> 1) & 0x01;
			if(m_s.speaker_data_on != speaker_data_on) {
				if(speaker_data_on) {
					g_pcspeaker.add_event(my_time_nsec, true, m_s.timer.read_OUT(2));
					g_pcspeaker.activate();
					PDEBUGF(LOG_V2, LOG_PIT, "pc speaker enable\n");
				} else {
					//the pc speaker mixer channel is disabled by the speaker
					PDEBUGF(LOG_V2, LOG_PIT, "pc speaker disable\n");
					g_pcspeaker.add_event(my_time_nsec, false, false);
				}
				m_s.speaker_data_on = speaker_data_on;
			}

			break;
	}

	if(time_passed || (m_s.last_next_event_time != m_s.timer.get_next_event_time())) {
		PDEBUGF(LOG_V2, LOG_PIT, "RESETting timer: ");
		g_machine.deactivate_timer(m_timer_handle);
		if(m_s.timer.get_next_event_time()) {
			//uint64_t nsecs = std::max(1u, CYCLE_TIME*m_s.timer.get_next_event_time());
			//uint32_t usecs = TICKS_TO_USEC(m_s.timer.get_next_event_time());
			uint64_t nsecs = CYCLE_TIME*m_s.timer.get_next_event_time();
			g_machine.activate_timer_ns(
				m_timer_handle,
				nsecs,
				false
			);
			PDEBUGF(LOG_V2, LOG_PIT, "activated timer: %u nsecs\n", nsecs);
		} else {
			PDEBUGF(LOG_V2, LOG_PIT, "deactivated timer\n");
		}
		m_s.last_next_event_time = m_s.timer.get_next_event_time();
	}

	PDEBUGF(LOG_V2, LOG_PIT, " s.last_nsec = %llu\n", m_s.last_nsec);
	PDEBUGF(LOG_V2, LOG_PIT, " next event time = %x\n", m_s.timer.get_next_event_time());
	PDEBUGF(LOG_V2, LOG_PIT, " last next event time = %d\n", m_s.last_next_event_time);
}

bool PIT::periodic(uint64_t _time, uint64_t _nsec_delta)
{
	uint64_t ticks_amount = 0;

	m_crnt_start_time = _time;

	m_s.total_nsec += _nsec_delta;
	//calculate the amount of PIT CLK ticks to emulate
	double dticks_amount = double(_nsec_delta)/double(CYCLE_TIME) + m_s.dticks_amount;
	ticks_amount = uint64_t(dticks_amount);
	m_s.dticks_amount = dticks_amount - ticks_amount;
	m_s.total_ticks += ticks_amount;

	while(ticks_amount > 0) {
		// how many CLK ticks till the next event?
		uint32_t next_event = m_s.timer.get_next_event_time();
		uint32_t ticks = next_event;
		if((next_event == 0) || (next_event>ticks_amount)) {
			//if the next event is NEVER or after the last emulated CLK tick
			//consume all the emulated ticks amount
			ticks = ticks_amount;
		}
		m_crnt_emulated_ticks = ticks;
		m_s.timer.clock_all(ticks);
		ticks_amount -= ticks;
	}

	m_crnt_emulated_ticks = 0;
	return 0;
}

void PIT::irq0_handler(bool value, uint32_t)
{
	if(value == true) {
		g_pic.raise_irq(0);
	} else {
		g_pic.lower_irq(0);
	}
}

void PIT::speaker_handler(bool value, uint32_t _remaining_ticks)
{
	if(!g_pit.m_s.speaker_data_on) {
		return;
	}

	uint64_t cur_time, elapsed_ticks, elapsed_nsec;
	if(g_pit.m_crnt_emulated_ticks!=0) {
		elapsed_ticks = g_pit.m_crnt_emulated_ticks - _remaining_ticks;
		elapsed_nsec = double(elapsed_ticks) * CYCLE_TIME;
		cur_time = g_pit.m_crnt_start_time + elapsed_nsec;
	} else {
		cur_time = g_machine.get_virt_time_ns();
	}

	g_pcspeaker.add_event(cur_time, true, value);
}


