/*
 * Copyright (C) 2016  Marco Bortolin
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
}

AdLib::~AdLib()
{
}

void AdLib::install()
{
	IODevice::install();
	m_OPL.install("AdLib",true);

	using namespace std::placeholders;
	m_channel = g_mixer.register_channel(
		std::bind(&AdLib::create_samples, this, _1, _2, _3),
		"AdLib FM");
	m_channel->set_disable_timeout(5000000);
	m_channel->register_capture_clbk(std::bind(
			&AdLib::on_capture, this, _1));
}

void AdLib::remove()
{
	IODevice::remove();
	m_OPL.remove();
	g_mixer.unregister_channel(m_channel);
}

void AdLib::reset(unsigned)
{
	m_channel->enable(false);
	Synth::reset();
	m_OPL.reset();
	m_s.reg_index = 0;
}

void AdLib::power_off()
{
	m_channel->enable(false);
}

void AdLib::config_changed()
{
	int rate = g_program.config().get_int(MIXER_SECTION, MIXER_RATE);
	m_OPL.config_changed(OPL::OPL2, rate);
	AudioSpec spec({AUDIO_FORMAT_S16, 1, unsigned(rate)});
	set_audio_spec(spec);
	m_channel->set_in_spec(spec);
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
						m_s.reg_index,
						uint8_t(_value)
					});
					if(!m_channel->is_enabled()) {
						Synth::enable_channel(m_channel.get());
					}
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
	m_OPL.save_state(_state);
	Synth::save_state(_state);
}

void AdLib::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_AUDIO, "AdLib: restoring state\n");
	_state.read(&m_s, {sizeof(m_s), name()});
	m_OPL.restore_state(_state);
	Synth::restore_state(_state);
	if(Synth::has_events() || !m_OPL.is_silent()) {
		Synth::enable_channel(m_channel.get());
	}
}

bool AdLib::create_samples(uint64_t _time_span_us, bool _prebuf, bool)
{
	uint64_t mtime_ns = g_machine.get_virt_time_ns_mt();

	auto result = play_events(mtime_ns, _time_span_us, _prebuf,
		[this](Event &_event) {
			m_OPL.write(0, _event.reg);
			m_OPL.write(1, _event.value);
			if(Synth::is_capturing()) {
				Synth::capture_command(0x5A, _event);
			}
		},
		[this](AudioBuffer &_buffer, int _frames) {
			m_OPL.generate(&_buffer.operator[]<int16_t>(0), _frames);
			m_channel->in().add_frames(_buffer);
		}
	);

	m_channel->input_finish();

	PDEBUGF(LOG_V2, LOG_AUDIO, "AdLib FM: mix %04d usecs, %d samples generated\n",
			_time_span_us, result.second);

	if(result.first && m_OPL.is_silent()) {
		return m_channel->check_disable_time(mtime_ns/1000);
	}
	m_channel->set_disable_time(mtime_ns/1000);
	return true;
}

void AdLib::on_capture(bool _enable)
{
	if(_enable) {
		try {
			Synth::start_capture("adlib");
			Synth::vgm().set_chip(VGMFile::YM3812);
			Synth::vgm().set_clock(3579545);
			PINFOF(LOG_V0, LOG_MIXER, "AdLib OPL: started audio capturing to '%s'\n",
					Synth::vgm().name());
		} catch(std::exception &e) {}
	} else {
		Synth::stop_capture();
	}
}
