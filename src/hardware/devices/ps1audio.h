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

#ifndef IBMULATOR_HW_PS1AUDIO_H
#define IBMULATOR_HW_PS1AUDIO_H

#include "hardware/iodevice.h"
#include "hardware/devices/sn76496.h"
#include "mixer.h"
#include "shared_deque.h"
#include "vgm.h"
#include <atomic>

class PS1Audio;
extern PS1Audio g_ps1audio;

#define PS1AUDIO_FIFO_SIZE 2048

class PS1Audio : public IODevice
{
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

	struct PSGEvent {
		uint64_t time;
		uint8_t b0;
	};

	shared_deque<PSGEvent> m_PSG_events;

	struct {
		struct DAC     DAC;
		struct SN76496 PSG;
		size_t PSG_events_cnt;
		uint8_t control_reg;
	} m_s;

	bool m_enabled;
	std::mutex m_DAC_lock;
	std::mutex m_PSG_lock;
	int m_DAC_empty_samples;
	//the DAC frequency value is written by the machine and read by the mixer
	std::atomic<int> m_DAC_freq;
	std::vector<uint8_t> m_DAC_samples;
	std::shared_ptr<MixerChannel> m_DAC_channel;
	std::shared_ptr<MixerChannel> m_PSG_channel;
	uint8_t m_DAC_last_value;
	int m_PSG_rate;
	double m_PSG_samples_per_ns;
	uint64_t m_PSG_last_mtime;
	VGMFile m_PSG_vgm;

	void FIFO_timer();
	void raise_interrupt();
	void lower_interrupt();
	int create_DAC_samples(int _mix_slice, bool _prebuf, bool _first_upd);
	int create_PSG_samples(int _mix_slice_us, bool _prebuf, bool _first_upd);
	void PSG_activate();
	int generate_PSG_samples(uint64_t _duration);
	void on_PSG_capture(bool _enable);

public:
	PS1Audio();
	~PS1Audio();

	void init();
	void reset(unsigned _type);
	void power_off();
	void config_changed();
	uint16_t read(uint16_t _address, unsigned _io_len);
	void write(uint16_t _address, uint16_t _value, unsigned _io_len);
	const char *get_name() { return "IBM PS/1 Audio/Joystick Card"; }

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
};

#endif
