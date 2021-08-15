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

#include <RmlUi/Core.h>
#include <sstream>

#include "hardware/devices/floppy.h"
#include "hardware/devices/storagectrl.h"
#include "hardware/devices/serial.h"

Status::Status(GUI * _gui, Machine *_machine)
:
Window(_gui, "status.rml"),
m_machine(_machine)
{
}

Status::~Status()
{
}

void Status::create()
{
	Window::create();

	m_indicators[IND::PWR   ].el = get_element("power");
	m_indicators[IND::FLP_A ].el = get_element("floppy_a");
	m_indicators[IND::FLP_B ].el = get_element("floppy_b");
	m_indicators[IND::HDD   ].el = get_element("hdd");
	m_indicators[IND::NET   ].el = get_element("net");
	m_indicators[IND::AUDREC].el = get_element("audrec");
	m_indicators[IND::VIDREC].el = get_element("vidrec");
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
		m_indicators[IND::PWR].set(LED::ACTIVE);
	} else {
		m_indicators[IND::PWR].set(LED::IDLE);
	}

	//Floppy A
	if(m_floppy && !m_indicators[IND::FLP_A].is(LED::HIDDEN)) {
		if(m_floppy->is_motor_on(0)) {
			m_indicators[IND::FLP_A].set(LED::ACTIVE);
		} else {
			m_indicators[IND::FLP_A].set(LED::IDLE);
		}
	}
	//Floppy B
	if(m_floppy && !m_indicators[IND::FLP_B].is(LED::HIDDEN)) {
		if(m_floppy->is_motor_on(1)) {
			m_indicators[IND::FLP_B].set(LED::ACTIVE);
		} else {
			m_indicators[IND::FLP_B].set(LED::IDLE);
		}
	}

	//HDD
	if(m_hdd && !m_indicators[IND::HDD].is(LED::HIDDEN)) {
		if(m_hdd->is_busy()) {
			m_indicators[IND::HDD].set(LED::ACTIVE);
		} else {
			m_indicators[IND::HDD].set(LED::IDLE);
		}
	}

	//Network
	if(m_serial && m_serial->is_network_mode(0)) {
		if(m_serial->is_network_connected(0)) {
			if(m_serial->is_network_rx_active(0) || m_serial->is_network_tx_active(0)) {
				m_indicators[IND::NET].set(LED::ACTIVE);
			} else {
				m_indicators[IND::NET].set(LED::IDLE);
			}
		} else  {
			m_indicators[IND::NET].set(LED::ATTN);
		}
	}

	//Audio & Video recording
	if(m_gui->is_audio_recording()) {
		m_indicators[IND::AUDREC].set(LED::ACTIVE);
	} else {
		m_indicators[IND::AUDREC].set(LED::IDLE);
	}
	if(m_gui->is_video_recording()) {
		m_indicators[IND::VIDREC].set(LED::ACTIVE);
	} else {
		m_indicators[IND::VIDREC].set(LED::IDLE);
	}
}

void Status::config_changed()
{
	m_floppy = m_machine->devices().device<FloppyCtrl>();
	m_hdd = m_machine->devices().device<StorageCtrl>();
	m_serial = m_machine->devices().device<Serial>();

	if(m_hdd) {
		m_indicators[IND::HDD].set(LED::IDLE);
	} else {
		m_indicators[IND::HDD].set(LED::HIDDEN);
	}

	if(m_floppy && m_floppy->drive_type(0)) {
		m_indicators[IND::FLP_A].set(LED::IDLE);
	} else {
		m_indicators[IND::FLP_A].set(LED::HIDDEN);
	}

	if(m_floppy && m_floppy->drive_type(1)) {
		m_indicators[IND::FLP_B].set(LED::IDLE);
	} else {
		m_indicators[IND::FLP_B].set(LED::HIDDEN);
	}

	if(m_serial && m_serial->is_network_mode(0)) {
		m_indicators[IND::NET].set(LED::ATTN);
	} else {
		m_indicators[IND::NET].set(LED::HIDDEN);
	}
}
