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

#include "ibmulator.h"
#include "machine.h"
#include "hardware/devices.h"
#include "hardware/devices/gameport.h"
#include <cstring>
#include <functional>
using namespace std::placeholders;

#define OHMS 60000.0

IODEVICE_PORTS(GamePort) = {
	{ 0x201, 0x201, PORT_8BIT|PORT_RW }
};

GamePort::GamePort(Devices *_dev)
: IODevice(_dev)
{
	memset(&m_s, 0, sizeof(m_s));
}

GamePort::~GamePort()
{
}

void GamePort::install()
{
	IODevice::install();

	g_machine.register_joystick_fun(
		std::bind(&GamePort::joystick_motion, this, _1, _2, _3),
		std::bind(&GamePort::joystick_button, this, _1, _2, _3)
	);
	
	PINFOF(LOG_V0, LOG_GAMEPORT, "Installed Game Port\n");
}

void GamePort::remove()
{
	g_machine.register_joystick_fun(nullptr,nullptr);
}

void GamePort::reset(unsigned)
{
	memset(&m_s, 0, sizeof(m_s));
}

void GamePort::power_off()
{
}

void GamePort::config_changed()
{
}

void GamePort::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_GAMEPORT, "GamePort: saving state\n");
	std::lock_guard<std::mutex> lock(m_stick_lock);
	StateHeader h;
	h.name = name();
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);
}

void GamePort::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_GAMEPORT, "GamePort: restoring state\n");
	std::lock_guard<std::mutex> lock(m_stick_lock);
	StateHeader h;
	h.name = name();
	h.data_size = sizeof(m_s);
	_state.read(&m_s,h);
}

uint16_t GamePort::read(uint16_t _address, unsigned)
{
	if(_address != 0x201) {
		PERRF(LOG_GAMEPORT, "unhandled read from port %0x04X!\n", _address);
		return ~0;
	}

	uint8_t value = 0xff;
	double now_us = g_machine.get_virt_time_us();

	if(m_s.stick[0].x_us < now_us) {
		value &= ~1;
	}
	if(m_s.stick[0].y_us < now_us) {
		value &= ~2;
	}
	if(m_s.stick[1].x_us < now_us) {
		value &= ~4;
	}
	if(m_s.stick[1].y_us < now_us) {
		value &= ~8;
	}

	std::lock_guard<std::mutex> lock(m_stick_lock);
	if(m_s.stick[0].button[0]) {
		value &= ~16;
	}
	if(m_s.stick[0].button[1]) {
		value &= ~32;
	}
	if(m_s.stick[1].button[0]) {
		value &= ~64;
	}
	if(m_s.stick[1].button[1]) {
		value &= ~128;
	}

	PDEBUGF(LOG_V2, LOG_GAMEPORT, "read from port 201h -> 0x%02X\n", value);

	return uint16_t(value);
}

void GamePort::write(uint16_t _address, uint16_t _value, unsigned)
{
	if(_address != 0x201) {
		PERRF(LOG_GAMEPORT, "unhandled write to port 0x%04X!\n", _address);
		return;
	}
	uint8_t value = _value & 0xFF;
	PDEBUGF(LOG_V2, LOG_GAMEPORT, "write to port 201h <- 0x%02X\n", value);
	std::lock_guard<std::mutex> lock(m_stick_lock);
	/*
	 * A write to port 201 causes all stick inputs to go high for a value specified by
	 * the equation TIME = 24.2 microseconds + ( 0.011 microseconds/ohm * resistance )
	 */
	double now_us = g_machine.get_virt_time_us();
	m_s.stick[0].x_us = now_us + (24.2 + 0.011 * (m_s.stick[0].xpos + 1.0) * OHMS);
	m_s.stick[0].y_us = now_us + (24.2 + 0.011 * (m_s.stick[0].ypos + 1.0) * OHMS);
	m_s.stick[1].x_us = now_us + (24.2 + 0.011 * (m_s.stick[1].xpos + 1.0) * OHMS);
	m_s.stick[1].y_us = now_us + (24.2 + 0.011 * (m_s.stick[1].ypos + 1.0) * OHMS);
}

void GamePort::joystick_motion(int _jid, int _axis, int _value)
{
	if(_jid > 1) {
		PDEBUGF(LOG_V0, LOG_GAMEPORT, "Invalid joystick id %d\n", _jid);
		return;
	}
	
	PDEBUGF(LOG_V2, LOG_GAMEPORT, "Joystick %s: axis %d = %d\n", _jid?"B":"A", _axis, _value);
	
	std::lock_guard<std::mutex> lock(m_stick_lock);
	float value = float(_value) / 32768.f;
	if(_axis == 0) {
		m_s.stick[_jid].xpos = value;
	} else {
		m_s.stick[_jid].ypos = value;
	}
}

void GamePort::joystick_button(int _jid, int _button, int _state)
{
	if(_jid > 1) {
		PDEBUGF(LOG_V0, LOG_GAMEPORT, "Invalid joystick id %d\n", _jid);
		return;
	}
	if(_button > 1) {
		PDEBUGF(LOG_V0, LOG_GAMEPORT, "Invalid button id %d\n", _button);
		return;
	}
	
	PDEBUGF(LOG_V2, LOG_GAMEPORT, "Joystick %s: button %d = %d\n", _jid?"B":"A", _button, _state);
	
	std::lock_guard<std::mutex> lock(m_stick_lock);
	m_s.stick[_jid].button[_button] = _state;
}
