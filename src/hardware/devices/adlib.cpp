/*
 * Copyright (C) 2016-2023  Marco Bortolin
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
#include "adlib.h"


IODEVICE_PORTS(AdLib) = {
	{ 0x388, 0x389, PORT_8BIT|PORT_RW }
};

AdLib::AdLib(Devices *_dev)
: IODevice(_dev)
{
	memset(&m_s, 0, sizeof(m_s));
}

AdLib::~AdLib()
{
}

void AdLib::install()
{
	IODevice::install();
	m_OPL.install(OPL::OPL2, OPL::ChipNames[OPL::OPL2], true);

	Synth::set_chip(0, &m_OPL);
	Synth::install("AdLib", 5_s,
		[this](Event &_event) {
			m_OPL.write(0, _event.reg);
			m_OPL.write(1, _event.value);
			Synth::capture_command(0x5A, _event);
		},
		[this](AudioBuffer &_buffer, int _sample_offset, int _frames) {
			m_OPL.generate(&_buffer.operator[]<int16_t>(_sample_offset), _frames, 1);
		},
		[this](bool _start, VGMFile& _vgm) {
			if(_start) {
				_vgm.set_chip(VGMFile::YM3812);
				_vgm.set_clock(3579545);
				_vgm.set_tag_system("IBM PC");
				_vgm.set_tag_notes("AdLib direct dump.");
			}
		}
	);

	Synth::channel()->set_features(
		MixerChannel::HasVolume |
		MixerChannel::HasBalance |
		MixerChannel::HasReverb |
		MixerChannel::HasChorus |
		MixerChannel::HasFilter
	);

	Synth::channel()->register_config_map({
		{ MixerChannel::ConfigParameter::Volume, { ADLIB_SECTION, ADLIB_VOLUME }},
		{ MixerChannel::ConfigParameter::Balance,{ ADLIB_SECTION, ADLIB_BALANCE }},
		{ MixerChannel::ConfigParameter::Reverb, { ADLIB_SECTION, ADLIB_REVERB }},
		{ MixerChannel::ConfigParameter::Chorus, { ADLIB_SECTION, ADLIB_CHORUS }},
		{ MixerChannel::ConfigParameter::Filter, { ADLIB_SECTION, ADLIB_FILTERS }},
	});

	PINFOF(LOG_V0, LOG_AUDIO, "Installed %s\n", name());
}

void AdLib::remove()
{
	IODevice::remove();
	Synth::remove();
}

void AdLib::reset(unsigned)
{
	Synth::reset();
	m_s.reg_index = 0;
}

void AdLib::power_off()
{
	Synth::power_off();
}

void AdLib::config_changed()
{
	unsigned rate = clamp(g_program.config().get_int(ADLIB_SECTION, ADLIB_RATE),
			MIXER_MIN_RATE, MIXER_MAX_RATE);
	Synth::config_changed({AUDIO_FORMAT_S16, 1, double(rate)});
}

uint16_t AdLib::read(uint16_t _address, unsigned)
{
	uint8_t value = m_OPL.read(_address-0x388);
	PDEBUGF(LOG_V2, LOG_AUDIO, "AdLib: status  -> %02Xh\n", value);
	return value;
}

void AdLib::write(uint16_t _address, uint16_t _value, unsigned)
{
	_address -= 0x388;
	switch(_address) {
		case 0: {
			m_s.reg_index = _value;
			PDEBUGF(LOG_V2, LOG_AUDIO, "AdLib: index   <- %02Xh\n", _value);
			break;
		}
		case 1: {
			switch(m_s.reg_index) {
				case 0x02:
				case 0x03:
				case 0x04:
					m_OPL.write_timers(m_s.reg_index, _value);
					break;
				default:
					PDEBUGF(LOG_V2, LOG_AUDIO, "AdLib: reg %02Xh <- %02Xh\n", m_s.reg_index, _value);
					Synth::add_event({
						g_machine.get_virt_time_ns(),
						0, // chip
						0, m_s.reg_index,
						1, uint8_t(_value)
					});
					Synth::enable_channel();
					break;
			}
			break;
		}
		default:
			break;
	}
}

void AdLib::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_AUDIO, "AdLib: saving state\n");
	_state.write(&m_s, {sizeof(m_s), name()});
	Synth::save_state(_state);
}

void AdLib::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_AUDIO, "AdLib: restoring state\n");
	_state.read(&m_s, {sizeof(m_s), name()});
	Synth::restore_state(_state);
}
