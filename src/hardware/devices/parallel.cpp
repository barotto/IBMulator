/*
 * Copyright (C) 2001-2014  The Bochs Project
 * Copyright (C) 2015-2020  Marco Bortolin
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

// This code was just a few stubs until Volker.Ruppert@t-online.de
// fixed it up in November 2001.

#include "ibmulator.h"
#include "program.h"
#include "machine.h"
#include "hardware/devices.h"
#include "hardware/devices/pic.h"
#include "parallel.h"
#include <cstring>

#define LPT_MAXDEV 3
#define LPT_DATA   0
#define LPT_STAT   1
#define LPT_CTRL   2

IODEVICE_PORTS(Parallel) = {
	{ 0x3BC, 0x3BE, PORT_8BIT|PORT_RW },
	{ 0x378, 0x37A, PORT_8BIT|PORT_RW },
	{ 0x278, 0x27A, PORT_8BIT|PORT_RW }
};
uint16_t Parallel::ms_irqs[3]  = {7, 7, 5};


std::map<std::string, uint> Parallel::ms_lpt_ports = {
	{ "LPT1", 0 },
	{ "LPT2", 1 },
	{ "LPT3", 2 }
};

Parallel::Parallel(Devices *_dev)
: IODevice(_dev)
{
	memset(&m_s, 0, sizeof(parport_t));
}

Parallel::~Parallel()
{
	if(m_s.output != nullptr) {
		fclose(m_s.output);
	}
}

void Parallel::install(void)
{
	//on the PS/1 there's only 1 port and its address assignment is
	//controlled by POS register 2.
	// don't install the ioports, POS will take care of that
	m_enabled = false; // POS determines the general state
	m_s.port = 0xFF;   // POS will set the port
	m_s.mode = 0xFF;

	// virtual_printer() opens output file on demand
}

void Parallel::remove()
{
	if(m_s.port < 3) {
		IODevice::remove(&ioports()->at(m_s.port), 1);
		g_machine.unregister_irq(ms_irqs[m_s.port], name());
		m_s.port = 0xFF;
	}
}

void Parallel::reset(unsigned)
{
	/* internal state */
	m_s.STATUS.error = 1;
	m_s.STATUS.slct  = 1;
	m_s.STATUS.pe    = 0;
	m_s.STATUS.ack   = 1;
	m_s.STATUS.busy  = 1;

	m_s.CONTROL.strobe   = 0;
	m_s.CONTROL.autofeed = 0;
	m_s.CONTROL.init     = 1;
	m_s.CONTROL.slct_in  = 1;
	m_s.CONTROL.irq      = 0;
	m_s.CONTROL.input    = 0;

	m_s.initmode = 0;
}

void Parallel::config_changed()
{
	if(m_s.output != nullptr) {
		fclose(m_s.output);
		m_s.output = nullptr;
	}
}

void Parallel::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_LPT, "saving state\n");

	StateHeader h;
	h.name = name();
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);
}

void Parallel::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_LPT, "restoring state\n");

	StateHeader h;
	h.name = name();
	h.data_size = sizeof(m_s);
	_state.read(&m_s,h);

	m_s.output = nullptr;
}

void Parallel::set_mode(uint8_t _mode)
{
	if(_mode != m_s.mode) {
		if(_mode == PARPORT_EXTENDED) {
			PINFOF(LOG_V1, LOG_LPT, "Parallel mode EXTENDED\n");
		} else {
			PINFOF(LOG_V1, LOG_LPT, "Parallel mode COMPATIBLE\n");
		}
		m_s.mode = _mode;
	}
}

void Parallel::set_port(uint8_t _port)
{
	_port %= 3;

	if(m_s.port == _port) {
		return;
	}

	char pname[5];
	sprintf(pname, "LPT%d", _port+1);

	if(m_s.port < 3) {
		IODevice::remove(&ioports()->at(m_s.port), 1);
		g_machine.unregister_irq(ms_irqs[m_s.port], name());
	}

	m_s.port = _port;
	IODevice::install(&ioports()->at(m_s.port), 1);
	g_machine.register_irq(ms_irqs[m_s.port], name());

	g_program.config().set_string(LPT_SECTION, LPT_PORT, pname);

	PINFOF(LOG_V0, LOG_LPT, "Parallel port at 0x%04X (%s), irq %d, mode %s\n",
			ioports()->at(m_s.port).from, pname, ms_irqs[m_s.port],
			m_s.mode==PARPORT_COMPATIBLE?"COMPATIBLE":"EXTENDED");
}

void Parallel::set_enabled(bool _enabled)
{
	if(_enabled != m_enabled) {
		PINFOF(LOG_V1, LOG_LPT, "Parallel port %s\n", _enabled?"ENABLED":"DISABLED");
		m_enabled = _enabled;
		if(_enabled) {
			reset(DEVICE_SOFT_RESET);
		}
	}
}

void Parallel::virtual_printer()
{
	if(!m_enabled) {
		return;
	}

	if(m_s.output == nullptr) {
		std::string filename = g_program.config().get_file(LPT_SECTION, LPT_FILE, FILE_TYPE_USER);
		if(!filename.empty()) {
			m_s.output = fopen(filename.c_str(), "wb");
			if(!m_s.output) {
				PERRF(LOG_LPT, "Could not open '%s' to write output\n", filename.c_str());
			}
		}
	}
	if(m_s.mode == PARPORT_EXTENDED) {
		if(m_s.STATUS.slct) {
			if(m_s.output != nullptr) {
				fputc(m_s.data, m_s.output);
				fflush (m_s.output);
			}
			if(m_s.CONTROL.irq == 1) {
				m_devices->pic()->raise_irq(ms_irqs[m_s.port]);
			}
			m_s.STATUS.ack = 0;
			m_s.STATUS.busy = 1;
		} else {
			PWARNF(LOG_LPT, "printer is offline\n");
		}
	} else {
		if(m_s.output != nullptr) {
			fputc(m_s.data, m_s.output);
			fflush(m_s.output);
		}
	}
}

uint16_t Parallel::read(uint16_t address, unsigned /*io_len*/)
{
	uint16_t retval = 0xFF;

	address = address - ioports()->at(m_s.port).from;

	switch(address) {
		case LPT_DATA:
			if(m_enabled) {
				if(m_s.mode == PARPORT_EXTENDED) {
					if(!m_s.CONTROL.input) {
						retval = m_s.data;
					} else {
						PWARNF(LOG_LPT, "read: input mode not supported\n");
					}
				} else {
					retval = m_s.data;
				}
			}
			break;
		case LPT_STAT: {
			if(m_enabled) {
				retval = ((m_s.STATUS.busy<< 7) |
						(m_s.STATUS.ack   << 6) |
						(m_s.STATUS.pe    << 5) |
						(m_s.STATUS.slct  << 4) |
						(m_s.STATUS.error << 3));
				if(m_s.STATUS.ack == 0) {
					m_s.STATUS.ack = 1;
					if(m_s.CONTROL.irq == 1) {
						m_devices->pic()->lower_irq(ms_irqs[m_s.port]);
					}
				}
				if(m_s.initmode == 1) {
					m_s.STATUS.busy  = 1;
					m_s.STATUS.slct  = 1;
					m_s.STATUS.ack  = 0;
					if(m_s.CONTROL.irq == 1) {
						m_devices->pic()->raise_irq(ms_irqs[m_s.port]);
					}
					m_s.initmode = 0;
				}
				PDEBUGF(LOG_V2, LOG_LPT, "read: status register returns 0x%02x\n", retval);
			}
			break;
		}
		case LPT_CTRL: {
			if(m_enabled) {
				retval = ((m_s.CONTROL.input  << 5) |
						(m_s.CONTROL.irq      << 4) |
						(m_s.CONTROL.slct_in  << 3) |
						(m_s.CONTROL.init     << 2) |
						(m_s.CONTROL.autofeed << 1) |
						(m_s.CONTROL.strobe));
				PDEBUGF(LOG_V2, LOG_LPT, "read: parport%d control register returns 0x%02x", retval);
			}
			break;
		}
		default:
			break;
	}
	return retval;
}

void Parallel::write(uint16_t address, uint16_t value, unsigned /*io_len*/)
{
	address = address - ioports()->at(m_s.port).from;

	switch(address) {
		case LPT_DATA:
			m_s.data = (uint8_t)value;
			PDEBUGF(LOG_V2, LOG_LPT, "write: data output register = 0x%02x\n", (uint8_t)value);
			if(m_s.mode == PARPORT_COMPATIBLE) {
				virtual_printer();
			}
			break;
		case LPT_CTRL: {
			if((value & 0x01) == 0x01) {
				if(m_s.CONTROL.strobe == 0) {
					m_s.CONTROL.strobe = 1;
					virtual_printer(); // data is valid now
				}
			} else {
				if(m_s.CONTROL.strobe == 1) {
					m_s.CONTROL.strobe = 0;
				}
			}
			m_s.CONTROL.autofeed = ((value & 0x02) == 0x02);
			if((value & 0x04) == 0x04) {
				if(m_s.CONTROL.init == 0) {
					m_s.CONTROL.init = 1;
					m_s.STATUS.busy  = 0;
					m_s.STATUS.slct  = 0;
					m_s.initmode = 1;
					PDEBUGF(LOG_V2, LOG_LPT, "printer init requested\n");
				}
			} else {
				if(m_s.CONTROL.init == 1) {
					m_s.CONTROL.init = 0;
				}
			}
			if((value & 0x08) == 0x08) {
				if(m_s.CONTROL.slct_in == 0) {
					m_s.CONTROL.slct_in = 1;
					PDEBUGF(LOG_V2, LOG_LPT, "printer now online\n");
				}
			} else {
				if(m_s.CONTROL.slct_in == 1) {
					m_s.CONTROL.slct_in = 0;
					PDEBUGF(LOG_V2, LOG_LPT, "printer now offline\n");
				}
			}
			m_s.STATUS.slct = m_s.CONTROL.slct_in;
			if((value & 0x10) == 0x10) {
				if(m_s.CONTROL.irq == 0) {
					m_s.CONTROL.irq = 1;
					g_machine.register_irq(ms_irqs[m_s.port], name());
					PDEBUGF(LOG_V2, LOG_LPT, "irq mode selected\n");
				}
			} else {
				if(m_s.CONTROL.irq == 1) {
					m_s.CONTROL.irq = 0;
					g_machine.unregister_irq(ms_irqs[m_s.port], name());
					PDEBUGF(LOG_V2, LOG_LPT, "polling mode selected\n");
				}
			}
			if((value & 0x20) == 0x20) {
				if(m_s.CONTROL.input == 0) {
					m_s.CONTROL.input = 1;
					PDEBUGF(LOG_V2, LOG_LPT, "data input mode selected\n");
				}
			} else {
				if(m_s.CONTROL.input == 1) {
					m_s.CONTROL.input = 0;
					PDEBUGF(LOG_V2, LOG_LPT, "data output mode selected\n");
				}
			}
			if((value & 0xC0) > 0) {
				PDEBUGF(LOG_V0, LOG_LPT, "write: unsupported control bit ignored\n");
			}
			break;
		}
		default:
			break;
	}
}
