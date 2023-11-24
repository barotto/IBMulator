/*
 * Copyright (C) 2002-2020  The DOSBox Team
 * Copyright (C) 2020-2023  Marco Bortolin
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
#include "machine.h"
#include "hardware/devices.h"
#include "pic.h"
#include "mpu401.h"
#include "program.h"
#include "mixer.h"

IODEVICE_PORTS(MPU401) = {};

static const IODevice::IOPorts mpu401_ports = {
	{ 0x0, 0x1, PORT_8BIT|PORT_RW }, // Data port (RW) / Status (R) / Command (W)
};

#define MPU401_VERSION      0x15
#define MPU401_REVISION     0x01
#define MPU401_TIMECONSTANT (60000000/1000.0)
#define MPU401_RESETBUSY    14_ms

// Messages sent to MPU-401 from host
#define MPU401_MSG_EOX          0xf7
#define MPU401_MSG_OVERFLOW     0xf8
#define MPU401_MSG_MARK         0xfc

// Messages sent to host from MPU-401
#define MPU401_MSG_OVERFLOW     0xf8
#define MPU401_MSG_COMMAND_REQ  0xf9
#define MPU401_MSG_END          0xfc
#define MPU401_MSG_CLOCK        0xfd
#define MPU401_MSG_ACK          0xfe

MPU401::MPU401(Devices *_dev)
: IODevice(_dev),
  m_iobase(0), m_irq(0), m_req_mode(MPU401::Mode::INTELLIGENT),
  m_eoi_timer(NULL_TIMER_ID), m_event_timer(NULL_TIMER_ID), m_reset_timer(NULL_TIMER_ID)
{
	memset(&m_s, 0, sizeof(m_s));
}

MPU401::~MPU401()
{
	
}

void MPU401::install()
{
	ms_ioports.clear();
	ms_ioports.insert(ms_ioports.end(), mpu401_ports.begin(), mpu401_ports.end());
	register_ports(0, g_program.config().get_int(MPU401_SECTION, MPU401_IOBASE));
	register_irq(g_program.config().get_int(MPU401_SECTION, MPU401_IRQ));

	using namespace std::placeholders;
	m_eoi_timer = g_machine.register_timer(
		std::bind(&MPU401::eoi_timer, this, _1),
		"MPU401 EoI"
	);
	m_event_timer = g_machine.register_timer(
		std::bind(&MPU401::event_timer, this, _1),
		"MPU401 Event"
	);
	m_reset_timer = g_machine.register_timer(
		std::bind(&MPU401::reset_timer, this, _1),
		"MPU401 Reset"
	);
	
	PINFOF(LOG_V0, LOG_AUDIO, "Installed %s\n", name());
}

void MPU401::remove()
{
	IODevice::remove();
	
	g_machine.unregister_irq(m_irq, name());
	
	g_machine.unregister_timer(m_eoi_timer);
	g_machine.unregister_timer(m_event_timer);
	g_machine.unregister_timer(m_reset_timer);
}

void MPU401::config_changed()
{
	unsigned new_base = g_program.config().get_int(MPU401_SECTION, MPU401_IOBASE);
	if(new_base != m_iobase) {
		IODevice::remove();
		register_ports(m_iobase, new_base);
	}

	unsigned new_irq = g_program.config().get_int(MPU401_SECTION, MPU401_IRQ);
	if(new_irq != m_irq) {
		g_machine.unregister_irq(m_irq, name());
		register_irq(new_irq);
	}

	static std::map<std::string, unsigned> modes = {
		{ "",            MPU401::Mode::INTELLIGENT },
		{ "intelligent", MPU401::Mode::INTELLIGENT },
		{ "uart",        MPU401::Mode::UART }
	};
	m_req_mode = static_cast<MPU401::Mode>(g_program.config().get_enum(MPU401_SECTION, MPU401_MODE, modes, true));
}

void MPU401::register_ports(unsigned _old_base, unsigned _new_base)
{
	rebase_ports(ms_ioports.begin()+1, ms_ioports.end(), _old_base, _new_base);
	IODevice::install();
	m_iobase = _new_base;
}

void MPU401::register_irq(unsigned _line)
{
	g_machine.register_irq(_line, name());
	m_irq = _line;
}

void MPU401::reset(unsigned _type)
{
	PDEBUGF(LOG_V1, LOG_AUDIO, "%s: reset\n", name());
	
	lower_interrupt();

	PDEBUGF(LOG_V1, LOG_AUDIO, "%s: %s mode\n", name(), m_req_mode==MPU401::Mode::INTELLIGENT?"intelligent":"UART");
	m_s.mode = m_req_mode;
	
	g_machine.deactivate_timer(m_eoi_timer);
	g_machine.deactivate_timer(m_event_timer);
	
	m_s.state.conductor = false;
	m_s.state.cond_req = false;
	m_s.state.cond_set = false;
	m_s.state.block_ack = false;
	m_s.state.playing = false;
	m_s.state.wsd = false;
	m_s.state.wsm = false;
	m_s.state.wsd_start = false;
	m_s.state.send_now = false;
	m_s.state.eoi_scheduled = false;
	m_s.state.data_onoff = -1;
	m_s.state.command_byte = 0;
	m_s.state.tmask = 0;
	m_s.state.cmask = 0xff;
	m_s.state.amask = 0;
	m_s.state.midi_mask = 0xffff;
	m_s.state.req_mask = 0;
	m_s.state.channel = 0;
	m_s.state.old_chan = 0;
	if(_type != DEVICE_SOFT_RESET) {
		m_s.state.cmd_pending = 0;
		m_s.state.reset = false;
	}
	
	m_s.clock.tempo = 100;
	m_s.clock.timebase = 120;
	m_s.clock.tempo_rel = 0x40;
	m_s.clock.tempo_grad = 0;
	m_s.clock.clock_to_host = false;
	m_s.clock.cth_rate = 60;
	m_s.clock.cth_counter = 0;
	m_s.clock.cth_savecount = 0;
	
	clear_queue();
	
	m_s.condbuf.counter = 0;
	m_s.condbuf.type = MPU401::DataType::T_OVERFLOW;
	
	for(int i=0; i<8; i++) {
		m_s.playbuf[i].type = MPU401::DataType::T_OVERFLOW;
		m_s.playbuf[i].counter = 0;
	}
}

void MPU401::power_off()
{
	
}

uint16_t MPU401::read(uint16_t _address, unsigned _io_len)
{
	UNUSED(_io_len);
	
	uint16_t address = _address - m_iobase;
	uint8_t value = ~0;
	
	switch(address) {
		case 0: // data
			value = MPU401_MSG_ACK;
			if(m_s.queue_used) {
				if(m_s.queue_pos >= MPU401_QUEUE_SIZE) {
					m_s.queue_pos -= MPU401_QUEUE_SIZE;
				}
				value = m_s.queue[m_s.queue_pos];
				m_s.queue_pos++;
				m_s.queue_used--;
			}
			
			if(m_s.mode != MPU401::Mode::INTELLIGENT) {
				break;
			}

			if(m_s.queue_used == 0) {
				lower_interrupt();
			}

			if(value >= 0xf0 && value <= 0xf7) {
				// MIDI data request
				m_s.state.channel = value & 7;
				m_s.state.data_onoff = 0;
				m_s.state.cond_req = false;
			}
			if(value == MPU401_MSG_COMMAND_REQ) {
				m_s.state.data_onoff = 0;
				m_s.state.cond_req = true;
				if(m_s.condbuf.type != MPU401::DataType::T_OVERFLOW) {
					m_s.state.block_ack = true;
					write_command(m_s.condbuf.value[0]);
					if(m_s.state.command_byte) {
						write_data(m_s.condbuf.value[1]);
					}
				}
				m_s.condbuf.type = MPU401::DataType::T_OVERFLOW;
			}
			if(value == MPU401_MSG_END || value == MPU401_MSG_CLOCK || value == MPU401_MSG_ACK) {
				m_s.state.data_onoff = -1;
				start_eoi_timer();
			}
			break;
		case 1: // status
			value = 0x3f; // Bits 6 and 7 clear
			if(m_s.state.cmd_pending) {
				value |= 0x40;
			}
			if(!m_s.queue_used) {
				value |= 0x80;
			}
			break;
		default:
			PDEBUGF(LOG_V0, LOG_AUDIO, "%s: unhandled read from port 0x%04X!\n", name(), _address);
			return ~0;
	}
	
	PDEBUGF(LOG_V2, LOG_AUDIO, "%s: read  0x%x -> 0x%02X\n", name(), _address, value);
	
	return value;
}

void MPU401::write(uint16_t _address, uint16_t _value, unsigned _io_len)
{
	UNUSED(_io_len);
	
	PDEBUGF(LOG_V2, LOG_AUDIO, "%s: write 0x%x <- 0x%02X\n", name(), _address, _value);
	
	uint16_t address = _address - m_iobase;
	switch(address) {
		case 0:
			write_data(_value);
			break;
		case 1:
			write_command(_value);
			break;
		default:
			break;
	}
}

void MPU401::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_AUDIO, "%s: saving state\n", name());
	_state.write(&m_s, {sizeof(m_s), name()});
}

void MPU401::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_AUDIO, "%s: restoring state\n", name());
	_state.read(&m_s, {sizeof(m_s), name()});
}

void MPU401::raise_interrupt()
{
	if(!m_s.state.irq_pending) {
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s: raising IRQ %d\n", name(), m_irq);
		m_devices->pic()->raise_irq(m_irq);
		m_s.state.irq_pending = true;
	}
}

void MPU401::lower_interrupt()
{
	if(m_s.state.irq_pending) {
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s: lowering IRQ %d\n", name(), m_irq);
		m_devices->pic()->lower_irq(m_irq);
		m_s.state.irq_pending = false;
	}
}

void MPU401::clear_queue()
{
	m_s.queue_used = 0;
	m_s.queue_pos = 0;
}

void MPU401::write_command(unsigned _val)
{
	if(m_s.mode == MPU401::Mode::UART && _val != 0xff) {
		return;
	}
	
	if(m_s.state.reset) {
		// original DOSBox-X comment:
		// THIS CODE IN DISPUTE [https://github.com/joncampbell123/dosbox-x/issues/917#issuecomment-538717798]
		if(m_s.state.cmd_pending || _val != 0xff) {
			m_s.state.cmd_pending = _val + 1;
			return;
		}
		g_machine.deactivate_timer(m_reset_timer);
		m_s.state.reset = false;
	}
	
	bool send_ack = true;
	if(_val <= 0x2f) {
		switch(_val & 3) {
			// MIDI stop, start, continue
			case 1: {
				g_mixer.midi()->cmd_put_byte(0xfc, g_machine.get_virt_time_ns());
				m_s.clock.cth_savecount = m_s.clock.cth_counter;
				break;
			}
			case 2: {
				g_mixer.midi()->cmd_put_byte(0xfa, g_machine.get_virt_time_ns());
				m_s.clock.cth_counter = m_s.clock.cth_savecount = 0;
				break;
			}
			case 3: {
				g_mixer.midi()->cmd_put_byte(0xfb, g_machine.get_virt_time_ns());
				m_s.clock.cth_counter = m_s.clock.cth_savecount;
				break;
			}
			default:
				break;
		}
		if(_val & 0x20) {
			PDEBUGF(LOG_V0, LOG_AUDIO, "%s: unhandled recording command 0x%x\n", name(), _val);
		}
		switch(_val & 0xc) {
			case 0x4: // Stop
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s: cmd: stop\n", name());
				if(m_s.state.playing && !m_s.clock.clock_to_host) {
					g_machine.deactivate_timer(m_event_timer);
				}
				m_s.state.playing = false;
				for(uint8_t i=0xb0; i<0xbf; i++) { // All notes off
					g_mixer.midi()->cmd_put_bytes({i, 0x7b, 0x00}, g_machine.get_virt_time_ns());
				}
				break;
			case 0x8: // Play
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s: cmd: play\n", name());
				if(!m_s.state.playing && !m_s.clock.clock_to_host) {
					start_event_timer();
				}
				m_s.state.playing = true;
				clear_queue();
				break;
			default:
				break;
		}
	}
	else if(_val >= 0xa0 && _val <= 0xa7) {
		// Request play counter
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s: cmd: request play counter\n", name());
		if(m_s.state.cmask & (1 << (_val & 7))) {
			queue_byte((uint8_t)m_s.playbuf[_val & 7].counter);
		}
	}
	else if(_val >= 0xd0 && _val <= 0xd7) {
		// Send data
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s: cmd: send data\n", name());
		m_s.state.old_chan = m_s.state.channel;
		m_s.state.channel = _val & 7;
		m_s.state.wsd = true;
		m_s.state.wsm = false;
		m_s.state.wsd_start = true;
	}
	else {
		switch(_val) {
			case 0xdf: // Send system message
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s: cmd: send system message\n", name());
				m_s.state.wsd = false;
				m_s.state.wsm = true;
				m_s.state.wsd_start = true;
				break;
			case 0x8e: // Conductor
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s: cmd: conductor\n", name());
				m_s.state.cond_set = false;
				break;
			case 0x8f:
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s: cmd: conductor\n", name());
				m_s.state.cond_set = true;
				break;
			case 0x94: // Clock to host
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s: cmd: clock to host\n", name());
				if(m_s.clock.clock_to_host && !m_s.state.playing) {
					stop_event_timer();
				}
				m_s.clock.clock_to_host = false;
				break;
			case 0x95:
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s: cmd: clock to host\n", name());
				if(!m_s.clock.clock_to_host && !m_s.state.playing) {
					start_event_timer();
				}
				m_s.clock.clock_to_host = true;
				break;
			case 0xc2: case 0xc3: case 0xc4: case 0xc5: case 0xc6: case 0xc7: case 0xc8:
				// Internal timebase
				m_s.clock.timebase = 24 * (_val & 0xf);
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s: cmd: timebase = %d\n", name(), m_s.clock.timebase);
				break;
			// Commands with data byte
			case 0xe0: case 0xe1: case 0xe2: case 0xe4: case 0xe6: 
			case 0xe7: case 0xec: case 0xed: case 0xee: case 0xef:
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s: cmd byte\n", name());
				m_s.state.command_byte = _val;
				break;
			case 0xab: // Request and clear recording counter
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s: cmd: request and clear recording counter\n", name());
				queue_byte(MPU401_MSG_ACK);
				queue_byte(0);
				return;
			case 0xac: // Request version
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s: cmd: request version\n", name());
				queue_byte(MPU401_MSG_ACK);
				queue_byte(MPU401_VERSION);
				return;
			case 0xad: // Request revision
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s: cmd: request revision\n", name());
				queue_byte(MPU401_MSG_ACK);
				queue_byte(MPU401_REVISION);
				return;
			case 0xaf: // Request tempo
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s: cmd: request tempo\n", name());
				queue_byte(MPU401_MSG_ACK);
				queue_byte(m_s.clock.tempo);
				return;
			case 0xb1: // Reset relative tempo
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s: cmd: reset tempo\n", name());
				m_s.clock.tempo_rel = 0x40;
				break;
			case 0xb8: // Clear play counters
			case 0xb9: // Clear play map
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s: cmd: clear play\n", name());
				for(uint8_t i=0xb0; i<0xbf; i++) {
					// All notes off
					g_mixer.midi()->cmd_put_bytes({i, 0x7b, 0x00}, g_machine.get_virt_time_ns());
				}
				for(int i=0; i<8; i++) {
					m_s.playbuf[i].counter = 0;
					m_s.playbuf[i].type = MPU401::DataType::T_OVERFLOW;
				}
				m_s.condbuf.counter = 0;
				m_s.condbuf.type = MPU401::DataType::T_OVERFLOW;
				if(!(m_s.state.conductor = m_s.state.cond_set)) {
					m_s.state.cond_req = 0;
				}
				m_s.state.amask = m_s.state.tmask;
				m_s.state.req_mask = 0;
				m_s.state.irq_pending = true;
				break;
			case 0xff: // Reset MPU-401
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s: cmd: reset\n", name());
				g_machine.activate_timer(m_reset_timer, MPU401_RESETBUSY, false);
				m_s.state.reset = true;
				if(m_s.mode == MPU401::Mode::UART) {
					send_ack = false;
				}
				reset(DEVICE_SOFT_RESET);
				break;
			case 0x3f: // UART mode
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s: cmd: set UART mode\n", name());
				m_s.mode = MPU401::Mode::UART;
				break;
			default:
				PDEBUGF(LOG_V1, LOG_AUDIO, "%s: cmd: unhandled command %X\n", name(), _val);
				break;
		}
	}
	
	if(send_ack) {
		queue_byte(MPU401_MSG_ACK);
	}
}

void MPU401::write_data(unsigned _val)
{
	if(m_s.mode == MPU401::Mode::UART) {
		g_mixer.midi()->cmd_put_byte(_val, g_machine.get_virt_time_ns());
		return;
	}
	
	switch(m_s.state.command_byte) { // 0xe# command data
		case 0x00:
			break;
		case 0xe0: // Set tempo
			m_s.state.command_byte = 0;
			if(_val > 250) {
				_val = 250; //range clamp of true MPU-401
			} else if(_val < 4) {
				_val = 4;
			}
			m_s.clock.tempo = (uint8_t)_val;
			return;
		case 0xe1: // Set relative tempo
			m_s.state.command_byte = 0;
			m_s.clock.tempo_rel = (uint8_t)_val;
			if(_val != 0x40) {
				PDEBUGF(LOG_V1, LOG_AUDIO, "%s: relative tempo change value 0x%x (%.3f)\n",
						name(), _val, (double)_val / 0x40);
			}
			return;
		case 0xe7: // Set internal clock to host interval
			m_s.state.command_byte = 0;
			m_s.clock.cth_rate = (uint8_t)(_val >> 2);
			return;
		case 0xec: // Set active track mask
			m_s.state.command_byte = 0;
			m_s.state.tmask = (uint8_t)_val;
			return;
		case 0xed: // Set play counter mask
			m_s.state.command_byte = 0;
			m_s.state.cmask = (uint8_t)_val;
			return;
		case 0xee: // Set 1-8 MIDI channel mask
			m_s.state.command_byte = 0;
			m_s.state.midi_mask &= 0xff00;
			m_s.state.midi_mask |= _val;
			return;
		case 0xef: // Set 9-16 MIDI channel mask
			m_s.state.command_byte = 0;
			m_s.state.midi_mask &= 0x00ff;
			m_s.state.midi_mask |= ((uint16_t)_val) << 8;
			return;
		//case 0xe2: Set graduation for relative tempo
		//case 0xe4: Set metronome
		//case 0xe6: Set metronome measure length
		default:
			m_s.state.command_byte = 0;
			return;
	}

	// FIXME: can't use static variables in functions!!!
	static unsigned length, cnt, posd;
	
	if(m_s.state.wsd) { // Directly send MIDI message
		if(m_s.state.wsd_start) {
			m_s.state.wsd_start = 0;
			cnt = 0;
			switch(_val & 0xf0) {
				case 0xc0: case 0xd0:
					m_s.playbuf[m_s.state.channel].value[0] = (uint8_t)_val;
					length = 2;
					break;
				case 0x80: case 0x90: case 0xa0: case 0xb0: case 0xe0:
					m_s.playbuf[m_s.state.channel].value[0] = (uint8_t)_val;
					length = 3;
					break;
				case 0xf0:
					PDEBUGF(LOG_V0, LOG_AUDIO, "%s: illegal WSD byte\n", name());
					m_s.state.wsd = 0;
					m_s.state.channel = m_s.state.old_chan;
					return;
				default: // MIDI with running status
					cnt++;
					g_mixer.midi()->cmd_put_byte(m_s.playbuf[m_s.state.channel].value[0], g_machine.get_virt_time_ns());
					// TODO: is this break or return?
					// dosbox doesn's have neither...
					break;
			}
		}
		if(cnt < length) {
			cnt++;
			g_mixer.midi()->cmd_put_byte((uint8_t)_val, g_machine.get_virt_time_ns());
		}
		if(cnt == length) {
			m_s.state.wsd = 0;
			m_s.state.channel = m_s.state.old_chan;
		}
		return;
	}
	
	if(m_s.state.wsm) { // Directly send system message
		if(_val == MPU401_MSG_EOX) {
			g_mixer.midi()->cmd_put_byte(MPU401_MSG_EOX, g_machine.get_virt_time_ns());
			m_s.state.wsm = 0;
			return;
		}
		if(m_s.state.wsd_start) {
			m_s.state.wsd_start = 0;
			cnt = 0;
			switch(_val) {
				case 0xf2: { length = 3; break; }
				case 0xf3: { length = 2; break; }
				case 0xf6: { length = 1; break; }
				case 0xf0: { length = 0; break; }
				default:   { length = 0; break; }
			}
		}
		if(length == 0 || cnt < length) {
			g_mixer.midi()->cmd_put_byte((uint8_t)_val, g_machine.get_virt_time_ns());
			cnt++;
		}
		if(cnt == length) {
			m_s.state.wsm = 0;
		}
		return;
	}
	
	if(m_s.state.cond_req) { // Command
		switch(m_s.state.data_onoff) {
			case -1:
				return;
			case 0: // Timing byte
				m_s.condbuf.vlength=0;
				if(_val < 0xf0) {
					m_s.state.data_onoff++;
				} else {
					m_s.state.data_onoff = -1;
					start_eoi_timer();
					return;
				}
				if(_val == 0) {
					m_s.state.send_now = true;
				} else {
					m_s.state.send_now = false;
				}
				m_s.condbuf.counter = (int)_val;
				break;
			case 1: // Command byte #1
				m_s.condbuf.type = MPU401::DataType::T_COMMAND;
				if(_val == 0xf8 || _val == 0xf9) {
					m_s.condbuf.type = MPU401::DataType::T_OVERFLOW;
				}
				m_s.condbuf.value[m_s.condbuf.vlength] = (uint8_t)_val;
				m_s.condbuf.vlength++;
				if((_val & 0xf0) != 0xe0) {
					start_eoi_timer();
				} else {
					m_s.state.data_onoff++;
				}
				break;
			case 2: // Command byte #2
				m_s.condbuf.value[m_s.condbuf.vlength] = (uint8_t)_val;
				m_s.condbuf.vlength++;
				start_eoi_timer();
				break;
			default:
				break;
		}
		return;
	}
	
	switch(m_s.state.data_onoff) { // Data
		case -1:
			return;
		case 0: // Timing byte
			if(_val < 0xf0) {
				m_s.state.data_onoff = 1;
			} else {
				m_s.state.data_onoff = -1;
				start_eoi_timer();
				return;
			}
			if(_val == 0) {
				m_s.state.send_now = true;
			} else {
				m_s.state.send_now = false;
			}
			m_s.playbuf[m_s.state.channel].counter = (int)_val;
			break;
		case 1: // MIDI
			m_s.playbuf[m_s.state.channel].vlength++;
			posd = m_s.playbuf[m_s.state.channel].vlength;
			if(posd == 1) {
				switch(_val & 0xf0) {
					case 0xf0: // System message or mark
						if(_val > 0xf7) {
							m_s.playbuf[m_s.state.channel].type = MPU401::DataType::T_MARK;
							m_s.playbuf[m_s.state.channel].sys_val = (uint8_t)_val;
							length = 1;
						} else {
							PDEBUGF(LOG_V0, LOG_AUDIO, "%s: illegal message\n", name());
							m_s.playbuf[m_s.state.channel].type = MPU401::DataType::T_MIDI_SYS;
							m_s.playbuf[m_s.state.channel].sys_val = (uint8_t)_val;
							length = 1;
						}
						break;
					case 0xc0: case 0xd0: // MIDI Message
						m_s.playbuf[m_s.state.channel].type = MPU401::DataType::T_MIDI_NORM;
						length = m_s.playbuf[m_s.state.channel].length = 2;
						break;
					case 0x80: case 0x90: case 0xa0:  case 0xb0: case 0xe0: 
						m_s.playbuf[m_s.state.channel].type = MPU401::DataType::T_MIDI_NORM;
						length = m_s.playbuf[m_s.state.channel].length = 3;
						break;
					default: // MIDI data with running status
						posd++;
						m_s.playbuf[m_s.state.channel].vlength++;
						m_s.playbuf[m_s.state.channel].type = MPU401::DataType::T_MIDI_NORM;
						length = m_s.playbuf[m_s.state.channel].length;
						break;
				}
			}
			if(!(posd == 1 && _val >= 0xf0)) {
				m_s.playbuf[m_s.state.channel].value[posd - 1] = (uint8_t)_val;
			}
			if(posd == length) {
				start_eoi_timer();
			}
			break;
		default:
			break;
	}
}

void MPU401::start_event_timer()
{
	double time_ms = MPU401_TIMECONSTANT / ((m_s.clock.tempo * m_s.clock.timebase * m_s.clock.tempo_rel) / 0x40);
	g_machine.activate_timer(m_event_timer, time_ms * 1000000.0, false);
}

void MPU401::stop_event_timer()
{
	g_machine.deactivate_timer(m_event_timer);
}

void MPU401::event_timer(uint64_t)
{
	if(m_s.mode == MPU401::Mode::UART) {
		return;
	}
	
	if(m_s.state.irq_pending) {
		start_event_timer();
		return;
	}
	
	if(m_s.state.playing) {
		for(uint8_t i=0; i<8; i++) { // Decrease counters
			if(m_s.state.amask & (1<<i)) {
				m_s.playbuf[i].counter--;
				if(m_s.playbuf[i].counter <= 0) {
					update_track(i);
				}
			}
		}
		if(m_s.state.conductor) {
			m_s.condbuf.counter--;
			if(m_s.condbuf.counter <= 0) {
				update_conductor();
			}
		}
	}
	
	if(m_s.clock.clock_to_host) {
		m_s.clock.cth_counter++;
		if(m_s.clock.cth_counter >= m_s.clock.cth_rate) {
			m_s.clock.cth_counter = 0;
			m_s.state.req_mask |= (1 << 13);
		}
	}
	
	if(!m_s.state.irq_pending && m_s.state.req_mask) {
		eoi_timer(0);
	}

	start_event_timer();
}

void MPU401::start_eoi_timer()
{
	if(m_s.state.send_now) {
		m_s.state.eoi_scheduled = true;
		// Possible a bit longer
		g_machine.activate_timer(m_eoi_timer, 60_us, false);
	} else if(!m_s.state.eoi_scheduled) {
		eoi_timer(0);
	}
}

void MPU401::eoi_timer(uint64_t)
{
	// Updates counters and requests new data on "End of Input"
	
	m_s.state.eoi_scheduled = false;
	if(m_s.state.send_now) {
		m_s.state.send_now = false;
		if(m_s.state.cond_req) {
			update_conductor();
		} else {
			update_track(m_s.state.channel);
		}
	}

	lower_interrupt();

	if(!m_s.state.req_mask) {
		return;
	}
	
	uint8_t i=0;
	do {
		if(m_s.state.req_mask & (1 << i)) {
			queue_byte(0xf0 + i);
			m_s.state.req_mask &= ~(1 << i);
			break;
		}
	} while((i++) < 16);
}

void MPU401::reset_timer(uint64_t)
{
	m_s.state.reset = false;
	if(m_s.state.cmd_pending) {
		write_command(m_s.state.cmd_pending - 1);
		m_s.state.cmd_pending = 0;
	}
}

void MPU401::update_track(uint8_t chan)
{
	intelligent_out(chan);
	
	if(m_s.state.amask & (1<<chan)) {
		m_s.playbuf[chan].vlength = 0;
		m_s.playbuf[chan].type = MPU401::DataType::T_OVERFLOW;
		m_s.playbuf[chan].counter = 0xf0;
		m_s.state.req_mask |= (1 << chan);
	} else {
		if(m_s.state.amask==0 && !m_s.state.conductor) {
			m_s.state.req_mask |= (1 << 12);
		}
	}
}

void MPU401::update_conductor()
{
	for(unsigned i=0; i < m_s.condbuf.vlength; i++) {
		if(m_s.condbuf.value[i] == 0xfc) {
			m_s.condbuf.value[i] = 0;
			m_s.state.conductor = false;
			m_s.state.req_mask &= ~(1 << 9);
			if(m_s.state.amask==0) {
				m_s.state.req_mask |= (1 << 12);
			}
			return;
		}
	}

	m_s.condbuf.vlength = 0;
	m_s.condbuf.counter = 0xf0;
	m_s.state.req_mask |= (1 << 9);
}

void MPU401::queue_byte(uint8_t _data)
{
	if(m_s.state.block_ack) {
		m_s.state.block_ack = false;
		return;
	}
	if(m_s.queue_used == 0 && m_s.mode == MPU401::Mode::INTELLIGENT) {
		raise_interrupt();
	}
	if(m_s.queue_used < MPU401_QUEUE_SIZE) {
		unsigned pos = m_s.queue_used + m_s.queue_pos;
		if(m_s.queue_pos >= MPU401_QUEUE_SIZE) {
			m_s.queue_pos -= MPU401_QUEUE_SIZE;
		}
		if(pos >= MPU401_QUEUE_SIZE) {
			pos -= MPU401_QUEUE_SIZE;
		}
		m_s.queue_used++;
		m_s.queue[pos] = _data;
	} else {
		PDEBUGF(LOG_V0, LOG_AUDIO, "%s: data queue full\n", name());
	}
}

void MPU401::intelligent_out(uint8_t _chan)
{
	unsigned val;
	switch(m_s.playbuf[_chan].type) {
		case MPU401::DataType::T_OVERFLOW:
			break;
		case MPU401::DataType::T_MARK:
			val = m_s.playbuf[_chan].sys_val;
			if(val == 0xfc) {
				g_mixer.midi()->cmd_put_byte(val, g_machine.get_virt_time_ns());
				m_s.state.amask &= ~(1 << _chan);
				m_s.state.req_mask &= ~(1 << _chan);
			}
			break;
		case MPU401::DataType::T_MIDI_NORM:
			for(unsigned i=0; i<m_s.playbuf[_chan].vlength; i++) {
				g_mixer.midi()->cmd_put_byte(m_s.playbuf[_chan].value[i], g_machine.get_virt_time_ns());
			}
			break;
		default:
			break;
	}
}