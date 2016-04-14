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

#ifndef IBMULATOR_HW_PS1AUDIO_H
#define IBMULATOR_HW_PS1AUDIO_H

#include "hardware/iodevice.h"
#include "hardware/devices/sn76496.h"
#include "mixer.h"
#include <atomic>
#include "audio/synth.h"

#define PS1AUDIO_FIFO_SIZE 2048

class PS1Audio : public IODevice, private Synth
{
	IODEVICE(PS1Audio, "IBM PS/1 Audio Card")

private:
	struct DAC
	{
		uint8_t  FIFO[PS1AUDIO_FIFO_SIZE];
		uint32_t read_ptr;
		uint32_t write_ptr;
		uint32_t write_avail;
		uint32_t read_avail;
		uint8_t  reload_reg;
		int      fifo_timer;
		uint64_t timer_last_time;
		uint16_t almost_empty_value;
		bool     almost_empty;

		void reset(unsigned type);
		uint8_t read();
		void write(uint8_t _data);
		void set_reload_register(int _value);
	};

	struct {
		struct DAC     DAC;
		struct SN76496 PSG;
		uint8_t control_reg;
	} m_s;

	std::mutex m_DAC_lock;
	int m_DAC_empty_samples;
	//the DAC frequency value is written by the machine and read by the mixer
	std::atomic<unsigned> m_DAC_freq;
	std::vector<uint8_t> m_DAC_samples;
	std::shared_ptr<MixerChannel> m_DAC_channel;
	std::shared_ptr<MixerChannel> m_PSG_channel;
	uint8_t m_DAC_last_value;

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
	void FIFO_timer();
	void raise_interrupt();
	void lower_interrupt();
	bool create_DAC_samples(uint64_t _time_span_us, bool _prebuf, bool _first_upd);
	bool create_PSG_samples(uint64_t _time_span_us, bool _prebuf, bool _first_upd);
	void PSG_activate();
	void on_PSG_capture(bool _enable);
};

#endif
