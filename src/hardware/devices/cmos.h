/*
 * 	Copyright (c) 2002-2012  The Bochs Project
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

#ifndef IBMULATOR_HW_CMOS_H
#define IBMULATOR_HW_CMOS_H

#include "hardware/iodevice.h"

#define CMOS_SIZE 64

class CMOS;
extern CMOS g_cmos;

class CMOS : public IODevice
{
private:

  struct {
    uint32_t periodic_interval_usec;
    time_t   timeval;
    uint8_t  cmos_mem_address;
    bool     timeval_change;
    bool     rtc_mode_12hour;
    bool     rtc_mode_binary;
    bool     rtc_sync;
    uint8_t  reg[CMOS_SIZE];
  } m_s;  // state information

  int m_periodic_timer_index;
  int m_one_second_timer_index;
  int m_uip_timer_index; //Update in Progress timer

  void update_clock(void);
  void update_timeval(void);
  void CRA_change(void);

public:

	CMOS();
	~CMOS();

	void init();
	uint16_t read(uint16_t _address, unsigned _io_len);
	void write(uint16_t _address, uint16_t _value, unsigned _io_len);
	const char* get_name() { return "CMOS"; }

	void periodic_timer();
	void one_second_timer();
	void uip_timer();

	void reset(unsigned type);
	void power_off();
	void config_changed();

	void save_image();

	uint8_t get_reg(uint8_t reg) {
		return m_s.reg[reg];
	}
	void set_reg(uint8_t reg, uint8_t val) {
		m_s.reg[reg] = val;
	}
	time_t get_timeval() {
		return m_s.timeval;
	}

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
};

#endif
