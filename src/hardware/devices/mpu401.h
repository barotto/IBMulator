/*
 * Copyright (C) 2002-2020  The DOSBox Team
 * Copyright (C) 2020  Marco Bortolin
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

#ifndef IBMULATOR_HW_MPU401_H
#define IBMULATOR_HW_MPU401_H

#include "hardware/iodevice.h"

#define MPU401_QUEUE_SIZE 32

class MPU401 : public IODevice
{
	IODEVICE(MPU401, "MPU-401");

	enum Mode {
		INTELLIGENT,
		UART
	};
	
	enum DataType {
		OVERFLOW, MARK, MIDI_SYS, MIDI_NORM, COMMAND
	};
	
	unsigned m_iobase;
	unsigned m_irq;
	Mode m_req_mode;
	
	struct {
		Mode mode;
		uint8_t  queue[MPU401_QUEUE_SIZE];
		unsigned queue_pos;
		unsigned queue_used;
		struct track {
			int counter;
			uint8_t value[8];
			uint8_t sys_val;
			uint8_t vlength;
			uint8_t length;
			DataType type;
		} playbuf[8], condbuf;
		struct {
			bool conductor;
			bool cond_req;
			bool cond_set;
			bool block_ack;
			bool playing;
			bool reset;
			bool wsd;
			bool wsm;
			bool wsd_start;
			bool irq_pending;
			bool send_now;
			bool eoi_scheduled;
			int data_onoff;
			unsigned command_byte;
			unsigned cmd_pending;
			uint8_t tmask;
			uint8_t cmask;
			uint8_t amask;
			uint16_t midi_mask;
			uint16_t req_mask;
			uint8_t channel;
			uint8_t old_chan;
		} state;
		struct {
			uint8_t timebase;
			uint8_t tempo, tempo_rel, tempo_grad;
			uint8_t cth_rate, cth_counter, cth_savecount;
			bool clock_to_host;
		} clock;
	} m_s;
	
	// TODO: these 3 timers can probably be consolidated into 1
	// but that would require some major refactoring
	int m_eoi_timer;
	int m_event_timer;
	int m_reset_timer;
	
public:
	MPU401(Devices *_dev);
	~MPU401();
	
	virtual void install();
	virtual void remove();
	
	void reset(unsigned _type);
	void power_off();
	void config_changed();
	uint16_t read(uint16_t _address, unsigned _io_len);
	void write(uint16_t _address, uint16_t _value, unsigned _io_len);
	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
	
private:
	void raise_interrupt();
	void lower_interrupt();
	void clear_queue();
	void write_command(unsigned _val);
	void write_data(unsigned _val);
	void start_event_timer();
	void stop_event_timer();
	void event_timer(uint64_t);
	void start_eoi_timer();
	void eoi_timer(uint64_t);
	void reset_timer(uint64_t);
	void update_track(uint8_t chan);
	void update_conductor();
	void queue_byte(uint8_t _data);
	void intelligent_out(uint8_t _chan);
};

#endif