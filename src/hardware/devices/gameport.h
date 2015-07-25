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

#ifndef IBMULATOR_HW_GAMEPORT_H
#define IBMULATOR_HW_GAMEPORT_H

#include "hardware/iodevice.h"

class GamePort;
extern GamePort g_gameport;

class GamePort : public IODevice
{
private:

  struct {
	  struct stick {
	  	float xpos, ypos;
	  	double x_us, y_us;
	  	bool button[2];
	  } stick[2];
  } m_s;  // state information

  std::mutex m_stick_lock;
  void joystick_motion(int _jid, int _axis, int _value);
  void joystick_button(int _jid, int _button, int _state);

public:
	GamePort();
	~GamePort();

	void init();
	void reset(unsigned type);
	void power_off();
	void config_changed();
	uint16_t read(uint16_t _address, unsigned _io_len);
	void write(uint16_t _address, uint16_t _value, unsigned _io_len);
	const char* get_name() { return "Game Port"; }

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
};

#endif
