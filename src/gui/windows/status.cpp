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
	m_indicators.power.el = get_element("power");
	m_indicators.floppy_a.el = get_element("floppy_a");
	m_indicators.floppy_b.el = get_element("floppy_b");
	m_indicators.hdd.el = get_element("hdd");
	m_indicators.net.el = get_element("net");

	m_machine = _machine;
	m_floppy = nullptr;
	m_hdd = nullptr;
	m_serial = nullptr;
}

Status::~Status()
{
}

void Status::Indicator::set(Status::LED _s)
{
	if(status != _s) {
		status = _s;
		el->SetClassNames(cls[ec_to_i(_s)]);
	}
}

void Status::update()
{
	//Power
	if(m_machine->is_on()) {
		m_indicators.power.set(LED::ACTIVE);
	} else {
		m_indicators.power.set(LED::IDLE);
	}

	//Floppy A
	if(m_floppy && !m_indicators.floppy_a.is(LED::HIDDEN)) {
		if(m_floppy->is_motor_on(0)) {
			m_indicators.floppy_a.set(LED::ACTIVE);
		} else {
			m_indicators.floppy_a.set(LED::IDLE);
		}
	}
	//Floppy B
	if(m_floppy && !m_indicators.floppy_b.is(LED::HIDDEN)) {
		if(m_floppy->is_motor_on(1)) {
			m_indicators.floppy_b.set(LED::ACTIVE);
		} else {
			m_indicators.floppy_b.set(LED::IDLE);
		}
	}

	//HDD
	if(m_hdd && !m_indicators.hdd.is(LED::HIDDEN)) {
		if(m_hdd->is_busy()) {
			m_indicators.hdd.set(LED::ACTIVE);
		} else {
			m_indicators.hdd.set(LED::IDLE);
		}
	}
	
	if(m_serial && m_serial->is_network_mode(0)) {
		if(m_serial->is_network_connected(0)) {
			if(m_serial->is_network_rx_active(0) || m_serial->is_network_tx_active(0)) {
				m_indicators.net.set(LED::ACTIVE);
			} else {
				m_indicators.net.set(LED::IDLE);
			}
		} else  {
			m_indicators.net.set(LED::ERROR);
		}
	}
}

void Status::config_changed()
{
	m_floppy = m_machine->devices().device<FloppyCtrl>();
	m_hdd = m_machine->devices().device<StorageCtrl>();
	m_serial = m_machine->devices().device<Serial>();

	m_indicators.power.set(LED::IDLE);

	if(m_hdd) {
		m_indicators.hdd.set(LED::IDLE);
	} else {
		m_indicators.hdd.set(LED::HIDDEN);
	}

	if(m_floppy && m_floppy->drive_type(0)) {
		m_indicators.floppy_a.set(LED::IDLE);
	} else {
		m_indicators.floppy_a.set(LED::HIDDEN);
	}

	if(m_floppy && m_floppy->drive_type(1)) {
		m_indicators.floppy_b.set(LED::IDLE);
	} else {
		m_indicators.floppy_b.set(LED::HIDDEN);
	}

	if(m_serial && m_serial->is_network_mode(0)) {
		m_indicators.net.set(LED::ERROR);
	} else {
		m_indicators.net.set(LED::HIDDEN);
	}
}

void Status::ProcessEvent(Rocket::Core::Event &)
{
	//Rocket::Core::Element * el = event.GetTargetElement();
}
