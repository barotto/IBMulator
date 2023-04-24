/*
 * Copyright (c) 2001-2014  The Bochs Project
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
 * Emulator of an Intel 8254 Programmable Interval Timer.
 * 
 * Comments by the original author, Greg Alexander <yakovlev@usa.com>:
 *
 * Things I am unclear on (greg):
 * 1.)What happens if both the status and count registers are latched,
 *  but the first of the two count registers has already been read?
 *  I.E.:
 *   latch count 0 (16-bit)
 *   Read count 0 (read LSByte)
 *   READ_BACK status of count 0
 *   Read count 0 - do you get MSByte or status?
 *  This will be flagged as an error.
 * 2.)What happens when we latch the output in the middle of a 2-part
 *  unlatched read?
 * 3.)I assumed that programming a counter removes a latched status.
 * 4.)I implemented the 8254 description of mode 0, not the 82C54 one.
 * 5.)clock() calls represent a rising clock edge followed by a falling
 *  clock edge.
 * 6.)What happens when we trigger mode 1 in the middle of a 2-part
 *  write?
 */

#include "ibmulator.h"
#include "program.h"
#include "machine.h"
#include "hardware/devices.h"
#include "pit.h"
#include "pic.h"
#include "mixer.h"
#include <cstring>

#define PIT_CNT1_AUTO_UPDATE false

IODEVICE_PORTS(PIT) = {
	{ 0x40, 0x43, PORT_8BIT|PORT_RW },
	{ 0x61, 0x61, PORT_8BIT|PORT_RW } // System Control Port B
};
#define PIT_IRQ 0


PIT::PIT(Devices *_dev)
: IODevice(_dev)
{
	memset(&m_s, 0, sizeof(m_s));
}

void PIT::install()
{
	IODevice::install();
	g_machine.register_irq(PIT_IRQ, name());

	m_systimer = g_machine.register_timer(
		std::bind(&PIT::handle_systimer, this, std::placeholders::_1),
		name()
	);
}

void PIT::remove()
{
	IODevice::remove();
	g_machine.unregister_irq(PIT_IRQ, name());
	g_machine.unregister_timer(m_systimer);
}

void PIT::reset(unsigned _type)
{
	if(_type == MACHINE_POWER_ON || _type == MACHINE_HARD_RESET) {
		g_machine.deactivate_timer(m_systimer);
		m_s.speaker_data_on = false;
		m_s.pit_time   = 0;
		m_s.pit_ticks  = 0;
		m_mt_pit_ticks = 0;

		PDEBUGF(LOG_V2, LOG_PIT, "Setting all counters read states to LSB\n");

		for(auto &cnt : m_s.counters) {
			// Chip IOs;
			cnt.GATE   = true;
			cnt.OUTpin = true;

			// Architected state;
			cnt.count        = 0;
			cnt.outlatch     = 0;
			cnt.inlatch      = 0;
			cnt.status_latch = 0;

			// Status Register data;
			cnt.rw_mode    = 1;
			cnt.mode       = 4;
			cnt.bcd_mode   = false;
			cnt.null_count = false;

			// Latch status data;
			cnt.count_LSB_latched = false;
			cnt.count_MSB_latched = false;
			cnt.status_latched    = false;

			// Miscelaneous State;
			cnt.count_binary     = 0;
			cnt.triggerGATE      = false;
			cnt.write_state      = LSByte;
			cnt.read_state       = LSByte;
			cnt.count_written    = true;
			cnt.first_pass       = false;
			cnt.state_bit_1      = false;
			cnt.state_bit_2      = false;
			cnt.next_change_time = 0;

			cnt.seen_problems = 0;
		}
	}
}

void PIT::config_changed()
{
	m_pcspeaker = m_devices->device<PCSpeaker>();
}

void PIT::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_PIT, "saving state\n");

	StateHeader h;
	h.name = name();
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);
}

void PIT::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_PIT, "restoring state\n");

	StateHeader h;
	h.name = name();
	h.data_size = sizeof(m_s);
	_state.read(&m_s,h);

	m_mt_pit_ticks = m_s.pit_ticks;
}

uint16_t PIT::read(uint16_t _address, unsigned /*io_len*/)
{
	// update the PIT emulation
	uint64_t cpu_time = g_machine.get_virt_time_ns();
	uint64_t pit_time = cpu_time / PIT_CLK_TIME * PIT_CLK_TIME;
	if(pit_time < cpu_time) {
		// a read/write advances the PIT time if it happened between two CLK
		// pulses. This puts the PIT in the future relative to the CPU time.
		pit_time += PIT_CLK_TIME;
	}
	update_emulation(pit_time);

	uint8_t value = 0;

	PDEBUGF(LOG_V2, LOG_PIT, "read  0x%02X ", _address);

	switch (_address) {
		case 0x40: /* timer 0 - system ticks */
			value = read_timer(0);
			PDEBUGF(LOG_V2, LOG_PIT, "T0 -> %02d\n", value);
			break;
		case 0x41: /* timer 1 read */
			value = read_timer(1);
			PDEBUGF(LOG_V2, LOG_PIT, "T1 -> %02d\n", value);
			break;
		case 0x42: /* timer 2 read */
			value = read_timer(2);
			PDEBUGF(LOG_V2, LOG_PIT, "T2 -> %02d\n", value);
			break;
		case 0x43: /* Control Byte Register */
			PDEBUGF(LOG_V2, LOG_PIT, "Control Word Reg. -> 0\n");
			break;
		case 0x61: {
			/* AT, port 61h */
			uint16_t refresh_clock_div2 = ((cpu_time / 15085) & 1);
			value = (m_s.counters[2].OUTpin << 5) |
			        (refresh_clock_div2     << 4) |
			        (m_s.speaker_data_on    << 1) |
			        (m_s.counters[2].GATE ? 1 : 0);
			PDEBUGF(LOG_V2, LOG_PIT, "SysCtrlB -> %02Xh\n", value);
			break;
		}
		default:
			throw std::logic_error("unhandled port read");
	}

	update_systimer(cpu_time);
	m_devices->set_io_time(pit_time - cpu_time);

	return value;
}

void PIT::write(uint16_t _address, uint16_t _value, unsigned /*io_len*/)
{
	// update the PIT emulation
	uint64_t cpu_time = g_machine.get_virt_time_ns();
	uint64_t pit_time = cpu_time / PIT_CLK_TIME * PIT_CLK_TIME;
	if(pit_time < cpu_time) {
		// a read/write advances the PIT time if it happened between two CLK
		// pulses. This puts the PIT in the future relative to the CPU time.
		pit_time += PIT_CLK_TIME;
	}
	update_emulation(pit_time);

	uint8_t value = (uint8_t)_value;

	switch (_address) {
		case 0x40: /* timer 0: write count register */
			write_timer(0, value);
			break;
		case 0x41: /* timer 1: write count register */
			write_timer(1, value);
			break;
		case 0x42: /* timer 2: write count register */
			write_timer(2, value);
			break;
		case 0x43: /* timer 0-2 mode control */
			write_timer(3, value);
			break;
		case 0x61: {
			PDEBUGF(LOG_V2, LOG_PIT, "write 0x61 SysCtrlB <- %02Xh ", value);
			bool t2_gate = value & 1;
			bool spkr_on = (value >> 1) & 0x01;
			if(t2_gate) { PDEBUGF(LOG_V2, LOG_PIT, "T2_GATE "); }
			if(spkr_on) { PDEBUGF(LOG_V2, LOG_PIT, "SPKR_ON "); }
			PDEBUGF(LOG_V2, LOG_PIT, "\n");
			set_GATE(2, t2_gate);
			if(m_s.speaker_data_on != spkr_on) {
				if(m_pcspeaker) {
					if(spkr_on) {
						m_pcspeaker->add_event(m_s.pit_ticks, true, m_s.counters[2].OUTpin);
						m_pcspeaker->activate();
						PDEBUGF(LOG_V2, LOG_PIT, "PC-Speaker enable\n");
					} else {
						//the pc speaker mixer channel is disabled by the speaker
						PDEBUGF(LOG_V2, LOG_PIT, "PC-Speaker disable\n");
						m_pcspeaker->add_event(m_s.pit_ticks, false, false);
					}
				}
				m_s.speaker_data_on = spkr_on;
			}
			break;
		}
		default:
			throw std::logic_error("unhandled port write");
	}

	update_systimer(cpu_time);

	// synchronize the CPU with the PIT otherwise any subsequent write or read
	// done before the current CLK tick will be wrong.
	assert(pit_time >= cpu_time);
	m_devices->set_io_time(pit_time - cpu_time);
}

uint8_t PIT::read_timer(uint8_t _address)
{
	if(_address > MAX_ADDRESS) {
		PDEBUGF(LOG_V2, LOG_PIT, "Counter address incorrect in data read\n");
		return 0;
	}

	if(_address == CONTROL_ADDRESS) {
		// Read from control word register;
		// This might be okay.  If so, 0 seems the most logical
		//  return value from looking at the docs.
		PDEBUGF(LOG_V2, LOG_PIT, "Read from control word register not defined\n");
		return 0;
	}

	// Read from a counter;
	PDEBUGF(LOG_V2, LOG_PIT, "PIT Read: Counter %u.", _address);

	Counter &cnt = m_s.counters[_address];

	if(cnt.status_latched) {
		//Latched Status Read;
		if(cnt.count_MSB_latched && (cnt.read_state == MSByte_multiple)) {
			PDEBUGF(LOG_V2, LOG_PIT, "T%u: Undefined output when status latched and count half read\n", _address);
			return 0;
		} else {
			cnt.status_latched = 0;
			return cnt.status_latch;
		}
	}

	// Latched Count Read;
	if(cnt.count_LSB_latched) {
		// Read Least Significant Byte;
		if(cnt.read_state == LSByte_multiple) {
			PDEBUGF(LOG_V2, LOG_PIT, "T%u: Setting read_state to MSB_mult\n", _address);
			cnt.read_state = MSByte_multiple;
		}
		cnt.count_LSB_latched = false;
		return (cnt.outlatch & 0xFF);
	} else if(cnt.count_MSB_latched) {
		// Read Most Significant Byte;
		if(cnt.read_state == MSByte_multiple) {
			PDEBUGF(LOG_V2, LOG_PIT, "T%u: Setting read_state to LSB_mult\n", _address);
			cnt.read_state = LSByte_multiple;
		}
		cnt.count_MSB_latched = false;
		return ((cnt.outlatch >> 8) & 0xFF);
	} else {
		// Unlatched Count Read;
		if(!(cnt.read_state & 0x1)) {
			// Read Least Significant Byte;
			if(cnt.read_state == LSByte_multiple) {
				cnt.read_state = MSByte_multiple;
				PDEBUGF(LOG_V2, LOG_PIT, "T%u: Setting read_state to MSB_mult\n", _address);
			}
			return (cnt.count & 0xFF);
		} else {
			// Read Most Significant Byte;
			if(cnt.read_state == MSByte_multiple) {
				PDEBUGF(LOG_V2, LOG_PIT, "T%u: Setting read_state to LSB_mult\n", _address);
				cnt.read_state = LSByte_multiple;
			}
			return ((cnt.count >> 8) & 0xFF);
		}
	}

	// Should only get here on errors;
	PDEBUGF(LOG_V0, LOG_PIT, "read_timer(%u) error\n", _address);
	return 0;
}

void PIT::write_timer(uint8_t _address, uint8_t _data)
{
	if(_address > MAX_ADDRESS) {
		PDEBUGF(LOG_V2, LOG_PIT, "Counter address incorrect in data write: %u\n", _address);
		return;
	}

	if(_address == CONTROL_ADDRESS) {

		m_s.control_word = _data;
		PDEBUGF(LOG_V2, LOG_PIT, "write Control Byte Register: ");
		uint8_t SC = (m_s.control_word >> 6) & 0x3; // Select Counter
		uint8_t RW = (m_s.control_word >> 4) & 0x3; // Read/Write counter
		uint8_t M  = (m_s.control_word >> 1) & 0x7; // Mode
		uint8_t BCD = m_s.control_word & 0x1;       // Binary Coded Decimal
		if(SC == 3) {
			// READ_BACK command;
			int i;
			PDEBUGF(LOG_V2, LOG_PIT, "READ_BACK\n");
			for(i=0; i<=MAX_COUNTER; i++) {
				if((M>>i) & 0x1) {
					Counter *ctr = &m_s.counters[i];
					// If we are using this counter;
					if(!((m_s.control_word >> 5) & 1)) {
						// Latch Count;
						latch(i);
					}
					if(!((m_s.control_word >> 4) & 1)) {
						// Latch Status;
						if(ctr->status_latched) {
							// Do nothing because latched status has not been read.
						} else {
							ctr->status_latch =
								((ctr->OUTpin & 0x1)     << 7) |
								((ctr->null_count & 0x1) << 6) |
								((ctr->rw_mode & 0x3)    << 4) |
								((ctr->mode & 0x7)       << 1) |
								(ctr->bcd_mode & 0x1);
							ctr->status_latched = true;
						}
					}
				}
			}
		} else {
			Counter *ctr = &m_s.counters[SC];
			if(RW == 0) {
				// Counter Latch command;
				PDEBUGF(LOG_V2, LOG_PIT, "Latch. SC=%d\n", SC);
				latch(SC);
			} else {
				// Counter Program Command;
				PDEBUGF(LOG_V2, LOG_PIT, "Program. SC=%d, RW=%d, M=%d, BCD=%d\n",
						SC, RW, M, BCD);
				ctr->null_count = 1;
				ctr->count_LSB_latched = false;
				ctr->count_MSB_latched = false;
				ctr->status_latched = false;
				ctr->inlatch = 0;
				ctr->count_written = false;
				ctr->first_pass = true;
				ctr->rw_mode = RW;
				ctr->bcd_mode = (BCD > 0);
				ctr->mode = M;
				switch(RW) {
					case 0x1:
						PDEBUGF(LOG_V2, LOG_PIT, "T%u: setting read_state to LSB\n", SC);
						ctr->read_state = LSByte;
						ctr->write_state = LSByte;
						break;
					case 0x2:
						PDEBUGF(LOG_V2, LOG_PIT, "T%u: setting read_state to MSB\n", SC);
						ctr->read_state = MSByte;
						ctr->write_state = MSByte;
						break;
					case 0x3:
						PDEBUGF(LOG_V2, LOG_PIT, "T%u: setting read_state to LSB_mult\n", SC);
						ctr->read_state = LSByte_multiple;
						ctr->write_state = LSByte_multiple;
						break;
					default:
						PDEBUGF(LOG_V2, LOG_PIT, "RW field invalid in control word write\n");
						break;
				}
				// All modes except mode 0 have initial output of 1.;
				if(M == 0) {
					set_OUT(SC, false, 0);
				} else {
					set_OUT(SC, true, 0);
				}
				ctr->next_change_time = 0;
			}
		}

	} else {

		Counter &cnt = m_s.counters[_address];

		PDEBUGF(LOG_V2, LOG_PIT, "write T%u: initial count <- %u ", _address, _data);
		switch(cnt.write_state) {
			case LSByte_multiple:
				cnt.inlatch = _data;
				cnt.write_state = MSByte_multiple;
				PDEBUGF(LOG_V2, LOG_PIT, "(LSByte->MSByte)");
				break;
			case LSByte:
				cnt.inlatch = _data;
				cnt.count_written = true;
				PDEBUGF(LOG_V2, LOG_PIT, "(LSByte)");
				break;
			case MSByte_multiple:
				cnt.write_state = LSByte_multiple;
				cnt.inlatch |= (_data << 8);
				cnt.count_written = true;
				PDEBUGF(LOG_V2, LOG_PIT, "(MSByte->LSByte)");
				break;
			case MSByte:
				cnt.inlatch = (_data << 8);
				cnt.count_written = true;
				PDEBUGF(LOG_V2, LOG_PIT, "(MSByte)");
				break;
		}
		PDEBUGF(LOG_V2, LOG_PIT, " (mode %u)\n", cnt.mode);
		if(cnt.count_written && cnt.write_state != MSByte_multiple) {
			cnt.null_count = true;
			// MODE 1,2,3,5:
			// The current counting sequence is not affected by a new count being
			// written to the counter. If the counter receives a trigger after a new
			// count is written, and before the end of the current count
			// cycle/half-cycle, the new count is loaded on the next CLK pulse, and
			// counting continues from the new count. If the trigger is not received
			// by the counter, the new count is loaded following the current
			// cycle/half-cycle.
			// The original Bochs code doesn't take this into account.
			if(cnt.mode == 0 || cnt.mode == 4) {
				set_count(_address, cnt.inlatch);
			}
		}
		switch(cnt.mode) {
			case 0:
				// If a new count is written to a counter while counting, it is loaded on
				// the next CLK pulse, and counting continues from the new count.
				// If a 2-byte count is written to the counter, the following occurs:
				// 1. The first byte written to the counter disables the counting. OUT
				// goes low immediately and there is no delay for the CLK pulse.
				// 2. When the second byte is written to the counter, the new count is
				// loaded on the next CLK pulse. OUT goes high when the counter
				// reaches 0.
				if(cnt.write_state != LSByte_multiple) {
					set_OUT(_address, false, 0);
				}
				cnt.next_change_time = 1;
				break;
			case 1:
				if(cnt.triggerGATE) { // for initial writes, if already saw trigger.
					cnt.next_change_time = 1;
				} // Otherwise, no change.
				break;
			case 6:
			case 2:
				cnt.next_change_time = 1; // FIXME: this could be loosened.
				break;
			case 7:
			case 3:
				cnt.next_change_time = 1; // FIXME: this could be loosened.
				break;
			case 4:
				cnt.next_change_time = 1;
				break;
			case 5:
				if(cnt.triggerGATE) { // for initial writes, if already saw trigger.
					cnt.next_change_time = 1;
				} // Otherwise, no change.
				break;
			default:
				PWARNF(LOG_V0, LOG_PIT, "Unknown mode %u\n", cnt.mode);
				break;
		}
	}
}

void PIT::handle_systimer(uint64_t _cpu_time)
{
	// this function must be called only on PIT CLK ticks
	assert((_cpu_time % PIT_CLK_TIME) == 0);
	uint64_t pit_time = _cpu_time;
	update_emulation(pit_time);
	update_systimer(_cpu_time);
}

void PIT::update_emulation(uint64_t _pit_time)
{
	assert(m_s.pit_time % PIT_CLK_TIME == 0);
	assert(_pit_time % PIT_CLK_TIME == 0);

	// _pit_time is the current time, it can be not a multiple of the PIT_CLK_TIME
	// but we can update the chip only on CLK ticks.
	// Emulate the ticks and return the nsecs not emulated.
	assert(_pit_time >= m_s.pit_time);
	if(_pit_time == m_s.pit_time) {
		PDEBUGF(LOG_V2, LOG_PIT, "nothing to emulate!\n");
		return;
	}

	// calculate the amount of PIT CLK ticks to emulate
	uint64_t elapsed_nsec = _pit_time - m_s.pit_time;
	assert(elapsed_nsec % PIT_CLK_TIME == 0);
	uint64_t ticks_amount = elapsed_nsec / PIT_CLK_TIME;

	PDEBUGF(LOG_V2, LOG_PIT, "emulating: elapsed time: %llu nsec, %llu CLK pulses\n",
			elapsed_nsec, ticks_amount);

	uint64_t prev_pit_time = m_s.pit_time;
	while(ticks_amount > 0) {
		// how many CLK ticks till the next event?
		uint8_t timer;
		uint32_t next_event = get_next_event_ticks(timer);
		uint32_t ticks = next_event;
		if((next_event == 0) || (next_event>ticks_amount)) {
			// if the next event is NEVER or after the last emulated CLK tick
			// consume all the emulated ticks amount
			ticks = ticks_amount;
		}
		m_crnt_emulated_ticks = ticks;
		clock_all(ticks);
		m_s.pit_ticks += ticks;
		m_s.pit_time += ticks * PIT_CLK_TIME;
		ticks_amount -= ticks;
	}
	m_crnt_emulated_ticks = 0;
	m_mt_pit_ticks = m_s.pit_ticks;

	assert(m_s.pit_time == prev_pit_time + elapsed_nsec);
	assert((m_s.pit_time % PIT_CLK_TIME) == 0);
	assert(m_s.pit_time / PIT_CLK_TIME == m_s.pit_ticks);
}

void PIT::update_systimer(uint64_t _cpu_time)
{
	// call update_emulation() before this function
	uint8_t timer;
	uint32_t next_event = get_next_event_ticks(timer);

	g_machine.deactivate_timer(m_systimer);

	if(next_event) {
		uint64_t next_event_eta = next_event * PIT_CLK_TIME;
		if(m_s.pit_time <= _cpu_time) {
			next_event_eta -= _cpu_time - m_s.pit_time;
		} else {
			next_event_eta += m_s.pit_time - _cpu_time;
		}
		uint64_t abs_time = g_machine.get_virt_time_ns() + next_event_eta;
		if((abs_time % PIT_CLK_TIME) != 0)
			assert((abs_time % PIT_CLK_TIME) == 0);
		g_machine.activate_timer(m_systimer, next_event_eta, false);
		PDEBUGF(LOG_V2, LOG_PIT, "next event: T%u, %u CLK, %llu nsecs (%.2f CLK)\n",
				timer, next_event, next_event_eta, (double(next_event_eta)/PIT_CLK_TIME));
	}
}

void PIT::latch(uint8_t _cnum)
{
	assert(_cnum <= MAX_COUNTER);
	Counter &cnt = m_s.counters[_cnum];

	if(cnt.count_LSB_latched || cnt.count_MSB_latched) {
		// Do nothing because previous latch has not been read
	} else {
		switch(cnt.read_state) {
			case MSByte:
				cnt.outlatch = cnt.count & 0xFFFF;
				cnt.count_MSB_latched = true;
				break;
			case LSByte:
				cnt.outlatch = cnt.count & 0xFFFF;
				cnt.count_LSB_latched = true;
				break;
			case LSByte_multiple:
				cnt.outlatch = cnt.count & 0xFFFF;
				cnt.count_LSB_latched = true;
				cnt.count_MSB_latched = true;
				break;
			case MSByte_multiple:
				if(!(cnt.seen_problems & UNL_2P_READ)) {
					cnt.seen_problems |= UNL_2P_READ;
					PDEBUGF(LOG_V2, LOG_PIT, "T%u: Unknown behavior when latching during 2-part read.\n", _cnum);
					PDEBUGF(LOG_V2, LOG_PIT, "  This message will not be repeated.\n");
				}
				// I guess latching and resetting to LSB first makes sense;
				PDEBUGF(LOG_V2, LOG_PIT, "T%u: Setting read_state to LSB_mult\n", _cnum);
				cnt.read_state = LSByte_multiple;
				cnt.outlatch = cnt.count & 0xFFFF;
				cnt.count_LSB_latched = true;
				cnt.count_MSB_latched = true;
				break;
			default:
				PDEBUGF(LOG_V2, LOG_PIT, "T%u: Unknown read mode found during latch command.\n", _cnum);
				break;
		}
	}
}

void PIT::set_count(uint8_t _cnum, uint32_t _data)
{
	m_s.counters[_cnum].count = _data & 0xFFFF;
	set_binary_to_count(_cnum);
}

void PIT::set_count_to_binary(uint8_t _cnum)
{
	assert(_cnum <= MAX_COUNTER);
	Counter &cnt = m_s.counters[_cnum];

	if(cnt.bcd_mode) {
		cnt.count=
			(((cnt.count_binary / 1) % 10) << 0) |
			(((cnt.count_binary / 10) % 10) << 4) |
			(((cnt.count_binary / 100) % 10) << 8) |
			(((cnt.count_binary / 1000) % 10) << 12);
	} else {
		cnt.count = cnt.count_binary;
	}
}

void PIT::set_binary_to_count(uint8_t _cnum)
{
	assert(_cnum <= MAX_COUNTER);
	Counter &cnt = m_s.counters[_cnum];

	if(cnt.bcd_mode) {
		cnt.count_binary=
				(1 * ((cnt.count >> 0) & 0xF)) +
				(10 * ((cnt.count >> 4) & 0xF)) +
				(100 * ((cnt.count >> 8) & 0xF)) +
				(1000 * ((cnt.count >> 12) & 0xF));
	} else {
		cnt.count_binary = cnt.count;
	}
}

bool PIT::decrement(uint8_t _cnum)
{
	assert(_cnum <= MAX_COUNTER);
	Counter &cnt = m_s.counters[_cnum];

	if(cnt.count == 0) {
		if(cnt.bcd_mode) {
			cnt.count = 0x9999;
			cnt.count_binary = 9999;
		} else {
			cnt.count = 0xFFFF;
			cnt.count_binary = 0xFFFF;
		}
		return true;
	}
	cnt.count_binary--;
	set_count_to_binary(_cnum);

	return false;
}

bool PIT::decrement_multiple(uint8_t _cnum, uint32_t _cycles)
{
	assert(_cnum <= MAX_COUNTER);
	Counter &cnt = m_s.counters[_cnum];

	bool wraparound = false;
	while(_cycles > 0) {
		if(_cycles <= cnt.count_binary) {
			cnt.count_binary -= _cycles;
			_cycles = 0;
			set_count_to_binary(_cnum);
		} else { // cycles > count_binary
			_cycles -= (cnt.count_binary + 1);
			cnt.count_binary = 0;
			set_count_to_binary(_cnum);
			decrement(_cnum);
			// the counter has reached zero!
			wraparound = true;
		}
	}

	return wraparound;
}

void PIT::clock(uint8_t _cnum, uint32_t _cycles)
{
	assert(_cnum <= MAX_COUNTER);
	Counter &cnt = m_s.counters[_cnum];

	switch(cnt.mode) {
		case 0:
			if(cnt.count_written) {
				if(cnt.null_count) {
					set_count(_cnum, cnt.inlatch);
					if(cnt.GATE) {
						if(cnt.count_binary == 0) {
							cnt.next_change_time = 1;
						} else {
							cnt.next_change_time = cnt.count_binary & 0xFFFF;
						}
					} else {
						cnt.next_change_time = 0;
					}
					cnt.null_count = 0;
				} else {
					if(cnt.GATE && (cnt.write_state != MSByte_multiple)) {
						decrement(_cnum);
						if(!cnt.OUTpin) {
							cnt.next_change_time = cnt.count_binary & 0xFFFF;
							if(!cnt.count) {
								set_OUT(_cnum, true, _cycles);
							}
						} else {
							cnt.next_change_time = 0;
						}
					} else {
						cnt.next_change_time = 0; // if the clock isn't moving.
					}
				}
			} else {
				cnt.next_change_time = 0; // default to 0.
			}
			cnt.triggerGATE = false;
			break;
		case 1:
			if(cnt.count_written) {
				if(cnt.triggerGATE) {
					set_count(_cnum, cnt.inlatch);
					if(cnt.count_binary == 0) {
						cnt.next_change_time = 1;
					} else {
						cnt.next_change_time = cnt.count_binary & 0xFFFF;
					}
					cnt.null_count = 0;
					set_OUT(_cnum, false, _cycles);
					if(cnt.write_state == MSByte_multiple) {
						PDEBUGF(LOG_V1, LOG_PIT,
								"T%u: Undefined behavior when loading a half loaded count.\n", _cnum);
					}
				} else {
					decrement(_cnum);
					if(!cnt.OUTpin) {
						if(cnt.count_binary == 0) {
							cnt.next_change_time = 1;
						} else {
							cnt.next_change_time = cnt.count_binary & 0xFFFF;
						}
						if(cnt.count == 0) {
							set_OUT(_cnum, true, _cycles);
						}
					} else {
						cnt.next_change_time = 0;
					}
				}
			} else {
				cnt.next_change_time = 0; // default to 0.
			}
			cnt.triggerGATE = false;
			break;
		case 2:
			if(cnt.count_written) {
				if(cnt.triggerGATE || cnt.first_pass) {
					set_count(_cnum, cnt.inlatch);
					cnt.next_change_time = (cnt.count_binary - 1) & 0xFFFF;
					cnt.null_count = 0;
					if(cnt.inlatch == 1) {
						PDEBUGF(LOG_V1, LOG_PIT,
								"T%u ERROR: count of 1 is invalid in pit mode 2.\n", _cnum);
					}
					if(!cnt.OUTpin) {
						set_OUT(_cnum, true, _cycles);
					}
					if(cnt.write_state == MSByte_multiple) {
						PDEBUGF(LOG_V1, LOG_PIT,
								"T%u: Undefined behavior when loading a half loaded count.\n", _cnum);
					}
					cnt.first_pass = false;
				} else {
					if(cnt.GATE) {
						decrement(_cnum);
						cnt.next_change_time = (cnt.count_binary - 1) & 0xFFFF;
						if(cnt.count == 1) {
							cnt.next_change_time = 1;
							set_OUT(_cnum, false, _cycles);
							cnt.first_pass = true;
						}
					} else {
						cnt.next_change_time = 0;
					}
				}
			} else {
				cnt.next_change_time = 0;
			}
			cnt.triggerGATE = false;
			break;
		case 3:
			if(cnt.count_written) {
				if((cnt.triggerGATE || cnt.first_pass || cnt.state_bit_2) && cnt.GATE)
				{
					set_count(_cnum, cnt.inlatch & 0xFFFE);
					cnt.state_bit_1 = cnt.inlatch & 0x1;
					uint32_t real_count = cnt.count_binary == 0 ? 65536 : cnt.count_binary;
					if(!cnt.OUTpin || !cnt.state_bit_1) {
						if(((real_count / 2) - 1) == 0) {
							cnt.next_change_time = 1;
						} else {
							// Bochs code: this is plain wrong. if the inlatch is
							// 0 (eq to 65536) then count_binary is 0 and
							// next_change_time will be 65535 which is not
							// correct, it should be 32767
							//   cnt.next_change_time = ((cnt.count_binary / 2) - 1) & 0xFFFF;
							cnt.next_change_time = ((real_count / 2) - 1) & 0xFFFF;
						}
					} else {
						if((real_count / 2) == 0) {
							cnt.next_change_time = 1;
						} else {
							cnt.next_change_time = (real_count / 2) & 0xFFFF;
						}
					}
					cnt.null_count = 0;
					if(cnt.inlatch == 1) {
						PDEBUGF(LOG_V2, LOG_PIT, "T%u: Count of 1 is invalid in pit mode 3.\n", _cnum);
					}
					if(!cnt.OUTpin) {
						set_OUT(_cnum, true, _cycles);
					} else if(cnt.OUTpin && !cnt.first_pass) {
						set_OUT(_cnum, false, _cycles);
					}
					if(cnt.write_state == MSByte_multiple) {
						PDEBUGF(LOG_V0, LOG_PIT,
								"T%u: Undefined behavior when loading a half loaded count.\n", _cnum);
					}
					cnt.state_bit_2 = false;
					cnt.first_pass = false;
				} else {
					if(cnt.GATE) {
						decrement(_cnum);
						decrement(_cnum);
						//see above
						uint32_t real_count = cnt.count_binary == 0 ? 65536 : cnt.count_binary;
						if(!cnt.OUTpin || !cnt.state_bit_1) {
							cnt.next_change_time = ((real_count / 2) - 1) & 0xFFFF;
						} else {
							cnt.next_change_time = (real_count / 2) & 0xFFFF;
						}
						if(cnt.count == 0) {
							cnt.state_bit_2 = true;
							cnt.next_change_time = 1;
						}
						if((cnt.count == 2) && (!cnt.OUTpin || !cnt.state_bit_1)) {
							cnt.state_bit_2 = true;
							cnt.next_change_time = 1;
						}
					} else {
						cnt.next_change_time = 0;
					}
				}
			} else {
				cnt.next_change_time = 0;
			}
			cnt.triggerGATE = false;
			break;
		case 4:
			if(cnt.count_written) {
				if(!cnt.OUTpin) {
					set_OUT(_cnum, true, _cycles);
				}
				if(cnt.null_count) {
					set_count(_cnum, cnt.inlatch);
					if(cnt.GATE) {
						if(cnt.count_binary == 0) {
							cnt.next_change_time = 1;
						} else {
							cnt.next_change_time = cnt.count_binary & 0xFFFF;
						}
					} else {
						cnt.next_change_time = 0;
					}
					cnt.null_count=0;
					if(cnt.write_state == MSByte_multiple) {
						PDEBUGF(LOG_V2, LOG_PIT,
								"T%u: Undefined behavior when loading a half loaded count.\n", _cnum);
					}
					cnt.first_pass = true;
				} else {
					if(cnt.GATE) {
						decrement(_cnum);
						if(cnt.first_pass) {
							cnt.next_change_time = cnt.count_binary & 0xFFFF;
							if(!cnt.count) {
								set_OUT(_cnum, false, _cycles);
								cnt.next_change_time = 1;
								cnt.first_pass = false;
							}
						} else {
							cnt.next_change_time = 0;
						}
					} else {
						cnt.next_change_time = 0;
					}
				}
			} else {
				cnt.next_change_time = 0;
			}
			cnt.triggerGATE = false;
			break;
		case 5:
			if(cnt.count_written) {
				if(!cnt.OUTpin) {
					set_OUT(_cnum, true, _cycles);
				}
				if(cnt.triggerGATE) {
					set_count(_cnum, cnt.inlatch);
					if(cnt.count_binary == 0) {
						cnt.next_change_time = 1;
					} else {
						cnt.next_change_time = cnt.count_binary & 0xFFFF;
					}
					cnt.null_count = 0;
					if(cnt.write_state == MSByte_multiple) {
						PDEBUGF(LOG_V2, LOG_PIT,
								"T%u: Undefined behavior when loading a half loaded count.\n", _cnum);
					}
					cnt.first_pass = true;
				} else {
					decrement(_cnum);
					if(cnt.first_pass) {
						cnt.next_change_time = cnt.count_binary & 0xFFFF;
						if(!cnt.count) {
							set_OUT(_cnum, false, _cycles);
							cnt.next_change_time = 1;
							cnt.first_pass = false;
						}
					} else {
						cnt.next_change_time = 0;
					}
				}
			} else {
				cnt.next_change_time = 0;
			}
			cnt.triggerGATE = false;
			break;
		default:
			PDEBUGF(LOG_V2, LOG_PIT, "Mode %u not implemented.\n", cnt.mode);
			cnt.next_change_time = 0;
			cnt.triggerGATE = false;
			break;
	}

}

void PIT::clock_multiple(uint8_t _cnum, uint32_t _cycles)
{
	assert(_cnum <= MAX_COUNTER);
	Counter &cnt = m_s.counters[_cnum];

	while(_cycles > 0) {
		if(cnt.next_change_time == 0) {
			if(cnt.count_written) {
				switch(cnt.mode) {
					case 0:
						if(cnt.GATE && (cnt.write_state != MSByte_multiple)) {
							decrement_multiple(_cnum, _cycles);
						}
						break;
					case 1:
						decrement_multiple(_cnum, _cycles);
						break;
					case 2:
						if(!cnt.first_pass && cnt.GATE) {
							decrement_multiple(_cnum, _cycles);
						}
						break;
					case 3:
						if(!cnt.first_pass && cnt.GATE) {
							// i think
							// the program can't get here because next_change_time is 0
							// only when (count_written==0) || (count_written==1 && GATE==0)
							decrement_multiple(_cnum, 2 * _cycles);
						}
						break;
					case 4:
						if(cnt.GATE) {
							decrement_multiple(_cnum, _cycles);
						}
						break;
					case 5:
						decrement_multiple(_cnum, _cycles);
						break;
					default:
						break;
				}
			}
			_cycles = 0;
		} else { // next_change_time!=0
			switch(cnt.mode) {
				case 0:
				case 1:
				case 2:
				case 4:
				case 5:
					if(cnt.next_change_time > _cycles) {
						decrement_multiple(_cnum, _cycles);
						cnt.next_change_time -= _cycles;
						_cycles = 0;
					} else {
						decrement_multiple(_cnum, (cnt.next_change_time - 1));
						_cycles -= cnt.next_change_time;
						clock(_cnum, _cycles);
					}
					break;
				case 3:
					if(cnt.next_change_time > (_cycles)) {
						decrement_multiple(_cnum, _cycles * 2);
						cnt.next_change_time -= _cycles;
						_cycles = 0;
					} else {
						decrement_multiple(_cnum, (cnt.next_change_time - 1) * 2);
						_cycles -= cnt.next_change_time;
						clock(_cnum, _cycles);
					}
					break;
				default:
					_cycles = 0;
					break;
			}
		}
	}
}

void PIT::clock_all(uint32_t _cycles)
{
	// PDEBUGF(LOG_V2, LOG_PIT, "clock_all: cycles=%d\n",cycles);
	clock_multiple(0, _cycles);
	clock_multiple(1, _cycles);
	clock_multiple(2, _cycles);
}

uint32_t PIT::get_next_event_ticks(uint8_t &_timer)
{
	uint32_t time0 = m_s.counters[0].next_change_time;
	uint32_t time1 = m_s.counters[1].next_change_time;
	uint32_t time2 = m_s.counters[2].next_change_time;

	uint32_t out = time0;
	_timer = 0;
	if(PIT_CNT1_AUTO_UPDATE && time1 && (time1<out)) {
		out = time1;
		_timer = 1;
	}
	if(time2 && (time2<out)) {
		out = time2;
		_timer = 2;
	}
	return out;
}

void PIT::set_OUT(uint8_t _cnum, bool _value, uint32_t _remaining_ticks)
{
	assert(_cnum <= MAX_COUNTER);
	if(m_s.counters[_cnum].OUTpin != _value) {
		m_s.counters[_cnum].OUTpin = _value;
		if(_cnum == 0) {
			if(_value == true) {
				PDEBUGF(LOG_V1, LOG_PIT, "raising IRQ %d\n", PIT_IRQ);
				m_devices->pic()->raise_irq(PIT_IRQ);
			} else {
				PDEBUGF(LOG_V2, LOG_PIT, "lowering IRQ %d\n", PIT_IRQ);
				m_devices->pic()->lower_irq(PIT_IRQ);
			}
		} else if(_cnum == 2 && m_pcspeaker && m_s.speaker_data_on) {
			uint64_t ticks;
			if(m_crnt_emulated_ticks) {
				uint32_t elapsed_ticks = m_crnt_emulated_ticks - _remaining_ticks;
				ticks = (m_s.pit_ticks + elapsed_ticks);
				PDEBUGF(LOG_V2, LOG_PIT, "PC speaker evt: emu ticks %d, elapsed %d, CLK %llu\n",
						m_crnt_emulated_ticks, elapsed_ticks, ticks);
			} else {
				// this case happens only on a write, the PIT time is updated
				ticks = m_s.pit_ticks;
			}
			m_pcspeaker->add_event(ticks, true, _value);
		}
	}
}

void PIT::set_GATE(uint8_t _cnum, bool _value)
{
	assert(_cnum <= MAX_COUNTER);
	Counter &cnt = m_s.counters[_cnum];

	if(!((cnt.GATE && _value) || (!(cnt.GATE || _value)))) {
		PDEBUGF(LOG_V2, LOG_PIT, "T%u: changing GATE to %d\n", _cnum, _value);
		cnt.GATE = _value;
		if(cnt.GATE) {
			cnt.triggerGATE = true;
		}
		switch(cnt.mode) {
			case 0:
				if(_value && cnt.count_written) {
					if(cnt.null_count) {
						cnt.next_change_time = 1;
					} else {
						if((!cnt.OUTpin) && (cnt.write_state != MSByte_multiple))
						{
							if(cnt.count_binary == 0) {
								cnt.next_change_time = 1;
							} else {
								cnt.next_change_time = cnt.count_binary & 0xFFFF;
							}
						} else {
							cnt.next_change_time = 0;
						}
					}
				} else {
					if(cnt.null_count) {
						cnt.next_change_time = 1;
					} else {
						cnt.next_change_time = 0;
					}
				}
				break;
			case 1:
				if(_value && cnt.count_written) { // only triggers cause a change.
					cnt.next_change_time = 1;
				}
			break;
			case 2:
				if(!_value) {
					set_OUT(_cnum, true, 0);
					cnt.next_change_time = 0;
				} else {
					if(cnt.count_written) {
						cnt.next_change_time = 1;
					} else {
						cnt.next_change_time = 0;
					}
				}
				break;
			case 3:
				if(!_value) {
					set_OUT(_cnum, true, 0);
					cnt.first_pass = true;
					cnt.next_change_time = 0;
				} else {
					if(cnt.count_written) {
						cnt.next_change_time = 1;
					} else {
						cnt.next_change_time = 0;
					}
				}
				break;
			case 4:
				if(!cnt.OUTpin || cnt.null_count) {
					cnt.next_change_time = 1;
				} else {
					if(_value && cnt.count_written) {
						if(cnt.first_pass) {
							if(cnt.count_binary == 0) {
								cnt.next_change_time = 1;
							} else {
								cnt.next_change_time = cnt.count_binary & 0xFFFF;
							}
						} else {
							cnt.next_change_time = 0;
						}
					} else {
						cnt.next_change_time = 0;
					}
				}
				break;
			case 5:
				if(_value && cnt.count_written) { // only triggers cause a change.
					cnt.next_change_time = 1;
				}
				break;
			default:
				break;
		}
	}
}
