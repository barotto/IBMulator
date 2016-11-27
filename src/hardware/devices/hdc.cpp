/*
 * Copyright (C) 2016  Marco Bortolin
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
#include "hdc.h"
#include "hddparams.h"
#include "hardware/devices.h"
#include <cstring>


IODEVICE_PORTS(HardDiskCtrl) = {};


HardDiskCtrl::HardDiskCtrl(Devices *_dev)
: IODevice(_dev)
{
}

HardDiskCtrl::~HardDiskCtrl()
{
}

void HardDiskCtrl::install()
{
	IODevice::install();

	m_disk.install();

	if(m_disk.type() == HDD_CUSTOM_DRIVE_IDX) {
		HDDParams params;
		params.cylinders  = m_disk.geometry().cylinders;
		params.heads      = m_disk.geometry().heads;
		params.rwcyl      = 0;
		params.wpcyl      = m_disk.geometry().wpcomp;
		params.ECClen     = 0;
		params.options    = (m_disk.geometry().heads>8 ? 0x08 : 0);
		params.timeoutstd = 0;
		params.timeoutfmt = 0;
		params.timeoutchk = 0;
		params.lzone      = m_disk.geometry().lzone;
		params.sectors    = m_disk.geometry().spt;
		params.reserved   = 0;
		g_machine.sys_rom().inject_custom_hdd_params(HDC_CUSTOM_BIOS_IDX, params);
	}
}

void HardDiskCtrl::remove()
{
	m_disk.remove();
	IODevice::remove();
}

void HardDiskCtrl::reset(unsigned _type)
{
	if(_type == MACHINE_POWER_ON) {
		m_disk.power_on(g_machine.get_virt_time_us());
	}
}

void HardDiskCtrl::power_off()
{
	m_disk.power_off();
}

void HardDiskCtrl::config_changed()
{
	m_disk.config_changed();
}

void HardDiskCtrl::save_state(StateBuf &_state)
{
	m_disk.save_state(_state);
}

void HardDiskCtrl::restore_state(StateBuf &_state)
{
	m_disk.restore_state(_state);
}

uint16_t HardDiskCtrl::read(uint16_t, unsigned)
{
	return ~0;
}

void HardDiskCtrl::write(uint16_t, uint16_t, unsigned)
{
	return;
}
