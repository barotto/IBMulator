/*
 * Copyright (c) 2001-2014  The Bochs Project
 * Copyright (C) 2015-2022  Marco Bortolin
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

IODEVICE_PORTS(PIT) = {
	{ 0x40, 0x43, PORT_8BIT|PORT_RW },
	{ 0x61, 0x61, PORT_8BIT|PORT_RW } // System Control Port B
};
#define PIT_IRQ 0


PIT::PIT(Devices* _dev)
: IODevice(_dev)
{
	m_systimer = NULL_TIMER_ID;
}

PIT::~PIT()
{
}

void PIT::install()
{
	IODevice::install();
	g_machine.register_irq(PIT_IRQ, name());

	m_systimer = g_machine.register_timer(
		std::bind(&PIT::handle_systimer, this, std::placeholders::_1),
		name()
	);
	m_s.timer.init();
}

void PIT::remove()
{
	IODevice::remove();
	g_machine.unregister_irq(PIT_IRQ, name());
	g_machine.unregister_timer(m_systimer);
}

void PIT::reset(unsigned type)
{
	m_s.timer.reset(type);

	if(type == MACHINE_POWER_ON || type == MACHINE_HARD_RESET) {
		g_machine.deactivate_timer(m_systimer);
		m_s.speaker_data_on = false;
		m_s.pit_time   = 0;
		m_s.pit_ticks  = 0;
		m_mt_pit_ticks = 0;
	}
}

void PIT::config_changed()
{
	m_pcspeaker = m_devices->device<PCSpeaker>();
	set_OUT_handlers();
}

void PIT::set_OUT_handlers()
{
	using namespace std::placeholders;
	m_s.timer.set_OUT_handler(0, std::bind(&PIT::irq0_handler, this, _1, _2));
	if(m_pcspeaker) {
		m_s.timer.set_OUT_handler(2, std::bind(&PIT::speaker_handler, this, _1, _2));
	} else {
		m_s.timer.set_OUT_handler(2, nullptr);
	}
}

void PIT::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_PIT, "saving state\n");

	// nullify the handlers before saving the state. why? because otherwise:
	// savedstate with non-null handlers -> restore -> set new handlers ->
	//    handler desctructor on invalid pointer -> SIGSEGV
	m_s.timer.set_OUT_handler(0, nullptr);
	m_s.timer.set_OUT_handler(2, nullptr);

	StateHeader h;
	h.name = name();
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);

	set_OUT_handlers();
}

void PIT::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_PIT, "restoring state\n");

	StateHeader h;
	h.name = name();
	h.data_size = sizeof(m_s);
	_state.read(&m_s,h);

	m_mt_pit_ticks = m_s.pit_ticks;

	set_OUT_handlers();
}

uint16_t PIT::read(uint16_t address, unsigned /*io_len*/)
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

	PDEBUGF(LOG_V2, LOG_PIT, "read  0x%02X ", address);

	switch (address) {
		case 0x40: /* timer 0 - system ticks */
			value = m_s.timer.read(0);
			PDEBUGF(LOG_V2, LOG_PIT, "T0 -> %02d\n", value);
			break;
		case 0x41: /* timer 1 read */
			value = m_s.timer.read(1);
			PDEBUGF(LOG_V2, LOG_PIT, "T1 -> %02d\n", value);
			break;
		case 0x42: /* timer 2 read */
			value = m_s.timer.read(2);
			PDEBUGF(LOG_V2, LOG_PIT, "T2 -> %02d\n", value);
			break;
		case 0x43: /* Control Byte Register */
			PDEBUGF(LOG_V2, LOG_PIT, "Control Word Reg. -> 0\n");
			break;
		case 0x61: {
			/* AT, port 61h */
			uint16_t refresh_clock_div2 = ((cpu_time / 15085) & 1);
			value = (m_s.timer.read_OUT(2) << 5) |
			        (refresh_clock_div2    << 4) |
			        (m_s.speaker_data_on   << 1) |
			        (m_s.timer.read_GATE(2) ? 1 : 0);
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
		case 0x61: {
			PDEBUGF(LOG_V2, LOG_PIT, "write 0x61 SysCtrlB <- %02Xh ", value);
			bool t2_gate = value & 1;
			bool spkr_on = (value >> 1) & 0x01;
			if(t2_gate) { PDEBUGF(LOG_V2, LOG_PIT, "T2_GATE "); }
			if(spkr_on) { PDEBUGF(LOG_V2, LOG_PIT, "SPKR_ON "); }
			PDEBUGF(LOG_V2, LOG_PIT, "\n");
			m_s.timer.set_GATE(2, t2_gate);
			if(m_s.speaker_data_on != spkr_on) {
				if(m_pcspeaker) {
					if(spkr_on) {
						m_pcspeaker->add_event(m_s.pit_ticks, true, m_s.timer.read_OUT(2));
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

	/* _time is the current time, it can be not a multiple of the PIT_CLK_TIME
	 * but we can update the chip only on CLK ticks.
	 * Emulate the ticks and return the nsecs not emulated.
	 */
	assert(_pit_time >= m_s.pit_time);
	if(_pit_time == m_s.pit_time) {
		PDEBUGF(LOG_V2, LOG_PIT, "nothing to emulate!\n");
		return;
	}

	//calculate the amount of PIT CLK ticks to emulate
	uint64_t elapsed_nsec = _pit_time - m_s.pit_time;
	assert(elapsed_nsec % PIT_CLK_TIME == 0);
	uint64_t ticks_amount = elapsed_nsec / PIT_CLK_TIME;

	PDEBUGF(LOG_V2, LOG_PIT, "emulating: elapsed time: %d nsec, %d CLK pulses\n",
			elapsed_nsec, ticks_amount);

	uint64_t prev_pit_time = m_s.pit_time;
	while(ticks_amount > 0) {
		// how many CLK ticks till the next event?
		uint8_t timer;
		uint32_t next_event = m_s.timer.get_next_event_ticks(timer);
		uint32_t ticks = next_event;
		if((next_event == 0) || (next_event>ticks_amount)) {
			//if the next event is NEVER or after the last emulated CLK tick
			//consume all the emulated ticks amount
			ticks = ticks_amount;
		}
		m_crnt_emulated_ticks = ticks;
		m_s.timer.clock_all(ticks);
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
	/* call update_emulation() before this function */
	uint8_t timer;
	uint32_t next_event = m_s.timer.get_next_event_ticks(timer);

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
		PDEBUGF(LOG_V2, LOG_PIT, "next event: T%d, %u CLK, %llu nsecs (%.2f CLK)\n",
				timer, next_event, next_event_eta, (double(next_event_eta)/PIT_CLK_TIME));
	}
}

void PIT::irq0_handler(bool value, uint32_t)
{
	if(value == true) {
		PDEBUGF(LOG_V1, LOG_PIT, "raising IRQ %d\n", PIT_IRQ);
		m_devices->pic()->raise_irq(PIT_IRQ);
	} else {
		PDEBUGF(LOG_V2, LOG_PIT, "lowering IRQ %d\n", PIT_IRQ);
		m_devices->pic()->lower_irq(PIT_IRQ);
	}
}

void PIT::speaker_handler(bool value, uint32_t _remaining_ticks)
{
	if(!m_pcspeaker || !m_s.speaker_data_on) {
		return;
	}
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
	m_pcspeaker->add_event(ticks, true, value);
}


