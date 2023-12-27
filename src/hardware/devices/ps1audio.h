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

#ifndef IBMULATOR_HW_PS1AUDIO_H
#define IBMULATOR_HW_PS1AUDIO_H

#include "hardware/iodevice.h"
#include "hardware/devices/sn76496.h"
#include "mixer.h"
#include <atomic>
#include "audio/synth.h"

#define PS1AUDIO_FIFO_SIZE 2048

class PS1Audio : public IODevice, protected Synth
{
	IODEVICE(PS1Audio, "IBM PS/1 Audio Card")

private:
	struct DAC
	{
		constexpr static unsigned BUF_SIZE  = 4096;
		
		enum State {
			ACTIVE, WAITING, STOPPED
		};
		State state;

		uint8_t  data[BUF_SIZE];
		double   rate;
		uint64_t period_ns;
		unsigned used;
		
		bool     new_data;
		uint8_t  last_value;
		uint32_t empty_samples;
		uint32_t empty_timeout;
		
		std::mutex mutex;
		std::shared_ptr<MixerChannel> channel;
		
		void reset();
		void add_sample(uint8_t _sample);
	};
	
	struct FIFO
	{
		constexpr static unsigned FIFO_SIZE = 2048;
		
		uint8_t  data[FIFO_SIZE];
		uint32_t read_ptr;
		uint32_t write_ptr;
		uint32_t write_avail;
		uint32_t read_avail;
		uint8_t  reload_reg;
		uint64_t timer_last_time;
		uint16_t almost_empty_value;
		bool     almost_empty;
		
		void reset(unsigned type);
		uint8_t read();
		void write(uint8_t _data);
	};

	struct {
		FIFO    fifo;
		uint8_t control_reg;
	} m_s;

	DAC     m_dac;
	SN76496 m_psg;

	TimerID m_fifo_timer = NULL_TIMER_ID;

public:
	PS1Audio(Devices *_dev);
	~PS1Audio();

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
	void dac_filter_cb();
	void synth_filter_cb();

	void fifo_timer(uint64_t);
	bool dac_create_samples(uint64_t _time_span_ns, bool _prebuf, bool _first_upd);
	void dac_set_state(DAC::State _state);
	void dac_update_frequency();

	void raise_interrupt();
	void lower_interrupt();

	void on_psg_capture(bool _enable);
};

#endif
