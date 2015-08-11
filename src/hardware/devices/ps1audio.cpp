/*
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
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
#define PS1AUDIO_IRQ 7
#define PS1AUDIO_PSG_DISABLE_TIMEOUT 2500000 //in usecs
#define PS1AUDIO_DAC_DISABLE_TIMEOUT 1000000 //in usecs
#define PS1AUDIO_DAC_FADE_IN false

PS1Audio g_ps1audio;


PS1Audio::PS1Audio()
:
m_enabled(false)
{
	m_DAC_samples.reserve(PS1AUDIO_FIFO_SIZE*2);
}

PS1Audio::~PS1Audio()
{

}

void PS1Audio::init()
{
	g_machine.register_irq(PS1AUDIO_IRQ, get_name());

	//Read Function
	g_devices.register_read_handler(this, 0x0200, 1);  //Read Analog to Digital Converter Data
	g_devices.register_read_handler(this, 0x0202, 1);  //Read Control Register
	g_devices.register_read_handler(this, 0x0203, 1);  //Read FIFO Timer reload value
	g_devices.register_read_handler(this, 0x0204, 1);  //Joystick (X Axis Stick A) P0
	g_devices.register_read_handler(this, 0x0205, 1);  //Joystick (Y Axis Stick A) P1
	g_devices.register_read_handler(this, 0x0206, 1);  //Joystick (X Axis Stick B) P2
	g_devices.register_read_handler(this, 0x0207, 1);  //Joystick (Y Axis Stick B) P3
	g_devices.register_read_handler(this, 0x0330, 1);  //Read to MIDI TXD Register
	g_devices.register_read_handler(this, 0x0331, 1);  //Read to MIDI IER Register
	g_devices.register_read_handler(this, 0x0332, 1);  //Read to MIDI IIR Register
	g_devices.register_read_handler(this, 0x0335, 1);  //Read to MIDI LSR Register

	//Write Function
	g_devices.register_write_handler(this, 0x0200, 1); //Write to Digital to Analog Converter
	g_devices.register_write_handler(this, 0x0202, 1); //Write to Control Register
	g_devices.register_write_handler(this, 0x0203, 1); //Write FIFO Timer reload value
	g_devices.register_write_handler(this, 0x0204, 1); //Write almost empty value
	g_devices.register_write_handler(this, 0x0205, 1); //Write to Sound Generator
	g_devices.register_write_handler(this, 0x0330, 1); //Write to MIDI TXD Register
	g_devices.register_write_handler(this, 0x0331, 1); //Write to MIDI IER Register
	g_devices.register_write_handler(this, 0x0332, 1); //Write to MIDI IIR Register
	g_devices.register_write_handler(this, 0x0335, 1); //Write to MIDI LSR Register

	//the DAC emulation can surely be done without a machine timer, but I find
	//this approach easier to read and follow.
	m_s.DAC.fifo_timer = g_machine.register_timer(
		std::bind(&PS1Audio::FIFO_timer,this),
		256,    // period usec
		false,  // continuous
		false,  // active
		"PS1AudioDAC" //name
	);

	m_DAC_channel = g_mixer.register_channel(
		std::bind(&PS1Audio::create_DAC_samples, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
		"PS/1 Audio DAC");
	m_DAC_channel->set_disable_timeout(PS1AUDIO_DAC_DISABLE_TIMEOUT);

	m_PSG_channel = g_mixer.register_channel(
		std::bind(&PS1Audio::create_PSG_samples, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
		"PS/1 Audio PSG");
	m_PSG_channel->set_disable_timeout(PS1AUDIO_PSG_DISABLE_TIMEOUT);

	m_PSG_channel->register_capture_clbk(
		std::bind(&PS1Audio::on_PSG_capture, this, std::placeholders::_1)
	);
	config_changed();
}

void PS1Audio::reset(unsigned _type)
{
	std::lock_guard<std::mutex> psg_lock(m_PSG_lock);
	m_s.PSG.reset(PS1AUDIO_INPUT_CLOCK, m_PSG_rate);
	m_PSG_events.clear();

	m_s.control_reg = 0;
	lower_interrupt();

	std::lock_guard<std::mutex> lock(m_DAC_lock);
	m_s.DAC.reset(_type);
	m_DAC_samples.clear();
	m_DAC_last_value = 128;
}

void PS1Audio::power_off()
{
	if(m_PSG_channel->is_enabled()) {
		m_PSG_channel->enable(false);
	}
	if(m_DAC_channel->is_enabled()) {
		m_DAC_channel->enable(false);
	}
}

void PS1Audio::config_changed()
{
	m_enabled = g_program.config().get_bool(MIXER_SECTION, MIXER_PS1AUDIO);
	m_PSG_rate = g_program.config().get_int(MIXER_SECTION, MIXER_RATE);
	m_PSG_samples_per_ns = double(m_PSG_rate)/1e9;
}

void PS1Audio::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_AUDIO, "PS/1: saving state\n");
	std::lock_guard<std::mutex> psg_lock(m_PSG_lock);
	std::lock_guard<std::mutex> dac_lock(m_DAC_lock);

	StateHeader h;
	m_s.PSG_events_cnt = m_PSG_events.size();
	h.name = get_name();
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);

	if(m_s.PSG_events_cnt) {
		std::deque<PSGEvent>::iterator it;
		if(!m_PSG_events.acquire_iterator(it)) {
			PERRF(LOG_AUDIO, "PS/1: error writing state\n");
			throw std::exception();
			return;
		}
		h.name = std::string(get_name()) + "-PSG evts";
		h.data_size = m_s.PSG_events_cnt * sizeof(PSGEvent);
		std::vector<uint8_t> evts(h.data_size);
		uint8_t *ptr = &evts[0];
		for(size_t i=0; i<m_s.PSG_events_cnt; i++,it++) {
			*((PSGEvent*)ptr) = *it;
			ptr += sizeof(PSGEvent);
		}
		m_PSG_events.release_iterator();
		_state.write(&evts[0], h);
	}
}

void PS1Audio::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_AUDIO, "PS/1: restoring state\n");
	std::lock_guard<std::mutex> psg_lock(m_PSG_lock);
	std::lock_guard<std::mutex> dac_lock(m_DAC_lock);
	m_PSG_channel->enable(false);
	m_DAC_channel->enable(false);

	StateHeader h;
	h.name = get_name();
	h.data_size = sizeof(m_s);
	_state.read(&m_s, h);

	m_PSG_events.clear();
	m_PSG_last_mtime = 0;
	if(m_s.PSG_events_cnt) {
		_state.get_next_lump_header(h);
		if(h.name.compare(std::string(get_name()) + "-PSG evts") != 0) {
			PERRF(LOG_AUDIO, "PS/1 PSG events expected in state buffer, found %s\n", h.name.c_str());
			throw std::exception();
		}
		size_t expsize = m_s.PSG_events_cnt*sizeof(PSGEvent);
		if(h.data_size != expsize) {
			PERRF(LOG_AUDIO, "PS/1 PSG events size mismatch in state buffer, expected %u, found %u\n",
					expsize, h.data_size);
			throw std::exception();
		}
		std::vector<PSGEvent> evts(m_s.PSG_events_cnt);
		_state.read((uint8_t*)&evts[0],h);
		for(size_t i=0; i<m_s.PSG_events_cnt; i++) {
			m_PSG_events.push(evts[i]);
		}
	}
	if(m_s.PSG_events_cnt || !m_s.PSG.is_silent()) {
		m_PSG_channel->enable(true);
	}

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
		g_pic.raise_irq(PS1AUDIO_IRQ);
	}
}

void PS1Audio::lower_interrupt()
{
	g_pic.lower_irq(PS1AUDIO_IRQ);
}

uint16_t PS1Audio::read(uint16_t _address, unsigned)
{
	uint16_t value = ~0;
	if(!m_enabled) {
		return value;
	}
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
	if(!m_enabled) {
		return;
	}

	uint8_t value = _value & 0xFF;
	switch(_address) {
		case 0x200:
			//Digital to Analog Converter
			m_s.DAC.write(value);
			//if the DAC is fetching from the FIFO but the timer is stopped
			//(for eg. because the fifo was empty long enough) then restart it
			if(m_s.DAC.reload_reg>0 && !g_machine.is_timer_active(m_s.DAC.fifo_timer)) {
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
				m_DAC_freq = 1000000 / (int(value)+1);
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
					push = ((value & 0xF) != 0xF) || m_PSG_channel->is_enabled();
					if(push && !m_PSG_channel->is_enabled()) {
						PSG_activate();
					}
				} else { /* frequency bit0-3 */ }
			} else {
				//DATA byte, frequency bit4-9
				if(!m_PSG_channel->is_enabled()) {
					PSG_activate();
				}
			}
			if(push) {
				m_PSG_events.push({g_machine.get_virt_time_ns(), value});
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

void PS1Audio::FIFO_timer()
{
	//A	pulse is generated on overflow and is used to latch data into the ADC
	//latch and to read data out of the FIFO.
	uint8_t value = m_DAC_last_value;
	if(m_s.DAC.read_avail > 0) {
		value = m_s.DAC.read();
		if(!m_DAC_channel->is_enabled()) {
			m_DAC_channel->enable(true);
		}
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
	if(m_DAC_empty_samples>10) {
		g_machine.deactivate_timer(m_s.DAC.fifo_timer);
		PDEBUGF(LOG_V1, LOG_AUDIO, "PS/1 DAC: FIFO timer deactivated\n");
	}

	std::lock_guard<std::mutex> lock(m_DAC_lock);
	m_DAC_samples.push_back(value);
}

void PS1Audio::PSG_activate()
{
	m_PSG_last_mtime = 0;
	m_PSG_channel->enable(true);
}

//this method is called by the Mixer thread
int PS1Audio::create_DAC_samples(int _mix_slice_us, bool _prebuf, bool _first_upd)
{
	m_DAC_lock.lock();
	uint64_t mtime_us = g_machine.get_virt_time_us_mt();
	int freq = m_DAC_freq.load();
	int samples = m_DAC_samples.size();
	int totsamples = samples;
	int avail = ((double(samples) / double(freq)) * 1e6);
	PDEBUGF(LOG_V2, LOG_AUDIO, "PS/1 DAC: mix time: %04dus, samples: %d at %dHz (%d us)\n",
			_mix_slice_us, samples, freq, avail);

	if(samples == 0) {
		m_DAC_lock.unlock();
		if(!m_DAC_channel->check_disable_time(mtime_us) && !_prebuf) {
			samples = Mixer::us_to_samples(_mix_slice_us, freq);
			if(m_DAC_last_value == 128 || _first_upd) {
				m_DAC_channel->fill_samples<uint8_t>(samples, m_DAC_last_value);
			} else {
				//try to prevent nasty pops (Space Quest 4)
				m_DAC_channel->fill_samples_fade_u8m(samples, m_DAC_last_value, 128);
			}
			m_DAC_last_value = 128;
			m_DAC_channel->mix_samples(freq, MIXER_FORMAT_U8, 1);
		}
		return samples;
	} else if(PS1AUDIO_DAC_FADE_IN && _first_upd && m_DAC_samples[0]!=128) {
		totsamples += m_DAC_channel->fill_samples_fade_u8m(
				Mixer::us_to_samples(_mix_slice_us/2, freq), 128, m_DAC_samples[0]);
	}

	m_DAC_channel->add_samples(&m_DAC_samples[0], samples);
	m_DAC_last_value = m_DAC_samples.back();
	m_DAC_samples.clear();
	m_DAC_lock.unlock();
	m_DAC_channel->mix_samples(freq, MIXER_FORMAT_U8, 1);
	m_DAC_channel->set_disable_time(mtime_us);

	return totsamples;
}

int PS1Audio::generate_PSG_samples(uint64_t _duration)
{
	static std::vector<int16_t> buffer;
	static double fsrem = 0.0;
	double fsamples = (double(_duration) * m_PSG_samples_per_ns) + fsrem;
	int samples = fsamples;
	if(samples > 0) {
		if(buffer.size() < unsigned(samples)) {
			buffer.resize(samples);
		}
		m_s.PSG.generate_samples(&buffer[0], samples);
		m_PSG_channel->add_samples((uint8_t*)&buffer[0], samples*2);
	}
	fsrem = fsamples - samples;
	return samples;
}

//this method is called by the Mixer thread
int PS1Audio::create_PSG_samples(int _mix_slice_us, bool _prebuf, bool /*_first_upd*/)
{
	//this lock is to prevent a sudden queue clear on reset
	std::lock_guard<std::mutex> lock(m_PSG_lock);

	uint64_t mtime_ns = g_machine.get_virt_time_ns_mt();
	uint64_t mtime_us = NSEC_TO_USEC(mtime_ns);

	PDEBUGF(LOG_V2, LOG_AUDIO, "PS/1 PSG: mix slice: %04d usecs, samples needed: %d\n",
			_mix_slice_us, int(round(double(_mix_slice_us) * 1000.0 * m_PSG_samples_per_ns)));

	PSGEvent event, next_event;
	uint64_t time_span;
	int generated_samples = 0;
	next_event.time = 0;
	bool empty = m_PSG_events.empty();

	while(next_event.time < mtime_ns) {
		empty = !m_PSG_events.try_and_copy(event);

		if(empty || event.time > mtime_ns) {
			if(m_PSG_last_mtime) {
				time_span = mtime_ns - m_PSG_last_mtime;
			} else {
				time_span = _mix_slice_us * 1000 * (!_prebuf);
			}
			generated_samples += generate_PSG_samples(time_span);
			break;
		} else if(m_PSG_last_mtime) {
			time_span = event.time - m_PSG_last_mtime;
			generated_samples += generate_PSG_samples(time_span);
		}
		m_PSG_last_mtime = 0;

		//send command to the PSG
		m_s.PSG.write(event.b0);
		if(m_PSG_vgm.is_open()) {
			m_PSG_vgm.command(NSEC_TO_USEC(event.time), 0x50, event.b0);
		}

		m_PSG_events.try_and_pop();
		if(!m_PSG_events.try_and_copy(next_event) || next_event.time > mtime_ns) {
			//no more events or the next event is in the future
			next_event.time = mtime_ns;
		}
		time_span = next_event.time - event.time;
		generated_samples += generate_PSG_samples(time_span);
	}

	if(generated_samples > 0) {
		m_PSG_channel->mix_samples(m_PSG_rate, MIXER_FORMAT_S16, 1);
	}

	PDEBUGF(LOG_V2, LOG_AUDIO, "%d samples generated\n", generated_samples);

	if(empty && m_s.PSG.is_silent()) {
		m_PSG_channel->check_disable_time(mtime_us);
	} else {
		m_PSG_channel->set_disable_time(mtime_us);
	}
	m_PSG_last_mtime = mtime_ns;

	return generated_samples;
}

//this method is called by the Mixer thread
void PS1Audio::on_PSG_capture(bool _enable)
{
	if(!m_enabled) {
		return;
	}
	if(_enable) {
		std::string path = g_program.config().get_file(PROGRAM_SECTION, PROGRAM_CAPTURE_DIR, FILE_TYPE_USER);
		std::string fname = FileSys::get_next_filename(path, "ps1psg_", ".vgm");
		if(!fname.empty()) {
			try {
				m_PSG_vgm.open(fname);
				m_PSG_vgm.set_chip(VGMFile::SN76489);
				m_PSG_vgm.set_clock(PS1AUDIO_INPUT_CLOCK);
				m_PSG_vgm.set_SN76489_feedback(6);
				m_PSG_vgm.set_SN76489_shift_width(16);
				PINFOF(LOG_V0, LOG_MIXER, "PS/1 PSG: started audio capturing to '%s'\n", fname.c_str());
			} catch(std::exception &e) { }
		}
	} else {
		try {
			m_PSG_vgm.close();
		} catch(std::exception &e) { }
	}
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

	//The time between reloads is one cycle longer than the value written to the reload register.
	g_machine.activate_timer(fifo_timer, _value+1, true);
	PDEBUGF(LOG_V1, LOG_AUDIO, "PS/1 DAC: FIFO timer activated, %dus\n", _value+1);
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
