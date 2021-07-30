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
	m_indicators.power = get_element("power");
	m_indicators.floppy_a = get_element("floppy_a");
	m_indicators.floppy_b = get_element("floppy_b");
	m_indicators.hdd = get_element("hdd");
	m_indicators.net = get_element("net");
	
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
	//Power
	if(m_machine->is_on() && m_status.power == LED::IDLE) {
		m_status.power = LED::ACTIVE;
		m_indicators.power->SetClassNames("active");
	} else if(!m_machine->is_on() && m_status.power == LED::ACTIVE) {
		m_status.power = LED::IDLE;
		m_indicators.power->SetClassNames("idle");
	}
	if(m_floppy) {
		bool motor;
		//Floppy A
		motor = m_floppy->is_motor_on(0);
		if(motor && m_status.floppy_a == LED::IDLE) {
			m_status.floppy_a = LED::ACTIVE;
			m_indicators.floppy_a->SetClassNames("active");
		} else if(!motor && m_status.floppy_a == LED::ACTIVE) {
			m_status.floppy_a = LED::IDLE;
			m_indicators.floppy_a->SetClassNames("idle");
		}
		//Floppy B
		motor = m_floppy->is_motor_on(1);
		if(motor && m_status.floppy_b == LED::IDLE) {
			m_status.floppy_b = LED::ACTIVE;
			m_indicators.floppy_b->SetClassNames("active");
		} else if(!motor && m_status.floppy_b == LED::ACTIVE) {
			m_status.floppy_b = LED::IDLE;
			m_indicators.floppy_b->SetClassNames("idle");
		}
	}
	if(m_hdd) {
		//HDD
		bool hdd_busy = m_hdd->is_busy();
		if(hdd_busy && m_status.hdd == LED::IDLE) {
			m_status.hdd = LED::ACTIVE;
			m_indicators.hdd->SetClassNames("active");
		} else if(!hdd_busy && m_status.hdd == LED::ACTIVE) {
			m_status.hdd = LED::IDLE;
			m_indicators.hdd->SetClassNames("idle");
		}
	}
	if(m_serial && m_serial->is_network_mode(0)) {
		if(m_serial->is_network_connected(0)) {
			bool is_rx = m_serial->is_network_rx_active(0);
			bool is_tx = m_serial->is_network_tx_active(0);
			if((is_rx||is_tx) && m_status.net != LED::ACTIVE) {
				m_indicators.net->SetClassNames("active");
				m_status.net = LED::ACTIVE;
			} else if(!(is_rx||is_tx) && m_status.net != LED::IDLE) {
				m_indicators.net->SetClassNames("idle");
				m_status.net = LED::IDLE;
			}
		} else if(m_status.net != LED::ERROR) {
			m_indicators.net->SetClassNames("error");
			m_status.net = LED::ERROR;
		}
	}
}

void Status::config_changed()
{
	m_floppy = m_machine->devices().device<FloppyCtrl>();
	m_hdd = m_machine->devices().device<StorageCtrl>();
	m_serial = m_machine->devices().device<Serial>();

	m_status.power = LED::IDLE;
	m_status.floppy_a = LED::IDLE;
	m_status.floppy_b = LED::IDLE;
	m_status.hdd = LED::IDLE;
	m_status.net = LED::HIDDEN;

	m_indicators.power->SetClassNames("idle");
	m_indicators.floppy_a->SetClassNames("idle");
	m_indicators.floppy_b->SetClassNames("idle");
	m_indicators.hdd->SetClassNames("idle");

	m_indicators.net->SetClassNames("hidden");
}

void Status::ProcessEvent(Rocket::Core::Event &)
{
	//Rocket::Core::Element * el = event.GetTargetElement();
}
