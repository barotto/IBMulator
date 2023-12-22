/*
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
#include "hardware/devices/pcspeaker.h"
#include "filesys.h"
#include "audio/convert.h"
#include <cstring>

#define PS1AUDIO_INPUT_CLOCK 4000000

IODEVICE_PORTS(PS1Audio) = {
	{ 0x200, 0x200, PORT_8BIT|PORT_RW },  // ADC (R) / DAC (W)
	//0x201 is used by the Game Port device
	{ 0x202, 0x202, PORT_8BIT|PORT_RW }, // Control Register
	{ 0x203, 0x203, PORT_8BIT|PORT_RW }, // FIFO Timer reload value
	{ 0x204, 0x204, PORT_8BIT|PORT_RW }, // Joystick (X Axis Stick A) P0 (R) / Almost empty value (W)
	{ 0x205, 0x205, PORT_8BIT|PORT_RW }, // Joystick (Y Axis Stick A) P1 (R) / Sound Generator (W)
	{ 0x206, 0x206, PORT_8BIT|PORT_R_ }, // Joystick (X Axis Stick B) P2
	{ 0x207, 0x207, PORT_8BIT|PORT_R_ }, // Joystick (Y Axis Stick B) P3
	/* UART MIDI is managed by the MPU-401 device, disable these ports to avoid conflicts
	{ 0x330, 0x330, PORT_8BIT|PORT_RW }, // MIDI TXD Register
	{ 0x331, 0x331, PORT_8BIT|PORT_RW }, // MIDI IER Register
	{ 0x332, 0x332, PORT_8BIT|PORT_RW }, // MIDI IIR Register
	{ 0x335, 0x335, PORT_8BIT|PORT_RW }  // MIDI LSR Register
	*/
};
#define PS1AUDIO_IRQ 7

PS1Audio::PS1Audio(Devices *_dev)
: IODevice(_dev)
{
	memset(&m_s, 0, sizeof(m_s));
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
	m_fifo_timer = g_machine.register_timer(
		std::bind(&PS1Audio::fifo_timer, this, _1),
		"PS/1 DAC" // name
	);

	unsigned ch_features =
		MixerChannel::HasVolume |
		MixerChannel::HasBalance |
		MixerChannel::HasReverb | MixerChannel::HasAutoReverb |
		MixerChannel::HasChorus |
		MixerChannel::HasFilter | MixerChannel::HasAutoFilter;

	using namespace std::placeholders;
	m_dac.channel = g_mixer.register_channel(
		std::bind(&PS1Audio::dac_create_samples, this, _1, _2, _3),
		"PS/1 DAC", MixerChannel::AUDIOCARD, MixerChannel::AudioType::DAC);
	m_dac.channel->set_disable_timeout(3_s);
	m_dac.channel->set_features(ch_features | MixerChannel::HasResamplingType);

	m_dac.channel->add_autoval_cb(MixerChannel::ConfigParameter::Reverb, std::bind(&PS1Audio::dac_reverb_cb, this));
	m_dac.channel->add_autoval_cb(MixerChannel::ConfigParameter::Filter, std::bind(&PS1Audio::dac_filter_cb, this));

	m_dac.channel->register_config_map({
		{ MixerChannel::ConfigParameter::Volume,     { PS1AUDIO_SECTION, PS1AUDIO_DAC_VOLUME }},
		{ MixerChannel::ConfigParameter::Balance,    { PS1AUDIO_SECTION, PS1AUDIO_DAC_BALANCE }},
		{ MixerChannel::ConfigParameter::Reverb,     { PS1AUDIO_SECTION, PS1AUDIO_DAC_REVERB }},
		{ MixerChannel::ConfigParameter::Chorus,     { PS1AUDIO_SECTION, PS1AUDIO_DAC_CHORUS }},
		{ MixerChannel::ConfigParameter::Filter,     { PS1AUDIO_SECTION, PS1AUDIO_DAC_FILTERS }},
		{ MixerChannel::ConfigParameter::Resampling, { PS1AUDIO_SECTION, PS1AUDIO_DAC_RESAMPLING }},
	});

	m_dac.state = DAC::State::STOPPED;

	m_psg.install(PS1AUDIO_INPUT_CLOCK);

	Synth::set_chip(0, &m_psg);
	Synth::install("PS/1 PSG", 5_s,
		[this](Event &_event) {
			m_psg.write(_event.value);
			Synth::capture_command(0x50, _event);
		},
		[this](AudioBuffer &_buffer, int _sample_offset, int _frames) {
			m_psg.generate(&_buffer.operator[]<int16_t>(_sample_offset), _frames, 1);
		},
		[this](bool _start, VGMFile& _vgm) {
			if(_start) {
				_vgm.set_chip(VGMFile::SN76489);
				_vgm.set_clock(PS1AUDIO_INPUT_CLOCK);
				_vgm.set_SN76489_feedback(6);
				_vgm.set_SN76489_shift_width(16);
				_vgm.set_tag_system("IBM PC");
				_vgm.set_tag_notes("IBM PS/1 Audio Card");
			}
		}
	);
	Synth::channel()->set_features(ch_features);
	Synth::channel()->add_autoval_cb(MixerChannel::ConfigParameter::Reverb, std::bind(&PS1Audio::synth_reverb_cb, this));
	Synth::channel()->add_autoval_cb(MixerChannel::ConfigParameter::Filter, std::bind(&PS1Audio::synth_filter_cb, this));
	Synth::channel()->register_config_map({
		{ MixerChannel::ConfigParameter::Volume, { PS1AUDIO_SECTION, PS1AUDIO_PSG_VOLUME }},
		{ MixerChannel::ConfigParameter::Balance,{ PS1AUDIO_SECTION, PS1AUDIO_PSG_BALANCE }},
		{ MixerChannel::ConfigParameter::Reverb, { PS1AUDIO_SECTION, PS1AUDIO_PSG_REVERB }},
		{ MixerChannel::ConfigParameter::Chorus, { PS1AUDIO_SECTION, PS1AUDIO_PSG_CHORUS }},
		{ MixerChannel::ConfigParameter::Filter, { PS1AUDIO_SECTION, PS1AUDIO_PSG_FILTERS }}
	});
}

void PS1Audio::dac_filter_cb()
{
	if(m_dac.channel->is_filter_auto()) {
		if(m_pc_speaker_ch) {
			m_dac.channel->set_filter(m_pc_speaker_ch->filter());
		} else {
			m_dac.channel->set_filter(DEFAULT_PCSPEAKER_FILTER);
		}
	}
}

void PS1Audio::dac_filterparams_cb()
{
	if(m_dac.channel->is_filter_auto()) {
		m_dac.channel->copy_filter_params(m_pc_speaker_ch->filter_chain());
	}
}

void PS1Audio::dac_reverb_cb()
{
	if(m_dac.channel->is_reverb_auto()) {
		if(m_pc_speaker_ch) {
			m_dac.channel->set_reverb(m_pc_speaker_ch->reverb());
		} else {
			m_dac.channel->set_reverb(DEFAULT_PCSPEAKER_REVERB);
		}
	}
}

void PS1Audio::synth_filter_cb()
{
	if(Synth::channel()->is_filter_auto()) {
		if(m_pc_speaker_ch) {
			Synth::channel()->set_filter(m_pc_speaker_ch->filter());
		} else {
			Synth::channel()->set_filter(DEFAULT_PCSPEAKER_FILTER);
		}
	}
}

void PS1Audio::synth_filterparams_cb()
{
	if(Synth::channel()->is_filter_auto()) {
		Synth::channel()->copy_filter_params(m_pc_speaker_ch->filter_chain());
	}
}

void PS1Audio::synth_reverb_cb()
{
	if(Synth::channel()->is_reverb_auto()) {
		if(m_pc_speaker_ch) {
			Synth::channel()->set_reverb(m_pc_speaker_ch->reverb());
		} else {
			Synth::channel()->set_reverb(DEFAULT_PCSPEAKER_REVERB);
		}
	}
}

void PS1Audio::remove()
{
	IODevice::remove();
	Synth::remove();
	g_machine.unregister_irq(PS1AUDIO_IRQ, name());
	g_machine.unregister_timer(m_fifo_timer);
	if(m_pc_speaker_ch) {
		m_pc_speaker_ch->remove_parameter_cb(MixerChannel::ConfigParameter::Reverb, "ps1-dac-reverb");
		m_pc_speaker_ch->remove_parameter_cb(MixerChannel::ConfigParameter::Reverb, "ps1-synth-reverb");
		m_pc_speaker_ch->remove_parameter_cb(MixerChannel::ConfigParameter::Filter, "ps1-dac-filter");
		m_pc_speaker_ch->remove_parameter_cb(MixerChannel::ConfigParameter::Filter, "ps1-synth-filter");
		m_pc_speaker_ch->remove_parameter_cb(MixerChannel::ConfigParameter::FilterParams, "ps1-dac-filterparam");
		m_pc_speaker_ch->remove_parameter_cb(MixerChannel::ConfigParameter::FilterParams, "ps1-synth-filterparam");
	}
	m_pc_speaker_ch.reset();
	g_mixer.unregister_channel(m_dac.channel);
}

void PS1Audio::reset(unsigned _type)
{
	Synth::reset();

	m_s.fifo.reset(_type);
	m_s.control_reg = 0;
	lower_interrupt();
	
	std::lock_guard<std::mutex> lock(m_dac.mutex);
	m_dac.reset();
	dac_set_state(DAC::State::STOPPED);
}

void PS1Audio::power_off()
{
	Synth::power_off();
	m_dac.channel->enable(false);
}

void PS1Audio::config_changed()
{
	unsigned rate = clamp(g_program.config().get_int(PS1AUDIO_SECTION, PS1AUDIO_PSG_RATE),
			MIXER_MIN_RATE, MIXER_MAX_RATE);
	Synth::config_changed({AUDIO_FORMAT_S16, 1, double(rate)});

	m_pc_speaker_ch = g_mixer.get_channel(PCSpeaker::NAME);
	if(m_pc_speaker_ch) {
		m_pc_speaker_ch->add_parameter_cb(MixerChannel::ConfigParameter::Reverb, "ps1-dac-reverb", std::bind(&PS1Audio::dac_reverb_cb, this));
		m_pc_speaker_ch->add_parameter_cb(MixerChannel::ConfigParameter::Reverb, "ps1-synth-reverb", std::bind(&PS1Audio::synth_reverb_cb, this));
		m_pc_speaker_ch->add_parameter_cb(MixerChannel::ConfigParameter::Filter, "ps1-dac-filter", std::bind(&PS1Audio::dac_filter_cb, this));
		m_pc_speaker_ch->add_parameter_cb(MixerChannel::ConfigParameter::Filter, "ps1-synth-filter", std::bind(&PS1Audio::synth_filter_cb, this));
		m_pc_speaker_ch->add_parameter_cb(MixerChannel::ConfigParameter::FilterParams, "ps1-dac-filterparam", std::bind(&PS1Audio::dac_filterparams_cb, this));
		m_pc_speaker_ch->add_parameter_cb(MixerChannel::ConfigParameter::FilterParams, "ps1-synth-filterparam", std::bind(&PS1Audio::synth_filterparams_cb, this));
	}
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

	// no mutex lock necessary, at this stage the mixer is stopped
	dac_set_state(DAC::State::STOPPED);
	m_dac.reset();

	if(m_s.fifo.read_avail && m_s.fifo.reload_reg) {
		dac_set_state(DAC::State::ACTIVE);
	}
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
			PDEBUGF(LOG_V1, LOG_AUDIO, "PS/1 ADC: read from port 200h: not implemented!\n");
			//TODO
			break;
		case 0x202: {
			//Control Register
			value = 0;
			value |= m_s.control_reg & 1; //AIE-0 Ext Int Enable
			value |= m_s.fifo.almost_empty << 1; //IR-1 Almost Empty Int
			value |= (m_s.fifo.read_avail  == 0) << 2; //FE-2 FIFO Empty
			value |= (m_s.fifo.write_avail == 0) << 3; //FF-3 FIFO Full
			//ADR-4 ADC Data Rdy TODO
			//JIE-5 Joystick Int TODO
			//JM-6 RIN0 Bit ???
			//RIO-7 RIN1 Bit ???
			if(value & 2) {
				PDEBUGF(LOG_V2, LOG_AUDIO, "PS/1: AE Int (FIFO:%db, limit:%db)\n",
						m_s.fifo.read_avail, m_s.fifo.almost_empty_value);
			}
			break;
		}
		case 0x203:
			//FIFO Timer reload value
			value = m_s.fifo.reload_reg;
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
			m_s.fifo.write(value);
			//if the DAC is fetching from the FIFO but the timer is stopped
			//(for eg. because the fifo was empty long enough) then restart it
			if(m_s.fifo.reload_reg>0 && m_dac.state != DAC::State::ACTIVE) {
				std::lock_guard<std::mutex> lock(m_dac.mutex);
				dac_set_state(DAC::State::ACTIVE);
			}
			break;
		case 0x202:
			//Control Register
			m_s.control_reg = value;
			if(!(_value & 2)) {
				//The interrupt flag is cleared by writing a 0 then a 1 to this bit.
				m_s.fifo.almost_empty = false;
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
			m_s.fifo.reload_reg = value;
			if(value != 0) {
				//a change in frequency or a DAC start.
				std::lock_guard<std::mutex> lock(m_dac.mutex);
				dac_set_state(DAC::State::ACTIVE);
			}
			break;
		case 0x204:
			//Almost empty value
			m_s.fifo.almost_empty_value = uint16_t(value) * 4;
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
				Synth::add_event({
					g_machine.get_virt_time_ns(), 
					0,       // chip
					0, 0,    // reg port, register
					0, value // value port, value
				});
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

void PS1Audio::dac_set_state(DAC::State _to_state)
{
	// caller must lock dac mutex
	
	switch(_to_state) {
		case DAC::State::ACTIVE: {
			if(m_dac.state == DAC::State::STOPPED) {
				m_dac.channel->enable(true);
				m_dac.new_data = true;
				m_dac.used = 0;
				m_dac.empty_samples = 0;
				PDEBUGF(LOG_V1, LOG_AUDIO, "PS/1 DAC: activated\n");
			} else if(m_dac.state == DAC::State::WAITING) {
				PDEBUGF(LOG_V1, LOG_AUDIO, "PS/1 DAC: reactivated\n");
			}
			uint64_t old_period = m_dac.period_ns;
			dac_update_frequency();
			if(old_period != m_dac.period_ns) {
				g_machine.activate_timer(m_fifo_timer, m_dac.period_ns, true);
				PDEBUGF(LOG_V2, LOG_AUDIO, "PS/1 DAC: FIFO timer period %llu ns (%.2f Hz)\n",
						m_dac.period_ns, m_dac.rate);
			}
			break;
		}
		case DAC::State::WAITING:
			if(m_dac.state != DAC::State::WAITING) {
				PDEBUGF(LOG_V1, LOG_AUDIO, "PS/1 DAC: waiting\n");
			}
			break;
		case DAC::State::STOPPED:
			if(m_dac.state != DAC::State::STOPPED) {
				g_machine.deactivate_timer(m_fifo_timer);
				m_dac.period_ns = 0;
				PDEBUGF(LOG_V1, LOG_AUDIO, "PS/1 DAC: deactivated\n");
			}
			break;
	}
	m_dac.state = _to_state;
}

void PS1Audio::dac_update_frequency()
{
	uint64_t fifo_rate_us = m_s.fifo.reload_reg;
	// The time between reloads is one cycle longer than the value written to the reload register.
	fifo_rate_us++;
	if(fifo_rate_us < 45) {
		// Limits are documented as "The FIFO Timer is clocked at 1 MHz", and 
		// "The maximum time is 256 microsecond."
		// Set 8 kHz which should be the default.
		// F-14 is the only game I know of that uses a reload of 1 to play samples.
		PDEBUGF(LOG_V1, LOG_AUDIO, "PS/1 DAC: rate out of range: %llu us\n", fifo_rate_us);
		fifo_rate_us = 125;
	}
	uint64_t old_period = m_dac.period_ns;
	double old_rate = m_dac.rate;
	m_dac.period_ns = fifo_rate_us * 1_us;
	m_dac.rate = 1e9 / double(m_dac.period_ns);
	m_dac.empty_timeout = 1 * m_dac.rate; // 1 second worth of samples
	if(m_dac.used && m_dac.period_ns != old_period) {
		PDEBUGF(LOG_V2, LOG_AUDIO, "PS/1 DAC: frequency change while dac is filled with %d samples at %.2f Hz\n",
				m_dac.used, m_dac.rate);
		// Convert the audio data in the DAC buffer to the new rate.
		// Use a fast resampler, it's usually just a handful of samples.
		static std::array<uint8_t,DAC::BUF_SIZE> tempbuf;
		size_t generated = Audio::Convert::resample_mono<uint8_t>(m_dac.data, m_dac.used, old_rate, &tempbuf[0], DAC::BUF_SIZE, m_dac.rate);
		memcpy(m_dac.data, &tempbuf[0], generated);
		m_dac.used = generated;
	}
}

void PS1Audio::fifo_timer(uint64_t)
{
	// A pulse is generated on overflow and is used to latch data into the ADC
	// latch and to read data out of the FIFO.
	uint8_t sample = m_dac.last_value;
	if(m_s.fifo.read_avail > 0) {
		sample = m_s.fifo.read();
		m_dac.empty_samples = 0;
	} else {
		std::lock_guard<std::mutex> lock(m_dac.mutex);
		dac_set_state(DAC::State::WAITING);
		m_dac.empty_samples++;
	}
	if(m_s.fifo.read_avail == m_s.fifo.almost_empty_value && (m_s.control_reg & 2) ) {
		m_s.fifo.almost_empty = true;
		raise_interrupt();
	}
	
	std::lock_guard<std::mutex> lock(m_dac.mutex);
	if(m_dac.empty_samples > m_dac.empty_timeout) {
		dac_set_state(DAC::State::STOPPED);
	} else {
		m_dac.add_sample(sample);
	}
}

//this method is called by the Mixer thread
bool PS1Audio::dac_create_samples(uint64_t _time_span_ns, bool, bool)
{
	m_dac.mutex.lock();
	
	uint64_t mtime_ns = g_machine.get_virt_time_ns_mt();
	unsigned presamples = 0, postsamples = 0;
	double needed_samples = ns_to_frames(_time_span_ns, m_dac.rate);
	unsigned samples = m_dac.used;
	bool chactive = true;
	static double balance = 0.0;

	m_dac.channel->set_in_spec({AUDIO_FORMAT_U8, 1, m_dac.rate});

	if(m_dac.new_data) {
		balance = 0.0;
	}
	
	if(m_dac.new_data && (samples < needed_samples)) {
		presamples = needed_samples - samples;
		// Some programs feed the DAC with 8-bit signed samples (eg Space
		// Quest 4), while others with 8-bit unsigned samples. The real HW
		// DAC *should* work with unsigned values (see eg. the POST beep
		// sound, which is emitted with the DAC not the PSG). There's no
		// way to know the type of the samples used so in order to avoid
		// pops interpolate from silence (128) to the last value.
		m_dac.channel->in().fill_frames_fade<uint8_t>(presamples, 128, m_dac.last_value);
		balance += presamples;
	}

	if(samples > 0) {
		m_dac.channel->in().add_samples(m_dac.data, samples);
		m_dac.channel->set_disable_time(mtime_ns);
		m_dac.used = 0;
		balance += samples;
	}

	balance -= needed_samples;
	
	if(m_dac.state == DAC::State::STOPPED && (balance <= 0) && presamples==0) {
		chactive = !m_dac.channel->check_disable_time(mtime_ns);
		postsamples = balance * -1.0;
		// See the comment above
		m_dac.channel->in().fill_frames_fade<uint8_t>(postsamples, m_dac.last_value, 128);
		m_dac.last_value = 128;
		balance += postsamples;
	}
	
	m_dac.new_data &= (samples == 0);
	m_dac.mutex.unlock();

	m_dac.channel->input_finish();

	unsigned total = presamples + samples + postsamples;
	PDEBUGF(LOG_V2, LOG_MIXER, "PS/1 DAC: mix time: %04llu ns, samples at %.2f Hz: %d+%d+%d (%.2f ns), balance: %.2f\n",
			_time_span_ns, m_dac.rate, presamples, samples, postsamples, frames_to_ns(total, m_dac.rate),
			balance);

	return chactive;
}

void PS1Audio::DAC::reset()
{
	used = 0;
	last_value = 128;
	new_data = true;
	empty_samples = 0;
	period_ns = 0;
	rate = 0.0;
}

void PS1Audio::DAC::add_sample(uint8_t _sample)
{
	// caller must lock dac mutex
	if(used < BUF_SIZE) {
		data[used] = _sample;
		used++;
	}
	last_value = _sample;
}

void PS1Audio::FIFO::reset(unsigned _type)
{
	// The reload register is not affected by a reset. It must be initialized at POR.
	if(_type == MACHINE_POWER_ON) {
		reload_reg = 0;
	}
	read_ptr = 0;
	write_ptr = 0;
	write_avail = PS1AUDIO_FIFO_SIZE;
	read_avail = 0;
	almost_empty_value = 0;
	almost_empty = false;
}

uint8_t PS1Audio::FIFO::read()
{
	if(read_avail == 0) {
		return 0;
	}
	uint8_t value = data[read_ptr];
	read_ptr = (read_ptr + 1) % PS1AUDIO_FIFO_SIZE;
	write_avail += 1;
	read_avail -= 1;
	almost_empty = false;

	return value;
}

void PS1Audio::FIFO::write(uint8_t _data)
{
	if(write_avail == 0) {
		// If the FIFO is full, any additional attempted writing of data results in lost data.
		return;
	}
	data[write_ptr] = _data;
	write_ptr = (write_ptr + 1) % PS1AUDIO_FIFO_SIZE;
	write_avail -= 1;
	read_avail += 1;
	// If another read or write occurs, the	almost empty flag is cleared.
	almost_empty = false;
}
