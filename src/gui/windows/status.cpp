/*
 * Copyright (C) 2015-2021  Marco Bortolin
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
#include "gui.h"
#include "status.h"

#include <Rocket/Core.h>
#include <sstream>

#include "hardware/devices/floppy.h"
#include "hardware/devices/storagectrl.h"
#include "hardware/devices/serial.h"

Status::Status(GUI * _gui, Machine *_machine)
:
Window(_gui, "status.rml")
{
	assert(m_wnd);
	m_status.power_led = get_element("power_led");
	m_status.floppy_a_led = get_element("floppy_a_led");
	m_status.floppy_b_led = get_element("floppy_b_led");
	m_status.hdd_led = get_element("hdd_led");
	m_status.net_led = get_element("net_led");
	
	m_machine = _machine;
	m_floppy = nullptr;
	m_hdd = nullptr;
	m_serial = nullptr;
}

Status::~Status()
{
}

void Status::update()
{
	bool motor;

	//Power led
	if(m_machine->is_on() && m_leds.power==false) {
		m_leds.power = true;
		m_status.power_led->SetClass("led_active", true);
	} else if(!m_machine->is_on() && m_leds.power==true) {
		m_leds.power = false;
		m_status.power_led->SetClass("led_active", false);
	}
	if(m_floppy) {
		//Floppy A
		motor = m_floppy->is_motor_on(0);
		if(motor && m_leds.floppy_a==false) {
			m_leds.floppy_a = true;
			m_status.floppy_a_led->SetClass("led_active", true);
		} else if(!motor && m_leds.floppy_a==true) {
			m_leds.floppy_a = false;
			m_status.floppy_a_led->SetClass("led_active", false);
		}
		//Floppy B
		motor = m_floppy->is_motor_on(1);
		if(motor && m_leds.floppy_b==false) {
			m_leds.floppy_b = true;
			m_status.floppy_b_led->SetClass("led_active", true);
		} else if(!motor && m_leds.floppy_b==true) {
			m_leds.floppy_b = false;
			m_status.floppy_b_led->SetClass("led_active", false);
		}
	}
	if(m_hdd) {
		//HDD
		bool hdd_busy = m_hdd->is_busy();
		if(hdd_busy && m_leds.hdd==false) {
			m_leds.hdd = true;
			m_status.hdd_led->SetClass("led_active", true);
		} else if(!hdd_busy && m_leds.hdd==true) {
			m_leds.hdd = false;
			m_status.hdd_led->SetClass("led_active", false);
		}
	}
	if(m_serial && m_serial->is_network_mode(0)) {
		if(m_serial->is_network_connected(0)) {
			bool is_rx = m_serial->is_network_rx_active(0);
			bool is_tx = m_serial->is_network_tx_active(0);
			if((is_rx||is_tx) && m_leds.net != LEDStatus::LED_ACTIVE) {
				m_status.net_led->SetClassNames("led led_active");
				m_leds.net = LEDStatus::LED_ACTIVE;
			} else if(!(is_rx||is_tx) && m_leds.net != LEDStatus::LED_INACTIVE) {
				m_status.net_led->SetClassNames("led led_inactive");
				m_leds.net = LEDStatus::LED_INACTIVE;
			}
		} else if(m_leds.net != LEDStatus::LED_ERROR) {
			m_status.net_led->SetClassNames("led led_error");
			m_leds.net = LEDStatus::LED_ERROR;
		}
	}
}

void Status::config_changed()
{
	m_floppy = m_machine->devices().device<FloppyCtrl>();
	m_hdd = m_machine->devices().device<StorageCtrl>();
	m_serial = m_machine->devices().device<Serial>();
	m_leds.power = false;
	m_leds.floppy_a = false;
	m_leds.floppy_b = false;
	m_leds.hdd = false;
	m_leds.net = LEDStatus::LED_HIDDEN;

	m_status.power_led->SetClass("led_active", false);
	m_status.floppy_a_led->SetClass("led_active", false);
	m_status.floppy_b_led->SetClass("led_active", false);
	m_status.hdd_led->SetClass("led_active", false);

	m_status.net_led->SetClassNames("led");
}

void Status::ProcessEvent(Rocket::Core::Event &)
{
	//Rocket::Core::Element * el = event.GetTargetElement();
}
