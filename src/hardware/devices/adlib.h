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

#ifndef IBMULATOR_HW_ADLIB_H
#define IBMULATOR_HW_ADLIB_H

#include "ibmulator.h"
#include "hardware/iodevice.h"
#include "mixer.h"
#include "opl.h"
#include "audio/synth.h"

class AdLib : public IODevice, private Synth
{
	IODEVICE(AdLib, "AdLib");

private:
	OPL m_OPL;
	struct {
		uint8_t reg_index;
	} m_s;
	std::shared_ptr<MixerChannel> m_channel;

public:
	AdLib(Devices *_dev);
	~AdLib();

	void install();
	void remove();
	void reset(unsigned _type);
	void power_off();
	void config_changed();
	uint16_t read(uint16_t _address, unsigned _io_len);
	void write(uint16_t _address, uint16_t _value, unsigned _io_len);
	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

private:
	bool create_samples(uint64_t _time_span_us, bool _prebuf, bool);
	void on_capture(bool _enable);
};

#endif
