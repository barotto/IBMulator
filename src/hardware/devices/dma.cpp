/*
 * Copyright (C) 2002-2014  The Bochs Project
 * Copyright (C) 2015, 2016  Marco Bortolin
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
#include "dma.h"
#include "hardware/devices.h"
#include "hardware/cpu.h"
#include <cstring>


#define DMA_MODE_DEMAND  0
#define DMA_MODE_SINGLE  1
#define DMA_MODE_BLOCK   2
#define DMA_MODE_CASCADE 3

IODEVICE_PORTS(DMA) = {
	{ 0x00, 0x0F, PORT_8BIT|PORT_RW },
	{ 0x80, 0x8F, PORT_8BIT|PORT_RW },
	{ 0xC0, 0xDE, PORT_8BIT|PORT_RW }
};

DMA::DMA(Devices* _dev)
: IODevice(_dev)
{

}

DMA::~DMA()
{
}

void DMA::install()
{
	IODevice::install();
	for(int i=0; i<8; i++) {
		m_channels[i].used = false;
		m_channels[i].device = "";
	}
	m_channels[4].used = true;
	m_channels[4].device = "cascade";
	PDEBUGF(LOG_V2, LOG_DMA, "channel 4 used by cascade\n");
}

void DMA::config_changed()
{
	//nothing to do.
	//no config dependent params
}

void DMA::reset(unsigned type)
{
	if(type==MACHINE_POWER_ON) {

		memset(&m_s, 0, sizeof(m_s));

		//everything is memsetted to 0, so is the following code relevant?
		unsigned c, i, j;
		for(i=0; i < 2; i++) {
			for(j=0; j < 4; j++) {
				m_s.dma[i].DRQ[j] = 0;
				m_s.dma[i].DACK[j] = 0;
			}
		}
		m_s.HLDA = false;
		m_s.TC = false;

		for(i=0; i<2; i++) {
			for(c=0; c<4; c++) {
				m_s.dma[i].chan[c].mode.mode_type = 0;         // demand mode
				m_s.dma[i].chan[c].mode.address_decrement = 0; // address increment
				m_s.dma[i].chan[c].mode.autoinit_enable = 0;   // autoinit disable
				m_s.dma[i].chan[c].mode.transfer_type = 0;     // verify
				m_s.dma[i].chan[c].base_address = 0;
				m_s.dma[i].chan[c].current_address = 0;
				m_s.dma[i].chan[c].base_count = 0;
				m_s.dma[i].chan[c].current_count = 0;
				m_s.dma[i].chan[c].page_reg = 0;
			}
		}
	}

	//HARD reset and POWER ON
	reset_controller(0);
	reset_controller(1);
}

void DMA::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_DMA, "saving state\n");

	StateHeader h;
	h.name = name();
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);
}

void DMA::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_DMA, "restoring state\n");

	StateHeader h;
	h.name = name();
	h.data_size = sizeof(m_s);
	_state.read(&m_s,h);
}

void DMA::reset_controller(unsigned num)
{
	m_s.dma[num].mask[0] = 1;
	m_s.dma[num].mask[1] = 1;
	m_s.dma[num].mask[2] = 1;
	m_s.dma[num].mask[3] = 1;
	m_s.dma[num].ctrl_disabled = 0;
	m_s.dma[num].command_reg = 0;
	m_s.dma[num].status_reg = 0;
	m_s.dma[num].flip_flop = 0;
}

// index to find channel from register number (only [0],[1],[2],[6] used)
uint8_t channelindex[7] = {2, 3, 1, 0, 0, 0, 0};

uint16_t DMA::read(uint16_t address, unsigned /*io_len*/)
{
	uint8_t retval;
	uint8_t channel;

	bool ma_sl = (address >= 0xc0);

	switch (address) {
		case 0x00: /* DMA-1 current address, channel 0 */
		case 0x02: /* DMA-1 current address, channel 1 */
		case 0x04: /* DMA-1 current address, channel 2 */
		case 0x06: /* DMA-1 current address, channel 3 */
		case 0xc0: /* DMA-2 current address, channel 0 */
		case 0xc4: /* DMA-2 current address, channel 1 */
		case 0xc8: /* DMA-2 current address, channel 2 */
		case 0xcc: /* DMA-2 current address, channel 3 */
			channel = (address >> (1 + ma_sl)) & 0x03;
			if(m_s.dma[ma_sl].flip_flop==0) {
				m_s.dma[ma_sl].flip_flop = !m_s.dma[ma_sl].flip_flop;
				retval = (m_s.dma[ma_sl].chan[channel].current_address & 0xff);
			} else {
				m_s.dma[ma_sl].flip_flop = !m_s.dma[ma_sl].flip_flop;
				retval = (m_s.dma[ma_sl].chan[channel].current_address >> 8);
			}
			break;
		case 0x01: /* DMA-1 current count, channel 0 */
		case 0x03: /* DMA-1 current count, channel 1 */
		case 0x05: /* DMA-1 current count, channel 2 */
		case 0x07: /* DMA-1 current count, channel 3 */
		case 0xc2: /* DMA-2 current count, channel 0 */
		case 0xc6: /* DMA-2 current count, channel 1 */
		case 0xca: /* DMA-2 current count, channel 2 */
		case 0xce: /* DMA-2 current count, channel 3 */
			channel = (address >> (1 + ma_sl)) & 0x03;
			if(m_s.dma[ma_sl].flip_flop==0) {
				m_s.dma[ma_sl].flip_flop = !m_s.dma[ma_sl].flip_flop;
				retval = (m_s.dma[ma_sl].chan[channel].current_count & 0xff);
			} else {
				m_s.dma[ma_sl].flip_flop = !m_s.dma[ma_sl].flip_flop;
				retval = (m_s.dma[ma_sl].chan[channel].current_count >> 8);
			}
			break;
		case 0x08: // DMA-1 Status Register
		case 0xd0: // DMA-2 Status Register
			// bit 7: 1 = channel 3 request
			// bit 6: 1 = channel 2 request
			// bit 5: 1 = channel 1 request
			// bit 4: 1 = channel 0 request
			// bit 3: 1 = channel 3 has reached terminal count
			// bit 2: 1 = channel 2 has reached terminal count
			// bit 1: 1 = channel 1 has reached terminal count
			// bit 0: 1 = channel 0 has reached terminal count
			// reading this register clears lower 4 bits (hold flags)
			retval = m_s.dma[ma_sl].status_reg;
			m_s.dma[ma_sl].status_reg &= 0xf0;
			break;
		case 0x0d: // DMA-1: temporary register
		case 0xda: // DMA-2: temporary register
			// only used for memory-to-memory transfers
			// write to 0x0d / 0xda clears temporary register
			// read of temporary register always returns 0
			retval = 0;
			break;
		case 0x81: // DMA-1 page register, channel 2
		case 0x82: // DMA-1 page register, channel 3
		case 0x83: // DMA-1 page register, channel 1
		case 0x87: // DMA-1 page register, channel 0
			channel = channelindex[address - 0x81];
			retval = m_s.dma[0].chan[channel].page_reg;
			break;
		case 0x89: // DMA-2 page register, channel 2
		case 0x8a: // DMA-2 page register, channel 3
		case 0x8b: // DMA-2 page register, channel 1
		case 0x8f: // DMA-2 page register, channel 0
			channel = channelindex[address - 0x89];
			retval = m_s.dma[1].chan[channel].page_reg;
			break;
		case 0x80:
		case 0x84:
		case 0x85:
		case 0x86:
		case 0x88:
		case 0x8c:
		case 0x8d:
		case 0x8e:
			// extra page registers, unused
			retval = m_s.ext_page_reg[address & 0x0f];
			break;
		case 0x0f: // DMA-1: undocumented: read all mask bits
		case 0xde: // DMA-2: undocumented: read all mask bits
			retval = m_s.dma[ma_sl].mask[0] |
			   (m_s.dma[ma_sl].mask[1] << 1) |
			   (m_s.dma[ma_sl].mask[2] << 2) |
			   (m_s.dma[ma_sl].mask[3] << 3);
			retval = (0xf0 | retval);
			break;
		default:
			PDEBUGF(LOG_V0, LOG_DMA, "unhandled read from port 0x%04X!\n", address);
			return ~0;
	}

	PDEBUGF(LOG_V2, LOG_DMA, "read  0x%03X -> 0x%04X\n", address, retval);

	return retval;
}

void DMA::write(uint16_t address, uint16_t value, unsigned /*io_len*/)
{
	uint8_t set_mask_bit;
	uint8_t channel;

	PDEBUGF(LOG_V2, LOG_DMA, "write 0x%03X <- 0x%04X ", address, value);

	bool ma_sl = (address >= 0xc0);

	switch (address) {
		case 0x00:
		case 0x02:
		case 0x04:
		case 0x06:
		case 0xc0:
		case 0xc4:
		case 0xc8:
		case 0xcc:
			channel = (address >> (1 + ma_sl)) & 0x03;
			if(m_s.dma[ma_sl].flip_flop==0) { /* 1st byte */
				m_s.dma[ma_sl].chan[channel].base_address = value;
				m_s.dma[ma_sl].chan[channel].current_address = value;
				PDEBUGF(LOG_V2, LOG_DMA, "\n");
			} else { /* 2nd byte */
				PDEBUGF(LOG_V2, LOG_DMA, "DMA-%d ch.%d addr", ma_sl+1, channel);
				m_s.dma[ma_sl].chan[channel].base_address |= (value << 8);
				m_s.dma[ma_sl].chan[channel].current_address |= (value << 8);
				PDEBUGF(LOG_V2, LOG_DMA, " base = %04x", m_s.dma[ma_sl].chan[channel].base_address);
				PDEBUGF(LOG_V2, LOG_DMA, " curr = %04x\n", m_s.dma[ma_sl].chan[channel].current_address);
			}
			m_s.dma[ma_sl].flip_flop = !m_s.dma[ma_sl].flip_flop;
			break;

		case 0x01:
		case 0x03:
		case 0x05:
		case 0x07:
		case 0xc2:
		case 0xc6:
		case 0xca:
		case 0xce:
			channel = (address >> (1 + ma_sl)) & 0x03;
			if(m_s.dma[ma_sl].flip_flop==0) { /* 1st byte */
				m_s.dma[ma_sl].chan[channel].base_count = value;
				m_s.dma[ma_sl].chan[channel].current_count = value;
				PDEBUGF(LOG_V2, LOG_DMA, "\n");
			} else { /* 2nd byte */
				PDEBUGF(LOG_V2, LOG_DMA, "DMA-%d ch.%d count", ma_sl+1, channel);
				m_s.dma[ma_sl].chan[channel].base_count |= (value << 8);
				m_s.dma[ma_sl].chan[channel].current_count |= (value << 8);
				PDEBUGF(LOG_V2, LOG_DMA, " base = %04x", m_s.dma[ma_sl].chan[channel].base_count);
				PDEBUGF(LOG_V2, LOG_DMA, " curr = %04x\n", m_s.dma[ma_sl].chan[channel].current_count);
			}
			m_s.dma[ma_sl].flip_flop = !m_s.dma[ma_sl].flip_flop;
			break;

		case 0x08: /* DMA-1: command register */
		case 0xd0: /* DMA-2: command register */
			m_s.dma[ma_sl].command_reg = value;
			m_s.dma[ma_sl].ctrl_disabled = (value >> 2) & 0x01;
			control_HRQ(ma_sl);
			PDEBUGF(LOG_V2, LOG_DMA, " cmd\n");
			if((value & 0xfb) != 0x00) {
				PERRF(LOG_DMA, "DMA command value 0x%02x not supported!\n", value);
			}
			break;

		case 0x09: // DMA-1: request register
		case 0xd2: // DMA-2: request register
			channel = value & 0x03;
			// note: write to 0x0d / 0xda clears this register
			if(value & 0x04) {
				// set request bit
				m_s.dma[ma_sl].status_reg |= (1 << (channel+4));
				PDEBUGF(LOG_V2, LOG_DMA, "DMA-%d: set request bit for ch.%u\n", ma_sl+1, channel);
			} else {
				// clear request bit
				m_s.dma[ma_sl].status_reg &= ~(1 << (channel+4));
				PDEBUGF(LOG_V2, LOG_DMA, "DMA-%d: cleared request bit for ch.%u\n", ma_sl+1, channel);
			}
			control_HRQ(ma_sl);
			break;

		case 0x0a:
		case 0xd4:
			set_mask_bit = value & 0x04;
			channel = value & 0x03;
			m_s.dma[ma_sl].mask[channel] = (set_mask_bit > 0);
			PDEBUGF(LOG_V2, LOG_DMA, "DMA-%d: set_mask_bit=%u, ch.=%u, mask now=%02xh\n",
					ma_sl+1, set_mask_bit, channel, m_s.dma[ma_sl].mask[channel]);
			control_HRQ(ma_sl);
			break;

		case 0x0b: /* DMA-1 mode register */
		case 0xd6: /* DMA-2 mode register */
			channel = value & 0x03;
			m_s.dma[ma_sl].chan[channel].mode.mode_type = (value >> 6) & 0x03;
			m_s.dma[ma_sl].chan[channel].mode.address_decrement = (value >> 5) & 0x01;
			m_s.dma[ma_sl].chan[channel].mode.autoinit_enable = (value >> 4) & 0x01;
			m_s.dma[ma_sl].chan[channel].mode.transfer_type = (value >> 2) & 0x03;
			PDEBUGF(LOG_V2, LOG_DMA,
					"DMA-%d: mode reg[%u]: mode=%u, dec=%u, autoinit=%u, txtype=%u (%s)\n",
					ma_sl+1, channel,
					m_s.dma[ma_sl].chan[channel].mode.mode_type,
					m_s.dma[ma_sl].chan[channel].mode.address_decrement,
					m_s.dma[ma_sl].chan[channel].mode.autoinit_enable,
					m_s.dma[ma_sl].chan[channel].mode.transfer_type,
					m_s.dma[ma_sl].chan[channel].mode.transfer_type==0?"verify":
						(m_s.dma[ma_sl].chan[channel].mode.transfer_type==1?"write":
							(m_s.dma[ma_sl].chan[channel].mode.transfer_type==2?"read":
								"undefined")));
			break;

		case 0x0c: /* DMA-1 clear byte flip/flop */
		case 0xd8: /* DMA-2 clear byte flip/flop */
			PDEBUGF(LOG_V2, LOG_DMA, "DMA-%d: clear flip/flop\n", ma_sl+1);
			m_s.dma[ma_sl].flip_flop = 0;
			break;

		case 0x0d: // DMA-1: master clear
		case 0xda: // DMA-2: master clear
			PDEBUGF(LOG_V2, LOG_DMA, "DMA-%d: master clear\n", ma_sl+1);
			// writing any value to this port resets DMA controller 1 / 2
			// same action as a hardware reset
			// mask register is set (chan 0..3 disabled)
			// command, status, request, temporary, and byte flip-flop are all cleared
			reset_controller(ma_sl);
			break;

		case 0x0e: // DMA-1: clear mask register
		case 0xdc: // DMA-2: clear mask register
			PDEBUGF(LOG_V2, LOG_DMA, "DMA-%d: clear mask reg\n", ma_sl+1);
			m_s.dma[ma_sl].mask[0] = 0;
			m_s.dma[ma_sl].mask[1] = 0;
			m_s.dma[ma_sl].mask[2] = 0;
			m_s.dma[ma_sl].mask[3] = 0;
			control_HRQ(ma_sl);
			break;

		case 0x0f: // DMA-1: write all mask bits
		case 0xde: // DMA-2: write all mask bits
			PDEBUGF(LOG_V2, LOG_DMA, "DMA-%d: write all mask bits\n", ma_sl+1);
			m_s.dma[ma_sl].mask[0] = value & 0x01; value >>= 1;
			m_s.dma[ma_sl].mask[1] = value & 0x01; value >>= 1;
			m_s.dma[ma_sl].mask[2] = value & 0x01; value >>= 1;
			m_s.dma[ma_sl].mask[3] = value & 0x01;
			control_HRQ(ma_sl);
			break;

		case 0x81: /* DMA-1 page register, channel 2 */
		case 0x82: /* DMA-1 page register, channel 3 */
		case 0x83: /* DMA-1 page register, channel 1 */
		case 0x87: /* DMA-1 page register, channel 0 */
			/* address bits A16-A23 for DMA channel */
			channel = channelindex[address - 0x81];
			m_s.dma[0].chan[channel].page_reg = value;
			PDEBUGF(LOG_V2, LOG_DMA, "DMA-1: page reg %d = %02x\n", channel, value);
			break;

		case 0x89: /* DMA-2 page register, channel 2 */
		case 0x8a: /* DMA-2 page register, channel 3 */
		case 0x8b: /* DMA-2 page register, channel 1 */
		case 0x8f: /* DMA-2 page register, channel 0 */
			/* address bits A16-A23 for DMA channel */
			channel = channelindex[address - 0x89];
			m_s.dma[1].chan[channel].page_reg = value;
			PDEBUGF(LOG_V2, LOG_DMA, "DMA-2: page reg %d = %02x\n", channel + 4, value);
			break;

		case 0x80:
		case 0x84:
		case 0x85:
		case 0x86:
		case 0x88:
		case 0x8c:
		case 0x8d:
		case 0x8e:
			PDEBUGF(LOG_V2, LOG_DMA, "extra page reg (unused)\n");
			m_s.ext_page_reg[address & 0x0f] = value;
			break;
		default:
			PDEBUGF(LOG_V0, LOG_DMA, "unhandled write to port 0x%04X!\n", address);
			break;
	}
}

void DMA::set_DRQ(unsigned channel, bool val)
{
	uint32_t dma_base, dma_roof;
	uint8_t ma_sl;

	PDEBUGF(LOG_V1, LOG_DMA, "set_DRQ: ch.=%d, val=%d\n", channel, val);
	
	if(channel > 7) {
		PERRF(LOG_DMA, "set_DRQ() channel > 7\n");
		return;
	}
	ma_sl = (channel > 3)?1:0;
	m_s.dma[ma_sl].DRQ[channel & 0x03] = val;
	if(!m_channels[channel].used) {
		PERRF(LOG_DMA, "set_DRQ(): channel %d not connected to device\n", channel);
		return;
	}
	channel &= 0x03;
	if(!val) {
		// clear bit in status reg
		m_s.dma[ma_sl].status_reg &= ~(1 << (channel+4));
		control_HRQ(ma_sl);
		return;
	}

	m_s.dma[ma_sl].status_reg |= (1 << (channel+4));

	if((m_s.dma[ma_sl].chan[channel].mode.mode_type != DMA_MODE_SINGLE) &&
	  (m_s.dma[ma_sl].chan[channel].mode.mode_type != DMA_MODE_DEMAND) &&
	  (m_s.dma[ma_sl].chan[channel].mode.mode_type != DMA_MODE_CASCADE))
	{
		PERRF(LOG_DMA, "set_DRQ: mode_type(%02x) not handled\n",
				m_s.dma[ma_sl].chan[channel].mode.mode_type);
		return;
	}

	dma_base = (m_s.dma[ma_sl].chan[channel].page_reg << 16) |
	           (m_s.dma[ma_sl].chan[channel].base_address << ma_sl);
	if(m_s.dma[ma_sl].chan[channel].mode.address_decrement) {
		dma_roof = dma_base - (m_s.dma[ma_sl].chan[channel].base_count << ma_sl);
	} else {
		dma_roof = dma_base + (m_s.dma[ma_sl].chan[channel].base_count << ma_sl);
	}
	if(channel!=0 && ((dma_base & (0x7fff0000 << ma_sl)) != (dma_roof & (0x7fff0000 << ma_sl)))) {
		PERRF(LOG_DMA, "dma_base = 0x%08x\n", dma_base);
		PERRF(LOG_DMA, "dma_base_count = 0x%08x\n", m_s.dma[ma_sl].chan[channel].base_count);
		PERRF(LOG_DMA, "dma_roof = 0x%08x\n", dma_roof);
		PERRF(LOG_DMA, "request outside %dk boundary\n", 64 << ma_sl);
		return;
	}

	control_HRQ(ma_sl);
}

bool DMA::get_DRQ(uint channel)
{
	uint8_t ma_sl = (channel > 3)?1:0;
	return m_s.dma[ma_sl].DRQ[channel & 0x03];
}

void DMA::control_HRQ(uint8_t ma_sl)
{
	unsigned channel;

	// do nothing if controller is disabled
	if(m_s.dma[ma_sl].ctrl_disabled)
		return;

	// deassert HRQ if no DRQ is pending
	if((m_s.dma[ma_sl].status_reg & 0xf0) == 0) {
		if(ma_sl) {
			g_cpu.set_HRQ(false);
		} else {
			set_DRQ(4, 0);
		}
		return;
	}
	// find highest priority channel
	for(channel=0; channel<4; channel++) {
		if((m_s.dma[ma_sl].status_reg & (1 << (channel+4))) && (m_s.dma[ma_sl].mask[channel]==0)) {
			if(ma_sl) {
				// assert Hold ReQuest line to CPU
				g_cpu.set_HRQ(true);
			} else {
				// send DRQ to cascade channel of the master
				set_DRQ(4, 1);
			}
			break;
		}
	}
}

void DMA::raise_HLDA(void)
{
	unsigned channel;
	uint32_t phy_addr;
	uint8_t ma_sl = 0;
	uint32_t maxlen;
	uint16_t len = 1;
	uint8_t buffer[DMA_BUFFER_SIZE];

	m_s.HLDA = true;
	// find highest priority channel
	for(channel=0; channel<4; channel++) {
		if((m_s.dma[1].status_reg & (1 << (channel+4))) && (m_s.dma[1].mask[channel]==0)) {
			ma_sl = 1;
			break;
		}
	}
	if(channel == 0) { // master cascade channel
		m_s.dma[1].DACK[0] = 1;
		for(channel=0; channel<4; channel++) {
			if((m_s.dma[0].status_reg & (1 << (channel+4))) && (m_s.dma[0].mask[channel]==0)) {
				ma_sl = 0;
				break;
			}
		}
	}
	if(channel >= 4) {
		// wait till they're unmasked
		return;
	}

	phy_addr = (m_s.dma[ma_sl].chan[channel].page_reg << 16) |
	           (m_s.dma[ma_sl].chan[channel].current_address << ma_sl);

	if(!m_s.dma[ma_sl].chan[channel].mode.address_decrement) {
		maxlen = (uint32_t(m_s.dma[ma_sl].chan[channel].current_count) + 1) << ma_sl;
		m_s.TC = (maxlen <= DMA_BUFFER_SIZE);
		if(maxlen > DMA_BUFFER_SIZE) {
			maxlen = DMA_BUFFER_SIZE;
		}
	} else {
		m_s.TC = (m_s.dma[ma_sl].chan[channel].current_count == 0);
		maxlen = 1 << ma_sl;
	}

	if(m_s.dma[ma_sl].chan[channel].mode.transfer_type == 1) { // write
		// DMA controlled xfer of bytes from I/O to Memory

		PDEBUGF(LOG_V2, LOG_DMA, "writing to memory max. %d bytes <- ch.%d\n", maxlen, channel);
		
		if(!ma_sl) {
			if(m_h[channel].dmaWrite8) {
				len = m_h[channel].dmaWrite8(buffer, maxlen);
			} else {
				PERRF(LOG_DMA, "no dmaWrite handler for channel %u\n", channel);
			}
			g_memory.DMA_write(phy_addr, len, buffer);

		} else {
			if(m_h[channel].dmaWrite16) {
				len = m_h[channel].dmaWrite16((uint16_t*)buffer, maxlen / 2);
			} else {
				PERRF(LOG_DMA, "no dmaWrite handler for channel %u\n", channel);
			}
			g_memory.DMA_write(phy_addr, len, buffer);

		}
	} else if(m_s.dma[ma_sl].chan[channel].mode.transfer_type == 2) { // read
		// DMA controlled xfer of bytes from Memory to I/O

		PDEBUGF(LOG_V2, LOG_DMA, "reading from memory max. %d bytes -> ch.%d\n", maxlen, channel);
		
		if(!ma_sl) {
			g_memory.DMA_read(phy_addr, maxlen, buffer);

			if(m_h[channel].dmaRead8) {
				len = m_h[channel].dmaRead8(buffer, maxlen);
			}
		} else {
			g_memory.DMA_read(phy_addr, maxlen, buffer);

			if(m_h[channel].dmaRead16){
				len = m_h[channel].dmaRead16((uint16_t*)buffer, maxlen / 2);
			}
		}
	} else if(m_s.dma[ma_sl].chan[channel].mode.transfer_type == 0) {
		// verify

		PDEBUGF(LOG_V2, LOG_DMA, "verify max. %d bytes, ch.%d\n", maxlen, channel);
		
		if(!ma_sl) {
			if(m_h[channel].dmaWrite8) {
				len = m_h[channel].dmaWrite8(buffer, 1);
			} else {
				PERRF(LOG_DMA, "no dmaWrite handler for channel %u\n", channel);
			}
		} else {
			if(m_h[channel].dmaWrite16) {
				len = m_h[channel].dmaWrite16((uint16_t*)buffer, 1);
			} else {
				PERRF(LOG_DMA, "no dmaWrite handler for channel %u.", channel);
			}
		}
	} else {
		PERRF(LOG_DMA, "hlda: transfer_type 3 is undefined\n");
	}

	m_s.dma[ma_sl].DACK[channel] = 1;
	// check for expiration of count, so we can signal TC and DACK(n)
	// at the same time.
	if(!m_s.dma[ma_sl].chan[channel].mode.address_decrement) {
		m_s.dma[ma_sl].chan[channel].current_address += len;
	} else {
		m_s.dma[ma_sl].chan[channel].current_address--;
	}
	m_s.dma[ma_sl].chan[channel].current_count -= len;
	if(m_s.dma[ma_sl].chan[channel].current_count == 0xffff) {
		// count expired, done with transfer
		// assert TC, deassert HRQ & DACK(n) lines
		m_s.dma[ma_sl].status_reg |= (1 << channel); // hold TC in status reg
		if(m_s.dma[ma_sl].chan[channel].mode.autoinit_enable == 0) {
			// set mask bit if not in autoinit mode
			m_s.dma[ma_sl].mask[channel] = 1;
		} else {
			// count expired, but in autoinit mode
			// reload count and base address
			m_s.dma[ma_sl].chan[channel].current_address = m_s.dma[ma_sl].chan[channel].base_address;
			m_s.dma[ma_sl].chan[channel].current_count = m_s.dma[ma_sl].chan[channel].base_count;
		}
		m_s.TC = false;            // clear TC, adapter card already notified
		m_s.HLDA = false;
		g_cpu.set_HRQ(false);           // clear HRQ to CPU
		m_s.dma[ma_sl].DACK[channel] = 0; // clear DACK to adapter card
		if(!ma_sl) {
			set_DRQ(4, 0); // clear DRQ to cascade
			m_s.dma[1].DACK[0] = 0; // clear DACK to cascade
		}
	}
}

void DMA::register_8bit_channel(unsigned channel,
		dma8_fun_t dmaRead, dma8_fun_t dmaWrite, const char *name)
{
	if(channel > 3) {
		PERRF(LOG_DMA, "register_8bit_channel: invalid channel number(%u)\n", channel);
		return;
	}
	if(m_channels[channel].used) {
		PERRF(LOG_DMA, "register_8bit_channel: channel(%u) already in use\n", channel);
		return;
	}
	PDEBUGF(LOG_V1, LOG_DMA, "channel %u used by '%s'\n", channel, name);
	m_h[channel].dmaRead8  = dmaRead;
	m_h[channel].dmaWrite8 = dmaWrite;
	m_channels[channel].used = true;
	m_channels[channel].device = name;
}

void DMA::register_16bit_channel(unsigned channel,
		dma16_fun_t dmaRead, dma16_fun_t dmaWrite, const char *name)
{
	if((channel < 4) || (channel > 7)) {
		PERRF(LOG_DMA, "register_16bit_channel: invalid channel number(%u)\n", channel);
		return;
	}
	if(m_channels[channel].used) {
		PERRF(LOG_DMA, "register_16bit_channel: channel(%u) already in use\n", channel);
		return;
	}
	PDEBUGF(LOG_V1, LOG_DMA, "channel %u used by %s", channel, name);
	m_h[channel&0x03].dmaRead16  = dmaRead;
	m_h[channel&0x03].dmaWrite16 = dmaWrite;
	m_channels[channel].used = true;
	m_channels[channel].device = name;
}

void DMA::unregister_channel(unsigned channel)
{
	assert(channel < 8);
	m_channels[channel].used = false;
	m_channels[channel].device = "";
	PDEBUGF(LOG_V1, LOG_DMA, "channel %u no longer used\n", channel);
}

std::string DMA::get_device_name(unsigned _channel)
{
	assert(_channel < 8);
	return m_channels[_channel].device;
}
