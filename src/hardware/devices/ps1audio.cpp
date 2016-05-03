/*
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
/*
 * IBM PS/1 Audio/Joystick Card
 *
 * The PSG emulation is based on the Texas Instruments SN76496, although the
 * real hardware is of unknown type. Unsurprisingly the generated sound is
 * very similar to the original, as also the IBM PCjr's PSG was based on the
 * TI SN76496.
 *
 * More info at http://www.vgmpf.com/Wiki/index.php?title=PS-1_Audio
 */

#include "ibmulator.h"
#include "machine.h"
#include "program.h"
#include "hardware/devices.h"
#include "hardware/devices/ps1audio.h"
#include "hardware/devices/pic.h"
#include "filesys.h"
#include <cstring>

#define PS1AUDIO_INPUT_CLOCK 4000000
#define PS1AUDIO_DAC_FADE_IN false
#define PS1AUDIO_DAC_EMPTY_THRESHOLD 1000 // number of empty DAC samples after
                                          // which the FIFO timer will be auto-deactivated

IODEVICE_PORTS(PS1Audio) = {
	{ 0x200, 0x200, PORT_8BIT|PORT_RW },  // ADC (R) / DAC (W)
	//0x201 is used by the Game Port device
	{ 0x202, 0x202, PORT_8BIT|PORT_RW }, // Control Register
	{ 0x203, 0x203, PORT_8BIT|PORT_RW }, // FIFO Timer reload value
	{ 0x204, 0x204, PORT_8BIT|PORT_RW }, // Joystick (X Axis Stick A) P0 (R) / Almost empty value (W)
	{ 0x205, 0x205, PORT_8BIT|PORT_RW }, // Joystick (Y Axis Stick A) P1 (R) / Sound Generator (W)
	{ 0x206, 0x206, PORT_8BIT|PORT_R_ }, // Joystick (X Axis Stick B) P2
	{ 0x207, 0x207, PORT_8BIT|PORT_R_ }, // Joystick (Y Axis Stick B) P3
	{ 0x330, 0x330, PORT_8BIT|PORT_RW }, // MIDI TXD Register
	{ 0x331, 0x331, PORT_8BIT|PORT_RW }, // MIDI IER Register
	{ 0x332, 0x332, PORT_8BIT|PORT_RW }, // MIDI IIR Register
	{ 0x335, 0x335, PORT_8BIT|PORT_RW }  // MIDI LSR Register
};
#define PS1AUDIO_IRQ 7

PS1Audio::PS1Audio(Devices *_dev)
: IODevice(_dev)
{
	m_DAC_samples.reserve(PS1AUDIO_FIFO_SIZE*2);
}

PS1Audio::~PS1Audio()
{
}

void PS1Audio::install()
{
	IODevice::install();
	g_machine.register_irq(PS1AUDIO_IRQ, name());

	//the DAC emulation can surely be done without a machine timer, but I find
	//this approach way easier to read and follow.
	using namespace std::placeholders;
	m_DAC_timer = g_machine.register_timer(
		std::bind(&PS1Audio::FIFO_timer, this, _1),
		"PS/1 Audio DAC" // name
	);

	using namespace std::placeholders;
	m_DAC_channel = g_mixer.register_channel(
		std::bind(&PS1Audio::create_DAC_samples, this, _1, _2, _3),
		"PS/1 Audio DAC");
	m_DAC_channel->set_disable_timeout(1000000);
	m_s.DAC.fifo_timer = m_DAC_timer;

	m_PSG.install(PS1AUDIO_INPUT_CLOCK);

	Synth::set_chip(0, &m_PSG);
	Synth::install("PS/1 Audio", 2500,
		[this](Event &_event) {
			m_PSG.write(_event.value);
			if(Synth::is_capturing()) {
				Synth::capture_command(0x50, _event);
			}
		},
		[this](AudioBuffer &_buffer, int _frames) {
			m_PSG.generate(&_buffer.operator[]<int16_t>(0), _frames, 1);
		},
		[this](bool _start, VGMFile& _vgm) {
			if(_start) {
				_vgm.set_chip(VGMFile::SN76489);
				_vgm.set_clock(PS1AUDIO_INPUT_CLOCK);
				_vgm.set_SN76489_feedback(6);
				_vgm.set_SN76489_shift_width(16);
			}
		}
	);
}

void PS1Audio::remove()
{
	IODevice::remove();
	Synth::remove();
	g_machine.unregister_irq(PS1AUDIO_IRQ);
	g_machine.unregister_timer(m_DAC_timer);
	g_mixer.unregister_channel(m_DAC_channel);
}

void PS1Audio::reset(unsigned _type)
{
	Synth::reset();

	m_s.control_reg = 0;
	lower_interrupt();

	m_DAC_channel->enable(false);
	std::lock_guard<std::mutex> lock(m_DAC_lock);
	m_s.DAC.reset(_type);
	m_DAC_samples.clear();
	m_DAC_last_value = 128;
}

void PS1Audio::power_off()
{
	Synth::power_off();
	m_DAC_channel->enable(false);
}

void PS1Audio::config_changed()
{
	unsigned rate = clamp(g_program.config().get_int(PS1AUDIO_SECTION, PS1AUDIO_RATE),
			MIXER_MIN_RATE, MIXER_MAX_RATE);
	float volume = clamp(g_program.config().get_real(PS1AUDIO_SECTION, PS1AUDIO_VOLUME),
			0.0, 10.0);
	Synth::config_changed({AUDIO_FORMAT_S16, 1, rate}, volume);
	m_DAC_channel->set_volume(volume);
}

void PS1Audio::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_AUDIO, "PS/1: saving state\n");
	_state.write(&m_s, {sizeof(m_s), name()});
	Synth::save_state(_state);
}

void PS1Audio::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_AUDIO, "PS/1: restoring state\n");
	_state.read(&m_s, {sizeof(m_s), name()});
	Synth::restore_state(_state);

	m_DAC_channel->enable(false);
	m_s.DAC.fifo_timer = m_DAC_timer;
	m_DAC_samples.clear();
	m_DAC_last_value = 128;
	m_DAC_empty_samples = 0;
	if(m_s.DAC.reload_reg != 0) {
		m_DAC_freq = 1000000 / (int(m_s.DAC.reload_reg)+1);
	} else {
		m_DAC_freq = 0;
	}
	m_s.DAC.set_reload_register(m_s.DAC.reload_reg);
}

void PS1Audio::raise_interrupt()
{
	if(m_s.control_reg & 1) {
		PDEBUGF(LOG_V2, LOG_AUDIO, "PS/1: raising IRQ %d\n", PS1AUDIO_IRQ);
		m_devices->pic()->raise_irq(PS1AUDIO_IRQ);
	}
}

void PS1Audio::lower_interrupt()
{
	m_devices->pic()->lower_irq(PS1AUDIO_IRQ);
}

uint16_t PS1Audio::read(uint16_t _address, unsigned)
{
	uint16_t value = ~0;

	switch(_address) {
		case 0x200:
			//Analog to Digital Converter Data
			PDEBUGF(LOG_V1, LOG_AUDIO, "PS/1 ADC: read from port 200h\n");
			//TODO
			break;
		case 0x202:
			//Control Register
			value = 0;
			value |= m_s.control_reg & 1; //AIE-0 Ext Int Enable
			value |= m_s.DAC.almost_empty << 1; //IR-1 Almost Empty Int
			value |= (m_s.DAC.read_avail  == 0) << 2; //FE-2 FIFO Empty
			value |= (m_s.DAC.write_avail == 0) << 3; //FF-3 FIFO Full
			//ADR-4 ADC Data Rdy TODO
			//JIE-5 Joystick Int TODO
			//JM-6 RIN0 Bit ???
			//RIO-7 RIN1 Bit ???
			if(value & 2) {
				PDEBUGF(LOG_V2, LOG_AUDIO, "PS/1: AE Int (FIFO:%db, limit:%db)\n",
						m_s.DAC.read_avail, m_s.DAC.almost_empty_value);
			}
			break;
		case 0x203:
			//FIFO Timer reload value
			value = m_s.DAC.reload_reg;
			break;
		case 0x204:
			//Joystick (X Axis Stick A) P0
			//TODO
			PDEBUGF(LOG_V1, LOG_AUDIO, "PS/1 JOY: read from port 204h\n");
			break;
		case 0x205:
			//Joystick (Y Axis Stick A) P1
			//TODO
			PDEBUGF(LOG_V1, LOG_AUDIO, "PS/1 JOY: read from port 205h\n");
			break;
		case 0x206:
			//Joystick (X Axis Stick B) P2
			//TODO
			PDEBUGF(LOG_V1, LOG_AUDIO, "PS/1 JOY: read from port 206h\n");
			break;
		case 0x207:
			//Joystick (Y Axis Stick B) P3
			//TODO
			PDEBUGF(LOG_V1, LOG_AUDIO, "PS/1 JOY: read from port 207h\n");
			break;
		case 0x330:
			//MIDI TXD Register
			//TODO
			PDEBUGF(LOG_V1, LOG_AUDIO, "PS/1 MIDI: read from port 330h\n");
			break;
		case 0x331:
			//MIDI IER Register
			//TODO
			PDEBUGF(LOG_V1, LOG_AUDIO, "PS/1 MIDI: read from port 331h\n");
			break;
		case 0x332:
			//MIDI IIR Register
			//TODO
			PDEBUGF(LOG_V1, LOG_AUDIO, "PS/1 MIDI: read from port 332h\n");
			break;
		case 0x335:
			//MIDI LSR Register
			//TODO
			PDEBUGF(LOG_V1, LOG_AUDIO, "PS/1 MIDI: read from port 335h\n");
			break;
		default:
			PERRF(LOG_AUDIO, "PS/1: unhandled read from port %0x04X!\n", _address);
			return ~0;
	}

	return value;
}

void PS1Audio::write(uint16_t _address, uint16_t _value, unsigned)
{
	uint8_t value = _value & 0xFF;
	switch(_address) {
		case 0x200:
			//Digital to Analog Converter
			m_s.DAC.write(value);
			//if the DAC is fetching from the FIFO but the timer is stopped
			//(for eg. because the fifo was empty long enough) then restart it
			if(m_s.DAC.reload_reg>0 && !g_machine.is_timer_active(m_DAC_timer)) {
				m_s.DAC.set_reload_register(m_s.DAC.reload_reg);
			}
			break;
		case 0x202:
			//Control Register
			m_s.control_reg = value;
			if(!(_value & 2)) {
				//The interrupt flag is cleared by writing a 0 then a 1 to this bit.
				m_s.DAC.almost_empty = false;
				lower_interrupt();
				PDEBUGF(LOG_V2, LOG_AUDIO, "PS/1: AE Int disabled\n");
			} else {
				PDEBUGF(LOG_V2, LOG_AUDIO, "PS/1: AE Int enabled\n");
			}
			//TODO
			if(_value & 0x20) {
				PDEBUGF(LOG_V2, LOG_AUDIO, "PS/1: Joystick Int enabled\n");
			} else {
				PDEBUGF(LOG_V2, LOG_AUDIO, "PS/1: Joystick Int disabled\n");
			}
			//TODO
			if(_value & 0x40) {
				PDEBUGF(LOG_V2, LOG_AUDIO, "PS/1: Joystick Auto mode\n");
			} else {
				PDEBUGF(LOG_V2, LOG_AUDIO, "PS/1: Joystick Manual mode\n");
			}
			break;
		case 0x203:
			//FIFO Timer reload value
			if(value>0 && value<22) {
				//TODO FIXME F14 sets a value of 1 (2us), which is unmanageable
				//what's the real hardware behaviour?
				PDEBUGF(LOG_V0, LOG_AUDIO, "PS/1 DAC: reload value out of range: %d\n", value);
				return;
			}
			if(value != 0) {
				//a change in frequency or a DAC start.
				m_DAC_freq = 1000000 / (unsigned(value)+1);
			}
			m_s.DAC.set_reload_register(value);
			break;
		case 0x204:
			//Almost empty value
			m_s.DAC.almost_empty_value = uint16_t(value) * 4;
			break;
		case 0x205: {
			//Sound Generator
			bool push = true;
			if(value & 0x80) {
				//LATCH/DATA byte
				//int reg = (value & 0x60) >> 5;
				if(value & 0x10) {
					// attenuation
					// push 0x0F (silence) only if the channel is active
					push = ((value & 0xF) != 0xF);
					if(push) {
						Synth::enable_channel();
					}
				} else { /* frequency bit0-3 */ }
			} else {
				//DATA byte, frequency bit4-9
				Synth::enable_channel();
			}
			if(push || Synth::is_channel_enabled()) {
				Synth::add_event({g_machine.get_virt_time_ns(), 0, value});
			}
			break;
		}
		case 0x330:
			//MIDI TXD Register
			//TODO
			PDEBUGF(LOG_V1, LOG_AUDIO, "PS/1 MIDI: write to port 330h <- 0x%02X\n", value);
			break;
		case 0x331:
			//MIDI IER Register
			//TODO
			PDEBUGF(LOG_V1, LOG_AUDIO, "PS/1 MIDI: write to port 331h <- 0x%02X\n", value);
			break;
		case 0x332:
			//MIDI IIR Register
			//TODO
			PDEBUGF(LOG_V1, LOG_AUDIO, "PS/1 MIDI: write to port 332h <- 0x%02X\n", value);
			break;
		case 0x335:
			//MIDI LSR Register
			//TODO
			PDEBUGF(LOG_V1, LOG_AUDIO, "PS/1 MIDI: write to port 335h <- 0x%02X\n", value);
			break;
		default:
			PERRF(LOG_AUDIO, "PS/1: unhandled write to port 0x%04X!\n", _address);
			return;
	}
}

void PS1Audio::FIFO_timer(uint64_t)
{
	//A	pulse is generated on overflow and is used to latch data into the ADC
	//latch and to read data out of the FIFO.
	m_DAC_channel->enable(true);
	uint8_t value = m_DAC_last_value;
	if(m_s.DAC.read_avail > 0) {
		value = m_s.DAC.read();
		m_DAC_empty_samples = 0;
	} else {
		m_DAC_empty_samples++;
	}
	if(	//m_s.DAC.almost_empty_value &&
		m_s.DAC.read_avail==m_s.DAC.almost_empty_value &&
		(m_s.control_reg & 2) )
	{
		m_s.DAC.almost_empty = true;
		raise_interrupt();
	}
	if(m_DAC_empty_samples > PS1AUDIO_DAC_EMPTY_THRESHOLD) {
		/* lots of software don't disable the FIFO timer so the channel remains
		 * open. If the DAC has been empty for long enough, stop the timer.
		 */
		g_machine.deactivate_timer(m_DAC_timer);
		PDEBUGF(LOG_V1, LOG_AUDIO, "PS/1 DAC: empty, FIFO timer deactivated\n");
	}

	std::lock_guard<std::mutex> lock(m_DAC_lock);
	m_DAC_samples.push_back(value);
}

//this method is called by the Mixer thread
bool PS1Audio::create_DAC_samples(uint64_t _time_span_us, bool _prebuf, bool _first_upd)
{
	m_DAC_lock.lock();
	uint64_t mtime_us = g_machine.get_virt_time_us_mt();
	unsigned freq = m_DAC_freq;
	unsigned samples = m_DAC_samples.size();
	PDEBUGF(LOG_V2, LOG_AUDIO, "PS/1 DAC: mix span: %04d us, samples: %d at %d Hz (%d us)\n",
			_time_span_us, samples, freq, unsigned((double(samples)/double(freq))*1e6));

	m_DAC_channel->set_in_spec({AUDIO_FORMAT_U8, 1, freq});

	if(samples == 0) {
		m_DAC_lock.unlock();
		if(!m_DAC_channel->check_disable_time(mtime_us) && !_prebuf) {
			samples = us_to_frames(_time_span_us, freq);
			/* Some programs feed the DAC with 8-bit signed samples (eg Space
			 * Quest 4), while others with 8-bit unsigned samples. The real HW
			 * DAC *should* work with unsigned values (see eg. the POST beep
			 * sound, which is emitted with the DAC not the PSG). There's no
			 * way to know the type of the samples used so in order to avoid
			 * pops fade to the final value of 128.
			 */
			if(_first_upd) {
				m_DAC_channel->in().fill_samples<uint8_t>(samples, m_DAC_last_value);
			} else {
				m_DAC_channel->in().fill_frames_fade<uint8_t>(samples, m_DAC_last_value, 128);
			}
			m_DAC_last_value = 128;
			m_DAC_channel->input_finish();
			return true;
		}
		return false;
	} else if(PS1AUDIO_DAC_FADE_IN && _first_upd) {
		/* See the comment above. This fade-in should remove the pop at the
		 * start but doesn't work for SQ4 because it seems the game starts its
		 * samples at 128 like it's a unsigned 8bit, but the actual played sound
		 * effects are still signed 8bit. A bug in the game?
		 */
		m_DAC_channel->in().fill_frames_fade<uint8_t>(
				us_to_frames(_time_span_us/2, freq), 128, m_DAC_samples[0]);
	}

	m_DAC_channel->in().add_samples(m_DAC_samples);
	m_DAC_last_value = m_DAC_samples.back();
	m_DAC_samples.clear();
	m_DAC_lock.unlock();
	m_DAC_channel->input_finish();
	m_DAC_channel->set_disable_time(mtime_us);

	return true;
}

void PS1Audio::DAC::reset(unsigned _type)
{
	// The reload register is not affected by a reset. It must be initialized at POR.
	if(_type == MACHINE_POWER_ON) {
		set_reload_register(0);
	}
	read_ptr = 0;
	write_ptr = 0;
	write_avail = PS1AUDIO_FIFO_SIZE;
	read_avail = 0;
	almost_empty_value = 0;
	almost_empty = false;
}

void PS1Audio::DAC::set_reload_register(int _value)
{
	//The FIFO Timer is clocked at 1 MHz: 1 cycle every 1us
	reload_reg = _value & 0xFF;

	if(_value == 0) {
		if(g_machine.is_timer_active(fifo_timer)) {
			g_machine.deactivate_timer(fifo_timer);
			PDEBUGF(LOG_V1, LOG_AUDIO, "PS/1 DAC: FIFO timer deactivated\n");
		}
		return;
	}
	_value += 1;
	//The time between reloads is one cycle longer than the value written to the reload register.
	g_machine.activate_timer(fifo_timer, uint64_t(_value)*1_us, true);
	PDEBUGF(LOG_V1, LOG_AUDIO, "PS/1 DAC: FIFO timer activated, %dus (%dHz)\n",
			_value, 1000000/_value);
}

uint8_t PS1Audio::DAC::read()
{
	if(read_avail == 0) {
		return 0;
	}
	uint8_t value = FIFO[read_ptr];
	read_ptr = (read_ptr + 1) % PS1AUDIO_FIFO_SIZE;
	write_avail += 1;
	read_avail -= 1;
	return value;
}

void PS1Audio::DAC::write(uint8_t _data)
{
	if(write_avail == 0) {
		// If the FIFO is full, any additional attempted writing of data results in lost data.
		return;
	}
	FIFO[write_ptr] = _data;
	write_ptr = (write_ptr + 1) % PS1AUDIO_FIFO_SIZE;
	write_avail -= 1;
	read_avail += 1;
}
